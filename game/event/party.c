#include "event/script.h"
#include "event/script.h"
/* Scripts and the party (event/party.h): who is in a place of it (Someone),
 * who leads it, someone leaving it, and the party's record, which the
 * menus keep (menu/party.h).
 *
 * The party's record is the menus': nine places, the three of the active
 * party and the six of the reserve, each a character or empty. A script asks
 * whether a character has been recruited (is in any of the nine) or is in the
 * active party, recruits one into the reserve, dismisses one, brings one into
 * the active party, or sends one of the active party to the reserve, through
 * the menus' services ($C2:8004, menu/party.h). On the map, the characters'
 * objects follow: one joining becomes the next member's, and the members
 * behind one sent away close up, with their palettes.
 *
 * These commands read their own operands (event/engine.h,
 * EVENT_UNDECODED_COMMANDS), and whether the object's turn goes on after
 * one is worked out as the original's engine decides it. */
#include "battle/battle.h"
#include "charge.h"
#include "event/engine.h"
#include "event/calls.h"
#include "event/party.h"
#include "core/program.h"
#include "field/bridge.h"
#include "field/main_loop.h"
#include "field/palettes.h"
#include "event/variables.h"
#include "field/field.h"
#include "field/party.h"
#include "menu/items.h"
#include "menu/party.h"

/* ---- Someone to walk up to or ask after -------------------------------------------------- */

Someone someone_of_object(const GameState *g, FieldObject object)
{
    return (Someone){ true, object_disabled(g, object), object_position(g, object), object };
}

Someone member_in_place(const GameState *g, PartyPlace place, FieldObject asking)
{
    (void)asking;
    FieldLink link = party_member_link(g, place);
    if (field_link_is_object(link))
        return someone_of_object(g, field_linked_object(link));
    Someone nobody = { .there = false };
    return nobody;
}

/* The marker's byte is an offset like an object's (field/object.h,
 * field_link_readable). */
