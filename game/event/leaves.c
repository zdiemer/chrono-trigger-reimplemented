/* What each command leaves for the original's sake (event/leaves.h), by
 * opcode: a table parallel to the language's, of what isn't the language's,
 * and the runner that works it out around the operation: before it, where
 * the parse stops and what is kept in the scratch cells; after it, the Y and
 * V that follow from the command, what it did and the state before and after.
 * The build without the original's effects has none of it. */
#include "event/leaves.h"
#include "cpu_io.h"
#include "event/contact.h"
#include "event/party.h"
#include "event/variables.h"
#include "field/field.h"
#include "field/layers.h"
#include "field/parts.h"
#include "field/palettes.h"
#include "field/party.h"
#include "port/vars_ram.h"

#if ORIGINAL_EFFECTS

/* The language's parameters by name, for what the table keeps of one. */
#define EVENT_PARAMETER_ID(type, name) PARAMETER_##name,
typedef enum { EVENT_PARAMETERS(EVENT_PARAMETER_ID) EVENT_PARAMETER_COUNT } EventParameter;
#undef EVENT_PARAMETER_ID

static uint32_t parameter(const EventCommand *c, EventParameter which)
{
    switch (which) {
#define PARAMETER_VALUE(type, name) case PARAMETER_##name: return (uint32_t)c->name;
    EVENT_PARAMETERS(PARAMETER_VALUE)
#undef PARAMETER_VALUE
    default: return 0;
    }
}

/* A value the original keeps in a scratch cell before the operation runs: one
 * of the operation's parameters, as a byte or a word; one of the command's
 * operand bytes as it is; or where one of its operands is. */
typedef enum { KEEP_NOTHING, KEEP_BYTE, KEEP_WORD, KEEP_OPERAND_BYTE, KEEP_OPERAND_OFFSET } KeptAs;

typedef struct {
    KeptAs as;
    EventParameter parameter;   /* KEEP_BYTE, KEEP_WORD */
    uint8_t operand;            /* KEEP_OPERAND_BYTE, KEEP_OPERAND_OFFSET */
} Kept;

/* The V flag the command leaves: as it found it, or the overflow of adding
 * the amount to the variable (or taking it away), in the command's width. */
typedef enum { V_AS_IT_WAS, V_OF_ADDING, V_OF_SUBTRACTING } OverflowLeft;

/* What a derivation looks at before the operation runs, where the operation
 * changes it: whether the command's work reaches past its first test (a call
 * the target takes, a move or a change that starts, a window that opens), and
 * whether a call names an empty place. */
typedef struct {
    bool starts;
    bool to_nobody;
    bool moved_on;   /* a party member took the next footprint of the trail */
    Touch touch;     /* what the leader's walking into an object did */
    bool looked_to_push;
    uint8_t length;   /* the command's bytes, as its row says */
    bool colour_effect_running;   /* the object remembered an effect on its colours */
    bool slot_free;               /* a palette animation slot was free for an effect, and which */
    uint8_t slot;
} Noted;

typedef void (*NoteBefore)(const GameState *g, const EventCommand *c, Noted *n);
typedef void (*DeriveAfter)(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l);

typedef struct {
    uint8_t parse;            /* the operand the original's parse stops at, going on: its Y */
    uint8_t parse_on_jump;    /* ... and taking its jump */
    bool shared_comparison;   /* made by the original's comparison routine, which notes where the jump
                               * byte is and runs with 8-bit index registers (Y keeps its low byte) */
    Kept first, second;       /* what it keeps in its first two scratch cells */
    bool offset_kept_last;    /* it keeps its own offset in the last cell, across a call it makes */
    OverflowLeft overflow;
    NoteBefore note;          /* what its derivation needs of the state before */
    DeriveAfter derive;       /* the rest of its Y and V, after */
} OriginalLeaves;

/* ---- The derivations ------------------------------------------------------------------------ */

/* Calls ($02-$07): the original works out the index of a place with
 * additions that never overflow (eight levels, 64 objects), so V is clear
 * whenever the call reached the target's places; and a call to a party
 * member whose place is empty stops its parse after the call byte. */
static void note_call(const GameState *g, const EventCommand *c, CallKind kind, Noted *n)
{
    n->starts = event_call_reaches_the_places(g, kind, c->self, c->target, c->there, c->level);
    n->to_nobody = !c->there && !(kind == CALL_AND_WAIT && event_call_waiting(g, c->self));
}
static void note_call_and_go(const GameState *g, const EventCommand *c, Noted *n) { note_call(g, c, CALL_AND_GO, n); }
static void note_call_when_taken(const GameState *g, const EventCommand *c, Noted *n)
{
    note_call(g, c, CALL_WHEN_TAKEN, n);
}
static void note_call_and_wait(const GameState *g, const EventCommand *c, Noted *n) { note_call(g, c, CALL_AND_WAIT, n); }

