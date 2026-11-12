#include "core/program.h"
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

#include "battle/inventory.h"
#include "charge.h"
#include "core/input.h"
#include "cpu_io.h"
#include "event/calls.h"
#include "event/chests.h"
#include "event/dialog.h"
#include "field/scratch.h"
#include "event/party.h"
#include "event/script.h"
#include "field/bridge.h"
#include "field/dialog_window.h"
#include "field/field.h"
#include "field/layers.h"
#include "field/main_loop.h"
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

/* The object is pushed: the push script walks it to the tile, at the most
 * urgent level, and no other push starts until it is done. */
static void push(GameState *g, FieldObject object, MapTile to)
{
    party_script_go_to(g, PUSH_SCRIPT, to);
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
    if (t.pushed)
        push(g, object, t.to);
}

/* ---- The confirm button ---------------------------------------------------------------- */

/* How near before the leader an object is to be talked to: along the way
 * the leader faces, under 28 pixels; across, under 12. */
enum { TALK_REACH_ALONG = 0x01C0, TALK_REACH_ACROSS = 0x00C0 };

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
static Search search_before_the_leader(const GameState *g, FieldLeader leader)
{
    Search r = { field_no_link(), 0, 0, 0 };
    FieldPosition at = field_leader_position(g, leader);
    Facing facing = field_leader_facing(g, leader);
    for (FieldObject obj = object_search_start(g); obj >= 1; obj--) {
        r.passed++;
        if (object_view(g, obj) == OUT_OF_VIEW)
            continue;
        ObjectRole role = object_role(g, obj);
        if (role.disabled || (role.kind != ROLE_WINDOW_ONLY && obj == leader.obj))
            continue;
        r.measured++;
        r.last_measured = obj;
        ObjectBearing b = object_bearing(at, object_position(g, obj), facing, measured_nearer(g, obj, leader.obj));
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

/* The original steps its index down the slots with the checks spelled out
 * for each, where the port reads a byte or two a slot: what the port charges
 * is that difference, a pass for every slot and more for each it measures,
 * at the confirm button's own rate (as field/motion.c's search does). */
enum { CONFIRM_BUTTON = 0xC01AAC, ACCESSES_A_SLOT = 1, ACCESSES_A_MEASURE = 6 };

static void find_the_object_before_the_leader(GameState *g)
{
    FieldLeader leader = field_leader(g);
    Search r = search_before_the_leader(g, leader);
    charge_as(g, CONFIRM_BUTTON, (uint32_t)r.passed * ACCESSES_A_SLOT +
                                                     (uint32_t)r.measured * ACCESSES_A_MEASURE);
    set_leader_contact(g, r.found);
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
    if (field_dialog_hold(g) == DIALOG_HOLD_UNTIL_CONFIRMED) {
        set_field_dialog_hold(g, 0);
        return nothing;
    }
    if (!party_is_the_players(g))
        return nothing;
    if (!leader_has_contact(g))
        find_the_object_before_the_leader(g);
    if (leader_has_contact(g)) {
        TIMED_AS(g, TALK, talk_to_the_leaders_contact(g));
    }
    return open_the_chest_before_the_leader(g);
}

/* ---- The confirm button, called by the still-gated field ----------------------------- */

/* A chest's gift goes to the party: the item into its inventory, or the gold
 * to its own (battle/inventory.h), each timed as the battle's routine that
 * takes it; then the field announces it. */
static void give_the_gift(GameState *g, ChestGift gift)
{
    if (gift.kind == GIFT_ITEM)
        TIMED_AS(g, 0xC1D005, battle_inventory_add(g, (uint8_t)gift.amount));
    else
        TIMED_AS(g, 0xC1D0A2, battle_give_gold(g, gift.amount));
    announce_the_gift(g, gift);
}

static bool confirm_fits(const GameState *g);

void field_confirm_pressed(GameState *g)
{
    vars_peek(true);
    bool fits = confirm_fits(g);
    vars_peek(false);
    if (!fits) {
        program_breaks_a_contract(g, 0xC01AAC, "the confirm button names an invalid contact or chest");
        return;
    }
    ChestGift gift = press_confirm(g);
    if (gift.kind != GIFT_NOTHING)
        give_the_gift(g, gift);
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

/* The confirm button's: what a press reads is the game's where it goes on
 * past the pad. The leader is an object the tables hold or nobody (the
 * empty place's marker), as the game keeps it (a word whose high byte is
 * 0); the search looks at slots inside the tables; the object talked to,
 * the walk's or the search's, is one; and where no dialog is open, the
 * chest's look stays on the map (and with nobody leading finds no chest). */
static bool confirm_fits(const GameState *g)
{
    if (!(input_mapped(g, ASSIGNED_PRESSED) & ASSIGNED_CONFIRM) || field_dialog_hold(g) == DIALOG_HOLD_UNTIL_CONFIRMED ||
        !party_is_the_players(g))
        return true;
    if (!field_leader_readable(g))
        return false;
    FieldLink talked_to = leader_contact(g);
    uint8_t high = field_leader_contact_high(g);
    if (!field_link_is_object(talked_to)) {
        if (!object_search_fits(g))
            return false;
        talked_to = search_before_the_leader(g, field_leader(g)).found;
        high = 0;
    }
    if (field_link_is_object(talked_to) && !names_a_contact(g, talked_to, high))
        return false;
    return dialog_on_screen(g) || chest_gift_fits(g);
}
