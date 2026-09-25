/* The leader meeting the map's objects: the player pressing the confirm
 * button, which talks to the object before the leader and opens the chest
 * before it (event/chests.c), and the leader walking into an object, which
 * touches it and may push it a tile along.
 *
 * The field notes the object the leader is up against (field/field.h,
 * leader_contact): the one its walk ran into this frame, or else the one the
 * confirm button finds before it, near enough the way it faces, which the
 * press notes the same way. Talking to it and touching it are calls the
 * field makes on the object's script, as objects make on each other
 * (event/calls.h): a talk calls its first function at the first level, a
 * touch its second function at the second, and only where the object takes
 * such a call now.
 *
 * An object that can be pushed (a crate, a statue) is pushed a tile the way
 * the leader faces, where that tile is open: it drops what it is doing for the
 * party's push script (event/party.h), which walks it there. One push at a
 * time, and not of an object running at the most urgent level already. */
#include "event/contact.h"

#include "charge.h"
#include "core/input.h"
#include "cpu_io.h"
#include "event/calls.h"
#include "event/chests.h"
#include "event/dialog.h"
#include "event/leaves.h"
#include "event/party.h"
#include "event/script.h"
#include "field/bridge.h"
#include "field/field.h"
#include "field/layers.h"
#include "field/party.h"

enum { TALK_LEVEL = 1, TALK_FUNCTION = 1, TOUCH_LEVEL = 2, TOUCH_FUNCTION = 2, PUSHED_LEVEL = 0 };

/* The object the leader is up against, if any. */
bool leader_has_contact(const GameState *g) { return field_link_is_object(leader_contact(g)); }
static FieldObject contact(const GameState *g) { return field_linked_object(leader_contact(g)); }

/* A call the field makes on the object: it drops what it is doing for one of
 * its functions. */
static void call_on(GameState *g, FieldObject object, uint8_t level, uint8_t function)
{
    object_run_script_from(g, object, level, object_function_start(g, object, function));
}

// $C0:5AC5
/* The player talks to the object before the leader. */
void talk_to_the_leaders_contact(GameState *g)
{
    FieldObject object = contact(g);
    if (object_takes_a_call_now(g, object, TALK_LEVEL))
        call_on(g, object, TALK_LEVEL, TALK_FUNCTION);
}

/* ---- Touching and pushing -------------------------------------------------------------- */

/* The tile past the object the way the leader faces, and whether there is
 * one: not past the map's top or left edge. */
typedef struct {
    bool there;
    MapTile tile;
} TileBeyond;

static TileBeyond tile_beyond(MapTile at, Facing way)
{
    switch (way) {
    case FACING_UP: return (TileBeyond){ at.y != 0, { at.x, (uint8_t)(at.y - 1) } };
    case FACING_DOWN: return (TileBeyond){ true, { at.x, (uint8_t)(at.y + 1) } };
    case FACING_LEFT: return (TileBeyond){ at.x != 0, { (uint8_t)(at.x - 1), at.y } };
    default: return (TileBeyond){ true, { (uint8_t)(at.x + 1), at.y } };
    }
}

static bool tile_blocked(const GameState *g, MapTile tile)
{
    return (map_tile_attributes(g, (MapPlace){ tile.y, tile.x }).lower & TILE_BLOCKED) != 0;
}

static Facing leaders_facing(const GameState *g)
{
    return object_facing(g, field_linked_object(party_member_link(g, PARTY_LEADER)));
}

bool leader_looks_to_push(const GameState *g, bool touched)
{
    FieldObject object = contact(g);
    uint8_t level = touched ? TOUCH_LEVEL : event_object_call_state_level(g, object);
    return object_takes_calls(g, object) && !field_object_being_pushed(g) && level != PUSHED_LEVEL;
}

Touch leaders_touch(const GameState *g)
{
    FieldObject object = contact(g);
    Touch t = { 0 };
    t.touched = object_takes_a_call_now(g, object, TOUCH_LEVEL);
    if (!leader_looks_to_push(g, t.touched) || !object_can_be_pushed(g, object))
        return t;
    TileBeyond beyond = tile_beyond(object_tile(g, object), leaders_facing(g));
    if (!beyond.there || tile_blocked(g, beyond.tile))
        return t;
    t.pushed = true;
    t.to = beyond.tile;
    return t;
}

/* What the original leaves in the arithmetic units: looking at a push, the
 * object's level as the multiplier's first operand (for the place it works
 * out next); and pushing, a word stored as the second operand, whose high
 * byte clears the divider's dividend's low byte. */