static void derive_call(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)g, (void)c, (void)flow;
    if (n->starts)
        l->overflow = false;
}

static void derive_call_that_waits(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    derive_call(g, c, flow, n, l);
    if (n->to_nobody)
        l->y = (uint16_t)(c->at + 3);
}

/* A return ($00) that resumes something searched the places the same way. */
static void derive_return(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)g, (void)c, (void)n;
    if (flow.kind == FLOW_GO_TO)
        l->overflow = false;
}

/* Facing another object ($A8, $A9): the field's direction routine's overflow
 * (field/scratch.h), from the tiles, which the turn doesn't move. */
static void derive_face_toward(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)n;
    if (flow.kind != FLOW_END_TURN)
        return;
    Aim aim = aim_toward_object(g, c->self, c->target);
    l->overflow = field_direction_overflow(aim, direction_between(g, aim.from, aim.to));
}

/* The screen test ($28, and a walk until well on screen, $8F): where the
 * tile is well on screen, the overflow of its last subtraction. $8F arrives
 * by it where the walker is neither next to the member it walks to nor
 * finds nobody to walk to; otherwise it set off, and says how itself. */
static void derive_screen_test(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow, (void)n;
    MapTile tile = object_tile(g, c->object);
    if (tile_well_on_screen(g, tile))
        l->overflow = field_screen_test_overflow(g, tile);
}

static void derive_arrived_on_screen(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)n;
    MapTile own = object_tile(g, c->self);
    Someone quarry = member_in_place(g, c->member, c->self);
    if (flow.kind == FLOW_NEXT && quarry.there && !quarry.disabled && !tiles_next_to(own, tile_at(quarry.at)))
        l->overflow = field_screen_test_overflow(g, own);
}

/* Moves that start with a velocity of their own ($7A, $7B), and a change of
 * location worked out in variables ($E2): they keep their offset, and the
 * parse stops where their start does. */
static void note_move_starts(const GameState *g, const EventCommand *c, Noted *n)
{
    n->starts = !object_scripted_move_marked(g, c->self);
}

static void derive_launch(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow;
    if (!n->starts)
        return;
    event_keep_offset(g, c->at);
    l->y = field_current_object(g);   /* the original's parse ends on the object */
}

static void derive_jump(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow;
    if (!n->starts)
        return;
    event_keep_offset(g, c->at);
    l->y = (uint16_t)(c->at & 0x00FF);   /* heading the object clears Y's high byte */
    l->overflow = field_jump_overflow(g, c->self);
}

static void note_change_asked(const GameState *g, const EventCommand *c, Noted *n)
{
    (void)c;
    n->starts = !(field_requests(g) & FIELD_REQUEST_LOCATION_CHANGE);
}

static void derive_change_asked(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow;
    if (!n->starts)
        return;
    event_keep_offset(g, c->at);
    l->y = (uint16_t)(c->at + 4);
}

/* A question ($C0, $C3, $C4) keeps its choices' operand as it opens its
 * window. */
static void note_question_opens(const GameState *g, const EventCommand *c, Noted *n)
{
    n->starts = event_question_opens(g, c->self);
}

static void derive_question(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow, (void)l;
    if (n->starts)
        keep_byte(g, KEPT_FIRST, script_byte(g, (uint16_t)(c->at + 2)));
}

/* Drifting the layers ($E6) loads Y with the speed to store it, for each
 * layer it drifts. */
enum { DRIFTING_LAYERS = 0x07 };

static void derive_drift(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)g, (void)flow, (void)n;
    if (c->layers & DRIFTING_LAYERS)
        l->y = c->drift;
}

/* The searchlight's moves ($FE): the original works the eight coordinates
 * out one after another (each corner's x, then its y) in the field page's
 * cells, dividing each one's distance by the frames in the divider, and
 * leaves the last there: no coordinates left, the last one's start, the
 * frames, the offset past the corners' table, where its parse read up to,
 * the division of the last coordinate that moves, and the overflow of the
 * last coordinate's distance, its end less its start in 16 bits. */
enum { SEARCHLIGHT_LENGTH = 18, COORDINATES = 2 * SEARCHLIGHT_CORNERS, COORDINATE_BYTES = 2 };

typedef struct {
    uint8_t from, to;
} Coordinate;

