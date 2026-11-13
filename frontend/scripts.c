#include "scripts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "event/script.h"
#include "packed.h"

/* ---- The language, from its table ------------------------------------------------------------- */

/* A parameter of a command as its text gives it: how its value comes from
 * the command's bytes (event/script.h's EventArg, as the engine decodes it). */
typedef enum {
    P_CONSTANT,   /* fixed by the opcode */
    P_IMPLIED,    /* fixed by the opcode, and no number: the running object, the story's progress */
    P_OPERAND,    /* an operand of `bytes` bytes, or bits of one (`mask` after `shift`), plus `add` */
    P_RAW,        /* `bytes` operand bytes as they are (0: to the command's end) */
} ParamForm;

typedef struct {
    const char *name;
    const char *prefix;   /* before the value: what the operand names */
    ParamForm form;
    uint8_t operand, bytes, shift;
    uint16_t mask;        /* 0: the whole operand */
    uint8_t add;
    uint16_t constant;
    uint8_t order;        /* its place among the language's parameters */
} Param;

enum { MOST_PARAMS = 12 };

typedef struct Row {
    bool known;
    uint8_t opcode;
    const char *name;
    const EventOpcode *event;
    uint16_t length;              /* the row's: the bytes its parameters are in */
    uint8_t jump;                 /* the operand holding a jump's distance, or 0 */
    bool back;
    unsigned params;
    Param param[MOST_PARAMS];
} Row;

static void add_param(Row *row, const char *name, const EventArg *a)
{
    Param p = { name, "", P_OPERAND, a->operand, 1, 0, 0, 0, 0, 0 };
    switch (a->kind) {
    case ARG_NONE: return;
    case ARG_CONSTANT: p.form = P_CONSTANT, p.constant = a->constant; break;
    case ARG_BYTE: break;
    case ARG_BYTE_PLUS_ONE: p.add = 1; break;
    case ARG_BITS: p.shift = a->shift, p.mask = a->constant; break;
    case ARG_WORD: p.bytes = 2; break;
    case ARG_WORD_BITS: p.bytes = 2, p.shift = a->shift, p.mask = a->constant; break;
    case ARG_ADDRESS: p.bytes = 3; break;
    case ARG_VARIABLE: case ARG_VARIABLE_BYTE: p.prefix = "var:"; break;
    case ARG_VARIABLE_WORD: p.prefix = "var16:"; break;
    case ARG_OBJECT: p.prefix = "obj:"; break;
    case ARG_SELF: p.form = P_IMPLIED, p.prefix = "self"; break;
    case ARG_MEMBER: case ARG_MEMBER_OBJECT: case ARG_MEMBER_PRESENT: p.prefix = "member:"; break;
    case ARG_STORY_WORD: p.prefix = "story:", p.bytes = 2; break;
    case ARG_STORY_PROGRESS: p.form = P_IMPLIED, p.prefix = "progress"; break;
    case ARG_BIT: case ARG_ALL_BUT_BIT: p.prefix = "bit:"; break;
    case ARG_FORMATION_PLACE: p.form = P_RAW, p.bytes = 2; break;
    case ARG_SEARCHLIGHT_MOVE: p.form = P_RAW, p.bytes = 16; break;
    case ARG_OBJECT_COLOUR_EFFECT: case ARG_SCREEN_COLOUR_EFFECT: p.form = P_RAW, p.bytes = 0; break;
    }
    p.order = (uint8_t)row->params;
    if (row->params < MOST_PARAMS)
        row->param[row->params++] = p;
    else
        row->known = false;   /* more than the text can hold: its commands are written as bytes */
}

/* Parameters in the order a line gives them: the fixed ones, then by operand. */
static int param_order(const void *a, const void *b)
{
    const Param *p = a, *q = b;
    int pf = p->form == P_CONSTANT || p->form == P_IMPLIED, qf = q->form == P_CONSTANT || q->form == P_IMPLIED;
    if (pf != qf)
        return qf - pf;
    if (p->operand != q->operand && !pf)
        return p->operand - q->operand;
    if (p->shift != q->shift && !pf)
        return p->shift - q->shift;
    return p->order - q->order;
}

enum { OPCODES = 256 };

/* Every opcode's row, worked out from the language's table once. */
static const Row *row_of(uint8_t opcode)
{
    static Row rows[OPCODES];
    static bool built;
    if (!built) {
        for (unsigned op = 0; op < OPCODES; op++) {
            Row *row = &rows[op];
            const EventOpcode *e = event_opcode((uint8_t)op);
            *row = (Row){ .opcode = (uint8_t)op };
            if (!e || !event_command_name(e))
                continue;
            *row = (Row){ true, (uint8_t)op, event_command_name(e), e, e->length, e->jump, e->back, 0, { { 0 } } };
#define ADD_PARAM(type, name) add_param(row, #name, &e->name);
            EVENT_PARAMETERS(ADD_PARAM)
            EVENT_RECORD_PARAMETERS(ADD_PARAM)
#undef ADD_PARAM
            if (!row->known)
                continue;
            qsort(row->param, row->params, sizeof row->param[0], param_order);
        }
        built = true;
    }
    return &rows[opcode];
}

/* The bytes the command at `p` takes, `available` there; 0 where it can't be told. */
static uint16_t length_of(const Row *row, const uint8_t *p, uint32_t available)
{
    return event_command_length(row->event, p, available > 0xFFFF ? 0xFFFF : (uint16_t)available);
}

