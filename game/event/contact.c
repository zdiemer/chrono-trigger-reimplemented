/* The leader meeting the map's objects: the player talking to the object
 * before the leader, and the leader walking into one, which touches it and
 * may push it a tile along.
 *
 * The field notes the object the leader is up against (field_leader_contact):
 * the one its walk ran into this frame, or the one before it when the player
 * presses the confirm button. Talking to it and touching it are calls the
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

#include "cpu_io.h"
#include "event/calls.h"
#include "event/leaves.h"
#include "event/party.h"
#include "event/script.h"
#include "field/bridge.h"
#include "field/field.h"
#include "field/layers.h"

enum { TALK_LEVEL = 1, TALK_FUNCTION = 1, TOUCH_LEVEL = 2, TOUCH_FUNCTION = 2, PUSHED_LEVEL = 0 };

/* The object the leader is up against, if any. */
bool leader_has_contact(const GameState *g) { return field_link_is_object(field_link_stored(field_leader_contact(g))); }
static FieldObject contact(const GameState *g) { return field_linked_object(field_link_stored(field_leader_contact(g))); }

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

Touch leaders_touch(const GameState *g)
{
    FieldObject object = contact(g);
    Touch t = { 0 };
    t.answers = object_takes_calls(g, object);
    if (!t.answers)
        return t;
    t.touched = object_takes_a_call_now(g, object, TOUCH_LEVEL);
    uint8_t level = t.touched ? TOUCH_LEVEL : event_object_call_state_level(g, object);
    t.looks_to_push = !(field_mode_flags(g) & FIELD_OBJECT_PUSHED) && level != PUSHED_LEVEL;
    if (!t.looks_to_push || !object_can_be_pushed(g, object))
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
    set_event_script_byte(g, PUSH_SCRIPT + PUSH_TILE, to.x);
    set_event_script_byte(g, PUSH_SCRIPT + PUSH_TILE + 1, to.y);
    ORIGINAL_EFFECT(leave_pushing(g));
    object_run_script_from(g, object, PUSHED_LEVEL, PUSH_SCRIPT);
    set_field_mode_flags(g, (uint8_t)(field_mode_flags(g) | FIELD_OBJECT_PUSHED));
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
    if (t.looks_to_push)
        ORIGINAL_EFFECT(leave_looking_to_push(g, object));
    if (t.pushed)
        push(g, object, t.to);
}

/* ---- The entries --------------------------------------------------------------------- */

/* The field's talk ($C0:1AD7) and event $B0's touch call them with 8-bit
 * index registers, the object the leader is up against one; they leave Y and
 * the overflow as event/leaves.h works them out. */
static void leave_talk_registers(Snes *s, bool called)
{
    m_set_flag(s, CPU_FLAG_V, event_place_worked_out_overflow(called, m_flag(s, CPU_FLAG_V)));
}

static void leave_touch_registers(Snes *s, Touch t)
{
    s->cpu.y = event_touch_y(state_of(s), t, s->cpu.y);
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

void event_touch_contact(Snes *s)
{
    GameState *g = state_of(s);
    vars_peek(true);
    Touch t = leaders_touch(g);
    vars_peek(false);
    touch_what_the_leader_met(g);
    ORIGINAL_EFFECT(leave_touch_registers(s, t));
    m_rts(s);
}

/* The guards: the object is one the tables hold, as the game keeps it (the
 * original adds its whole word to a place's offset), and runs at one of the
 * levels the places hold; the leader, whose facing a push goes by, faces one
 * of its four ways; and a push looks at a tile of the map. */
static bool contact_is_one(const GameState *g)
{
    if (!field_names_object(field_leader_contact(g)) || field_leader_contact_high(g) != 0)
        return false;
    return event_object_call_state_level(g, contact(g)) < EVENT_LEVELS;
}

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