static Coordinate searchlight_coordinate(const SearchlightMove *m, unsigned i)
{
    unsigned corner = i / 2;
    return (i & 1) ? (Coordinate){ m->from[corner].y, m->to[corner].y }
                   : (Coordinate){ m->from[corner].x, m->to[corner].x };
}

static void derive_searchlight(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow, (void)n;
    Coordinate last = searchlight_coordinate(&c->searchlight, COORDINATES - 1);
    l->overflow = field_subtract_overflows16((uint16_t)(last.to << 8), (uint16_t)(last.from << 8));
    set_event_searchlight_work_coordinates_left(g, 0);
    set_event_searchlight_work_start(g, (uint16_t)(last.from << 8));
    set_event_searchlight_work_frames(g, c->frames);
    set_event_searchlight_work_next(g, COORDINATES * COORDINATE_BYTES);
    event_keep_offset(g, (uint16_t)(c->at + SEARCHLIGHT_LENGTH));
    for (unsigned i = COORDINATES; i-- > 0;) {
        Coordinate k = searchlight_coordinate(&c->searchlight, i);
        if (k.from != k.to) {
            hw_set_dividend(g, (uint16_t)((k.to > k.from ? k.to - k.from : k.from - k.to) << 8));
            hw_divide_by(g, c->frames);
            break;
        }
    }
}

/* The party's step ($B0, and $AF once): the step's routines run with 8-bit
 * index registers, and leave Y the one stepping (for a member, the link of
 * its place, which can be emptied under it), or where the leader looked
 * to push what it walked into, that; and the overflow of the leader's or the
 * member's step (field/party.h), or clear where the leader's touch worked
 * out a place among the calls'. The party holding still leaves both. */
static void note_party_step(const GameState *g, const EventCommand *c, Noted *n)
{
    if (field_screen_recentring(g) != 0 || !object_is_party_member(g, c->self))
        return;
    PartyPlace place = object_party_place(g, c->self);
    if (place != PARTY_LEADER)
        n->moved_on = party_member_moves_on(g, place);
    else if (leader_has_contact(g)) {
        n->touch = leaders_touch(g);
        n->looked_to_push = leader_looks_to_push(g, n->touch.touched);
    }
}

static void derive_party_step(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow;
    if (field_screen_recentring(g) != 0)
        return;
    event_keep_offset(g, c->at);
    if (!object_is_party_member(g, c->self)) {
        l->y = (uint16_t)(c->at & 0x00FF);
        return;
    }
    PartyPlace place = object_party_place(g, c->self);
    if (place != PARTY_LEADER) {
        l->y = field_link_byte(party_member_link(g, place));
        l->overflow = field_member_step_overflow(g, place, n->moved_on);
        return;
    }
    l->overflow = field_leader_step_overflow(g, l->overflow);
    l->y = event_touch_y(g, n->looked_to_push, field_object_offset(c->self));
    l->overflow = event_place_worked_out_overflow(n->touch.touched || n->touch.pushed, l->overflow);
}

/* The party's regrouping ($D9, $DA; event/party.c): each keeps its offset;
 * sending the party off, it hands each its script with 8-bit index
 * registers, which leaves Y's low byte, and works out a place among the
 * calls', which leaves the overflow clear; the formation's parse is past its
 * places when it is over, and the gathering keeps its offset in a second
 * cell as it starts. */
static void note_formation_starts(const GameState *g, const EventCommand *c, Noted *n)
{
    (void)c;
    n->starts = variable_byte(g, FORMATION_COUNT) == 0;
}

static void derive_formation(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    event_keep_offset(g, c->at);
    if (n->starts) {
        l->y = (uint16_t)(c->at & 0x00FF);
        l->overflow = false;
    } else if (flow.kind == FLOW_END_TURN) {
        l->y = (uint16_t)(c->at + n->length);
    }
}

static void note_gathering_starts(const GameState *g, const EventCommand *c, Noted *n)
{
    (void)c;
    n->starts = place_taken(g, PARTY_FIRST_MEMBER) && !field_party_gathering(g);
}

static void derive_gathering(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow;
    event_keep_offset(g, c->at);
    if (!n->starts)
        return;
    original_cell16(g, 0xC3, c->at);
    l->y = (uint16_t)(c->at & 0x00FF);
    l->overflow = false;
}

/* A tile copy made now ($E4): the command keeps its last byte's offset, and
 * the copy leaves Y and the overflow (field/layers.h). */
static void derive_tile_copy(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow, (void)n;
    event_keep_offset(g, (uint16_t)(c->at + n->length - 1));
    l->y = field_tile_copy_y(g, l->y);
    l->overflow = field_tile_copy_overflow(g, l->overflow);
}

