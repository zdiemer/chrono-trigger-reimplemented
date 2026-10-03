/* Scripts and the party (event/party.h): who is in a place of it (Someone),
 * who leads it, someone leaving it, and the menus' party routines, which the
 * scripts call on (what routines 6, 7 and $0A do for the party isn't
 * established; docs/TODO.md).
 *
 * The party's record is the menus': nine places, the three of the active
 * party and the six of the reserve, each a character or empty. A script asks
 * whether a character has been recruited (is in any of the nine) or is in the
 * active party, recruits one into the reserve, dismisses one, brings one into
 * the active party, or sends one of the active party to the reserve. The
 * menus' routines ($C2:8004) keep the record, and are still the original's
 * code; on the map, the characters' objects follow: one joining becomes the
 * next member's, and the members behind one sent away close up, with their
 * palettes.
 *
 * Leaving and the menus' routines are bridges: the first because what it
 * hands back is the carry the command was entered with (below), the others
 * because the menus' routines are still the original's code. */
#include "charge.h"
#include "event/bridge.h"
#include "event/calls.h"
#include "event/party.h"
#include "field/palettes.h"
#include "event/variables.h"
#include "field/field.h"
#include "field/party.h"

/* ---- Someone to walk up to or ask after -------------------------------------------------- */

Someone someone_of_object(const GameState *g, FieldObject object)
{
    return (Someone){ true, object_disabled(g, object), object_position(g, object), object };
}

/* For shadow mode: whom the original finds in an empty place. It takes the
 * place's marker for an object like any other, and reads the slot its byte
 * names past the object tables (field/object.h, object_past_the_tables): for
 * the game's marker, a slot disabled when object 0's script has got to an
 * offset with the low byte's top bit set, and standing where object 0's
 * vertical position and horizontal velocity say. So the moves toward the
 * member mostly find nobody to walk to, as the game means, and now and then
 * set off toward that tile; and noting the member's tile notes it.
 *
 * None of it shows. Over the movie library the moves are made by the
 * objects that stand in for a party's second and third members in the first
 * maps of a new game (locations $001 and $002), following them while Crono
 * is on his own: 3 calls of each object's
 * 5,000 set off (just after the map loads, while object 0's script is early
 * on), and the objects are out of view. The tile is noted every frame by an
 * ending with no party at all, and nothing reads the variables before the
 * map changes. A native build has nobody there.
 *
 * One read of the original comes later than the port's: lining up with the
 * slot, it stops the walker first, and the slot's vertical fine position is
 * the low byte of object 0's horizontal velocity, which the stop zeroes when
 * the walker is object 0. */
static void see_what_the_original_finds(const GameState *g, FieldLink marker, FieldObject asking, Someone *found)
{
    FieldObject slot = field_linked_object(marker);
    ObjectPastTheTables past = object_past_the_tables(g, slot);
    if (asking == slot - FIELD_OBJECTS)
        past.at.y &= 0xFF00;
    *found = (Someone){ true, past.disabled, past.at, slot };
}

Someone member_in_place(const GameState *g, PartyPlace place, FieldObject asking)
{
    FieldLink link = party_member_link(g, place);
    if (field_link_is_object(link))
        return someone_of_object(g, field_linked_object(link));
    Someone nobody = { .there = false };
    ORIGINAL_EFFECT(see_what_the_original_finds(g, link, asking, &nobody));
    return nobody;
}

/* The marker's byte is an offset like an object's, which the slot past the
 * tables stands for when it is even; an odd one reads the tables out of step
 * (a slot's two bytes from two slots), which the game never makes: its
 * marker is $80. */
bool member_place_readable(const GameState *g, PartyPlace place)
{
    FieldLink link = party_member_link(g, place);
    return field_link_is_object(link) ? field_link_names_object(link) : !(field_link_byte(link) & 1);
}

// $C0:658F
/* A variable learns who leads the party. */
static EventFlow note_leader(GameState *g, EventVariable variable)
{
    set_variable_byte(g, variable, party_leader(g));
    return next_command();
}
EVENT_OPERATION(note_leader, variable)

// $C0:3A07
/* A character leaves the party: the first member who is that character
 * leaves the map and their place empties. Returns whether they were there. */
