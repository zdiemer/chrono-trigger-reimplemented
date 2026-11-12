/* Field objects' script turns: when each object's script runs.
 *
 * An object's script doesn't run every frame. It takes a turn every so many
 * frames, as many as the object's move length (field/object.h), so a script
 * that sets its object walking gets its next turn as the walk's steps run
 * out. Each frame the field counts every object down to its next turn and
 * queues those whose turn has come: the leader at the front, so that the
 * player's character answers first, the rest at the back. Then the queued
 * turns run, the event engine running each object's script (event/engine.c),
 * for as long as the frame has time for them (platform/timing.h,
 * tm_frame_time_left): the original stops once the picture has reached a line
 * the map sets, 72 after a map loads, and what is left of the queue waits for
 * the next frame, that frame's turns joining it behind. A host has time for
 * all of it.
 *
 * The queue is a list of the objects' links from its head to its tail, each
 * object's link naming the one after it. */
#include "field/field.h"
#include "field/object.h"
#include "platform/timing.h"

#include "charge.h"
#include "core/program.h"
#include "event/engine.h"
#include "field/bridge.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The queue -------------------------------------------------------------------------------- */

static FieldLink queue_head(const GameState *g) { return field_link_stored(field_script_queue_head(g)); }
static FieldLink queue_tail(const GameState *g) { return field_link_stored(field_script_queue_tail(g)); }
static void set_queue_head(GameState *g, FieldLink l) { set_field_script_queue_head(g, field_link_byte(l)); }
static void set_queue_tail(GameState *g, FieldLink l) { set_field_script_queue_tail(g, field_link_byte(l)); }
static FieldLink queued_after(const GameState *g, FieldObject obj)
{
    return field_link_stored(field_object_script_link(g, obj));
}
static void set_queued_after(GameState *g, FieldObject obj, FieldLink l)
{
    set_field_object_script_link(g, obj, field_link_byte(l));
}

static void start_queue_with(GameState *g, FieldObject obj)
{
    set_queue_head(g, field_link_to(obj));
    set_queue_tail(g, field_link_to(obj));
}

/* Put the object at the front of the queue. False if it is there already. */
static bool queue_first(GameState *g, FieldObject obj)
{
    FieldLink head = queue_head(g);
    if (field_same_link(head, field_link_to(obj)))
        return false;
    if (!field_link_is_object(head)) {
        start_queue_with(g, obj);
    } else {
        set_queued_after(g, obj, head);
        set_queue_head(g, field_link_to(obj));
    }
    return true;
}

/* Put the object at the back of the queue. False if it is queued already:
 * at the back, or with an object after it. */
static bool queue_last(GameState *g, FieldObject obj)
{
    FieldLink tail = queue_tail(g);
    if (field_same_link(tail, field_link_to(obj)) || field_link_is_object(queued_after(g, obj)))
        return false;
    if (!field_link_is_object(tail)) {
        start_queue_with(g, obj);
    } else {
        set_queued_after(g, field_linked_object(tail), field_link_to(obj));
        set_queue_tail(g, field_link_to(obj));
    }
    return true;
}

/* Take the first object off the queue (which isn't empty). */
static FieldObject take_the_first(GameState *g)
{
    FieldLink head = queue_head(g);
    FieldObject obj = field_linked_object(head);
    if (field_same_link(head, queue_tail(g))) {
        set_queue_head(g, field_no_link());
        set_queue_tail(g, field_no_link());
    } else {
        set_queue_head(g, queued_after(g, obj));
    }
    set_queued_after(g, obj, field_no_link());
    return obj;
}

/* ---- The turns -------------------------------------------------------------------------------- */

// $C0:59D9
/* Every frame: each object counts down to its script's next turn, and those
 * whose turn has come are queued for it, which starts their countdown again.
 * A disabled object's turn waits until it is enabled. */
static void queue_due_turns(GameState *g)
{
    uint8_t count = event_object_count(g);
    charge_legacy_accesses(g, 4); /* loop bound's former word read/write */
    for (FieldObject obj = 0; obj < count; obj++) {
        if (!object_turn_comes(g, obj))
            continue;
        ObjectRole role = object_role(g, obj);
        if (role.disabled)
            continue;
        bool queued = role.kind == ROLE_LEADER ? queue_first(g, obj) : queue_last(g, obj);
        if (queued)
            object_restart_turn_countdown(g, obj);
    }
}

/* How a frame's turns ended: all of them run, or the frame's time up with
 * some left; or a turn didn't come back to the field's loop (it went
 * elsewhere: a new location, a battle), and the field's loop is left as the
 * turn left it. */
typedef enum { TURNS_ALL_RUN, TURNS_OUT_OF_TIME, TURN_WENT_ELSEWHERE } TurnsEnd;

/* An object's turn: its script run by the event engine. False if the turn
 * didn't come back. */
typedef bool (*TakeTurn)(void *ctx, FieldObject obj);

// $C0:5A46
/* Every frame, after the turns are queued: the queued objects take their
 * turns, the first in the queue first, while the frame has time for them. */
static TurnsEnd run_queued_turns(GameState *g, Platform *pf, TakeTurn take, void *ctx)
{
    while (tm_frame_time_left(pf, PF_AT(0xC05A49), field_script_line_limit(g))) {
        if (!field_link_is_object(queue_head(g)))
            return TURNS_ALL_RUN;
        if (!take(ctx, take_the_first(g)))
            return TURN_WENT_ELSEWHERE;
    }
    return TURNS_OUT_OF_TIME;
}

/* A queued object's turn, timed as the routine that runs one. A turn the
 * port refuses breaks the contract and stops the program; a location change
 * or a battle ends the turn with the field. */
enum { RUN_A_TURN = 0xC05A93 };

/* ---- The turns ------------------------------------------------------------------------------ */

/* Each turn the event engine's (event/engine.h). */
static bool take_turn(void *ctx, FieldObject obj)
{
    EventScriptContext *c = ctx;
    EventRunner r = event_runner(c);
    uint16_t at;
    EventTurnEnd end = TURN_OVER;
    TIMED_AS(c->game, RUN_A_TURN, end = event_run_turn(c->game, obj, &r, &at));
    if (end == TURN_OVER)
        return true;
    if (end != TURN_HANDED_BACK)
        program_breaks_a_contract(c->game, RUN_A_TURN, "a turn the event engine hands to the original");
    return false;
}

void field_queue_the_due_turns(GameState *g) { queue_due_turns(g); }

bool field_queued_turns_run(GameState *g, Platform *pf)
{
    EventScriptContext c = { g, pf };
    return run_queued_turns(g, pf, take_turn, &c) != TURN_WENT_ELSEWHERE;
}

/* ---- What the port runs ------------------------------------------------------------------------ */

/* A queue the port reads as the original does: empty, or each object in it
 * named by its slot doubled, from the head to the tail. */
enum { LONGEST_QUEUE = FIELD_OBJECTS };