#define PARSE(n) .parse = (n), .parse_on_jump = (n)
/* Casting a playable character ($57-$6D, $80): the original reads the
 * character's sprite record where the character is in the party, which ends
 * with Y at the command's end, kept, and the overflow of its sums, clear;
 * where the character isn't, it only notes the character, and Y is where its
 * parse got. */
static bool character_in_the_party(const GameState *g, uint8_t character)
{
    for (unsigned place = 0; place < PARTY_PLACES; place++)
        if (party_members(g, place) == character)
            return true;
    return false;
}

static void derive_character_cast(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow;
    if (!character_in_the_party(g, c->character)) {
        keep_byte(g, KEPT_FIRST, c->character);
        l->y = (uint16_t)(c->at + n->length - 1);
        return;
    }
    l->y = (uint16_t)(c->at + n->length);
    l->overflow = false;
    event_keep_offset(g, l->y);
}

/* ... and apart from the party ($81), always. */
static void derive_character_apart(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow;
    l->y = (uint16_t)(c->at + n->length);
    l->overflow = false;
    event_keep_offset(g, l->y);
}

/* The object's colours loaded into its slot ($33, and $88 ending its effect)
 * leave Y past the colours written in the palette on the screen, a slot's
 * sixteen to 32 bytes, and the overflow of those sums, clear. */
enum { WORKING_PALETTE_AT = 0x2200, OBJECT_COLOURS_FROM = 2 * (128 + 1), COLOUR_BYTES = 24, SLOT_COLOUR_BYTES = 32 };

static uint16_t past_the_object_colours(uint8_t slot)
{
    return (uint16_t)(WORKING_PALETTE_AT + OBJECT_COLOURS_FROM + COLOUR_BYTES + slot * SLOT_COLOUR_BYTES);
}

static void derive_palette_slot(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow;
    l->y = past_the_object_colours(c->palette);
    l->overflow = false;
    event_keep_offset(g, (uint16_t)(c->at + n->length));
}

/* Effects on colours ($88, $2E): the slot free for them, and whether the
 * object remembered one, before. */
static void note_colour_effect(const GameState *g, const EventCommand *c, Noted *n)
{
    n->colour_effect_running = event_object_remembers_colour_effect(g, c->self);
    n->slot_free = event_colour_effect_slot(g, &n->slot);
}

/* The script's own colours: Y past them in the palette as loaded; and the
 * overflow of taking the script's place back from the copy's end, where the
 * parse goes on. The original keeps the colours' place, the copy's start in
 * the script and its count less one. */
enum { LOADED_PALETTE_AT = 0x2000, SCRIPT_AT = 0x2001 };

static void derive_colours_written(GameState *g, const ColourEffect *e, unsigned first, EventLeaves *l)
{
    uint16_t from = (uint16_t)(SCRIPT_AT + e->colours_at), end = (uint16_t)(from + e->bytes);
    keep_word(g, KEPT_SECOND, from);
    keep_word(g, KEPT_THIRD, (uint16_t)(first * 2));
    original_cell16(g, FIELD_WORK_CELLS, (uint16_t)(e->bytes - 1));
    l->y = (uint16_t)(LOADED_PALETTE_AT + first * 2 + e->bytes);
    l->overflow = field_subtract_overflows16(end, SCRIPT_AT);
}

/* A fade keeps its levels, and leaves the target's nibble, shifted up, in its
 * first cell. */
static void keep_fade(GameState *g, const ColourEffect *e)
{
    keep_byte(g, KEPT_THIRD, e->levels);
    keep_byte(g, KEPT_FIRST, (uint8_t)(e->levels << 4));
}

/* The slots' search ends at the slot it found, or past the eight, a slot's
 * place twelve bytes on from the one before. */
enum { SLOT_BYTES = 12, PAST_THE_SLOTS = 8 * SLOT_BYTES };

static void derive_object_colours(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow;
    const ColourEffect *e = &c->colour_effect;
    event_keep_offset(g, c->at);
    uint8_t palette_base = (uint8_t)object_first_colour(g, c->self);
    if (e->kind != COLOURS_STOP)
        keep_byte(g, KEPT_FIRST, e->animation);
    switch (e->kind) {
    case COLOURS_STOP:
        if (n->colour_effect_running) {
            l->y = past_the_object_colours((uint8_t)((palette_base - 128) / 16));
            l->overflow = false;
        }
        break;
    case COLOURS_ROTATE:
    case COLOURS_FADE:
        l->y = n->slot_free ? (uint16_t)(n->slot * SLOT_BYTES) : (uint16_t)PAST_THE_SLOTS;
        l->overflow = false;
        if (!n->slot_free)
            break;
        keep_byte(g, KEPT_SECOND, palette_base);
        if (e->kind == COLOURS_FADE)
            keep_fade(g, e);
        else
            keep_byte(g, KEPT_FIRST, (uint8_t)(e->first << 4 | e->count));
        break;
    case COLOURS_SET:
        derive_colours_written(g, e, palette_base | e->first, l);
        break;
    default:
        break;
    }
}