static bool take_out_of_party(GameState *g, uint8_t character)
{
    for (PartyPlace place = 0; place < PARTY_PLACES; place++) {
        if (!place_taken(g, place))
            continue;
        FieldObject object = member_object(g, place);
        if (object_character(g, object) != character)
            continue;
        object_leave_the_party(g, object);
        empty_place(g, place);
        return true;
    }
    return false;
}

/* Whether the object's turn goes on after someone leaves. The original goes
 * on by the carry its search leaves: that of its last comparison of a
 * member's character with the one leaving, set when it found them and when
 * the last member it compared is that character or a later one; with nobody
 * to compare, the carry the command was entered with. It means nothing a
 * script asks for, but the engine goes on by it, so the port keeps it. */
static bool goes_on_after_leaving(const GameState *g, uint8_t character, bool entered_with)
{
    bool any = false, at_least = false;
    for (PartyPlace place = 0; place < PARTY_PLACES; place++) {
        if (!place_taken(g, place))
            continue;
        if (object_character(g, member_object(g, place)) == character)
            return true;
        any = true;
        at_least = object_character(g, member_object(g, place)) >= character;
    }
    return any ? at_least : entered_with;
}

/* $D6's entry. */
void event_cmd_take_out_of_party(Snes *s)
{
    enum { LENGTH = 2 };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    uint8_t character = event_command_of(s, 0xD6).character;
    ORIGINAL_EFFECT(event_keep_offset(g, (uint16_t)(at + LENGTH)));
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, character));
    bool go_on = goes_on_after_leaving(g, character, m_flag(s, CPU_FLAG_C));
    take_out_of_party(g, character);
    event_return(s, (EventStep){ (uint16_t)(at + LENGTH), go_on });
}

/* The menus' party routines ($C2:8004): the routine in A, its arguments in X
 * and Y, which the menus' dispatcher ($C2:8C36) takes as they come. The
 * script goes on after the command, with the carry the routine leaves. */
bool event_call_party_routine(Snes *s, uint32_t site, uint8_t routine)
{
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | routine);
    m_set_nz8(s, routine);
    return bridge_call_long(s, site, 0xC28004, 0, event_machine(s, false));
}

// $C0:3A68
/* Routine $0A, with the command's first operand in Y and its second in X. */
void event_cmd_party_routine_0a(Snes *s)
{
    enum { ROUTINE = 0x0A, LENGTH = 3 };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    uint16_t resume = (uint16_t)(at + LENGTH);
    ORIGINAL_EFFECT(event_keep_offset(g, resume));
    EventCommand c = event_command_of(s, 0xD5);
    s->cpu.a = 0;
    s->cpu.x = c.second_argument;
    s->cpu.y = c.first_argument;
    if (event_call_party_routine(s, 0xC03A7C, ROUTINE))
        event_return(s, (EventStep){ resume, m_flag(s, CPU_FLAG_C) });
}

/* Routines 6 and 7 take no arguments; X is the dispatch's as it came, and Y
 * the command's offset past its opcode, which the dispatcher stores
 * regardless. */
static void party_routines(Snes *s, uint32_t first_site, uint8_t first, bool then_7)
{
    uint16_t resume = (uint16_t)(event_command_offset(s) + 1);
    s->cpu.y = resume;
    ORIGINAL_EFFECT(event_keep_offset(state_of(s), resume));
    if (!event_call_party_routine(s, first_site, first))
        return;
    if (then_7 && !event_call_party_routine(s, 0xC03E67, 7))
        return;
    event_return(s, (EventStep){ resume, m_flag(s, CPU_FLAG_C) });
}

/* ---- The party's record, through the menus' routines ---------------------------------------- */

/* The menus' routines, by number: whether a character is in the active
 * party, or recruited at all (each answering 0 if so); a character brought
 * into the active party (the third member to the reserve where it is full),
 * one of the active party sent to the front of the reserve, one recruited
 * into the reserve, one dismissed; and how many of an item the party
 * holds. */
typedef enum {
    IS_IN_THE_ACTIVE_PARTY = 0,
    BRING_INTO_THE_ACTIVE_PARTY = 1,
    SEND_TO_THE_RESERVE = 2,
    IS_RECRUITED = 3,
    RECRUIT = 4,
    DISMISS = 5,
    COUNT_ITEM = 0x0C,
} MenusPartyRoutine;
enum { MENUS_ANSWER_YES = 0 };