/* Whether a parameter's operand lies inside a command of `length` bytes. */
static bool param_in(const Param *p, uint16_t length)
{
    if (p->form == P_CONSTANT || p->form == P_IMPLIED)
        return true;
    return p->operand < length && (p->form == P_RAW && p->bytes == 0 ? true : p->operand + p->bytes <= length);
}

static uint16_t raw_bytes(const Param *p, uint16_t length) { return p->bytes ? p->bytes : (uint16_t)(length - p->operand); }

static uint32_t operand_value(const uint8_t *c, const Param *p)
{
    uint32_t v = 0;
    for (unsigned k = 0; k < p->bytes; k++)
        v |= (uint32_t)c[p->operand + k] << (8 * k);
    if (p->mask)
        v = v >> p->shift & p->mask;
    return (v + p->add) & (p->bytes == 3 ? 0xFFFFFFu : p->bytes == 2 ? 0xFFFFu : 0xFFu);
}

static void put_operand(uint8_t *c, const Param *p, uint32_t value)
{
    uint32_t v = (value - p->add) & (p->bytes == 3 ? 0xFFFFFFu : p->bytes == 2 ? 0xFFFFu : 0xFFu);
    for (unsigned k = 0; k < p->bytes; k++) {
        uint32_t keep = p->mask ? ~((uint32_t)p->mask << p->shift) : 0;
        uint32_t bits = p->mask ? (v & p->mask) << p->shift : v;
        c[p->operand + k] = (uint8_t)((c[p->operand + k] & (keep >> (8 * k))) | (bits >> (8 * k)));
    }
}

/* The bits of a command's byte k that its parameters and its jump read. */
static uint8_t covered(const Row *row, unsigned k, uint16_t length)
{
    uint8_t bits = row->jump && k == row->jump ? 0xFF : 0;
    for (unsigned i = 0; i < row->params; i++) {
        const Param *p = &row->param[i];
        if (!param_in(p, length) || p->form == P_CONSTANT || p->form == P_IMPLIED || k < p->operand)
            continue;
        if (p->form == P_RAW) {
            if (k < p->operand + raw_bytes(p, length))
                bits = 0xFF;
        } else if (k < p->operand + p->bytes) {
            uint32_t mask = p->mask ? (uint32_t)p->mask << p->shift : 0xFFFFFFu;
            bits |= (uint8_t)(mask >> (8 * (k - p->operand)));
        }
    }
    return bits;
}

/* ---- The script's shape ----------------------------------------------------------------------- */

/* An unpacked script: its objects' count, then 16 words an object, each
 * where one of its functions starts, from the byte after the count (a
 * script's offsets count from there: game/event/calls.c), then the code. */
enum { FUNCTIONS = 16, TABLE_AT = 1, LINE_BYTES = 16 };

/* The game's buffer for a script, before the commands it copies after it
 * (game/field/location.c): an edited script may grow to it. */
enum { SCRIPT_ROOM = 0x1700 };

static uint16_t offset_of(uint32_t at) { return (uint16_t)(at - TABLE_AT); }

/* What a place in the unpacked bytes is, as the script is written. */
enum { NOTHING, COMMAND, RAW, LABEL = 0x80 };

/* ---- Writing ------------------------------------------------------------------------------------- */

static void put_label(DecodedText *t, uint32_t at, const uint8_t *mark, uint32_t len, const char *before)
{
    char s[16];
    if (at < len && (mark[at] & LABEL))
        snprintf(s, sizeof s, "%sL%04X", before, offset_of(at));
    else
        snprintf(s, sizeof s, "%s%04X", before, offset_of(at));
    decoded_put(t, s);
}

static bool is_start(const uint8_t *mark, uint32_t at, uint32_t len)
{
    return at < len && (mark[at] & ~LABEL) != NOTHING;
}

static void put_hex(DecodedText *t, const uint8_t *p, unsigned n, const char *between)
{
    char s[4];
    for (unsigned k = 0; k < n; k++) {
        snprintf(s, sizeof s, "%02X", p[k]);
        if (k)
            decoded_put(t, between);
        decoded_put(t, s);
    }
}

/* The row a line names (below). */
typedef struct Line Line;
typedef struct JumpTo JumpTo;
static const char *split_line(const char *s, const char *end, Line *l);
static const char *read_command(Line *l, uint8_t *c, uint16_t *length, JumpTo *jump, const struct Row **found);
static int read_as(const char *text, size_t len, bool back);

/* A command's line: its name, its parameters, what no parameter reads, its
 * jump's place, and its opcode where its name and parameters would be
 * another's. */