static void leave_looking_to_push(GameState *g, FieldObject object)
{
    if (!leader_looks_to_push(g, false))
        return;
    hw_set_multiplicand(g, event_object_call_state_level(g, object));
}

static void leave_pushing(GameState *g)
{
    hw_set_dividend(g, (uint16_t)(hw_dividend(g) & 0xFF00));
}

/* The object is pushed: the push script walks it to the tile, at the most
 * urgent level, and no other push starts until it is done. */
static void push(GameState *g, FieldObject object, MapTile to)
{
    party_script_go_to(g, PUSH_SCRIPT, to);
    ORIGINAL_EFFECT(leave_pushing(g));
    object_run_script_from(g, object, PUSHED_LEVEL, party_script_start(PUSH_SCRIPT));
    mark_field_object_pushed(g);
}

// $C0:3154, and its push $C0:31B1
/* The leader walks into the object it is up against: the object is touched,
 * and where it can be it is pushed. */
void touch_what_the_leader_met(GameState *g)
{
    FieldObject object = contact(g);
    Touch t = leaders_touch(g);
    if (t.touched)
        call_on(g, object, TOUCH_LEVEL, TOUCH_FUNCTION);
    ORIGINAL_EFFECT(leave_looking_to_push(g, object));
    if (t.pushed)
        push(g, object, t.to);
}

/* ---- The confirm button ---------------------------------------------------------------- */

/* A dialog that stays until the player presses the confirm button
 * (field_dialog_hold). */
enum { HOLD_UNTIL_CONFIRMED = 0xFFFF };

/* How near before the leader an object is to be talked to: along the way
 * the leader faces, under 28 pixels; across, under 12. */
enum { TALK_REACH_ALONG = 0x01C0, TALK_REACH_ACROSS = 0x00C0 };

static FieldObject the_leader(const GameState *g) { return field_linked_object(party_member_link(g, PARTY_LEADER)); }

/* Whether the original measures the object a sixteenth nearer along
 * (field/object.h, object_bearing): its subtraction borrows what its
 * comparison with the leader's slot left, for an object in a slot before
 * the leader's; it doesn't compare an object that only comes and goes with
 * the view, whose check leaves no borrow. */
static bool measured_nearer(const GameState *g, FieldObject obj, FieldObject leader)
{
    return object_role(g, obj).kind != ROLE_WINDOW_ONLY && obj < leader;
}

static bool within_talking_reach(ObjectBearing b)
{
    return b.ahead && b.along < TALK_REACH_ALONG && b.across < TALK_REACH_ACROSS;
}

/* The object before the leader, and how far the search went (for its time,
 * and the last object it measured, for what the original leaves). */
typedef struct {
    FieldLink found;
    uint8_t passed, measured;
    FieldObject last_measured;
} Search;

// $C0:1CFC, and its four ways $C0:1D5E-$C0:1DF3
/* The first object, from one slot past the map's down to the second (never
 * the first), that is in view, not disabled and not the leader, and near
 * enough before it. An object that only comes and goes with the view is
 * looked at even in the leader's slot. */
static Search search_before_the_leader(const GameState *g, FieldObject leader)
{
    Search r = { field_no_link(), 0, 0, 0 };
    FieldPosition at = object_position(g, leader);
    Facing facing = object_facing(g, leader);
    for (FieldObject obj = object_search_start(g); obj >= 1; obj--) {
        r.passed++;
        if (object_view(g, obj) == OUT_OF_VIEW)
            continue;
        ObjectRole role = object_role(g, obj);
        if (role.disabled || (role.kind != ROLE_WINDOW_ONLY && obj == leader))
            continue;
        r.measured++;
        r.last_measured = obj;
        ObjectBearing b = object_bearing(at, object_position(g, obj), facing, measured_nearer(g, obj, leader));
        if (within_talking_reach(b)) {
            r.found = field_link_to(obj);
            break;
        }
    }
    return r;
}

/* For the original's code and shadow mode: the search keeps the leader's
 * facing doubled in the second scratch word and its position in two cells
 * of the field's page ($C3, $C5); the last object it measured leaves the
 * overflow of its last subtraction, along the way or, where the object was
 * near enough along, across. */
enum { CELL_LEADER_X = 0xC3, CELL_LEADER_Y = 0xC5 };

static bool subtraction_overflows(uint16_t from, uint16_t take, bool borrow)
{
    return field_add_with_carry_overflows16(from, (uint16_t)~take, !borrow);
}