/* Ask the menus, from the JSL at `site`, with the routine in A (its high
 * byte clear) and its arguments in X and Y: the character in Y, or the item
 * in X. True if it came back as the engine runs; the answer is A's low byte. */
static bool ask_the_menus(Snes *s, uint32_t site, MenusPartyRoutine routine, uint16_t x, uint16_t y)
{
    s->cpu.a = 0;
    s->cpu.x = x;
    s->cpu.y = y;
    return event_call_party_routine(s, site, (uint8_t)routine);
}

static uint8_t menus_answer(const Snes *s) { return (uint8_t)s->cpu.a; }

/* For the original's code and shadow mode: a jump's distance kept, and the
 * overflow of the sum that took it. */
static void leave_jump(Snes *s, uint16_t from, uint8_t distance)
{
    keep_word(state_of(s), KEPT_FIRST, distance);
    m_set_flag(s, CPU_FLAG_V, field_add_overflows16(from, distance));
}

/* A condition on the record ($D2, $CF): on after it if the menus answer yes,
 * otherwise its jump, by the distance in its last operand. The original keeps
 * where the command's character operand ends. */
static void ask_after_character(Snes *s, uint8_t opcode, uint32_t site, MenusPartyRoutine routine)
{
    enum { LENGTH = 3, JUMP_BYTE = 2 };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    uint8_t character = event_command_of(s, opcode).character;
    ORIGINAL_EFFECT(event_keep_offset(g, (uint16_t)(at + JUMP_BYTE)));
    if (!ask_the_menus(s, site, routine, (uint16_t)(at + JUMP_BYTE), character))
        return;
    if (menus_answer(s) == MENUS_ANSWER_YES) {
        event_return(s, (EventStep){ (uint16_t)(at + LENGTH), true });
        return;
    }
    uint16_t from = (uint16_t)(at + JUMP_BYTE);
    uint8_t distance = script_byte(g, from);
    ORIGINAL_EFFECT(leave_jump(s, from, distance));
    event_return(s, (EventStep){ (uint16_t)(from + distance), true });
}

// $C0:38E8
/* Whether a character is in the active party. */
void event_cmd_if_in_the_active_party(Snes *s) { ask_after_character(s, 0xD2, 0xC038F6, IS_IN_THE_ACTIVE_PARTY); }

// $C0:388D
/* Whether a character has been recruited, active or in reserve. */
void event_cmd_if_recruited(Snes *s) { ask_after_character(s, 0xCF, 0xC0389B, IS_RECRUITED); }

/* A change to the record ($D0, $D1): the script goes on with the carry the
 * menus leave, which is the one they were called with. */
static void change_the_record(Snes *s, uint8_t opcode, uint32_t site, MenusPartyRoutine routine)
{
    enum { LENGTH = 2 };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    uint8_t character = event_command_of(s, opcode).character;
    ORIGINAL_EFFECT(event_keep_offset(g, (uint16_t)(at + LENGTH)));
    if (ask_the_menus(s, site, routine, (uint16_t)(at + LENGTH), character))
        event_return(s, (EventStep){ (uint16_t)(at + LENGTH), m_flag(s, CPU_FLAG_C) });
}

// $C0:38BE
/* A character is recruited into the reserve, unless already recruited. */
void event_cmd_recruit(Snes *s) { change_the_record(s, 0xD0, 0xC038CC, RECRUIT); }

// $C0:38D3
/* A character is dismissed from the party's record. */
void event_cmd_dismiss(Snes *s) { change_the_record(s, 0xD1, 0xC038E1, DISMISS); }

// $C0:37F9
/* A variable learns how many of an item the party holds. */
void event_cmd_note_item_count(Snes *s)
{
    enum { LENGTH = 3, ITEM_ENDS = 2 };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    EventCommand c = event_command_of(s, 0xD7);
    ORIGINAL_EFFECT(event_keep_offset(g, (uint16_t)(at + ITEM_ENDS)));
    if (!ask_the_menus(s, 0xC03807, COUNT_ITEM, c.item, at))
        return;
    uint8_t count = menus_answer(s);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, count));
    set_variable_byte(g, c.variable, count);
    event_return(s, (EventStep){ (uint16_t)(at + LENGTH), true });
}

/* A character joining the active party: its object on the map, where it has
 * one, becomes the next member's -- the first's place where it is empty, the
 * second's otherwise -- and shows its first frame at once. */