static void derive_screen_colours(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow;
    const ColourEffect *e = &c->colour_effect;
    event_keep_offset(g, c->at);
    keep_byte(g, KEPT_FIRST, e->animation);
    if (e->kind == COLOURS_SET) {
        derive_colours_written(g, e, e->first, l);
        return;
    }
    if (e->kind != COLOURS_FADE)
        return;
    l->y = n->slot_free ? (uint16_t)(n->slot * SLOT_BYTES) : (uint16_t)PAST_THE_SLOTS;
    /* the search's sums clear the overflow, unless the first slot was free */
    if (!n->slot_free || n->slot != 0)
        l->overflow = false;
    if (n->slot_free)
        keep_fade(g, e);
}

/* The inventory's and the gold's ($C7-$CE): the item or the amount stays in Y,
 * and the original keeps where the command's operands end. */
static void derive_item(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow, (void)n;
    l->y = c->item;
    event_keep_offset(g, (uint16_t)(c->at + 2));
}

static void derive_gold(GameState *g, const EventCommand *c, EventFlow flow, const Noted *n, EventLeaves *l)
{
    (void)flow, (void)n;
    l->y = c->amount;
    event_keep_offset(g, (uint16_t)(c->at + 3));
}

#define KEEP(how, parameter) { KEEP_##how, PARAMETER_##parameter, 0 }
#define KEEP_OPERAND(n) { KEEP_OPERAND_BYTE, 0, (n) }
#define KEEP_OPERAND_AT(n) { KEEP_OPERAND_OFFSET, 0, (n) }