static void leave_search(GameState *g, FieldObject leader, Search r)
{
    FieldPosition at = object_position(g, leader);
    Facing facing = object_facing(g, leader);
    keep_word(g, KEPT_SECOND, (uint16_t)(facing * 2));
    original_cell16(g, CELL_LEADER_X, at.x);
    original_cell16(g, CELL_LEADER_Y, at.y);
    if (r.measured == 0)
        return;
    FieldPosition there = object_position(g, r.last_measured);
    bool nearer = measured_nearer(g, r.last_measured, leader);
    ObjectBearing b = object_bearing(at, there, facing, nearer);
    bool vertical = facing == FACING_UP || facing == FACING_DOWN;
    if (b.ahead && b.along < TALK_REACH_ALONG)
        event_leave_overflow(subtraction_overflows(vertical ? there.x : there.y, vertical ? at.x : at.y, true));
    else
        event_leave_overflow(subtraction_overflows(vertical ? there.y : there.x, vertical ? at.y : at.x, nearer));
}

/* The original steps its index down the slots with the checks spelled out
 * for each, where the port reads a byte or two a slot: what the port charges
 * is that difference, a pass for every slot and more for each it measures,
 * at the confirm button's own rate (as field/motion.c's search does). */
enum { CONFIRM_BUTTON = 0xC01AAC, ACCESSES_A_SLOT = 1, ACCESSES_A_MEASURE = 6 };

static void find_the_object_before_the_leader(GameState *g)
{
    FieldObject leader = the_leader(g);
    Search r = search_before_the_leader(g, leader);
    ORIGINAL_EFFECT(leave_search(g, leader, r));
    ORIGINAL_EFFECT(charge_as(g, CONFIRM_BUTTON, (uint32_t)r.passed * ACCESSES_A_SLOT +
                                                     (uint32_t)r.measured * ACCESSES_A_MEASURE));
    set_leader_contact(g, r.found);
}

/* A talk leaves the overflow clear where the object takes the call. */
static void leave_talk(const GameState *g)
{
    if (object_takes_a_call_now(g, contact(g), TALK_LEVEL))
        event_leave_overflow(false);
}

enum { TALK = 0xC05AC5 };

// $C0:1AAC
/* The player presses the confirm button on the field. Where a dialog waits
 * for it, the press is the dialog's alone. Otherwise, while the player has
 * the party, the leader talks to the object it walked into this frame or,
 * failing that, the one before it; then it opens the chest before it, whose
 * gift the party's inventory takes before the field announces it. */
ChestGift press_confirm(GameState *g)
{
    ChestGift nothing = { GIFT_NOTHING, 0 };
    if (!(input_mapped(g, ASSIGNED_PRESSED) & ASSIGNED_CONFIRM))
        return nothing;
    if (field_dialog_hold(g) == HOLD_UNTIL_CONFIRMED) {
        set_field_dialog_hold(g, 0);
        return nothing;
    }
    if (!party_is_the_players(g))
        return nothing;
    if (!leader_has_contact(g))
        find_the_object_before_the_leader(g);
    if (leader_has_contact(g)) {
        ORIGINAL_EFFECT(leave_talk(g));
        TIMED_AS(g, TALK, talk_to_the_leaders_contact(g));
    }
    return open_the_chest_before_the_leader(g);
}

/* ---- The entries --------------------------------------------------------------------- */

/* The field's talk ($C0:1AD7) and event $B0's touch call them with 8-bit
 * index registers, the object the leader is up against one; they leave Y and
 * the overflow as event/leaves.h works them out. */
static void leave_talk_registers(Snes *s, bool called)
{
    m_set_flag(s, CPU_FLAG_V, event_place_worked_out_overflow(called, m_flag(s, CPU_FLAG_V)));
}

static void leave_touch_registers(Snes *s, Touch t, bool looked_to_push)
{
    s->cpu.y = event_touch_y(state_of(s), looked_to_push, s->cpu.y);
    m_set_flag(s, CPU_FLAG_V, event_place_worked_out_overflow(t.touched || t.pushed, m_flag(s, CPU_FLAG_V)));
}

void event_talk_to_contact(Snes *s)
{
    GameState *g = state_of(s);
    vars_peek(true);
    bool calls = object_takes_a_call_now(g, contact(g), TALK_LEVEL);
    vars_peek(false);
    talk_to_the_leaders_contact(g);
    ORIGINAL_EFFECT(leave_talk_registers(s, calls));
    m_rts(s);
}

/* The confirm button, from the field's main loop with 16-bit index
 * registers on the field's page. A chest's gift goes to the party through
 * the original's inventory ($C1:8003: A what, 1 an item or 4 gold, and Y
 * which or how much), which keeps the flags as it found them; the entry
 * leaves the overflow as the press worked it out. */