static void object_joins_the_party(GameState *g, uint8_t character)
{
    field_character_joining(g);
    FieldLink object = field_link_stored(field_character_objects(g, character));
    if (!field_link_is_object(object))
        return;
    ORIGINAL_EFFECT(keep_byte(g, KEPT_SECOND, field_link_byte(object)));
    PartyPlace place = place_taken(g, PARTY_FIRST_MEMBER) ? PARTY_SECOND_MEMBER : PARTY_FIRST_MEMBER;
    set_field_party_objects(g, place, field_link_byte(object));
}

// $C0:3919
/* A character is brought into the active party. */
void event_cmd_bring_into_the_party(Snes *s)
{
    enum { LENGTH = 2 };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    uint8_t character = event_command_of(s, 0xD3).character;
    ORIGINAL_EFFECT(event_keep_offset(g, (uint16_t)(at + LENGTH)));
    ORIGINAL_EFFECT(keep_word(g, KEPT_FIRST, character));
    if (!ask_the_menus(s, 0xC0392B, BRING_INTO_THE_ACTIVE_PARTY, (uint16_t)(at + LENGTH), character))
        return;
    object_joins_the_party(g, character);
    event_return(s, (EventStep){ (uint16_t)(at + LENGTH), m_flag(s, CPU_FLAG_C) });
}

/* The members behind a place take the roles of the places before theirs, as
 * far as there are members. */
static void roles_close_up(GameState *g, PartyPlace from)
{
    for (PartyPlace place = from; place + 1 < PARTY_PLACES; place++) {
        FieldLink behind = party_member_link(g, (PartyPlace)(place + 1));
        if (!field_link_is_object(behind))
            return;
        object_become_party_member(g, field_linked_object(behind), place);
    }
}

/* The places behind a place move up into it, with their palettes, and the
 * last place empties. */
static void places_close_up(GameState *g, PartyPlace from)
{
    for (PartyPlace place = from; place + 1 < PARTY_PLACES; place++) {
        set_field_party_objects(g, place, field_party_objects(g, (PartyPlace)(place + 1)));
        party_palette_moves_up(g, (PartyPlace)(place + 1));
    }
    party_place_left_empty(g, PARTY_SECOND_MEMBER);
}

/* Whether the member in a place is the character. */
static bool member_is(const GameState *g, PartyPlace place, uint8_t character)
{
    return place_taken(g, place) && object_character(g, member_object(g, place)) == character;
}

/* A character of the active party goes to the reserve: its object stays on
 * the map as one apart from the party, and the members behind close up. The
 * original closes up from the first member's place whenever it is empty, its
 * own or not; and a second member sent away keeps its place's object, the
 * place only marked empty. */
static void member_steps_out(GameState *g, uint8_t character)
{
    if (member_is(g, PARTY_LEADER, character)) {
        object_leave_the_party(g, member_object(g, PARTY_LEADER));
        roles_close_up(g, PARTY_LEADER);
        places_close_up(g, PARTY_LEADER);
        return;
    }
    if (!place_taken(g, PARTY_FIRST_MEMBER)) {
        places_close_up(g, PARTY_FIRST_MEMBER);
        return;
    }
    if (member_is(g, PARTY_FIRST_MEMBER, character)) {
        object_leave_the_party(g, member_object(g, PARTY_FIRST_MEMBER));
        roles_close_up(g, PARTY_FIRST_MEMBER);
        places_close_up(g, PARTY_FIRST_MEMBER);
        return;
    }
    if (member_is(g, PARTY_SECOND_MEMBER, character)) {
        object_leave_the_party(g, member_object(g, PARTY_SECOND_MEMBER));
        party_place_marked_out(g, PARTY_SECOND_MEMBER);
    }
}

/* For the original's code and shadow mode: the X the menus are called with,
 * which they keep (the menus' dispatcher stores it). The original leaves it
 * the palettes' move's end where the members closed up, and otherwise the
 * last member's object it compared. */
enum { PALETTES_MOVED_X = 0x2200 };

static uint16_t x_for_the_menus(const FieldLink before[PARTY_PLACES], const GameState *g, uint8_t character)
{
    bool leader_goes = field_link_is_object(before[0]) && object_character(g, field_linked_object(before[0])) == character;
    if (leader_goes || !field_link_is_object(before[1]))
        return PALETTES_MOVED_X;
    if (object_character(g, field_linked_object(before[1])) == character)
        return PALETTES_MOVED_X;
    return field_link_byte(field_link_is_object(before[2]) ? before[2] : before[1]);
}

