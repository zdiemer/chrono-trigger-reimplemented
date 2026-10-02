/* The CPU half of the sound driver, in bank C7.
 *
 * The sound driver proper runs on the SPC700. This is the code on the CPU that
 * uploads it at boot ($C7:0000) and then sends it commands: a caller puts a
 * command in $1E00 and its parameters in $1E01-$1E03 and calls $C7:0004 (the
 * audio service's au_command does exactly that). The driver
 *
 * - sends most commands as they are: the parameters to APU ports 3-1, the
 *   command to port 0, then it waits for the SPC700 to echo the command;
 * - for a song (commands $10, $11, $14 and $15), sends the song's command and
 *   then uploads what the song needs that the SPC700 doesn't have yet: the
 *   blocks it uses, into memory slots after the driver's own data (moving the
 *   blocks it keeps down first), three small tables about them, its sequence,
 *   and its percussion, twelve three-byte entries;
 * - for command $70, switches between the two data sets at SPC700 $2F00;
 * - for command $71, uploads one block into a given slot.
 *
 * Uploads follow Square's protocol on top of the ports. A header gives the
 * SPC700 address on ports 3-2 and a kind on port 1; the data then goes three
 * bytes at a time on ports 1-3. Every message ends with a counter on port 0
 * that the SPC700 echoes; it starts at $E0 for a header and goes up by one,
 * masked to 7 bits ($C7:09DA). The SPC700 answers a header it takes with its
 * kind on port 1; otherwise the driver sends the counter again ($C7:09EA),
 * which starts it over, and repeats the header.
 *
 * How the port is built. Each wait for the SPC700 is a loop in the original
 * that has to keep running on the machine while the SPC700 runs, so the port
 * stops at each one (it hands PC back to the original at the loop, which is an
 * idle entry taking the loop's own time per pass) and the code after a loop is
 * an entry of its own; the counter exchange, which waits, is called out to.
 * Every access to the ports goes through the audio service's driver calls
 * (platform/audio.h), which sync the machine first, since the SPC700 watches
 * when the ports change; a message's port writes are attributed to the
 * exchange that ends it (its JSR $09DA).
 *
 * Each routine is a C function of what it does, in values (sound_driver_* is
 * its registered entry, at the end of the file, which only moves the original's
 * registers in and out), and the functions it calls out to are reached through
 * stubs that set just the registers the callee takes. Work RAM is named from
 * game/ram.h and addressed in bank 0, where the original reaches it through the
 * direct page $1E00 and the data bank $00; every entry but the two the game
 * calls, which set both, is guarded on them. Scratch cells the original uses
 * along the way are still written, since the game can see them. What the
 * driver reads from the ROM goes through core/sound_data.h. */
#include "core/sound_driver.h"

#include "core/sound_data.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "ram.h"

/* ---- Work RAM ---------------------------------------------------------------------- */

#define COMMAND RAM_ABS(RAM_SOUND_COMMAND)
#define PARAMETERS RAM_ABS(RAM_SOUND_COMMAND_PARAMETERS)
#define SONG RAM_ABS(RAM_SOUND_DRIVER_SONG)
#define UNUSED_0A RAM_ABS(RAM_SOUND_DRIVER_UNUSED_0A)
#define WORK RAM_ABS(RAM_SOUND_DRIVER_WORK)
#define SONG_IDS_END RAM_ABS(RAM_SOUND_DRIVER_SONG_IDS_END)
#define SET_2F00 RAM_ABS(RAM_SOUND_DRIVER_2F00_SET)
#define SET_2F00_SENT RAM_ABS(RAM_SOUND_DRIVER_2F00_SET_SENT)
#define DATA_POINTER RAM_ABS(RAM_SOUND_DRIVER_DATA_POINTER)
/* While play_song sorts a song's ids, the pointer's first two bytes and the two
 * after them are two 16-bit pointers into the lists. */
#define LIST_POINTER (DATA_POINTER + 2)
#define LOAD_SLOT RAM_ABS(RAM_SOUND_DRIVER_LOAD_SLOT)
#define LOAD_INDEX RAM_ABS(RAM_SOUND_DRIVER_LOAD_INDEX)
#define SLOT_IDS RAM_ABS(RAM_SOUND_DRIVER_SLOT_IDS)
#define SLOT_LENGTHS RAM_ABS(RAM_SOUND_DRIVER_SLOT_LENGTHS)
#define SLOT_ADDRESSES RAM_ABS(RAM_SOUND_DRIVER_SLOT_ADDRESSES)
#define INDEX_SCRATCH RAM_ABS(RAM_SOUND_DRIVER_INDEX_SCRATCH)
#define COUNTER RAM_ABS(RAM_SOUND_DRIVER_UPLOAD_COUNTER)
#define HEADER_KIND RAM_ABS(RAM_SOUND_DRIVER_HEADER_KIND)
#define RESERVE RAM_ABS(RAM_SOUND_DRIVER_RESERVE)
#define UNUSED_F1 RAM_ABS(RAM_SOUND_DRIVER_UNUSED_F1)
#define SLOT_LIMIT RAM_ABS(RAM_SOUND_DRIVER_SLOT_LIMIT)
#define PENDING_2F00 RAM_ABS(RAM_SOUND_DRIVER_2F00_PENDING)
#define FREE_ADDRESS RAM_ABS(RAM_SOUND_DRIVER_FREE_ADDRESS)
#define BASE_ADDRESS RAM_ABS(RAM_SOUND_DRIVER_BASE_ADDRESS)
#define UNUSED_FA RAM_ABS(RAM_SOUND_DRIVER_UNUSED_FA)
#define UPLOAD_SLOT RAM_ABS(RAM_SOUND_DRIVER_UPLOAD_SLOT)
#define SONG_IDS RAM_ABS(RAM_SOUND_DRIVER_SONG_IDS)
#define MISSING_IDS RAM_ABS(RAM_SOUND_DRIVER_MISSING_IDS)
#define ADDRESS_TABLE RAM_ABS(RAM_SOUND_DRIVER_ADDRESS_TABLE)

enum { DRIVER_PAGE_SIZE = 0x100 };

/* The slot tables hold a word per slot, and the driver indexes them with the
 * slot number times 2, which is what a `slot` holds below. It reaches them
 * through the direct page, so an index past the tables wraps within bank 0;
 * the tables are 16 slots, SLOTS_END past the first. */
enum { SLOTS_END = 16 * 2 };
static uint16_t slot_word(Snes *s, uint16_t table, uint16_t slot) { return m_read16(s, (uint16_t)(table + slot)); }
static void set_slot_word(Snes *s, uint16_t table, uint16_t slot, uint16_t v)
{
    m_write16(s, (uint16_t)(table + slot), v);
}
static uint8_t slot_byte(Snes *s, uint16_t table, uint16_t at) { return m_read8(s, (uint16_t)(table + at)); }
static void set_slot_byte(Snes *s, uint16_t table, uint16_t at, uint8_t v) { m_write8(s, (uint16_t)(table + at), v); }

/* The id lists and the slot tables where the original reaches them through
 * the data bank ($00), so an index past the end runs on into bank 1. */
static uint16_t list_word(Snes *s, uint16_t table, uint16_t at) { return m_read16(s, (uint32_t)table + at); }
static void set_list_word(Snes *s, uint16_t table, uint16_t at, uint16_t v) { m_write16(s, (uint32_t)table + at, v); }

/* ---- The machine at the driver's edges ----------------------------------------------
 *
 * From here to "The protocol": the translation between the original's
 * registers and the C, which a build for the 65816 replaces with its own
 * calling convention. The driver runs with 16-bit index registers throughout;
 * A is 8-bit except where a routine says. */

static void set_widths(Snes *s, bool a16)
{
    s->cpu.p = (uint8_t)((s->cpu.p & ~(CPU_FLAG_M | CPU_FLAG_X)) | (a16 ? 0 : CPU_FLAG_M));
}
static void set_a_low(Snes *s, uint8_t v) { s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | v); }