enum { GIVE_TO_THE_PARTY = 0xC18003, GIVES_AN_ITEM = 1, GIVES_GOLD = 4 };
enum { THE_INVENTORY_IGNORES = DC_X | DC_N | DC_Z | DC_C | DC_AH };

static void leave_overflow(Snes *s) { m_set_flag(s, CPU_FLAG_V, event_overflow_left(m_flag(s, CPU_FLAG_V))); }

void field_press_confirm(Snes *s)
{
    GameState *g = state_of(s);
    ORIGINAL_EFFECT(event_forget_overflow());
    ChestGift gift = press_confirm(g);
    if (gift.kind != GIFT_NOTHING) {
        ORIGINAL_EFFECT(leave_overflow(s));
        s->cpu.y = gift.amount;
        bool came_back;
        if (gift.kind == GIFT_ITEM) {
            s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | GIVES_AN_ITEM);
            came_back = bridge_call_long(s, 0xC01E72, GIVE_TO_THE_PARTY, THE_INVENTORY_IGNORES, on_the_field_page(s, false));
        } else {
            s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | GIVES_GOLD);
            came_back = bridge_call_long(s, 0xC01E84, GIVE_TO_THE_PARTY, THE_INVENTORY_IGNORES, on_the_field_page(s, false));
        }
        if (!came_back)
            return;
        announce_the_gift(g, gift);
    }
    ORIGINAL_EFFECT(leave_overflow(s));
    m_rts(s);
}

void event_touch_contact(Snes *s)
{
    GameState *g = state_of(s);
    vars_peek(true);
    Touch t = leaders_touch(g);
    bool looks = leader_looks_to_push(g, t.touched);
    vars_peek(false);
    touch_what_the_leader_met(g);
    ORIGINAL_EFFECT(leave_touch_registers(s, t, looks));
    m_rts(s);
}

/* The guards: the object is one the tables hold, as the game keeps it (the
 * original adds its whole word to a place's offset), and runs at one of the
 * levels the places hold; the leader, whose facing a push goes by, faces one
 * of its four ways; and a push looks at a tile of the map. */
static bool names_a_contact(const GameState *g, FieldLink object, uint8_t high)
{
    if (!field_names_object(field_link_byte(object)) || high != 0)
        return false;
    return event_object_call_state_level(g, field_linked_object(object)) < EVENT_LEVELS;
}

static bool contact_is_one(const GameState *g) { return names_a_contact(g, leader_contact(g), field_leader_contact_high(g)); }

bool event_contact_is_one(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    bool ok = contact_is_one(state_of(s));
    vars_peek(false);
    return ok;
}

bool event_touch_is_ours(const GameState *g)
{
    if (!contact_is_one(g))
        return false;
    FieldObject object = contact(g);
    FieldLink leader = party_member_link(g, PARTY_LEADER);
    if (!object_takes_calls(g, object) || !object_can_be_pushed(g, object))
        return true;
    if (!field_link_names_object(leader) || !object_facing_is_one(g, field_linked_object(leader)))
        return false;
    TileBeyond beyond = tile_beyond(object_tile(g, object), leaders_facing(g));
    return !beyond.there || tile_on_the_map(beyond.tile);
}

bool event_touch_can_run(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    bool ok = event_touch_is_ours(state_of(s));
    vars_peek(false);
    return ok;
}

/* The confirm button's: what a press reads is the game's where it goes on
 * past the pad. The leader is an object the tables hold, as the game keeps
 * it (a word whose high byte is 0); the search looks at slots inside the
 * tables; the object talked to, the walk's or the search's, is one; and
 * where no dialog is open, the chest's look stays on the map. */
static bool confirm_fits(const GameState *g)
{
    if (!(input_mapped(g, ASSIGNED_PRESSED) & ASSIGNED_CONFIRM) || field_dialog_hold(g) == HOLD_UNTIL_CONFIRMED ||
        !party_is_the_players(g))
        return true;
    FieldLink leader = party_member_link(g, PARTY_LEADER);
    if (field_party_objects_high(g, PARTY_LEADER) != 0 || !field_link_names_object(leader))
        return false;
    FieldLink talked_to = leader_contact(g);
    uint8_t high = field_leader_contact_high(g);
    if (!field_link_is_object(talked_to)) {
        if (!object_search_fits(g))
            return false;
        talked_to = search_before_the_leader(g, field_linked_object(leader)).found;
        high = 0;
    }
    if (field_link_is_object(talked_to) && !names_a_contact(g, talked_to, high))
        return false;
    return dialog_on_screen(g) || chest_look_fits(g);
}

bool field_confirm_can_run(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    bool ok = confirm_fits(state_of(s));
    vars_peek(false);
    return ok;
}