/* Whether the object's turn goes on after someone is sent to the reserve. As
 * with someone leaving ($D6, above), the original goes on by the carry of its
 * last comparison of a member's character with the one sent, which the
 * menus' routine keeps: set where it found them, and where the last member it
 * compared is a later character; with nobody to compare, the carry the
 * command was entered with. Its search stops at the member found, and at the
 * first member's place where that is empty. */
static bool goes_on_after_sending(const GameState *g, uint8_t character, bool entered_with)
{
    bool at_least = entered_with;
    for (PartyPlace place = PARTY_LEADER; place < PARTY_PLACES; place++) {
        if (!place_taken(g, place)) {
            if (place == PARTY_LEADER)
                continue;
            return at_least;
        }
        uint8_t theirs = object_character(g, member_object(g, place));
        at_least = theirs >= character;
        if (theirs == character)
            return true;
    }
    return at_least;
}

// $C0:394E
/* A character of the active party is sent to the reserve. */
void event_cmd_send_to_the_reserve(Snes *s)
{
    enum { LENGTH = 2 };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    uint8_t character = event_command_of(s, 0xD4).character;
    FieldLink before[PARTY_PLACES];
    for (PartyPlace place = 0; place < PARTY_PLACES; place++)
        before[place] = party_member_link(g, place);
    ORIGINAL_EFFECT(event_keep_offset(g, (uint16_t)(at + LENGTH)));
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, character));
    vars_peek(true);
    uint16_t x = x_for_the_menus(before, g, character);
    bool go_on = goes_on_after_sending(g, character, m_flag(s, CPU_FLAG_C));
    vars_peek(false);
    member_steps_out(g, character);
    m_set_flag(s, CPU_FLAG_C, go_on);
    if (ask_the_menus(s, 0xC039DA, SEND_TO_THE_RESERVE, x, character))
        event_return(s, (EventStep){ (uint16_t)(at + LENGTH), m_flag(s, CPU_FLAG_C) });
}

/* Their guards: the character's cell for its object on the map among the
 * seven; the party's places, where taken, objects the tables hold. */
static bool joining_character(const GameState *g, uint8_t character)
{
    (void)g;
    return character < FIELD_CHARACTER_OBJECTS_COUNT;
}
EVENT_CHECK(joining_character, character)

static bool places_hold_objects(const GameState *g)
{
    for (PartyPlace place = 0; place < PARTY_PLACES; place++) {
        FieldLink link = party_member_link(g, place);
        if (field_link_is_object(link) && !field_link_names_object(link))
            return false;
    }
    return true;
}
EVENT_CHECK_TAKING_NOTHING(places_hold_objects)

// $C0:3E5C
/* The menus' party routines 6, then 7. */
void event_cmd_party_routine_6_7(Snes *s) { party_routines(s, 0xC03E61, 6, true); }

// $C0:3E6E
void event_cmd_party_routine_6(Snes *s) { party_routines(s, 0xC03E67, 6, false); }

// $C0:3E75
void event_cmd_party_routine_7(Snes *s) { party_routines(s, 0xC03E67, 7, false); }

/* ---- The party regrouping ---------------------------------------------------------------- *
 *
 * Two commands take the party out of the player's hands and move it as one,
 * through the party's own scripts (event/party.h), each member dropping what
 * it was doing for its script at the most urgent level: $D9 walks each of
 * the party to a place in a formation, and $DA gathers the members back to
 * the leader. Each counts its members in as they get there, in a variable
 * the scripts share, and waits until all have; then the members pick the
 * leader's trail up afresh, from where the leader stands (field/party.h). */

enum { PARTY_SCRIPT_LEVEL = 0 };

/* Where each of the party's scripts starts, and where in it the command
 * that starts it writes the tile to go to (a column, then a row) and, for a
 * formation's, the way to face (the facing command's operand). */
static const struct {
    uint16_t start;
    uint8_t tile_operand, facing_operand;
} party_scripts[] = {
    [GATHERING_SCRIPT] = { 0x16FF, 0, 0 },
    [FORMATION_SCRIPT_LEADER] = { 0x171F, 8, 11 },
    [FORMATION_SCRIPT_FIRST_MEMBER] = { 0x173F, 8, 11 },
    [FORMATION_SCRIPT_SECOND_MEMBER] = { 0x175F, 8, 11 },
    [PUSH_SCRIPT] = { 0x177F, 1, 0 },
};