static const OriginalLeaves by_opcode[256] = {
    /* animation */
    [0xAA] = { .parse = 1 },
    /* calls between objects */
    [0x00] = { .derive = derive_return },
    [0x02] = { .parse = 3, .note = note_call_and_go, .derive = derive_call },
    [0x03] = { .note = note_call_when_taken, .derive = derive_call_that_waits },
    [0x04] = { .note = note_call_and_wait, .derive = derive_call_that_waits },
    [0x05] = { .parse = 3, .note = note_call_and_go, .derive = derive_call },
    [0x06] = { .note = note_call_when_taken, .derive = derive_call_that_waits },
    [0x07] = { .note = note_call_and_wait, .derive = derive_call_that_waits },
    [0x0A] = { .parse = 1 },
    [0x0B] = { .parse = 1 },
    [0x0C] = { .parse = 1 },
    /* dialog */
    [0x19] = { .parse = 1 },
    [0x1A] = { PARSE(1) },
    [0x1C] = { .parse = 2 },
    [0xC0] = { .note = note_question_opens, .derive = derive_question },
    [0xC3] = { .note = note_question_opens, .derive = derive_question },
    [0xC4] = { .note = note_question_opens, .derive = derive_question },
    /* facing */
    [0x1E] = { .parse = 1 },
    [0x1F] = { .parse = 1 },
    [0x23] = { .parse = 3 },
    [0x24] = { .parse = 3 },
    [0x25] = { .parse = 1 },
    [0x26] = { .parse = 1 },
    [0xA6] = { .parse = 1 },
    [0xA7] = { .parse = 1 },
    [0xA8] = { .derive = derive_face_toward },
    [0xA9] = { .derive = derive_face_toward },
    /* jumps and conditions */
    [0x10] = { PARSE(1) },
    [0x11] = { PARSE(1) },
    [0x12] = { PARSE(2), .shared_comparison = true, .first = KEEP(BYTE, left), .second = KEEP(BYTE, right) },
    [0x13] = { PARSE(2), .shared_comparison = true, .first = KEEP(WORD, left), .second = KEEP(WORD, right) },
    [0x14] = { PARSE(3), .shared_comparison = true, .first = KEEP(BYTE, left), .second = KEEP(BYTE, right) },
    [0x15] = { PARSE(3), .shared_comparison = true, .first = KEEP(WORD, left), .second = KEEP(WORD, right) },
    [0x16] = { PARSE(1), .shared_comparison = true },
    [0x18] = { PARSE(1) },
    [0x27] = { PARSE(1) },
    [0x28] = { PARSE(1), .derive = derive_screen_test },
    /* the controller: its parse stops on the jump when the button is down, before it when not */
    [0x2D] = { .parse = 2, .parse_on_jump = 1 },
    [0x30] = { .parse = 2, .parse_on_jump = 1 },
    [0x31] = { .parse = 2, .parse_on_jump = 1 },
    [0x34] = { .parse = 2, .parse_on_jump = 1 },
    [0x35] = { .parse = 2, .parse_on_jump = 1 },
    [0x36] = { .parse = 2, .parse_on_jump = 1 },
    [0x37] = { .parse = 2, .parse_on_jump = 1 },
    [0x38] = { .parse = 2, .parse_on_jump = 1 },
    [0x39] = { .parse = 2, .parse_on_jump = 1 },
    [0x3B] = { .parse = 2, .parse_on_jump = 1 },
    [0x3C] = { .parse = 2, .parse_on_jump = 1 },
    [0x3F] = { .parse = 2, .parse_on_jump = 1 },
    [0x40] = { .parse = 2, .parse_on_jump = 1 },
    [0x41] = { .parse = 2, .parse_on_jump = 1 },
    [0x42] = { .parse = 2, .parse_on_jump = 1 },
    [0x43] = { .parse = 2, .parse_on_jump = 1 },
    [0x44] = { .parse = 2, .parse_on_jump = 1 },
    /* the party */
    [0xD9] = { .note = note_formation_starts, .derive = derive_formation },
    /* who the objects are, and their colours */
    [0x57] = { .derive = derive_character_cast },
    [0x5C] = { .derive = derive_character_cast },
    [0x62] = { .derive = derive_character_cast },
    [0x68] = { .derive = derive_character_cast },
    [0x6A] = { .derive = derive_character_cast },
    [0x6C] = { .derive = derive_character_cast },
    [0x6D] = { .derive = derive_character_cast },
    [0x80] = { .derive = derive_character_cast },
    [0x81] = { .derive = derive_character_apart },
    [0x33] = { .derive = derive_palette_slot },
    [0x88] = { .note = note_colour_effect, .derive = derive_object_colours },
    [0x2E] = { .note = note_colour_effect, .derive = derive_screen_colours },
    /* items and gold */
    [0xC7] = { .derive = derive_item },
    [0xC9] = { .derive = derive_item },
    [0xCA] = { .derive = derive_item },
    [0xCB] = { .derive = derive_item },
    [0xCC] = { .derive = derive_gold },
    [0xCD] = { .derive = derive_gold },
    [0xCE] = { .derive = derive_gold },
    [0xDA] = { .note = note_gathering_starts, .derive = derive_gathering },
    /* places */
    [0xE2] = { .note = note_change_asked, .derive = derive_change_asked },
    [0xDC] = { .first = KEEP(WORD, map_and_how) },
    [0xDD] = { .first = KEEP(WORD, map_and_how) },
    [0xDE] = { .first = KEEP(WORD, map_and_how) },
    /* memory by address */
    [0x48] = { .first = KEEP_OPERAND_AT(4) },
    [0x49] = { .first = KEEP_OPERAND_AT(4) },
    [0x4C] = { .first = KEEP_OPERAND_AT(4) },
    [0x4D] = { .first = KEEP_OPERAND_AT(4) },
    /* moves */
    [0x7A] = { .note = note_move_starts, .derive = derive_jump },
    [0x7B] = { .note = note_move_starts, .derive = derive_launch },
    [0x8F] = { .derive = derive_arrived_on_screen },
    [0xB0] = { .note = note_party_step, .derive = derive_party_step },
    [0xAF] = { .note = note_party_step, .derive = derive_party_step },
    [0xB5] = { .offset_kept_last = true },
    [0xB6] = { .offset_kept_last = true },
    /* object properties */
    [0x0D] = { .parse = 1 },
    [0x0E] = { .parse = 1 },
    [0x21] = { .parse = 4 },
    [0x22] = { .parse = 4 },
    [0x7C] = { .parse = 1 },
    [0x7D] = { .parse = 1 },
    [0x84] = { .parse = 2 },
    [0x87] = { .parse = 1 },
    [0x89] = { .parse = 1 },
    [0x8A] = { .parse = 1 },
    [0x8B] = { .parse = 2 },
    [0x8C] = { .parse = 2 },
    [0x8D] = { .parse = 3 },
    [0x8E] = { .parse = 1 },
    /* the screen */
    [0x47] = { .parse = 2 },
    [0xE3] = { .parse = 2 },
    [0xE4] = { .derive = derive_tile_copy },
    [0xE6] = { .first = KEEP(BYTE, layers), .second = KEEP(WORD, drift), .derive = derive_drift },
    [0xF0] = { .first = KEEP_OPERAND(1) },
    [0xFE] = { .derive = derive_searchlight },
    /* the story */
    [0x56] = { .parse = 4, .first = KEEP(BYTE, value) },
    [0x58] = { .parse = 4, .first = KEEP(WORD, value) },
    [0x59] = { .parse = 4, .first = KEEP(WORD, value) },
    [0x5A] = { .parse = 1 },
    [0x65] = { .parse = 3, .second = KEEP_OPERAND(1) },
    [0x66] = { .parse = 3, .second = KEEP_OPERAND(1) },
    /* variables, and the party leader */
    [0x20] = { .parse = 2 },
    [0x4F] = { .parse = 1, .first = KEEP(BYTE, value) },
    [0x50] = { .parse = 1, .first = KEEP(WORD, value) },
    [0x51] = { .parse = 2, .first = KEEP(WORD, value) },
    [0x52] = { .parse = 2, .first = KEEP(WORD, value) },
    [0x53] = { .parse = 3, .first = KEEP(WORD, value) },
    [0x54] = { .parse = 3, .first = KEEP(WORD, value) },
    [0x55] = { .parse = 1 },
    [0x5B] = { .parse = 3, .first = KEEP(BYTE, amount), .overflow = V_OF_ADDING },
    [0x5D] = { .parse = 3, .first = KEEP(WORD, amount), .overflow = V_OF_ADDING },
    [0x5E] = { .parse = 3, .first = KEEP(WORD, amount), .overflow = V_OF_ADDING },
    [0x5F] = { .parse = 3, .first = KEEP(BYTE, amount), .overflow = V_OF_SUBTRACTING },
    [0x60] = { .parse = 4, .first = KEEP(WORD, amount), .overflow = V_OF_SUBTRACTING },
    [0x61] = { .parse = 3, .first = KEEP(WORD, amount), .overflow = V_OF_SUBTRACTING },
    [0x63] = { .parse = 3, .first = KEEP(BYTE, flags) },
    [0x64] = { .parse = 3, .first = KEEP(BYTE, flags) },
    [0x67] = { .parse = 3, .first = KEEP(BYTE, flags) },
    [0x69] = { .parse = 3, .first = KEEP(BYTE, flags) },
    [0x6B] = { .parse = 3, .first = KEEP(BYTE, flags) },
    [0x6F] = { .parse = 3 },
    [0x71] = { .parse = 2 },
    [0x72] = { .parse = 2 },
    [0x73] = { .parse = 2 },
    [0x75] = { .parse = 2 },
    [0x76] = { .parse = 2 },
    [0x77] = { .parse = 2 },
    [0x7F] = { .parse = 2 },
};