static void write_command(DecodedText *out, const Row *row, const uint8_t *c, uint16_t length, uint32_t at,
                          const uint8_t *mark, uint32_t len)
{
    char s[48];
    DecodedText line = { 0 }, *t = &line;
    decoded_put(t, row->name);
    for (unsigned i = 0; i < row->params; i++) {
        const Param *p = &row->param[i];
        if (!param_in(p, length))
            continue;
        decoded_put(t, " ");
        decoded_put(t, p->name);
        decoded_put(t, "=");
        decoded_put(t, p->prefix);
        if (p->form == P_IMPLIED)
            continue;
        if (p->form == P_RAW) {
            put_hex(t, c + p->operand, raw_bytes(p, length), ".");
            continue;
        }
        if (p->form == P_CONSTANT)
            snprintf(s, sizeof s, "%02X", p->constant);
        else
            snprintf(s, sizeof s, "%0*X", (int)(2 * (p->bytes & 3)), (unsigned)operand_value(c, p));
        decoded_put(t, s);
    }
    for (unsigned k = 1; k < row->length && k < length; k++) {
        uint8_t rest = (uint8_t)(c[k] & ~covered(row, k, length));
        if (rest) {
            snprintf(s, sizeof s, " byte%u=%02X", k, rest);
            decoded_put(t, s);
        }
    }
    if (length > row->length && covered(row, row->length, length) != 0xFF) {
        decoded_put(t, " data=");
        put_hex(t, c + row->length, (unsigned)(length - row->length), ".");
    }
    if (row->jump) {
        uint8_t distance = c[row->jump];
        uint32_t from = at + row->jump;
        uint32_t to = row->back ? from - distance : from + distance;
        if (!row->back && to < len && (mark[to] & LABEL)) {
            put_label(t, to, mark, len, " to=");
        } else if (row->back && from >= distance && (mark[to] & LABEL)) {
            put_label(t, to, mark, len, " to=");
        } else {
            snprintf(s, sizeof s, " to=+%02X", distance);
            decoded_put(t, s);
        }
    }
    /* the opcode, where the line would be read as another's */
    if (!line.full && read_as(line.text, line.len, row->back) != row->opcode) {
        snprintf(s, sizeof s, " @%02X", row->opcode);
        decoded_put(t, s);
    }
    decoded_put(t, "\n");
    decoded_put(t, "x");   /* room for a NUL */
    if (line.full) {
        out->full = true;
    } else {
        line.text[line.len - 1] = 0;
        decoded_put(out, line.text);
    }
    free(line.text);
}

/* Where each command starts, from the code's start on: a byte with a row
 * whose length can be told starts a command; anything else is bytes, up to
 * the next place the table names, which a command may not run over. */
static void sweep(const uint8_t *u, uint32_t len, uint32_t code, uint8_t *mark)
{
    uint32_t at = code;
    while (at < len) {
        const Row *row = row_of(u[at]);
        uint16_t length = row->known ? length_of(row, u + at, len - at) : 0;
        bool clear = length > 0;
        for (uint32_t k = 1; clear && k < length; k++)
            clear = !(mark[at + k] & LABEL);
        if (clear) {
            mark[at] |= COMMAND;
            at += length;
            continue;
        }
        mark[at] |= RAW;
        do
            at++;
        while (at < len && !(mark[at] & LABEL));
    }
}

/* The places the code's jumps name, where they land on a command's start. */
static void mark_jumps(const uint8_t *u, uint32_t len, uint8_t *mark)
{
    for (uint32_t at = 0; at < len; at++) {
        if ((mark[at] & ~LABEL) != COMMAND)
            continue;
        const Row *row = row_of(u[at]);
        if (!row->jump)
            continue;
        uint32_t from = at + row->jump, distance = u[from];
        if (row->back ? from >= distance : from + distance < len) {
            uint32_t to = row->back ? from - distance : from + distance;
            if (is_start(mark, to, len))
                mark[to] |= LABEL;
        }
    }
}

static bool write_script(DecodedText *t, const uint8_t *u, uint32_t len)
{
    char s[48];
    uint32_t objects = len ? u[0] : 0, code = TABLE_AT + objects * FUNCTIONS * 2;
    if (len == 0 || code > len)
        return false;
    uint8_t *mark = calloc(len, 1);
    if (!mark)
        return false;
    for (uint32_t k = 0; k < objects * FUNCTIONS; k++) {
        uint32_t to = (uint32_t)(u[TABLE_AT + 2 * k] | u[TABLE_AT + 2 * k + 1] << 8) + TABLE_AT;
        if (to >= code && to < len)
            mark[to] |= LABEL;
    }
    sweep(u, len, code, mark);
    mark_jumps(u, len, mark);
    snprintf(s, sizeof s, "objects %u\n", (unsigned)objects);
    decoded_put(t, s);
    for (uint32_t o = 0; o < objects; o++) {
        snprintf(s, sizeof s, "functions %02X", (unsigned)o);
        decoded_put(t, s);
        for (unsigned f = 0; f < FUNCTIONS; f++) {
            uint32_t k = TABLE_AT + 2 * (o * FUNCTIONS + f);
            uint32_t to = (uint32_t)(u[k] | u[k + 1] << 8) + TABLE_AT;
            put_label(t, to, mark, len, " ");
        }
        decoded_put(t, "\n");
    }
    for (uint32_t at = code; at < len;) {
        if (mark[at] & LABEL) {
            snprintf(s, sizeof s, "L%04X:\n", offset_of(at));
            decoded_put(t, s);
        }
        if ((mark[at] & ~LABEL) == COMMAND) {
            const Row *row = row_of(u[at]);
            uint16_t length = length_of(row, u + at, len - at);
            write_command(t, row, u + at, length, at, mark, len);
            at += length;
            continue;
        }
        uint32_t n = 0;
        while (at + n < len && n < LINE_BYTES && (n == 0 || !(mark[at + n] & LABEL)) &&
               (n == 0 || (mark[at + n] & ~LABEL) != COMMAND))
            n++;
        decoded_put(t, "bytes ");
        put_hex(t, u + at, n, " ");
        decoded_put(t, "\n");
        at += n;
    }
    free(mark);
    return true;
}