/* PHP and PLP, for a routine that saves the caller's P at entry and restores it
 * on the way out; with 8-bit index registers, PLP truncates X and Y. */
static void save_p(Snes *s) { m_push8(s, s->cpu.p); }
static void restore_p(Snes *s)
{
    s->cpu.p = m_pull8(s);
    if (s->cpu.p & CPU_FLAG_X) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
}

/* Where a routine hands the machine back to the original: one of its wait
 * loops. The loop compares port 0 with A's low byte, and a counted one gives
 * up after X runs down from $0400. `dead`: what is left as it happens to be
 * (the flags, which every loop sets before it tests them, and the registers
 * the code after the loop sets before reading), which shadow mode doesn't
 * compare there. */
typedef enum {
    WAIT_COMMAND_ECHO,       /* $C7:0635, then $C7:0643 or, given up, $C7:063D */
    WAIT_SONG_ECHO,          /* $C7:0A71, then $C7:0A7B or $C7:0A79 */
    WAIT_SONG_REFUSED,       /* $C7:0A8B: port 0 to change, then $C7:0A90 */
    WAIT_UPLOAD_END,         /* $C7:060C, then $C7:0614 */
    WAIT_SLOT_UPLOAD_END,    /* $C7:08CD, then the change wait $C7:08D5 */
    WAIT_2F00_COMMAND_ECHO,  /* $C7:08FA, then $C7:08FF */
    WAIT_2F00_END,           /* $C7:0912, then the change wait $C7:091A */
} Wait;

static const struct {
    uint16_t loop;
    bool counted;
    uint16_t dead;
} waits[] = {
    [WAIT_COMMAND_ECHO] = { 0x0635, true, DC_AH | DC_Y },
    [WAIT_SONG_ECHO] = { 0x0A71, true, DC_AH },
    [WAIT_SONG_REFUSED] = { 0x0A8B, false, DC_AH | DC_X },
    [WAIT_UPLOAD_END] = { 0x060C, true, DC_AH | DC_Y },
    [WAIT_SLOT_UPLOAD_END] = { 0x08CD, true, DC_AH | DC_Y },
    [WAIT_2F00_COMMAND_ECHO] = { 0x08FA, false, DC_AH | DC_X | DC_Y },
    [WAIT_2F00_END] = { 0x0912, true, DC_AH | DC_Y },
};

enum { ECHO_TRIES = 0x0400 };

static void stop_at(Snes *s, uint16_t loop, uint16_t dead)
{
    m_stop_at(s, 0xC70000u | loop, (uint16_t)(dead | DC_FLAGS));
}

/* How a driver routine ends, which its entry carries out. */
typedef enum {
    END_RETURN,   /* back to the driver's caller (leave_driver) */
    END_WAIT,     /* at `wait`, for `value` */
    END_LOST,     /* a call-out didn't come back: leave the machine where it is */
} EndKind;
typedef struct {
    EndKind kind;
    Wait wait;
    uint8_t value;
} End;

static End end_return(void) { return (End){ END_RETURN, 0, 0 }; }
static End end_lost(void) { return (End){ END_LOST, 0, 0 }; }
static End end_wait(Wait wait, uint8_t value) { return (End){ END_WAIT, wait, value }; }

// $C7:0140
/* The prologue of the entries the game calls (and the boot's copy at
 * $C7:0038): save the caller's data bank, direct page, P, A, X and Y on the
 * stack, 16-bit, and set the data bank $00 and direct page $1E00 the driver
 * runs with. The original leaves A's low byte 0 and X $1E00. */
static void enter_driver(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push16(s, c->d);
    m_push8(s, c->p);
    m_push16(s, c->a);
    m_push16(s, c->x);
    m_push16(s, c->y);
    set_widths(s, false);
    set_a_low(s, 0);
    c->db = 0x00;
    c->d = RAM_PAGE_SOUND;
    c->x = RAM_PAGE_SOUND;
}

// $C7:0192
/* The driver's common exit: the command is done, so clear it, and restore the
 * caller's registers. N and Z come out of the data bank, which is restored
 * last. */
static void leave_driver(Snes *s)
{
    Cpu *c = &s->cpu;
    m_write8(s, COMMAND, 0);
    c->y = m_pull16(s);
    c->x = m_pull16(s);
    c->a = m_pull16(s);
    restore_p(s);
    c->d = m_pull16(s);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rtl(s);
}

static void finish(Snes *s, End e)
{
    if (e.kind == END_RETURN) {
        leave_driver(s);
    } else if (e.kind == END_WAIT) {
        set_widths(s, false);
        set_a_low(s, e.value);
        if (waits[e.wait].counted)
            s->cpu.x = ECHO_TRIES;
        stop_at(s, waits[e.wait].loop, waits[e.wait].dead);
    }
}

/* A JSR from $C7:site to a driver routine that waits for the SPC700: it runs on
 * the machine (so frames pass while it waits), with A as wide as the callee
 * expects it and set where it takes a value (`takes_a`); the rest of the
 * registers and the flags it only saves and restores. False if it didn't come
 * back to the instruction after the JSR with the widths it was called with
 * (APU-direct gave up waiting): the routine then stops where the machine is. */