bool member_place_readable(const GameState *g, PartyPlace place)
{
    return field_link_readable(party_member_link(g, place));
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

/* ---- The party's record, through the menus' services ----------------------------------------- */

/* The menus' services, which the original's long call reaches; their time
 * is theirs. */
enum { MENUS_SERVICES = 0xC28004 };

/* What every one of the menus' services ends with, after its work. */
static void shown(GameState *g) { party_wear_it_shows(g); }

/* Every character's HP filled, then (or alone) every character's MP, each
 * through the menus' services; `answered` follows each, saying whether the
 * other follows. */
static void fill_with(GameState *g, bool hp, bool mp)
{
    if (hp) {
        TIMED_AS(g, MENUS_SERVICES, (party_fill_hp(g), shown(g)));
    }
    if (mp) {
        TIMED_AS(g, MENUS_SERVICES, (party_fill_mp(g), shown(g)));
    }
}

/* A condition on the record ($D2, $CF): on after it if the character is
 * among the record's first `places`, otherwise its jump, by the distance in
 * its last operand. */
enum { ASKING_LENGTH = 3, ASKING_JUMP_BYTE = 2 };

static bool among_the_record(GameState *g, uint8_t character, unsigned places)
{
    bool there = party_place_of(g, character, places) != NOT_IN_THE_RECORD;
    shown(g);
    return there;
}

static EventStep after_asking(const GameState *g, uint16_t at, bool there)
{
    if (there)
        return (EventStep){ (uint16_t)(at + ASKING_LENGTH), true };
    uint16_t from = (uint16_t)(at + ASKING_JUMP_BYTE);
    return (EventStep){ (uint16_t)(from + script_byte(g, from)), true };
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
    PartyPlace place = place_taken(g, PARTY_FIRST_MEMBER) ? PARTY_SECOND_MEMBER : PARTY_FIRST_MEMBER;
    set_field_party_objects(g, place, field_link_byte(object));
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

/* ---- The commands ------------------------------------------------------------------------- */

/* After the commands that call the menus' services the object's turn ends:
 * the original's dispatch reaches them with the carry clear, which its
 * engine takes as the turn's end. */
enum { THE_TURN_GOES_ON_AFTER_THE_MENUS = false };

bool event_bring_into_the_party(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    enum { LENGTH = 2 };
    uint8_t character = event_decode(g, pf, event_opcode(0xD3), at).character;
    TIMED_AS(g, MENUS_SERVICES, ((void)party_bring_in(g, character), shown(g)));
    object_joins_the_party(g, character);
    *step = (EventStep){ (uint16_t)(at + LENGTH), THE_TURN_GOES_ON_AFTER_THE_MENUS };
    return true;
}

static bool asked(GameState *g, Platform *pf, uint16_t at, EventStep *step, uint8_t opcode, unsigned places)
{
    uint8_t character = event_decode(g, pf, event_opcode(opcode), at).character;
    bool there = false;
    TIMED_AS(g, MENUS_SERVICES, there = among_the_record(g, character, places));
    *step = after_asking(g, at, there);
    return true;
}

static bool filled(GameState *g, uint16_t at, EventStep *step, bool hp, bool mp)
{
    fill_with(g, hp, mp);
    *step = (EventStep){ (uint16_t)(at + 1), THE_TURN_GOES_ON_AFTER_THE_MENUS };
    return true;
}

bool event_fill_hp_and_mp(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    (void)pf;
    return filled(g, at, step, true, true);
}

bool event_fill_hp(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    (void)pf;
    return filled(g, at, step, true, false);
}

bool event_fill_mp(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    (void)pf;
    return filled(g, at, step, false, true);
}

bool event_if_in_the_active_party(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    return asked(g, pf, at, step, 0xD2, PARTY_ACTIVE_PLACES);
}

bool event_if_recruited(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    return asked(g, pf, at, step, 0xCF, PARTY_RECORD_PLACES);
}

/* A change to the record: a character recruited into the reserve ($D0), or
 * dismissed from it ($D1). */
static bool record_changed(GameState *g, Platform *pf, uint16_t at, EventStep *step, uint8_t opcode)
{
    enum { LENGTH = 2 };
    uint8_t character = event_decode(g, pf, event_opcode(opcode), at).character;
    if (opcode == 0xD0)
        TIMED_AS(g, MENUS_SERVICES, ((void)party_recruit(g, character), shown(g)));
    else
        TIMED_AS(g, MENUS_SERVICES, ((void)party_dismiss(g, character), shown(g)));
    *step = (EventStep){ (uint16_t)(at + LENGTH), THE_TURN_GOES_ON_AFTER_THE_MENUS };
    return true;
}

bool event_recruit(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    return record_changed(g, pf, at, step, 0xD0);
}

bool event_dismiss(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    return record_changed(g, pf, at, step, 0xD1);
}

/* A variable learns how many of an item the party holds ($D7). */
bool event_note_item_count(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    enum { LENGTH = 3 };
    EventCommand c = event_decode(g, pf, event_opcode(0xD7), at);
    uint8_t count = 0;
    TIMED_AS(g, MENUS_SERVICES, (count = menu_items_held(g, c.item), shown(g)));
    set_variable_byte(g, c.variable, count);
    *step = (EventStep){ (uint16_t)(at + LENGTH), true };
    return true;
}

bool event_equip(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    enum { LENGTH = 3 };
    EventCommand c = event_decode(g, pf, event_opcode(0xD5), at);
    uint8_t character = (uint8_t)c.first_argument, item = (uint8_t)c.second_argument;
    TIMED_AS(g, MENUS_SERVICES, (party_equip(g, character, item), shown(g)));
    *step = (EventStep){ (uint16_t)(at + LENGTH), THE_TURN_GOES_ON_AFTER_THE_MENUS };
    return true;
}

bool event_take_out_of_party(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    enum { LENGTH = 2 };
    uint8_t character = event_decode(g, pf, event_opcode(0xD6), at).character;
    bool go_on = goes_on_after_leaving(g, character, THE_TURN_GOES_ON_AFTER_THE_MENUS);
    take_out_of_party(g, character);
    *step = (EventStep){ (uint16_t)(at + LENGTH), go_on };
    return true;
}

bool event_send_to_the_reserve(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    enum { LENGTH = 2 };
    uint8_t character = event_decode(g, pf, event_opcode(0xD4), at).character;
    bool go_on = goes_on_after_sending(g, character, THE_TURN_GOES_ON_AFTER_THE_MENUS);
    member_steps_out(g, character);
    TIMED_AS(g, MENUS_SERVICES, ((void)party_send_to_the_reserve(g, character), shown(g)));
    *step = (EventStep){ (uint16_t)(at + LENGTH), go_on };
    return true;
}

/* What the port checks of them: the menus' services read the active party as characters
 * (menu/party.h), and equip one of the characters; one joining has its cell
 * for its object on the map among the seven; the party's places, where
 * taken, are objects the tables hold. */
static bool record_shows_characters(const GameState *g) { return party_record_shows_characters(g); }
EVENT_CHECK_TAKING_NOTHING(record_shows_characters)

static bool equipping_a_character(const GameState *g, uint16_t character, uint16_t item)
{
    return character < CHARACTER_RECORDS_COUNT && party_can_equip(g, (uint8_t)item) && party_record_shows_characters(g);
}
EVENT_CHECK(equipping_a_character, first_argument, second_argument)

static bool joining_character(const GameState *g, uint8_t character)
{
    return character < FIELD_CHARACTER_OBJECTS_COUNT && party_record_shows_characters(g);
}
EVENT_CHECK(joining_character, character)

static bool places_hold_objects(const GameState *g)
{
    for (PartyPlace place = 0; place < PARTY_PLACES; place++) {
        FieldLink link = party_member_link(g, place);
        if (field_link_is_object(link) && !field_link_names_object(link))
            return false;
    }
    return party_record_shows_characters(g);
}
EVENT_CHECK_TAKING_NOTHING(places_hold_objects)

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

/* The original keeps the operand bytes of the last place it lays out: the
 * column and the row, each with its facing bit. */
enum { FACING_BIT = 0x80 };

/* A member takes its place in the formation: it drops what it was doing for
 * its script, which is told the tile and the way to face. */
static void send_to_formation_place(GameState *g, PartyPlace place, FormationPlace p)
{
    PartyScript script = formation_script(place);
    object_run_script_from(g, member_object(g, place), PARTY_SCRIPT_LEVEL, party_script_start(script));
    party_script_go_to(g, script, p.tile);
    party_script_face(g, script, p.facing);
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

// $C0:034B
/* After a battle the party comes back together: the members walk back to
 * the leader through the gathering script, as $DA sends them, and catch its
 * trail up; the gathering count starts as $DA's does. With nobody behind the
 * leader, the party is the player's at once; a battle that leaves the party
 * where it stands sends nobody. */
void party_back_together(GameState *g)
{
    vars_peek(true);
    bool fits = party_can_come_back_together(g);
    vars_peek(false);
    if (!fits) {
        program_breaks_a_contract(g, 0xC0034B, "the returning party names invalid objects or script levels");
        return;
    }
    if (battle_terms(g, BATTLE_TERMS_PARTY_STAYS_PUT)) {
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
        TIMED_AS(g, 0xC05B1F, member_back_to_the_leader(g, member));
        gathered--;
    }
    set_variable_byte(g, GATHERING_COUNT, gathered);
    party_trail_catch_up(g);
}

/* Whether the port brings the party back together: the leader's contact
 * cell's high byte clear; the members there are objects the tables
 * hold at levels the places hold, and the cell the original names them in
 * holds only their offsets (it adds its whole word to a place's). */
bool party_can_come_back_together(const GameState *g)
{
    bool ok = field_leader_contact_high(g) == 0;
    for (PartyPlace place = PARTY_FIRST_MEMBER; place < PARTY_PLACES && ok && place_taken(g, place); place++) {
        FieldLink link = party_member_link(g, place);
        ok = field_link_names_object(link) &&
             event_object_call_state_level(g, field_linked_object(link)) < EVENT_LEVELS;
    }
    return ok;
}

/* What the port checks of the regrouping: the party's places hold objects the tables do, as
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

/* What the port checks of $D6: every party member's object is one the port reaches, or there
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
    { 0xD5, .first_argument = OPERAND(1), .second_argument = OPERAND(2), .length = 3, CHECK(equipping_a_character) },
    { 0xD2, .character = OPERAND(1), .length = 3, .jump = 2, CHECK(record_shows_characters) },
    { 0xCF, .character = OPERAND(1), .length = 3, .jump = 2, CHECK(record_shows_characters) },
    { 0xD0, .character = OPERAND(1), .length = 2, CHECK(record_shows_characters) },
    { 0xD1, .character = OPERAND(1), .length = 2, CHECK(record_shows_characters) },
    { 0xD3, .character = OPERAND(1), .length = 2, CHECK(joining_character) },
    { 0xD4, .character = OPERAND(1), .length = 2, CHECK(places_hold_objects) },
    { 0xD7, .item = OPERAND(1), .variable = VARIABLE(2), .length = 3, CHECK(record_shows_characters) },
    { 0xF8, .length = 1, CHECK(record_shows_characters) },
    { 0xF9, .length = 1, CHECK(record_shows_characters) },
    { 0xFA, .length = 1, CHECK(record_shows_characters) });