/* ---- The world maps' places --------------------------------------------------------------------- */

/* A world map's lists of places (game/overworld/setup.c, load_the_places):
 * each its count of records and the records -- the exits, the events and
 * the halts, then where each event's script starts -- and what follows. A
 * record is its fields, a byte or a word each (game/overworld/places.c). */
enum { PLACE_LISTS = 4, MOST_PLACE_FIELDS = 6 };

static const struct {
    const char *name;       /* a record's line starts with it */
    unsigned fields;
    uint8_t bytes[MOST_PLACE_FIELDS];
} PLACE_LIST[PLACE_LISTS] = {
    { "exit", 6, { 1, 1, 1, 2, 1, 1 } },
    { "event", 3, { 1, 1, 1 } },
    { "halt", 3, { 1, 1, 1 } },
    { "script", 1, { 2 } },
};

static unsigned record_bytes(unsigned list)
{
    unsigned n = 0;
    for (unsigned f = 0; f < PLACE_LIST[list].fields; f++)
        n += PLACE_LIST[list].bytes[f];
    return n;
}

static bool write_places(DecodedText *t, const uint8_t *u, uint32_t len)
{
    char s[16];
    uint32_t at = 0;
    decoded_put(t, "# exit: its map tile's column (bit 7: open) and row, the name its sign shows, the\n"
                   "# location (a word: 01FF an event script, the next byte its entry), where the\n"
                   "# party arrives (x, y); event: column (bit 7: armed), row, its script's entry;\n"
                   "# halt: column, row and a byte; script: where an event's script starts\n");
    for (unsigned list = 0; list < PLACE_LISTS; list++) {
        if (at >= len || at + 1 + u[at] * record_bytes(list) > len)
            return false;
        unsigned count = u[at++];
        for (unsigned k = 0; k < count; k++) {
            decoded_put(t, PLACE_LIST[list].name);
            for (unsigned f = 0; f < PLACE_LIST[list].fields; f++) {
                if (PLACE_LIST[list].bytes[f] == 2)
                    snprintf(s, sizeof s, " %04X", (unsigned)(u[at] | u[at + 1] << 8));
                else
                    snprintf(s, sizeof s, " %02X", u[at]);
                decoded_put(t, s);
                at += PLACE_LIST[list].bytes[f];
            }
            decoded_put(t, "\n");
        }
    }
    while (at < len) {
        unsigned n = len - at < LINE_BYTES ? (unsigned)(len - at) : LINE_BYTES;
        decoded_put(t, "rest ");
        put_hex(t, u + at, n, " ");
        decoded_put(t, "\n");
        at += n;
    }
    return true;
}

typedef enum { FORM_SCRIPT, FORM_PLACES } Form;

static bool form_of(const AssetEntry *e, Form *f)
{
    const char *c = asset_codec_names[e->codec];
    if (!strcmp(c, "lz:event"))
        *f = FORM_SCRIPT;
    else if (!strcmp(c, "lz:place-lists"))
        *f = FORM_PLACES;
    else
        return false;
    return true;
}

static bool decodes(const AssetEntry *e, bool *named)
{
    Form f;
    *named = true;
    return form_of(e, &f);
}

static bool write_resource(DecodedText *t, const AssetStore *store, const AssetEntry *e, const uint8_t *bytes)
{
    (void)store;
    size_t len = 0;
    bool long_runs = false;
    uint8_t *unpacked = packed_unpack(bytes, e->length, &len, &long_runs);
    if (!unpacked)
        return false;
    char line[64];
    snprintf(line, sizeof line, "# %u bytes unpacked\n", (unsigned)len);
    decoded_put(t, line);
    packed_write_form(t, long_runs);
    Form f = FORM_SCRIPT;
    form_of(e, &f);
    bool written = f == FORM_PLACES ? write_places(t, unpacked, (uint32_t)len) : write_script(t, unpacked, (uint32_t)len);
    free(unpacked);
    return written;
}

/* ---- A command's line, read ----------------------------------------------------------------------- */

static bool space(char c) { return c == ' ' || c == '\t'; }

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* `n` hex digits from s, exactly. */
static bool hex_exactly(const char *s, const char *end, unsigned n, uint32_t *v)
{
    if ((size_t)(end - s) != n)
        return false;
    *v = 0;
    for (unsigned i = 0; i < n; i++) {
        int d = hex_digit(s[i]);
        if (d < 0)
            return false;
        *v = *v << 4 | (unsigned)d;
    }
    return true;
}

/* Bytes in hex joined by `between` (a dot, or a space), into out: how many, or -1. */
static int hex_run(const char *s, const char *end, char between, uint8_t *out, unsigned most)
{
    unsigned n = 0;
    while (s < end) {
        uint32_t v;
        if (n == most || end - s < 2 || !hex_exactly(s, s + 2, 2, &v))
            return -1;
        out[n++] = (uint8_t)v;
        s += 2;
        if (s < end && *s++ != between)
            return -1;
        if (s == end && s[-1] == between)
            return -1;
    }
    return (int)n;
}

/* A line's words: its first, and its `key=value` pairs. */
enum { MOST_WORDS = 40, MOST_COMMAND = SCRIPT_ROOM };