static const OriginalLeaves *original_leaves(uint8_t opcode) { return &by_opcode[opcode]; }

static uint16_t parse_stop(const OriginalLeaves *l, uint16_t at, uint8_t operand)
{
    uint16_t y = (uint16_t)(at + operand);
    return l->shared_comparison ? (uint16_t)(y & 0x00FF) : y;
}

static void keep(GameState *g, unsigned cell, Kept k, const EventCommand *c, uint16_t at)
{
    switch (k.as) {
    case KEEP_BYTE: keep_byte(g, cell, (uint8_t)parameter(c, k.parameter)); break;
    case KEEP_WORD: keep_word(g, cell, (uint16_t)parameter(c, k.parameter)); break;
    case KEEP_OPERAND_BYTE: keep_byte(g, cell, script_byte(g, (uint16_t)(at + k.operand))); break;
    case KEEP_OPERAND_OFFSET: keep_word(g, cell, (uint16_t)(at + k.operand)); break;
    default: break;
    }
}

/* Before the operation: Y where the parse stops, what is kept in the scratch
 * cells (from the command's parameters; the jump operand's place for the
 * comparisons), and what the derivation needs noted. */
static void before(GameState *g, const OriginalLeaves *l, const EventOpcode *row, const EventCommand *c, Noted *n,
                   EventLeaves *left)
{
    left->y = parse_stop(l, c->at, l->parse);
    keep(g, KEPT_FIRST, l->first, c, c->at);
    keep(g, KEPT_SECOND, l->second, c, c->at);
    if (l->offset_kept_last)
        keep_word(g, KEPT_SIXTH, c->at);
    /* the comparisons' shared routine notes where the jump byte is, and
     * reads it back itself */
    if (l->shared_comparison)
        set_event_condition_resume(g, (uint16_t)(c->at + row->jump));
    n->length = row->length;
    if (l->note)
        l->note(g, c, n);
}