static bool call_driver(Snes *s, uint16_t site, uint16_t target, bool a16, bool takes_a)
{
    set_widths(s, a16);
    uint16_t untaken = (uint16_t)(DC_X | DC_Y | DC_FLAGS | (takes_a ? DC_AH : DC_A));
    au_driver_call(m_platform(s), PF_CALL(0xC70000u | site, 0xC70000u | target), untaken);
    return cpu_pc24(&s->cpu) == (0xC70000u | (uint16_t)(site + 3)) && !s->cpu.e &&
           (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == (a16 ? 0 : CPU_FLAG_M);
}

/* $C7:09DA: send `counter` on port 0 and wait for its echo; the next counter
 * comes back in A. */
static bool exchange_counter(Snes *s, uint16_t site, uint8_t counter, uint8_t *next)
{
    set_a_low(s, counter);
    if (!call_driver(s, site, 0x09DA, false, true))
        return false;
    *next = (uint8_t)s->cpu.a;
    return true;
}

/* $C7:09EA: the SPC700 didn't take a header: send the counter again, which
 * starts it over at $E0. */
static bool resend_counter(Snes *s, uint16_t site) { return call_driver(s, site, 0x09EA, false, false); }

/* ---- The protocol ---------------------------------------------------------------------- */

enum {
    COUNTER_FIRST = 0xE0,     /* a header's counter, and the end of an exchange */
    COUNTER_START_SLOT = 0xFE, /* command $71's opening counter */
    KIND_TABLE = 2,           /* header kinds: a table of words, two bytes a message */
    KIND_DATA = 3,            /* data, three bytes a message */
    KIND_SLOT_DATA = 5,       /* data for a slot (command $71), or the $2F00 set for command $70 */
    KIND_MOVE = 7,            /* play_song's block moves */
    SOUND_COMMAND_2F00 = 0xFE, /* command $70's message to the SPC700 */
};

static void port_write(Snes *s, uint16_t at, uint8_t port, uint8_t v)
{
    au_driver_write(m_platform(s), PF_AT(0xC70000u | at), port, v);
}
static uint8_t port_read(Snes *s, uint16_t at, uint8_t port)
{
    return au_driver_read(m_platform(s), PF_AT(0xC70000u | at), port);
}

/* A message's two data bytes on ports 1 and 2 (a word, low byte first), or on
 * ports 2 and 3 (an SPC700 address in a header). */
static void send_word_12(Snes *s, uint16_t at, uint16_t v)
{
    port_write(s, at, 1, v & 0xFF);
    port_write(s, at, 2, v >> 8);
}
static void send_word_23(Snes *s, uint16_t at, uint16_t v)
{
    port_write(s, at, 2, v & 0xFF);
    port_write(s, at, 3, v >> 8);
}

/* End a message with the upload counter and keep the next one. */
static bool send_counter(Snes *s, uint16_t site)
{
    uint8_t next;
    if (!exchange_counter(s, site, m_read8(s, COUNTER), &next))
        return false;
    m_write8(s, COUNTER, next);
    return true;
}

/* The same, where the original doesn't keep the next counter (a header with its
 * own first counter follows). */
static bool send_counter_unkept(Snes *s, uint16_t site)
{
    uint8_t next;
    return exchange_counter(s, site, m_read8(s, COUNTER), &next);
}

/* End a message with `first` (a header's $E0), keeping the next. */
static bool send_first_counter(Snes *s, uint16_t site, uint8_t first)
{
    uint8_t next;
    if (!exchange_counter(s, site, first, &next))
        return false;
    m_write8(s, COUNTER, next);
    return true;
}

/* An exchange is over: the next starts with $E0. */
static void end_exchange(Snes *s) { m_write8(s, COUNTER, COUNTER_FIRST); }

/* A header, repeated after $C7:09EA until the SPC700 answers with `kind` on
 * port 1. `site` is its JSR $09DA; the resend is $0C bytes on. */
static bool send_header(Snes *s, uint16_t site, uint16_t to, uint8_t kind)
{
    for (;;) {
        send_word_23(s, site, to);
        port_write(s, site, 1, kind);
        if (!send_counter(s, site))
            return false;
        if (port_read(s, site, 1) == kind)
            return true;
        if (!resend_counter(s, (uint16_t)(site + 0x0C)))
            return false;
    }
}

/* A header the SPC700 isn't asked to confirm, with its own first counter. */
static bool send_table_header(Snes *s, uint16_t site, uint16_t to)
{
    send_word_23(s, site, to);
    port_write(s, site, 1, KIND_TABLE);
    return send_first_counter(s, site, COUNTER_FIRST);
}

/* Where an upload reads its data: the long pointer the original keeps in work
 * RAM, plus an offset (its Y). Where the data may run into the next bank the
 * pointer's bank goes up when the offset wraps (`carry`); the original checks
 * that once and picks one of two loops. */
typedef struct {
    uint32_t pointer;
    uint16_t offset;
    bool carry;
} Upload;

/* Upload from `at` the way the original sets up a resource from a table: the
 * pointer holds just the bank, and the offset the rest. */
static Upload upload_from(Snes *s, uint32_t at)
{
    m_write16(s, DATA_POINTER, 0);
    m_write8(s, DATA_POINTER + 2, (uint8_t)(at >> 16));
    return (Upload){ at & 0xFF0000u, (uint16_t)at, true };
}

/* Upload from `at` with the whole address in the pointer (the boot blocks, the
 * $2F00 sets and play_song's percussion). */
static Upload upload_at(Snes *s, uint32_t at, uint16_t offset)
{
    m_write16(s, DATA_POINTER, (uint16_t)at);
    m_write8(s, DATA_POINTER + 2, (uint8_t)(at >> 16));
    return (Upload){ at, offset, false };
}

static uint8_t upload_byte(Snes *s, Upload *u)
{
    uint8_t v = sound_data_byte(s, u->pointer + u->offset);
    u->offset++;
    if (u->offset == 0 && u->carry) {
        u->pointer += 0x10000u;
        m_write8(s, DATA_POINTER + 2, (uint8_t)(u->pointer >> 16));
    }
    return v;
}

/* A resource's length prefix, which the original keeps in RAM_SOUND_DRIVER_WORK. */
static uint16_t upload_length(Snes *s, Upload *u)
{
    uint16_t length = upload_byte(s, u);
    length |= (uint16_t)(upload_byte(s, u) << 8);
    m_write16(s, WORK, length);
    return length;
}

/* Choose the loop for `length` more bytes: the one that carries into the next
 * bank if the data runs past this one. */
static void upload_plan(Upload *u, uint16_t length) { u->carry = (uint32_t)u->offset + length > 0xFFFF; }

/* Three bytes on ports 1-3 and the counter. */
static bool send_three(Snes *s, Upload *u, uint16_t site)
{
    for (uint8_t port = 1; port <= 3; port++)
        port_write(s, site, port, upload_byte(s, u));
    return send_counter(s, site);
}

/* `length` bytes, three at a time while the count left isn't zero (the
 * original's loop; a length that isn't a multiple of 3 runs on through the
 * count's wrap). `site`, `carry_site`: the two loops' JSR $09DA. */
static bool send_data(Snes *s, Upload *u, uint16_t length, uint16_t site, uint16_t carry_site)
{
    upload_plan(u, length);
    uint16_t left = length;
    do {
        if (!send_three(s, u, u->carry ? carry_site : site))
            return false;
        left = (uint16_t)(left - 3);
    } while (left != 0);
    return true;
}

/* ---- The reserve ------------------------------------------------------------------- */

/* The SPC700's memory from page $F1 - 8 * reserve up is kept free of blocks. */
static uint8_t slot_limit_for(uint8_t reserve) { return (uint8_t)(0xF1 - (reserve << 3)); }

// $C7:0A12
/* Forget the slots from the first whose block ends at or past the slot limit
 * (its id, its length and where it ends, a byte at a time up to the tables'
 * end), stopping at the first slot that ends at page 0: an empty one. */
static void forget_slots_past_limit(Snes *s)
{
    for (uint16_t slot = 0; slot != SLOTS_END; slot += 2) {
        uint8_t end_page = slot_byte(s, SLOT_ADDRESSES + 3, slot);
        if (end_page == 0)
            return;
        if (end_page >= m_read8(s, SLOT_LIMIT)) {
            for (uint16_t at = slot; at != SLOTS_END; at++) {
                set_slot_byte(s, SLOT_IDS, at, 0);
                set_slot_byte(s, SLOT_LENGTHS, at, 0);
                set_slot_byte(s, SLOT_ADDRESSES + 2, at, 0);
            }
            return;
        }
    }
}

// $C7:09FD
/* Set the reserve and the slot limit it makes, and forget what's past it. */
static void set_reserve(Snes *s, uint8_t reserve)
{
    m_write8(s, RESERVE, reserve);
    m_write8(s, SLOT_LIMIT, slot_limit_for(reserve));
    forget_slots_past_limit(s);
}

/* ---- Commands ---------------------------------------------------------------------- */

// $C7:061C
/* Send the command as it is: its parameters to ports 3-1, the command to
 * port 0, and wait for the echo. */
static End send_command(Snes *s)
{
    port_write(s, 0x061C, 3, m_read8(s, PARAMETERS + 2));
    port_write(s, 0x061C, 2, m_read8(s, PARAMETERS + 1));
    port_write(s, 0x061C, 1, m_read8(s, PARAMETERS));
    uint8_t command = m_read8(s, COMMAND);
    port_write(s, 0x061C, 0, command);
    return end_wait(WAIT_COMMAND_ECHO, command);
}

// $C7:0A5A
/* The song's command, sent the same way ($02 and $03 from the song's table). */
static End send_song_command(Snes *s)
{
    port_write(s, 0x0A5A, 3, m_read8(s, PARAMETERS + 2));
    port_write(s, 0x0A5A, 2, m_read8(s, PARAMETERS + 1));
    port_write(s, 0x0A5A, 1, m_read8(s, PARAMETERS));
    uint8_t command = m_read8(s, COMMAND);
    port_write(s, 0x0A5A, 0, command);
    return end_wait(WAIT_SONG_ECHO, command);
}

// $C7:0A39
/* Before a song's blocks go up: its parameter to $02 and its reserve to $03
 * (from its entry at $C7:241D), and a new reserve set if it differs; then the
 * song's command. */
static End send_song(Snes *s)
{
    uint8_t song = m_read8(s, PARAMETERS);
    m_write8(s, PARAMETERS + 1, sound_song_parameter(s, song));
    uint8_t reserve = sound_song_reserve(s, song) & 0x0F;
    m_write8(s, PARAMETERS + 2, reserve);
    if (reserve != m_read8(s, RESERVE))
        set_reserve(s, reserve);
    return send_song_command(s);
}

// $C7:0A7B
/* The SPC700 echoed the song's command. Its answer on port 1 says whether it
 * wants the song: if so, back to play_song (true); if not, it is told so on
 * port 0 with the next counter, and the driver waits for port 0 to move on and
 * returns without the song ($C7:0A90). */
static bool song_echoed(Snes *s, uint8_t echoed, End *refused)
{
    uint8_t counter = (echoed + 1) & 0x7F;
    m_write8(s, COUNTER, counter);
    if (!(port_read(s, 0x0A80, 1) & 0x80))
        return true;
    port_write(s, 0x0A87, 0, counter);
    *refused = end_wait(WAIT_SONG_REFUSED, (uint8_t)(counter - 1));
    return false;
}

// $C7:0734
/* Commands $14 and up, and songs in the list at $C7:0AD8, get bit 2 of the
 * command set. */
static void mark_listed_song(Snes *s)
{
    uint8_t command = m_read8(s, COMMAND);
    bool mark = command >= 0x14;
    for (uint16_t i = 0; !mark; i++) {
        uint8_t listed = sound_marked_song(s, i);
        if (listed & 0x80)
            break;
        mark = listed == m_read8(s, PARAMETERS);
    }
    if (mark)
        m_write8(s, COMMAND, command | 0x04);
}

/* play_song, once the slots are settled. Each returns false where a call-out
 * didn't come back. */

// $C7:0655
/* Upload the block whose id is in slot RAM_SOUND_DRIVER_LOAD_SLOT into it:
 * the slot's address in a data header, then the block. The slot's length is
 * the block's, and the next slot starts where it ends. */
static bool upload_block(Snes *s)
{
    uint16_t slot = m_read16(s, LOAD_SLOT);
    uint16_t id = slot_word(s, SLOT_IDS, slot);
    m_write16(s, INDEX_SCRATCH, (uint16_t)(id - 1));
    Upload u = upload_from(s, sound_block(s, id));
    uint16_t address = slot_word(s, SLOT_ADDRESSES, slot);
    if (!send_header(s, 0x0691, address, KIND_DATA))
        return false;
    uint16_t length = upload_length(s, &u);
    set_slot_word(s, SLOT_LENGTHS, slot, length);
    set_slot_word(s, SLOT_ADDRESSES, (uint16_t)(slot + 2), (uint16_t)(address + length));
    if (!send_data(s, &u, length, 0x070D, 0x06ED))
        return false;
    end_exchange(s);
    return true;
}

// $C7:0326
/* Load the missing blocks into the slots from the first free one (all of them,
 * forgotten, if none is free), then clear the lengths and ends of the slots
 * after the last one loaded; RAM_SOUND_DRIVER_FREE_ADDRESS is where it ends. */
static bool load_missing_blocks(Snes *s)
{
    m_write8(s, UNUSED_0A, 0);
    uint16_t slot = 0;
    while (slot != SLOTS_END && slot_byte(s, SLOT_IDS, slot) != 0)   /* the id's low byte */
        slot += 2;
    if (slot == SLOTS_END) {
        for (uint16_t at = SLOTS_END; at != 0; at--)
            set_slot_byte(s, SLOT_IDS - 1, at, 0);
        slot = 0;
    }
    for (uint16_t entry = 0;;) {
        uint16_t id = list_word(s, MISSING_IDS, entry);
        if (id == 0) {
            m_write16(s, FREE_ADDRESS, slot_word(s, SLOT_ADDRESSES, slot));
            do {
                set_slot_word(s, SLOT_ADDRESSES + 2, slot, 0);
                set_slot_word(s, SLOT_LENGTHS, slot, 0);
                slot += 2;
            } while (slot != SLOTS_END);
            break;
        }
        set_slot_word(s, SLOT_IDS, slot, id);
        m_write16(s, LOAD_SLOT, slot);
        m_write16(s, LOAD_INDEX, entry);
        if (!call_driver(s, 0x034F, 0x0655, true, false))
            return false;
        slot = (uint16_t)(m_read16(s, LOAD_SLOT) + 2);
        entry = (uint16_t)(m_read16(s, LOAD_INDEX) + 2);
        if (entry == SLOTS_END)
            break;
    }
    end_exchange(s);
    return true;
}

// $C7:037B
/* For each of the song's blocks, its $1E80 entry: the address of the slot it's
 * in and that plus the block's address offset, or two zeros if it isn't
 * loaded. The search runs over 32 words from the slot ids, into the lengths. */
static void build_address_table(Snes *s)
{
    uint16_t at = ADDRESS_TABLE;
    for (uint16_t entry = 0; entry != SLOTS_END; entry += 2) {
        uint16_t id = list_word(s, SONG_IDS, entry);
        if (id == 0)
            break;
        uint16_t found = 0;
        while (found != 2 * SLOTS_END && list_word(s, SLOT_IDS, found) != id)
            found += 2;
        uint16_t address = 0, second = 0;
        if (found != 2 * SLOTS_END) {
            address = list_word(s, SLOT_ADDRESSES, found);
            second = (uint16_t)(address + sound_block_address_offset(s, id));
        }
        m_write16(s, at, address);
        m_write16(s, (uint16_t)(at + 2), second);
        at = (uint16_t)(at + 4);
    }
    m_write16(s, DATA_POINTER, at);
}

/* The table built above to SPC700 $1E80, a word a message. */
static bool send_address_table(Snes *s)
{
    if (!send_header(s, 0x03DB, 0x1E80, KIND_TABLE))
        return false;
    for (uint16_t at = 0; at != 0x40; at += 2) {
        send_word_12(s, 0x03FD, m_read16(s, ADDRESS_TABLE + at));
        if (!send_counter(s, 0x03FD))
            return false;
    }
    end_exchange(s);
    return true;
}

// $C7:040D
/* A word per block of the song, from one of the tables at $C7:0C20 and
 * $C7:0C9C, to the SPC700's table at `to` ($1F40, $1FC0). */
static bool send_block_words(Snes *s, uint16_t header_site, uint16_t to, uint16_t (*word)(Snes *, uint16_t),
                             uint16_t site)
{
    if (!send_header(s, header_site, to, KIND_TABLE))
        return false;
    for (uint16_t entry = 0; entry != SLOTS_END; entry += 2) {
        send_word_12(s, site, word(s, list_word(s, SONG_IDS, entry)));
        if (!send_counter(s, site))
            return false;
    }
    end_exchange(s);
    return true;
}

// $C7:04A9
/* Command $71 uploads into the slot before the one holding the song's first
 * block (compared by the id's low byte). The $2F00 set goes up again if the one
 * asked for changed or the last song's sequence ran over it. */
static bool prepare_2f00(Snes *s)
{
    uint8_t first = m_read8(s, SONG_IDS);
    if (first != 0) {
        uint16_t slot = SLOTS_END;
        while (slot != 0 && slot_byte(s, SLOT_IDS - 2, slot) != first)
            slot -= 2;
        m_write16(s, UPLOAD_SLOT, (uint16_t)(slot - 2));
    }
    uint8_t set = m_read8(s, SET_2F00) != 0;
    if (set != m_read8(s, SET_2F00_SENT)) {
        m_write8(s, SET_2F00, set);
        m_write8(s, SET_2F00_SENT, set);
        m_write8(s, PENDING_2F00, (uint8_t)(m_read8(s, PENDING_2F00) + 1));
    }
    if (m_read8(s, PENDING_2F00) == 0)
        return true;
    return call_driver(s, 0x04D8, 0x0922, false, false);
}

// $C7:04DB
/* The song's sequence to SPC700 $2000. One that reaches $2F00 overwrites the
 * set there, which then has to go up again. The loop counts down while the
 * count left isn't negative, so it sends a message more than the length needs. */
static bool send_sequence(Snes *s, uint8_t song)
{
    if (!send_header(s, 0x04EE, 0x2000, KIND_DATA))
        return false;
    m_write16(s, INDEX_SCRATCH, song);
    Upload u = upload_from(s, sound_song(s, song));
    uint16_t length = upload_length(s, &u);
    if (length >= 0x0F00)
        m_write8(s, PENDING_2F00, (uint8_t)(m_read8(s, PENDING_2F00) + 1));
    upload_plan(&u, length);
    uint16_t left = length;
    do {
        if (!send_three(s, &u, u.carry ? 0x056C : 0x058C))
            return false;
        left = (uint16_t)(left - 3);
    } while (!(left & 0x8000));
    end_exchange(s);
    return true;
}

// $C7:059A
/* The song's twelve percussion entries to SPC700 $F120, and the end of the upload: $E0
 * on ports 1 and 0. */
static End send_percussion(Snes *s, uint8_t song)
{
    m_write16(s, INDEX_SCRATCH, (uint16_t)(12 * song));
    Upload u = upload_at(s, sound_song_percussion_table(), (uint16_t)(36 * song));
    if (!send_header(s, 0x05CF, 0xF120, KIND_DATA))
        return end_lost();
    for (int i = 0; i < SOUND_PERCUSSION_ENTRIES; i++)
        if (!send_three(s, &u, 0x05F7))
            return end_lost();
    port_write(s, 0x05FF, 1, COUNTER_FIRST);
    port_write(s, 0x05FF, 0, COUNTER_FIRST);
    return end_wait(WAIT_UPLOAD_END, COUNTER_FIRST);
}

/* The rest of a song's upload, from the blocks on. */
static End song_upload_from_blocks(Snes *s, uint8_t song, bool load_blocks)
{
    if (load_blocks && !load_missing_blocks(s))
        return end_lost();
    build_address_table(s);
    if (!send_address_table(s) || !send_block_words(s, 0x0420, 0x1F40, sound_block_tuning, 0x044B) ||
        !send_block_words(s, 0x046E, 0x1FC0, sound_block_envelope, 0x0499) || !prepare_2f00(s) ||
        !send_sequence(s, song))
        return end_lost();
    return send_percussion(s, song);
}

/* The SPC700 refused the first block move: send it zeros for good measure and
 * forget every slot; all of the song's ids go to both lists, so every block is
 * uploaded again. */
static bool forget_all_slots(Snes *s, uint8_t song)
{
    send_word_23(s, 0x02F0, 0);
    if (!send_counter_unkept(s, 0x02F0))
        return false;
    for (int16_t i = 15; i >= 0; i--) {
        uint16_t id = sound_song_block_id(s, (uint8_t)(song + 1), (int16_t)(i - 16));
        uint16_t at = (uint16_t)(2 * i);
        set_list_word(s, SONG_IDS, at, id);
        set_list_word(s, MISSING_IDS, at, id);
        set_list_word(s, SLOT_IDS, at, 0);
        set_list_word(s, SLOT_LENGTHS, at, 0);
        set_list_word(s, SLOT_ADDRESSES + 2, at, 0);
    }
    return true;
}

/* Move the blocks the song keeps down into the free slots before them, telling
 * the SPC700 each block's address, new address and length (kind 7 on port 1,
 * whose answer after the first move says whether it agrees). */
static bool move_blocks_down(Snes *s, uint8_t song)
{
    port_write(s, 0x025E, 1, KIND_MOVE);
    m_write16(s, WORK, 0);
    for (uint16_t free = 0; free != SLOTS_END; free += 2) {
        if (list_word(s, SLOT_IDS, free) != 0)
            continue;
        uint16_t used = (uint16_t)(free + 2);
        while (used != SLOTS_END && slot_word(s, SLOT_IDS, used) == 0)
            used += 2;
        if (used == SLOTS_END)
            break;
        send_word_23(s, 0x028B, slot_word(s, SLOT_ADDRESSES, used));
        if (!send_counter(s, 0x028B))
            return false;
        if (m_read8(s, WORK) == 0) {
            m_write8(s, WORK, 1);
            if (port_read(s, 0x0298, 1) != KIND_MOVE) {
                if (!forget_all_slots(s, song))
                    return false;
                break;
            }
        }
        set_list_word(s, SLOT_IDS, free, slot_word(s, SLOT_IDS, used));
        set_slot_word(s, SLOT_IDS, used, 0);
        uint16_t address = list_word(s, SLOT_ADDRESSES, free);
        send_word_23(s, 0x02BF, address);
        set_list_word(s, SLOT_ADDRESSES + 2, free, (uint16_t)(address + slot_word(s, SLOT_LENGTHS, used)));
        if (!send_counter(s, 0x02BF))
            return false;
        uint16_t length = slot_word(s, SLOT_LENGTHS, used);
        set_list_word(s, SLOT_LENGTHS, free, length);
        send_word_23(s, 0x02D6, length);
        if (!send_counter(s, 0x02D6))
            return false;
    }
    end_exchange(s);
    return true;
}

// $C7:01A1
/* Commands $10, $11, $14 and $15: play song `$01`. Unless it's out of range,
 * send its command, then list its block ids ($C7:0E11) and those no slot holds.
 * If some are missing: forget the slots it doesn't use, move the rest down, and
 * load the missing blocks after them. Then the tables, the sequence and the
 * entries (song_upload_from_blocks). RAM_SOUND_DRIVER_SONG holds the song
 * meanwhile, which makes the driver ignore commands until the end. */
static End play_song(Snes *s)
{
    uint8_t song = m_read8(s, PARAMETERS);
    if (song >= sound_song_count(s))
        return end_return();
    mark_listed_song(s);
    if (!call_driver(s, 0x01AE, 0x0A39, false, false))
        return end_lost();
    m_write8(s, SONG, song);

    for (uint16_t entry = 0; entry != SLOTS_END; entry += 2) {
        set_list_word(s, SONG_IDS, entry, 0);
        set_list_word(s, MISSING_IDS, entry, 0);
    }
    uint16_t used_end = SLOTS_END;
    while (used_end != 0 && list_word(s, SLOT_IDS - 2, used_end) == 0)
        used_end -= 2;
    m_write16(s, WORK, used_end);
    m_write16(s, SONG_IDS_END, (uint16_t)(32 * song + SLOTS_END));
    uint16_t ids = SONG_IDS, missing = MISSING_IDS;
    for (int16_t i = 0; i < 16; i++) {
        uint16_t id = sound_song_block_id(s, song, i);
        if (id == 0)
            break;
        m_write16(s, ids, id);
        ids += 2;
        uint16_t at = used_end;
        while (at != 0 && list_word(s, SLOT_IDS - 2, at) != id)
            at -= 2;
        if (at == 0) {
            m_write16(s, missing, id);
            missing += 2;
        }
    }
    m_write16(s, DATA_POINTER, ids);
    m_write16(s, LIST_POINTER, missing);
    if (list_word(s, MISSING_IDS, 0) == 0)
        return song_upload_from_blocks(s, song, false);

    for (uint16_t slot = 0; slot != SLOTS_END; slot += 2) {
        uint16_t id = slot_word(s, SLOT_IDS, slot);
        if (id == 0)
            break;
        uint16_t entry = 0;
        while (entry != SLOTS_END && list_word(s, SONG_IDS, entry) != id)
            entry += 2;
        if (entry == SLOTS_END)
            set_slot_word(s, SLOT_IDS, slot, 0);
    }
    uint16_t first_free = 0;
    while (first_free != SLOTS_END && slot_word(s, SLOT_IDS, first_free) != 0)
        first_free += 2;
    m_write16(s, WORK, first_free);
    uint16_t last_end = SLOTS_END;
    while (last_end != 0 && slot_word(s, SLOT_IDS - 2, last_end) == 0)
        last_end -= 2;
    if (last_end != first_free && !move_blocks_down(s, song))
        return end_lost();
    return song_upload_from_blocks(s, song, true);
}

// $C7:0922
/* The $2F00 set RAM_SOUND_DRIVER_2F00_SET asks for, to SPC700 $2F00 (in a
 * slot-data header for command $70, a data header otherwise). */
static bool upload_2f00_set(Snes *s)
{
    uint8_t kind = m_read8(s, COMMAND) == 0x70 ? KIND_SLOT_DATA : KIND_DATA;
    m_write8(s, HEADER_KIND, kind);
    if (!send_header(s, 0x0941, 0x2F00, kind))
        return false;
    bool second = m_read8(s, SET_2F00) != 0;
    uint32_t set = sound_2f00_set(second);
    uint16_t length = sound_data_length(s, set);
    m_write16(s, WORK, length);
    Upload u = upload_at(s, set + 2, 0);
    uint16_t left = length;
    do {
        if (!send_three(s, &u, second ? 0x09C9 : 0x0987))
            return false;
        left = (uint16_t)(left - 3);
    } while (left != 0);
    m_write8(s, PENDING_2F00, 0);
    end_exchange(s);
    return true;
}

// $C7:08E3
/* Command $70: choose the $2F00 set (1 if the parameter is nonzero). If that
 * changes it, tell the SPC700 ($FE) and upload it when that's echoed. */
static End select_2f00_set(Snes *s)
{
    uint8_t set = m_read8(s, PARAMETERS) != 0;
    if (set == m_read8(s, SET_2F00_SENT))
        return end_return();
    m_write8(s, SET_2F00, set);
    m_write8(s, SET_2F00_SENT, set);
    port_write(s, 0x08F7, 0, SOUND_COMMAND_2F00);
    return end_wait(WAIT_2F00_COMMAND_ECHO, SOUND_COMMAND_2F00);
}

// $C7:08FF
/* Command $70's message was echoed: upload the set, and end with $E0. */
static End upload_2f00_after_echo(Snes *s, uint8_t echoed)
{
    m_write8(s, COUNTER, (echoed + 1) & 0x7F);
    if (!call_driver(s, 0x0904, 0x0922, false, false))
        return end_lost();
    port_write(s, 0x0907, 1, COUNTER_FIRST);
    port_write(s, 0x0907, 0, COUNTER_FIRST);
    return end_wait(WAIT_2F00_END, COUNTER_FIRST);
}

// $C7:0755
/* Command $71: upload block `$01` into slot RAM_SOUND_DRIVER_UPLOAD_SLOT, if
 * a song is loaded and the block fits before the next slot: the block (in a
 * slot-data header after $FE), then the slot's $1E80 entry and the block's
 * words for $1F40 and $1FC0, each in a table of one. Nothing is sent again.
 * $02 holds the block's index times 2 from here on. */
static End upload_to_slot(Snes *s)
{
    if (slot_word(s, SLOT_IDS, 0) == 0)
        return end_return();
    uint8_t id = m_read8(s, PARAMETERS);
    if (id == 0)
        return end_return();
    uint16_t index = (uint8_t)(id - 1);
    m_write16(s, INDEX_SCRATCH, index);
    m_write16(s, PARAMETERS + 1, (uint16_t)(2 * index));
    Upload u = upload_from(s, sound_block(s, id));
    uint16_t length = upload_length(s, &u);
    uint16_t slot = m_read16(s, UPLOAD_SLOT);
    if ((uint16_t)(length + slot_word(s, SLOT_ADDRESSES, slot) - 1) >= slot_word(s, SLOT_ADDRESSES, (uint16_t)(slot + 2)))
        return end_return();

    if (!send_first_counter(s, 0x07B2, COUNTER_START_SLOT))
        return end_lost();
    send_word_23(s, 0x07CA, slot_word(s, SLOT_ADDRESSES, slot));
    port_write(s, 0x07CA, 1, KIND_SLOT_DATA);
    if (!send_counter(s, 0x07CA))
        return end_lost();
    set_slot_word(s, SLOT_LENGTHS, slot, length);
    if (!send_data(s, &u, length, 0x081F, 0x07FF))
        return end_lost();

    uint16_t address = slot_word(s, SLOT_ADDRESSES, slot);
    if (!send_table_header(s, 0x083A, 0x1E80))
        return end_lost();
    send_word_12(s, 0x0850, address);
    if (!send_counter(s, 0x0850))
        return end_lost();
    send_word_12(s, 0x086B, (uint16_t)(address + sound_block_address_offset(s, id)));
    if (!send_counter_unkept(s, 0x086B) || !send_table_header(s, 0x087F, 0x1F40))
        return end_lost();
    send_word_12(s, 0x0895, sound_block_tuning(s, id));
    if (!send_counter_unkept(s, 0x0895) || !send_table_header(s, 0x08A9, 0x1FC0))
        return end_lost();
    send_word_12(s, 0x08BF, sound_block_envelope(s, id));
    if (!send_counter_unkept(s, 0x08BF))
        return end_lost();
    port_write(s, 0x08C2, 1, COUNTER_FIRST);
    port_write(s, 0x08C2, 0, COUNTER_FIRST);
    return end_wait(WAIT_SLOT_UPLOAD_END, COUNTER_FIRST);
}

// $C7:0155
/* Dispatch the command in $1E00. While RAM_SOUND_DRIVER_SONG has bit 7 clear
 * (a song is going up) every command is ignored. $80-$FF and $18-$2F go as
 * they are; $10-$17 through the table at $C7:0AD9 ($10, $11, $14 and $15 play
 * a song, the rest nothing); $30-$3F stand for the four bytes at $C7:0A98
 * (a song command), and go round again; $70 and $71 as above. */
static End command(Snes *s)
{
    for (;;) {
        if (!(m_read8(s, SONG) & 0x80))
            return end_return();
        uint8_t command = m_read8(s, COMMAND);
        if (command >= 0x80)
            return send_command(s);
        if (command < 0x10)
            return end_return();
        if (command < 0x18)
            return command & 0x02 ? end_return() : play_song(s);
        if (command < 0x30)
            return send_command(s);
        if (command < 0x40) {
            uint8_t n = command & 0x0F;
            m_write16(s, PARAMETERS + 1, sound_shortcut_command_word(s, n, 1));
            m_write16(s, COMMAND, sound_shortcut_command_word(s, n, 0));
            continue;
        }
        if (command == 0x70)
            return select_2f00_set(s);
        if (command == 0x71)
            return upload_to_slot(s);
        return end_return();
    }
}

/* ---- The boot ------------------------------------------------------------------------ */

enum {
    BOOT_ROM_READY = 0xBBAA,   /* what the SPC700's boot ROM shows on ports 0-1 */
    BOOT_KICK = 0xCC,          /* the boot ROM's start signal */
    DRIVER_START = 0x0200,     /* where the uploaded driver starts */
    BOOT_DELAY = 0x0800,       /* passes of $C7:011B's countdown */
    BOOT_RESERVE = 5,
    SONG_READY = 0xE0,         /* RAM_SOUND_DRIVER_SONG between songs: bit 7 set */
};

// $C7:005F
/* The boot ROM is ready: the first block's address, and the start signal. */
static void boot_start(Snes *s)
{
    send_word_23(s, 0x005F, sound_boot_block_destination(s, 0));
    port_write(s, 0x0072, 1, BOOT_KICK);
    port_write(s, 0x0075, 0, BOOT_KICK);
}

// $C7:007D
/* Block `block` (times 2): point the upload at it, note where it ends (its
 * length plus the two bytes of the length), and send its first byte with index
 * 0. Returns the offset of the byte. */
static uint16_t boot_block(Snes *s, uint16_t block)
{
    uint32_t at = sound_boot_block(s, block);
    Upload u = upload_at(s, at, 0);
    m_write16(s, WORK, (uint16_t)(sound_data_length(s, at) + 2));
    u.offset = 2;
    port_write(s, 0x00A4, 1, upload_byte(s, &u));
    port_write(s, 0x00A8, 0, 0);
    return 2;
}

/* Where the boot goes after a byte is echoed. */
typedef enum { BOOT_NEXT_BYTE, BOOT_NEXT_BLOCK, BOOT_JUMP } BootStep;

// $C7:00B0
/* Byte `index` of the block was echoed. Send the next with the next index, or
 * after the block's last, start the next block at an index 3 on (never 0), or
 * after the last block, tell the boot ROM to run the driver. */
static BootStep boot_next_byte(Snes *s, uint8_t *index, uint16_t *block, uint16_t *offset)
{
    (*index)++;
    (*offset)++;
    if (*offset != m_read16(s, WORK)) {
        uint32_t pointer = (uint32_t)m_read16(s, DATA_POINTER) | (uint32_t)m_read8(s, DATA_POINTER + 2) << 16;
        port_write(s, 0x00A4, 1, sound_data_byte(s, pointer + *offset));
        port_write(s, 0x00A8, 0, *index);
        return BOOT_NEXT_BYTE;
    }
    *index = (uint8_t)(*index + 3);
    if (*index == 0)
        *index = 1;
    *block += 2;
    if (*block != 2 * SOUND_BOOT_BLOCKS) {
        send_word_23(s, 0x00C5, sound_boot_block_destination(s, *block));
        port_write(s, 0x00D5, 1, *index);
        port_write(s, 0x00D8, 0, *index);
        return BOOT_NEXT_BLOCK;
    }
    send_word_23(s, 0x00E2, DRIVER_START);
    port_write(s, 0x00EF, 1, 0);
    port_write(s, 0x00F3, 0, *index);
    return BOOT_JUMP;
}

// $C7:00FB
/* The driver runs: `value` (0) to port 0 and over the driver's page, and the
 * first slot, the free space and the base all start after boot block 2. */
static void boot_started(Snes *s, uint8_t value)
{
    port_write(s, 0x00FC, 0, value);
    for (uint16_t i = 0; i < DRIVER_PAGE_SIZE; i++)
        m_write8(s, (uint16_t)(RAM_PAGE_SOUND + i), value);
    uint16_t base = (uint16_t)(SOUND_BOOT_BLOCK_2_DESTINATION + sound_boot_block_2_length(s));
    m_write16(s, BASE_ADDRESS, base);
    m_write16(s, SLOT_ADDRESSES, base);
    m_write16(s, FREE_ADDRESS, base);
}

// $C7:011E
/* After the countdown: the initial reserve (without forgetting slots: there
 * are none), and ready for songs. */
static void boot_finish(Snes *s)
{
    m_write16(s, UNUSED_FA, 0xC700);
    m_write8(s, RESERVE, BOOT_RESERVE);
    m_write8(s, SLOT_LIMIT, slot_limit_for(BOOT_RESERVE));
    m_write8(s, UNUSED_F1, 0);
    m_write8(s, SONG, SONG_READY);
}

/* ---- Entries ------------------------------------------------------------------------ */

/* The driver's code: APU-direct runs it while PC stays in here. */
#define DRIVER_FIRST 0xC70000
#define DRIVER_LAST 0xC70FFF

/* Every entry but the two the game calls runs with the driver's data bank and
 * direct page, which it reaches work RAM and the ports through. */
bool sound_driver_frame_is_set(Snes *s) { return s->cpu.db == 0x00 && s->cpu.d == RAM_PAGE_SOUND; }

/* Command $70, the one whose choice the driver keeps where the game also
 * writes it (the set of effects the next song brings, RAM_SOUND_DRIVER_2F00_SET). */
enum { SELECT_2F00_SET = 0x70 };

/* A host playing a driver of its own took the call (au_driver_boot_taken):
 * back to the caller with its registers, as the driver's exit leaves them,
 * and the command done. The set of effects asked for is kept as the
 * original keeps it: none after the boot, and command $70's choice. */
static void taken_by_host(Snes *s)
{
    m_write8(s, COMMAND, 0);
    m_set_nz8(s, s->cpu.db);
    m_rtl(s);
}

// $C7:0000
/* The JSL that uploads the driver (a JMP to $C7:0038): the driver's frame, and
 * X = what the boot ROM will show when it's ready, for the loop at $C7:0050. */
void sound_driver_boot(Snes *s)
{
    if (au_driver_run_direct(m_platform(s), DRIVER_FIRST, DRIVER_LAST))
        return;
    if (au_driver_boot_taken(m_platform(s))) {
        m_write8(s, SET_2F00, 0);
        taken_by_host(s);
        return;
    }
    enter_driver(s);
    s->cpu.x = BOOT_ROM_READY;
    stop_at(s, 0x0050, DC_A);
}

void sound_driver_boot_start(Snes *s)
{
    boot_start(s);
    s->cpu.x = 0;
    set_a_low(s, BOOT_KICK);
    stop_at(s, 0x0078, DC_AH);
}

void sound_driver_boot_block(Snes *s)
{
    s->cpu.y = boot_block(s, s->cpu.x);
    set_a_low(s, 0);
    stop_at(s, 0x00AB, DC_AH);
}

void sound_driver_boot_next_byte(Snes *s)
{
    uint8_t index = (uint8_t)s->cpu.a;
    uint16_t block = s->cpu.x, offset = s->cpu.y;
    BootStep step = boot_next_byte(s, &index, &block, &offset);
    s->cpu.x = block;
    s->cpu.y = offset;
    if (step == BOOT_NEXT_BYTE) {
        set_a_low(s, index);
        stop_at(s, 0x00AB, DC_AH);
    } else if (step == BOOT_NEXT_BLOCK) {
        set_a_low(s, index);
        stop_at(s, 0x00DB, DC_AH | DC_Y);
    } else {
        s->cpu.a = index;   /* the high byte is read after the wait */
        stop_at(s, 0x00F6, DC_X | DC_Y);
    }
}

void sound_driver_boot_started(Snes *s)
{
    boot_started(s, (uint8_t)(s->cpu.a >> 8));
    set_widths(s, true);
    s->cpu.x = BOOT_DELAY;
    stop_at(s, 0x011B, DC_A | DC_Y);
}

void sound_driver_boot_finish(Snes *s)
{
    boot_finish(s);
    leave_driver(s);
}

// $C7:0004
/* The JSL the game sends every command through (a JMP to $C7:0140). With
 * APU-direct the whole command runs synchronously. */
void sound_driver_command(Snes *s)
{
    if (au_driver_run_direct(m_platform(s), DRIVER_FIRST, DRIVER_LAST))
        return;
    if (au_driver_hosted(m_platform(s))) {
        AudioCommand c = { .command = m_read8(s, COMMAND), .p1 = m_read8(s, PARAMETERS),
                           .p2 = m_read8(s, PARAMETERS + 1), .p3 = m_read8(s, PARAMETERS + 2), .count = 3 };
        au_driver_command_taken(m_platform(s), c, m_read8(s, SET_2F00));
        if (c.command == SELECT_2F00_SET)
            m_write8(s, SET_2F00, c.p1 != 0);
        taken_by_host(s);
        return;
    }
    enter_driver(s);
    finish(s, command(s));
}

// $C7:063D
/* The command's echo didn't come: send it again, unless it's been cleared. */
void sound_driver_command_timed_out(Snes *s)
{
    finish(s, m_read8(s, COMMAND) == 0 ? end_return() : send_command(s));
}

// $C7:0643
/* The command was echoed: a $FC command's parameter sets a new reserve. */
void sound_driver_command_echoed(Snes *s)
{
    if ((uint8_t)s->cpu.a == 0xFC) {
        uint8_t reserve = m_read8(s, PARAMETERS) & 0x0F;
        if (reserve != m_read8(s, RESERVE))
            set_reserve(s, reserve);
    }
    leave_driver(s);
}

// $C7:0614
/* The song's upload ended (echoed or not): the driver is ready again, with
 * the value it waited for ($E0) as the song, and forgets what's past the
 * reserve. */
void sound_driver_upload_finished(Snes *s)
{
    m_write8(s, SONG, (uint8_t)s->cpu.a);
    forget_slots_past_limit(s);
    leave_driver(s);
}

/* The subroutines return as they were called, with P as it was. $C7:0655
 * saves it on the stack, where it stays under the return addresses of the
 * calls it makes. */
void sound_driver_upload_block(Snes *s)
{
    save_p(s);
    if (!upload_block(s))
        return;
    restore_p(s);
    m_rts(s);
}

void sound_driver_upload_2f00_data(Snes *s)
{
    if (upload_2f00_set(s))
        m_rts(s);
}

void sound_driver_mark_listed_song(Snes *s)
{
    uint8_t p = s->cpu.p;
    mark_listed_song(s);
    s->cpu.p = p;
    m_rts(s);
}

void sound_driver_set_reserve(Snes *s)
{
    uint8_t p = s->cpu.p;
    set_reserve(s, (uint8_t)s->cpu.a);
    s->cpu.p = p;
    m_rts(s);
}

void sound_driver_forget_slots_past_limit(Snes *s)
{
    uint8_t p = s->cpu.p;
    forget_slots_past_limit(s);
    s->cpu.p = p;
    m_rts(s);
}

void sound_driver_2f00_command_echoed(Snes *s) { finish(s, upload_2f00_after_echo(s, (uint8_t)s->cpu.a)); }

// $C7:08DA
/* Command $71 is done: the slot holds the block (its id's low byte). */
void sound_driver_slot_uploaded(Snes *s)
{
    set_slot_byte(s, SLOT_IDS, m_read16(s, UPLOAD_SLOT), m_read8(s, PARAMETERS));
    leave_driver(s);
}

/* The song's command goes in a frame of its own: P, saved here, comes back in
 * $C7:0A7B's return. */
void sound_driver_send_song(Snes *s)
{
    save_p(s);
    finish(s, send_song(s));
}

// $C7:0A79
void sound_driver_song_timed_out(Snes *s) { finish(s, send_song_command(s)); }

void sound_driver_song_echoed(Snes *s)
{
    End refused;
    if (!song_echoed(s, (uint8_t)s->cpu.a, &refused)) {
        finish(s, refused);
        return;
    }
    restore_p(s);
    m_stop_dont_care(s, 0xC70A97, DC_A);   /* the RTS after $C7:0A96's PLP */
    m_rts(s);
}

// $C7:0A90
/* Refused: drop the song's frame and play_song's return, and leave. */
void sound_driver_song_refused(Snes *s)
{
    s->cpu.s = (uint16_t)(s->cpu.s + 3);
    leave_driver(s);
}

void sound_driver_return(Snes *s) { leave_driver(s); }

// $C7:09DA
/* Send the counter in A on port 0 in a frame of its own, and wait for the
 * echo ($C7:09E0). */
void sound_driver_send_counter(Snes *s)
{
    save_p(s);
    s->cpu.p |= CPU_FLAG_M;
    port_write(s, 0x09DD, 0, (uint8_t)s->cpu.a);
    stop_at(s, 0x09E0, 0);
}

// $C7:09E5
/* Echoed: the next counter, and back. */
void sound_driver_counter_echoed(Snes *s)
{
    set_a_low(s, (uint8_t)((s->cpu.a + 1) & 0x7F));
    restore_p(s);
    m_rts(s);
}

// $C7:09EA
/* Send the current counter again, likewise ($C7:09F2). */
void sound_driver_resend_counter(Snes *s)
{
    save_p(s);
    s->cpu.p |= CPU_FLAG_M;
    uint8_t counter = m_read8(s, COUNTER);
    port_write(s, 0x09EF, 0, counter);
    set_a_low(s, counter);
    stop_at(s, 0x09F2, 0);
}

// $C7:09F7
/* Echoed: the next exchange starts over. */
void sound_driver_counter_resent(Snes *s)
{
    end_exchange(s);
    set_a_low(s, COUNTER_FIRST);
    restore_p(s);
    m_rts(s);
}

/* ---- Wait loops: idle entries, one pass each ----------------------------------------
 *
 * A pass doesn't reproduce the flags the loop's compare leaves (nor the boot
 * ROM loop's A): the loop sets them again before testing, and the code after
 * it, all entries here, doesn't read them (their registry entries say so). */

/* Has the SPC700 put `value` on port 0? */
static bool echoed(Snes *s, uint16_t at, uint8_t value) { return port_read(s, at, 0) == value; }

/* One pass of a counted echo wait: on to `echo` when port 0 shows A's low
 * byte, or to `given_up` when X runs out. */
static void counted_echo_wait(Snes *s, uint16_t at, uint16_t echo, uint16_t given_up)
{
    if (echoed(s, at, (uint8_t)s->cpu.a))
        s->cpu.pc = echo;
    else if (--s->cpu.x == 0)
        s->cpu.pc = given_up;
}

static void echo_wait(Snes *s, uint16_t at, uint16_t echo)
{
    if (echoed(s, at, (uint8_t)s->cpu.a))
        s->cpu.pc = echo;
}

/* One pass of a wait for port 0 to change from A's low byte. */
static void change_wait(Snes *s, uint16_t at, uint16_t changed)
{
    if (!echoed(s, at, (uint8_t)s->cpu.a))
        s->cpu.pc = changed;
}

// $C7:0050
/* The boot ROM shows $BBAA (X) on ports 0-1 when it's ready; meanwhile the
 * driver keeps writing $F0 to port 1 and $FF to port 0. */
void sound_driver_boot_wait_ready_loop(Snes *s)
{
    port_write(s, 0x0052, 1, 0xF0);
    port_write(s, 0x0057, 0, 0xFF);
    uint16_t shown = port_read(s, 0x005A, 0);
    shown |= (uint16_t)(port_read(s, 0x005A, 1) << 8);
    if (shown == s->cpu.x)
        s->cpu.pc = 0x005F;
}

// $C7:0078
void sound_driver_boot_wait_start_loop(Snes *s) { echo_wait(s, 0x0078, 0x007D); }
// $C7:00AB
void sound_driver_boot_wait_byte_loop(Snes *s) { echo_wait(s, 0x00AB, 0x00B0); }
// $C7:00DB
/* Exits to the BRA back to $C7:007D. */
void sound_driver_boot_wait_block_loop(Snes *s) { echo_wait(s, 0x00DB, 0x00E0); }
// $C7:00F6
void sound_driver_boot_wait_jump_loop(Snes *s) { echo_wait(s, 0x00F6, 0x00FB); }

// $C7:011B
/* A countdown of $0800 passes that gives the SPC700 time to start the driver
 * before the first command: a wait on time alone, kept as the loop so the time
 * passes (docs/TODO.md). */
void sound_driver_boot_delay_loop(Snes *s)
{
    if (--s->cpu.x == 0)
        s->cpu.pc = 0x011E;
}

// $C7:0635
void sound_driver_wait_command_echo_loop(Snes *s) { counted_echo_wait(s, 0x0635, 0x0643, 0x063D); }
// $C7:0A71
void sound_driver_wait_song_echo_loop(Snes *s) { counted_echo_wait(s, 0x0A71, 0x0A7B, 0x0A79); }
// $C7:060C
void sound_driver_wait_upload_echo_loop(Snes *s) { counted_echo_wait(s, 0x060C, 0x0614, 0x0614); }
// $C7:08CD
void sound_driver_wait_slot_upload_echo_loop(Snes *s) { counted_echo_wait(s, 0x08CD, 0x08D5, 0x08D5); }
// $C7:0912
void sound_driver_wait_2f00_echo_loop(Snes *s) { counted_echo_wait(s, 0x0912, 0x091A, 0x091A); }
// $C7:09E0
void sound_driver_wait_echo_loop(Snes *s) { echo_wait(s, 0x09E0, 0x09E5); }
// $C7:09F2
void sound_driver_wait_resend_echo_loop(Snes *s) { echo_wait(s, 0x09F2, 0x09F7); }
// $C7:08FA
void sound_driver_wait_2f00_command_echo_loop(Snes *s) { echo_wait(s, 0x08FA, 0x08FF); }
// $C7:0A8B
void sound_driver_wait_port0_change_loop(Snes *s) { change_wait(s, 0x0A8B, 0x0A90); }
// $C7:08D5
void sound_driver_wait_slot_upload_change_loop(Snes *s) { change_wait(s, 0x08D5, 0x08DA); }
// $C7:091A
/* Exits to the JMP to $C7:0192. */
void sound_driver_wait_2f00_change_loop(Snes *s) { change_wait(s, 0x091A, 0x091F); }