typedef struct {
    const char *key, *key_end, *value, *end;
    bool used;
} Pair;

struct Line {
    const char *name, *name_end;
    Pair pair[MOST_WORDS];
    unsigned pairs;
    int hint;       /* the @XX opcode, or -1 */
    int back;       /* whether a jump's place is behind it: -1 not known, 0 or 1 */
};

static const char *split_line(const char *s, const char *end, Line *l)
{
    *l = (Line){ .hint = -1, .back = -1 };
    l->name = s;
    while (s < end && !space(*s))
        s++;
    l->name_end = s;
    while (s < end) {
        while (s < end && space(*s))
            s++;
        if (s == end)
            break;
        const char *w = s;
        while (s < end && !space(*s))
            s++;
        if (*w == '@') {
            uint32_t v;
            if (l->hint >= 0 || !hex_exactly(w + 1, s, 2, &v))
                return "an opcode is @ and two hex digits, once";
            l->hint = (int)v;
            continue;
        }
        const char *eq = memchr(w, '=', (size_t)(s - w));
        if (!eq || eq == w || l->pairs == MOST_WORDS)
            return "a command's parameters are name=value";
        l->pair[l->pairs++] = (Pair){ w, eq, eq + 1, s, false };
    }
    return NULL;
}

static bool word_is(const char *s, const char *end, const char *word)
{
    return (size_t)(end - s) == strlen(word) && !memcmp(s, word, (size_t)(end - s));
}

static Pair *pair_of(Line *l, const char *key)
{
    for (unsigned i = 0; i < l->pairs; i++)
        if (word_is(l->pair[i].key, l->pair[i].key_end, key))
            return &l->pair[i];
    return NULL;
}

/* A jump's place, as the line gives it: a label's name or +XX. */
struct JumpTo {
    bool label;
    const char *name, *end;
    uint8_t distance;
};

/* The command `row` makes of a line, into c (room for MOST_COMMAND): its
 * length, or 0 where the line isn't one of the row's. */
static uint16_t encode(const Row *row, Line *l, uint8_t *c, JumpTo *jump)
{
    uint8_t data[MOST_COMMAND];
    int data_n = 0;
    memset(c, 0, MOST_COMMAND);
    c[0] = row->opcode;
    for (unsigned i = 0; i < l->pairs; i++)
        l->pair[i].used = false;
    for (unsigned i = 0; i < row->params; i++) {
        const Param *p = &row->param[i];
        Pair *v = pair_of(l, p->name);
        size_t prefix = strlen(p->prefix);
        if (!v)
            continue;
        if ((size_t)(v->end - v->value) < prefix || memcmp(v->value, p->prefix, prefix))
            return 0;
        const char *s = v->value + prefix;
        uint32_t value;
        v->used = true;
        if (p->form == P_IMPLIED) {
            if (s != v->end)
                return 0;
        } else if (p->form == P_CONSTANT) {
            if (s == v->end || v->end - s > 4 || !hex_exactly(s, v->end, (unsigned)(v->end - s), &value) ||
                value != p->constant)
                return 0;
        } else if (p->form == P_RAW) {
            int n = hex_run(s, v->end, '.', c + p->operand, (unsigned)(MOST_COMMAND - p->operand));
            if (n <= 0 || (p->bytes && n != p->bytes))
                return 0;
        } else {
            if (!hex_exactly(s, v->end, 2u * p->bytes, &value) || (p->mask && value > p->mask))
                return 0;
            put_operand(c, p, value);
        }
    }
    for (unsigned i = 0; i < l->pairs; i++) {
        Pair *v = &l->pair[i];
        uint32_t k, bits;
        if (v->used)
            continue;
        if (v->key_end - v->key > 4 && !memcmp(v->key, "byte", 4)) {
            const char *n = v->key + 4;
            k = 0;
            while (n < v->key_end && *n >= '0' && *n <= '9' && k < MOST_COMMAND)
                k = k * 10 + (unsigned)(*n++ - '0');
            if (n != v->key_end || k == 0 || k >= row->length || !hex_exactly(v->value, v->end, 2, &bits))
                return 0;
            c[k] |= (uint8_t)bits;
        } else if (word_is(v->key, v->key_end, "data")) {
            data_n = hex_run(v->value, v->end, '.', data, sizeof data);
            if (data_n <= 0)
                return 0;
        } else if (word_is(v->key, v->key_end, "to") && row->jump) {
            *jump = (JumpTo){ false, NULL, NULL, 0 };
            if (v->end - v->value == 3 && v->value[0] == '+' && hex_exactly(v->value + 1, v->end, 2, &bits))
                jump->distance = (uint8_t)bits;
            else if (v->end - v->value > 1 && v->value[0] == 'L')
                *jump = (JumpTo){ true, v->value, v->end, 0 };
            else
                return 0;
            c[row->jump] = jump->distance;
        } else {
            return 0;
        }
        v->used = true;
    }
    if (row->jump && !pair_of(l, "to"))
        return 0;
    /* the command's length, its data after its operands */
    if ((unsigned)row->length + (unsigned)data_n > MOST_COMMAND)
        return 0;
    memcpy(c + row->length, data, (size_t)data_n);
    uint16_t length = length_of(row, c, MOST_COMMAND);
    if (length == 0)
        return 0;
    /* past its operands, a command's bytes are a parameter's or its data */
    bool past = length > row->length && covered(row, row->length, length) != 0xFF;
    if (data_n != (past ? (int)(length - row->length) : 0))
        return 0;
    /* every parameter inside the command given, none outside it, and its bytes its own */
    for (unsigned i = 0; i < row->params; i++) {
        const Param *p = &row->param[i];
        const Pair *v = pair_of(l, p->name);
        if (param_in(p, length) != (v != NULL))
            return 0;
        if (v && p->form == P_RAW && p->bytes == 0 &&
            hex_run(v->value, v->end, '.', data, sizeof data) != (int)(length - p->operand))
            return 0;
    }
    for (unsigned k = length; k < row->length; k++)
        if (c[k])
            return 0;
    return length;
}