/* The V an addition leaves, which follows from the amount and the variable as
 * it now is. */
static bool arithmetic_overflow(const GameState *g, OverflowLeft how, const EventCommand *c)
{
    Width w = c->width;
    uint16_t amount = c->amount, now = variable(g, c->variable, w);
    if (how == V_OF_SUBTRACTING) {
        uint16_t was = (uint16_t)(now + amount);
        return w == AS_BYTE ? field_subtract_overflows8((uint8_t)was, (uint8_t)amount)
                            : field_subtract_overflows16(was, amount);
    }
    uint16_t was = (uint16_t)(now - amount);
    return w == AS_BYTE ? field_add_overflows8((uint8_t)was, (uint8_t)amount) : field_add_overflows16(was, amount);
}

/* After it: the arithmetic's V, a jump's distance, V and Y, and the
 * command's own derivation. */
static void after(GameState *g, const OriginalLeaves *l, const EventOpcode *row, const EventCommand *c, EventFlow flow,
                  const Noted *n, EventLeaves *left)
{
    if (l->overflow != V_AS_IT_WAS)
        left->overflow = arithmetic_overflow(g, l->overflow, c);
    if (flow.kind == FLOW_JUMP) {
        uint16_t from = (uint16_t)(c->at + row->jump);
        uint8_t distance = script_byte(g, from);
        keep_word(g, KEPT_FIRST, distance);
        left->overflow = row->back ? field_subtract_overflows16(from, distance) : field_add_overflows16(from, distance);
        left->y = parse_stop(l, c->at, l->parse_on_jump);
    }
    if (l->derive)
        l->derive(g, c, flow, n, left);
}

/* The record the operation's own leaves go to while it runs, and the
 * command's offset. */
static struct {
    EventLeaves *left;
    uint16_t at;
} running;

EventStep event_perform_leaving(GameState *g, Platform *pf, const EventOpcode *row, uint16_t at, EventLeaves *left)
{
    const OriginalLeaves *l = original_leaves(row->opcode);
    EventCommand c = event_decode(g, pf, row, at);
    Noted n = { 0 };
    /* what the derivations look at costs the machine no time: the original
     * never reads it again (port/vars_ram.h) */
    vars_peek(true);
    before(g, l, row, &c, &n, left);
    vars_peek(false);
    running.left = left;
    running.at = at;
    EventFlow flow = row->run(g, &c);
    running.left = NULL;
    vars_peek(true);
    after(g, l, row, &c, flow, &n, left);
    vars_peek(false);
    return event_follow(g, row, at, flow);
}

/* Whether a walking object's animation went with its movement, which leaves
 * the walking animation's overflow (a script's animation keeps it). */
static bool walk_animated(const GameState *g, FieldObject self)
{
    return object_animation_mode(g, self) == ANIMATE_WITH_MOVEMENT;
}

/* A move set off (event/movement.c): the Y and the overflow the step's start
 * leaves, or the walking animation's overflow where it picked one. The
 * original keeps the command's offset across its calls, and reads it back
 * itself; the check for objects in the way keeps it too. */
void leave_set_off(GameState *g, FieldObject self, StepStart start, bool walking)
{
    event_keep_offset(g, running.at);
    running.left->y = field_step_start_y(g, start, running.at);
    running.left->overflow = walking && walk_animated(g, self) ? field_walk_overflow(g, self)
                                                               : field_step_start_overflow(g, self, start);
}

void leave_in_the_way_check(GameState *g, FieldObject self) { field_leave_in_the_way_check(g, self, running.at); }

bool event_place_worked_out_overflow(bool worked_out, bool before) { return worked_out ? false : before; }

uint16_t event_touch_y(const GameState *g, bool looked_to_push, uint16_t before)
{
    return looked_to_push ? field_leader_contact(g) : before;
}

static struct {
    bool left, overflow;
} confirm_overflow;

void event_forget_overflow(void) { confirm_overflow.left = false; }

void event_leave_overflow(bool overflow)
{
    confirm_overflow.left = true;
    confirm_overflow.overflow = overflow;
}

bool event_overflow_left(bool before) { return confirm_overflow.left ? confirm_overflow.overflow : before; }

void leave_walk_overflow(const GameState *g, FieldObject self)
{
    if (walk_animated(g, self))
        running.left->overflow = field_walk_overflow(g, self);
}

#else

EventStep event_perform_leaving(GameState *g, Platform *pf, const EventOpcode *row, uint16_t at, EventLeaves *left)
{
    (void)left;
    return event_perform(g, pf, row, at);
}

#endif