uint16_t party_script_start(PartyScript script) { return party_scripts[script].start; }

void party_script_go_to(GameState *g, PartyScript script, MapTile tile)
{
    unsigned at = (unsigned)party_scripts[script].start + party_scripts[script].tile_operand;
    set_event_script_byte(g, at, tile.x);
    set_event_script_byte(g, at + 1, tile.y);
}

MapTile party_script_destination(const GameState *g, PartyScript script)
{
    unsigned at = (unsigned)party_scripts[script].start + party_scripts[script].tile_operand;
    return (MapTile){ event_script_byte(g, at), event_script_byte(g, at + 1) };
}

/* The facing command's operand is the way to face as the objects number the
 * facings (field/object.h). */
void party_script_face(GameState *g, PartyScript script, Facing facing)
{
    set_event_script_byte(g, (unsigned)party_scripts[script].start + party_scripts[script].facing_operand,
                          (uint8_t)facing);
}

/* A formation place's facing bits, column's and row's, by the way they say. */
static const struct {
    bool column, row;
} facing_bits[] = {
    [FACING_UP] = { false, false },
    [FACING_DOWN] = { false, true },
    [FACING_LEFT] = { true, false },
    [FACING_RIGHT] = { true, true },
};

Facing formation_facing(bool column_bit, bool row_bit)
{
    for (unsigned f = FACING_UP; f <= FACING_RIGHT; f++)
        if (facing_bits[f].column == column_bit && facing_bits[f].row == row_bit)
            return (Facing)f;
    return FACING_UP;
}

void formation_facing_bits(Facing facing, bool *column_bit, bool *row_bit)
{
    *column_bit = facing_bits[facing].column;
    *row_bit = facing_bits[facing].row;
}

/* The original keeps the operand bytes of the last place it lays out: the
 * column and the row, each with its facing bit. */
enum { FACING_BIT = 0x80 };

static void keep_formation_place(GameState *g, FormationPlace p)
{
    bool column_bit, row_bit;
    formation_facing_bits(p.facing, &column_bit, &row_bit);
    keep_byte(g, KEPT_SECOND, (uint8_t)(p.tile.x | (column_bit ? FACING_BIT : 0)));
    keep_byte(g, KEPT_THIRD, (uint8_t)(p.tile.y | (row_bit ? FACING_BIT : 0)));
}

/* A member takes its place in the formation: it drops what it was doing for
 * its script, which is told the tile and the way to face. */
static void send_to_formation_place(GameState *g, PartyPlace place, FormationPlace p)
{
    PartyScript script = formation_script(place);
    object_run_script_from(g, member_object(g, place), PARTY_SCRIPT_LEVEL, party_script_start(script));
    party_script_go_to(g, script, p.tile);
    party_script_face(g, script, p.facing);
    ORIGINAL_EFFECT(keep_formation_place(g, p));
}

/* Whether any of the party still runs the party's script. */
static bool someone_still_moving(const GameState *g)
{
    for (PartyPlace place = 0; place < PARTY_PLACES; place++)
        if (place_taken(g, place) && event_object_call_state_level(g, member_object(g, place)) == PARTY_SCRIPT_LEVEL)
            return true;
    return false;
}

/* The count the formation's scripts reach once all of the party is in
 * place: it starts at one more for each place that is empty. */
enum { ALL_IN_FORMATION = 4 };

// $C0:326C
/* The party walks into a formation, each to its tile, facing its way, and the
 * script waits until all are there and done. The leader's place comes first;
 * the members' are left out where their places are empty. */
static EventFlow take_formation(GameState *g, FormationPlace leader_place, FormationPlace first_place,
                                FormationPlace second_place)
{
    uint8_t in_place = variable_byte(g, FORMATION_COUNT);
    if (in_place != 0) {
        if (in_place < ALL_IN_FORMATION)
            return stay();
        party_trail_regroup(g);
        if (someone_still_moving(g))
            return stay();
        set_variable_byte(g, FORMATION_COUNT, 0);
        return end_turn();
    }
    hand_the_party_to_the_player(g, false);
    const FormationPlace places[PARTY_PLACES] = { leader_place, first_place, second_place };
    uint8_t count = 1;
    for (PartyPlace place = 0; place < PARTY_PLACES; place++) {
        if (!place_taken(g, place)) {
            count = (uint8_t)(ALL_IN_FORMATION - place);
            break;
        }
        send_to_formation_place(g, place, places[place]);
    }
    set_variable_byte(g, FORMATION_COUNT, count);
    return stay();
}
EVENT_OPERATION(take_formation, leader_place, first_place, second_place)