/* ---- Reading ------------------------------------------------------------------------------------- */

typedef struct {
    char name[8];
    uint32_t at;
} Label;

typedef struct {
    uint32_t at;          /* the jump's byte, or the table's word */
    bool word, back;
    uint32_t from;        /* where a jump's distance counts from */
    char name[8];
    unsigned line;
} Fixup;

typedef struct {
    int entry;
    Label *labels;
    unsigned labels_n, labels_room;
    Fixup *fixups;
    unsigned fixups_n, fixups_room;
    uint32_t objects;
    unsigned tables;      /* `functions` lines read */
    unsigned list;        /* the list of places being read */
    uint32_t count_at;    /* where its count is */
    bool rest;            /* what follows the lists is being read */
} Reading;

enum { PART_FORM, PART_OBJECTS, PART_TABLE, PART_CODE };

static Reading *reading_of(DecodedReading *r)
{
    Reading *s = r->own;
    if (!s) {
        s = r->own = calloc(1, sizeof *s);
        if (!s)
            return NULL;
        s->entry = -1;
    }
    if (s->entry != r->entry) {
        s->entry = r->entry;
        s->labels_n = s->fixups_n = 0;
        s->objects = 0;
        s->tables = 0;
        s->list = 0;
        s->count_at = 0;
        s->rest = false;
    }
    return s;
}

static void forget(DecodedReading *r)
{
    Reading *s = r->own;
    if (s) {
        free(s->labels);
        free(s->fixups);
        free(s);
    }
    r->own = NULL;
}

static bool label_name(const char *s, const char *end, char out[8])
{
    size_t n = (size_t)(end - s);
    if (n < 2 || n > 7 || *s != 'L')
        return false;
    for (size_t i = 1; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') ||
              s[i] == '_'))
            return false;
    memcpy(out, s, n);
    out[n] = 0;
    return true;
}

static const char *add_fixup(Reading *s, Fixup f)
{
    if (s->fixups_n == s->fixups_room) {
        unsigned room = s->fixups_room ? 2 * s->fixups_room : 64;
        Fixup *more = realloc(s->fixups, room * sizeof *more);
        if (!more)
            return "out of memory";
        s->fixups = more;
        s->fixups_room = room;
    }
    s->fixups[s->fixups_n++] = f;
    return NULL;
}

static const char *add_label(Reading *s, const char name[8], uint32_t at)
{
    for (unsigned i = 0; i < s->labels_n; i++)
        if (!strcmp(s->labels[i].name, name))
            return "that place is named twice";
    if (s->labels_n == s->labels_room) {
        unsigned room = s->labels_room ? 2 * s->labels_room : 64;
        Label *more = realloc(s->labels, room * sizeof *more);
        if (!more)
            return "out of memory";
        s->labels = more;
        s->labels_room = room;
    }
    Label *l = &s->labels[s->labels_n++];
    memcpy(l->name, name, 8);
    l->at = at;
    return NULL;
}

/* The row a line names: the hinted opcode, or the first of its name's rows
 * its parameters fit; and the command made of it. */
static const char *read_command(Line *l, uint8_t *c, uint16_t *length, JumpTo *jump, const Row **found)
{
    /* of the rows the line fits, the shortest, the first of those; a jump
     * to a place goes back to one already named and on to one that isn't */
    *found = NULL;
    for (unsigned op = 0; op < OPCODES; op++) {
        const Row *row = row_of((uint8_t)op);
        if (!row->known || !word_is(l->name, l->name_end, row->name) || (l->hint >= 0 && l->hint != (int)op))
            continue;
        JumpTo to = { false, NULL, NULL, 0 };
        uint16_t n = encode(row, l, c, &to);
        if (n == 0 || (to.label && l->back >= 0 && row->back != (l->back > 0)))
            continue;
        if (!*found || n < *length) {
            *found = row;
            *length = n;
        }
    }
    if (*found) {
        *jump = (JumpTo){ false, NULL, NULL, 0 };
        *length = encode(*found, l, c, jump);
        return NULL;
    }
    for (unsigned op = 0; op < OPCODES; op++)
        if (row_of((uint8_t)op)->known && word_is(l->name, l->name_end, row_of((uint8_t)op)->name))
            return l->hint >= 0 ? "that opcode isn't one of the command's, or its parameters aren't the opcode's"
                                : "the command's parameters aren't any of its forms'";
    return "not a command of the language, a place (Lxxxx:) or bytes";
}

/* The opcode a command's line is read as, without its @XX; -1 for none. */
static int read_as(const char *text, size_t len, bool back)
{
    static uint8_t c[MOST_COMMAND];
    Line l;
    uint16_t length;
    JumpTo jump;
    const Row *row = NULL;
    if (split_line(text, text + len, &l))
        return -1;
    l.back = back;
    if (read_command(&l, c, &length, &jump, &row))
        return -1;
    return row->opcode;
}

/* A list's count put in, the list starting where the made bytes end. */
static const char *start_list(DecodedReading *r, Reading *st)
{
    uint8_t *p = decoded_grow(r, 1);
    if (!p)
        return "out of memory";
    st->count_at = r->made_len - 1;
    return NULL;
}

static const char *const EMPTY_LIST = "every list has a record: the game reads none as 65,536";

/* The lists from `st->list` up to `list` started, each one passed over that
 * has no record refused. */
static const char *lists_up_to(DecodedReading *r, Reading *st, unsigned list)
{
    while (st->list < list) {
        if (r->made[st->count_at] == 0)
            return EMPTY_LIST;
        st->list++;
        const char *wrong = start_list(r, st);
        if (wrong)
            return wrong;
    }
    return NULL;
}

static const char *read_places_line(DecodedReading *r, Reading *st, const char *s, const char *end)
{
    if (r->made_len == 0) {
        const char *wrong = start_list(r, st);
        if (wrong)
            return wrong;
    }
    if (decoded_word(&s, end, "rest")) {
        uint8_t bytes[LINE_BYTES * 4];
        const char *wrong = st->rest ? NULL : lists_up_to(r, st, PLACE_LISTS - 1);
        if (!wrong && !st->rest && r->made[st->count_at] == 0)
            wrong = EMPTY_LIST;
        if (wrong)
            return wrong;
        while (s < end && space(*s))
            s++;
        int n = hex_run(s, end, ' ', bytes, sizeof bytes);
        if (n <= 0)
            return "the rest is bytes in hex, a space between";
        st->rest = true;
        uint8_t *p = decoded_grow(r, (uint32_t)n);
        if (!p)
            return "out of memory";
        memcpy(p, bytes, (size_t)n);
        return r->made_len > r->most ? "more than the cartridge's stream unpacks to" : NULL;
    }
    unsigned list = 0;
    while (list < PLACE_LISTS && !decoded_word(&s, end, PLACE_LIST[list].name))
        list++;
    if (list == PLACE_LISTS || list < st->list || st->rest)
        return "a record is an exit, an event, a halt or a script, the lists in that order, then the rest";
    const char *wrong = lists_up_to(r, st, list);
    if (wrong)
        return wrong;
    if (r->made[st->count_at] == 0xFF)
        return "a list has at most 255 records";
    uint8_t *p = decoded_grow(r, record_bytes(list));
    if (!p)
        return "out of memory";
    for (unsigned f = 0; f < PLACE_LIST[list].fields; f++) {
        unsigned v;
        if (!decoded_number(&s, end, 16, PLACE_LIST[list].bytes[f] == 2 ? 0xFFFF : 0xFF, &v))
            return list == 0 ? "an exit is its column, row, name, location (a word), x and y, in hex"
                   : list == 3 ? "a script is where it starts, a word in hex"
                               : "an event or a halt is its column, row and a byte, in hex";
        *p++ = (uint8_t)v;
        if (PLACE_LIST[list].bytes[f] == 2)
            *p++ = (uint8_t)(v >> 8);
    }
    if (s != end)
        return "nothing goes after a record's fields";
    r->made[st->count_at]++;
    return r->made_len > r->most ? "more than the cartridge's stream unpacks to" : NULL;
}