/* A member goes to the leader: it drops what it was doing for the party's
 * gathering script. */
static void send_to_the_leader(GameState *g, FieldObject member)
{
    object_run_script_from(g, member, PARTY_SCRIPT_LEVEL, party_script_start(GATHERING_SCRIPT));
}

/* The gathering scripts count up to PARTY_ALL_GATHERED (event/party.h),
 * starting at one more for the second place empty. */


// $C0:345A
/* The members gather to the leader, and the script waits until they are
 * there; with nobody but the leader, it goes on at once. */
static EventFlow gather_party(GameState *g)
{
    if (!place_taken(g, PARTY_FIRST_MEMBER))
        return end_turn();
    if (field_party_gathering(g)) {
        if (variable_byte(g, GATHERING_COUNT) < PARTY_ALL_GATHERED)
            return stay();
        set_field_party_gathering(g, false);
        party_trail_regroup(g);
        return next_command();
    }
    send_to_the_leader(g, member_object(g, PARTY_FIRST_MEMBER));
    uint8_t count = PARTY_ALL_GATHERED - 1;
    if (place_taken(g, PARTY_SECOND_MEMBER)) {
        send_to_the_leader(g, member_object(g, PARTY_SECOND_MEMBER));
        count = PARTY_ALL_GATHERED - 2;
    }
    set_variable_byte(g, GATHERING_COUNT, count);
    set_field_party_gathering(g, true);
    return stay();
}
EVENT_OPERATION_TAKING_NOTHING(gather_party)

// $C0:5B1F
/* After a battle a member goes back to the leader, unless it is disabled or
 * already runs the party's script. */
void member_back_to_the_leader(GameState *g, FieldObject member)
{
    if (object_disabled(g, member) || event_object_call_state_level(g, member) == PARTY_SCRIPT_LEVEL)
        return;
    send_to_the_leader(g, member);
}

/* Its entry: the field's routine after a battle ($C0:034B) names the member
 * in the cell the leader's contact is kept in, with 8-bit index registers. */
void event_member_back_to_the_leader(Snes *s)
{
    GameState *g = state_of(s);
    member_back_to_the_leader(g, field_linked_object(field_link_stored(field_leader_contact(g))));
    m_rts(s);
}

/* The battle an event starts can leave the party where it stands afterward
 * (bit 7 of the flags the event hands the battle, event/locations.c). */
enum { PARTY_STAYS_PUT = 0x80 };

/* For the original: it names each member it sends back in the cell the
 * leader's contact is kept in, which the walk sets again before it reads
 * it. */
static void leave_member_named(GameState *g, FieldObject member)
{
    set_field_leader_contact(g, field_object_offset(member));
}

// $C0:034B
/* After a battle the party comes back together: the members walk back to
 * the leader through the gathering script, as $DA sends them, and catch its
 * trail up; the gathering count starts as $DA's does. With nobody behind the
 * leader, the party is the player's at once; a battle that leaves the party
 * where it stands sends nobody. */
void party_back_together(GameState *g)
{
    if (battle_event_flags(g) & PARTY_STAYS_PUT) {
        set_variable_byte(g, GATHERING_COUNT, PARTY_ALL_GATHERED);
        return;
    }
    if (!place_taken(g, PARTY_FIRST_MEMBER)) {
        hand_the_party_to_the_player(g, true);
        set_variable_byte(g, GATHERING_COUNT, PARTY_ALL_GATHERED);
        return;
    }
    uint8_t gathered = PARTY_ALL_GATHERED;
    for (PartyPlace place = PARTY_FIRST_MEMBER; place < PARTY_PLACES && place_taken(g, place); place++) {
        FieldObject member = member_object(g, place);
        ORIGINAL_EFFECT(leave_member_named(g, member));
        TIMED_AS(g, 0xC05B1F, member_back_to_the_leader(g, member));
        gathered--;
    }
    set_variable_byte(g, GATHERING_COUNT, gathered);
    party_trail_catch_up(g);
}