static const char *read_line(Decoded *d, DecodedReading *r, const char *s, const char *end)
{
    Reading *st = reading_of(r);
    if (!st)
        return "out of memory";
    Form form = FORM_SCRIPT;
    form_of(&asset_catalogue[r->entry], &form);
    if (r->form == PACKED_UNSAID) {
        const char *wrong = packed_read_form(d, r, s, end);
        if (form == FORM_SCRIPT && r->most < SCRIPT_ROOM)
            r->most = SCRIPT_ROOM;
        r->part = PART_OBJECTS;
        return wrong;
    }
    if (form == FORM_PLACES)
        return read_places_line(r, st, s, end);
    if (r->part == PART_OBJECTS) {
        unsigned n;
        if (!decoded_word(&s, end, "objects") || !decoded_number(&s, end, 10, 255, &n) || s != end)
            return "a script starts `objects N`, the count of its objects";
        uint8_t *p = decoded_grow(r, 1);
        if (!p)
            return "out of memory";
        *p = (uint8_t)n;
        st->objects = n;
        r->part = n ? PART_TABLE : PART_CODE;
        return NULL;
    }
    if (r->part == PART_TABLE) {
        unsigned o;
        if (!decoded_word(&s, end, "functions") || !decoded_number(&s, end, 16, 255, &o) || o != st->tables)
            return "then `functions NN` and where each of its 16 functions starts, an object a line, in order";
        for (unsigned f = 0; f < FUNCTIONS; f++) {
            while (s < end && space(*s))
                s++;
            const char *w = s;
            while (s < end && !space(*s))
                s++;
            uint8_t *p = decoded_grow(r, 2);
            uint32_t v;
            char name[8];
            if (!p)
                return "out of memory";
            if (label_name(w, s, name)) {
                const char *wrong = add_fixup(st, (Fixup){ r->made_len - 2, true, false, 0, { 0 }, r->line });
                if (wrong)
                    return wrong;
                memcpy(st->fixups[st->fixups_n - 1].name, name, 8);
            } else if (hex_exactly(w, s, 4, &v)) {
                p[0] = (uint8_t)v;
                p[1] = (uint8_t)(v >> 8);
            } else {
                return "a function starts at a place (Lxxxx) or an offset in four hex digits";
            }
        }
        while (s < end && space(*s))
            s++;
        if (s != end)
            return "an object has 16 functions";
        if (++st->tables == st->objects)
            r->part = PART_CODE;
        return NULL;
    }
    /* the code: a place, bytes, or a command */
    if (end[-1] == ':') {
        char name[8];
        if (!label_name(s, end - 1, name))
            return "a place is L and its name, then a colon";
        return add_label(st, name, offset_of(r->made_len));
    }
    if (decoded_word(&s, end, "bytes")) {
        uint8_t bytes[LINE_BYTES * 4];
        while (s < end && space(*s))
            s++;
        int n = hex_run(s, end, ' ', bytes, sizeof bytes);
        if (n <= 0)
            return "bytes are two hex digits each, a space between";
        uint8_t *p = decoded_grow(r, (uint32_t)n);
        if (!p)
            return "out of memory";
        memcpy(p, bytes, (size_t)n);
        return r->made_len > r->most ? "the script is longer than the game's buffer for it" : NULL;
    }
    Line l;
    const char *wrong = split_line(s, end, &l);
    if (wrong)
        return wrong;
    const Pair *to = pair_of(&l, "to");
    char to_name[8];
    if (to && label_name(to->value, to->end, to_name)) {
        l.back = 0;
        for (unsigned k = 0; k < st->labels_n; k++)
            if (!strcmp(st->labels[k].name, to_name))
                l.back = 1;
    }
    uint8_t c[MOST_COMMAND];
    uint16_t length;
    JumpTo jump;
    const Row *row;
    if ((wrong = read_command(&l, c, &length, &jump, &row)) != NULL)
        return wrong;
    uint8_t *p = decoded_grow(r, length);
    if (!p)
        return "out of memory";
    memcpy(p, c, length);
    if (r->made_len > r->most)
        return "the script is longer than the game's buffer for it";
    if (jump.label) {
        uint32_t at = r->made_len - length + row->jump;
        Fixup f = { at, false, row->back, offset_of(at), { 0 }, r->line };
        if (!label_name(jump.name, jump.end, f.name))
            return "a jump's place is Lxxxx, or its distance +XX";
        return add_fixup(st, f);
    }
    return NULL;
}

static const char *read_end(Decoded *d, DecodedReading *r)
{
    Reading *st = reading_of(r);
    if (!st)
        return "out of memory";
    Form form = FORM_SCRIPT;
    form_of(&asset_catalogue[r->entry], &form);
    if (form == FORM_PLACES) {
        const char *wrong = r->form == PACKED_UNSAID ? "a packed resource starts `packed short` or `packed long`" : NULL;
        if (!wrong && r->made_len == 0)
            wrong = start_list(r, st);
        if (!wrong && !st->rest)
            wrong = lists_up_to(r, st, PLACE_LISTS - 1);
        if (!wrong && !st->rest && r->made[st->count_at] == 0)
            wrong = EMPTY_LIST;
        r->wrong_at = wrong ? r->last_line : 0;
        return wrong ? wrong : packed_end(d, r);
    }
    if (r->part != PART_CODE)
        return "a script has its objects' count and each object's functions";
    for (unsigned i = 0; i < st->fixups_n; i++) {
        const Fixup *f = &st->fixups[i];
        const Label *to = NULL;
        for (unsigned k = 0; k < st->labels_n && !to; k++)
            if (!strcmp(st->labels[k].name, f->name))
                to = &st->labels[k];
        r->wrong_at = f->line;
        if (!to)
            return "no place has that name";
        if (f->word) {
            r->made[f->at] = (uint8_t)to->at;
            r->made[f->at + 1] = (uint8_t)(to->at >> 8);
            continue;
        }
        uint32_t distance = f->back ? f->from - to->at : to->at - f->from;
        if ((f->back ? to->at > f->from : to->at < f->from) || distance > 0xFF)
            return f->back ? "the place is not within 255 bytes before the jump" : "the place is not within 255 bytes after the jump";
        r->made[f->at] = (uint8_t)distance;
    }
    r->wrong_at = 0;
    return packed_end(d, r);
}

const DecodedKind script_text = {
    "scripts",
    "# Chrono Trigger's event scripts and the world maps' places, from your cartridge:\n"
    "# not for sharing. Each location's script: its objects' count, each object's 16\n"
    "# functions (where they start), then its commands, a line each: the command's\n"
    "# name and its parameters as name=value, in hex; a place a jump or a function\n"
    "# names is Lxxxx:, and bytes no command was read from are `bytes`. @XX picks\n"
    "# the opcode where another of the command's forms would read the same. A world\n"
    "# map's places: its exits, events, halts and event scripts, a record a line.\n"
    "# Edited, a resource is packed again as it is read, and must fit.\n"
    "# Edit and start the game again; delete the file to write it afresh.\n",
    decodes, write_resource, read_line, read_end, forget,
};