/* Its entry: from the field's return from a battle ($C0:027E), which goes on
 * at $C0:00EB, the field's frame, where the original's routine ends. */
void event_party_back_together(Snes *s)
{
    party_back_together(state_of(s));
    m_rep(s, CPU_FLAG_X);
    m_stop_at(s, 0xC000EB, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
}

/* Its guard: on the field's page; the members there are objects the tables
 * hold at levels the places hold, and the cell the original names them in
 * holds only their offsets (it adds its whole word to a place's). */
bool event_party_back_can_run(Snes *s)
{
    if (!field_on_page_low_bank(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = field_leader_contact_high(g) == 0;
    for (PartyPlace place = PARTY_FIRST_MEMBER; place < PARTY_PLACES && ok && place_taken(g, place); place++) {
        FieldLink link = party_member_link(g, place);
        ok = field_link_names_object(link) &&
             event_object_call_state_level(g, field_linked_object(link)) < EVENT_LEVELS;
    }
    vars_peek(false);
    return ok;
}

/* Its guard: the member is an object the tables hold, as the game keeps it
 * (the original adds its whole word to the place's offset), at a level the
 * places hold. */
bool event_member_can_go_back(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t member = field_leader_contact(g);
    bool ok = field_names_object(member) && field_leader_contact_high(g) == 0 &&
              event_object_call_state_level(g, field_linked_object(field_link_stored(member))) < EVENT_LEVELS;
    vars_peek(false);
    return ok;
}

/* The regrouping's guard: the party's places hold objects the tables do, as
 * the game keeps them (the original adds each one's whole word to a place's
 * offset), at levels the places hold, where they are taken (the leader's always is:
 * the game never regroups a party with nobody leading it), and the trail's
 * places are its footprints. */
static bool party_can_regroup(const GameState *g)
{
    if (!place_taken(g, 0))
        return false;
    for (PartyPlace place = 0; place < PARTY_PLACES; place++) {
        if (!place_taken(g, place))
            continue;
        FieldLink link = party_member_link(g, place);
        if (!field_link_names_object(link) || field_party_objects_high(g, place) != 0 ||
            event_object_call_state_level(g, field_linked_object(link)) >= EVENT_LEVELS)
            return false;
    }
    return party_trail_places_hold(g);
}
EVENT_CHECK_TAKING_NOTHING(party_can_regroup)

/* $D6's guard: every party member's object is one the port reaches, or there
 * is nobody in the place. */
static bool members_are_objects(const GameState *g)
{
    for (PartyPlace place = 0; place < PARTY_PLACES; place++)
        if (place_taken(g, place) && !field_link_names_object(party_member_link(g, place)))
            return false;
    return true;
}
EVENT_CHECK_TAKING_NOTHING(members_are_objects)

EVENT_OPCODES(event_party_opcodes,
    { 0x20, OPERATION(note_leader), .variable = VARIABLE(1), .length = 2 },
    { 0xD9, OPERATION(take_formation), .leader_place = FORMATION_PLACE(1), .first_place = FORMATION_PLACE(3),
      .second_place = FORMATION_PLACE(5), .length = 7, CHECK(party_can_regroup) },
    { 0xDA, OPERATION(gather_party), .length = 1, CHECK(party_can_regroup) },
    { 0xD6, .character = OPERAND(1), .length = 2, CHECK(members_are_objects) },
    { 0xD5, .first_argument = OPERAND(1), .second_argument = OPERAND(2), .length = 3 },
    { 0xD2, .character = OPERAND(1), .length = 3, .jump = 2 },
    { 0xCF, .character = OPERAND(1), .length = 3, .jump = 2 },
    { 0xD0, .character = OPERAND(1), .length = 2 },
    { 0xD1, .character = OPERAND(1), .length = 2 },
    { 0xD3, .character = OPERAND(1), .length = 2, CHECK(joining_character) },
    { 0xD4, .character = OPERAND(1), .length = 2, CHECK(places_hold_objects) },
    { 0xD7, .item = OPERAND(1), .variable = VARIABLE(2), .length = 3 },
    { 0xF8, .length = 1 },
    { 0xF9, .length = 1 },
    { 0xFA, .length = 1 });
