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

#include "original_effects.h"
#include "charge.h"
#include "core/program.h"
#include "event/engine.h"
#include "field/bridge.h"
#include "port/machine.h"
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

/* For the original's code and shadow mode: the count of objects doubled,
 * where the objects' loops keep their end, as the object update does
 * (field/object_update.c). */
static void leave_loop_end(GameState *g, uint8_t count)
{
    set_event_object_end(g, (uint16_t)((event_object_end(g) & 0xFF00) | (uint8_t)(count * 2)));
}

// $C0:59D9
/* Every frame: each object counts down to its script's next turn, and those
 * whose turn has come are queued for it, which starts their countdown again.
 * A disabled object's turn waits until it is enabled. */
static void queue_due_turns(GameState *g)
{
    uint8_t count = event_object_count(g);
    ORIGINAL_EFFECT(leave_loop_end(g, count));
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

/* ---- The entries ------------------------------------------------------------------------------ */

void field_queue_due_turns(Snes *s)
{
    queue_due_turns(state_of(s));
    m_rts(s);
}

/* Each turn is the event engine's entry ($C0:5A93), called on the machine
 * from the original's call of it: one call for the last object in the
 * queue, another for the rest. The beam would read right through a C call
 * too (platform/timing.h waits for the original's latch in shadow mode); the
 * call is for shadow mode's comparison, which then compares at each turn and
 * waits only a turn for the original's latch, where a C call would stretch
 * one comparison across the frame's turns and leave more calls interrupted.
 * A build with no machine calls the engine for each. The engine reads the
 * object from X and nothing else of the registers. */
enum { RUN_A_TURN = 0xC05A93, A_TURN_AMONG_OTHERS = 0xC05A7C, THE_LAST_TURN = 0xC05A8B };
enum { THE_ENGINE_READS_X = DC_A | DC_Y | DC_N | DC_Z | DC_C };

static bool take_turn_on_the_machine(void *ctx, FieldObject obj)
{
    Snes *s = ctx;
    bool last = !field_link_is_object(queue_head(state_of(s)));
    s->cpu.x = field_object_offset(obj);
    return bridge_call(s, last ? THE_LAST_TURN : A_TURN_AMONG_OTHERS, RUN_A_TURN, THE_ENGINE_READS_X,
                       on_the_field_page(s, true));
}

void field_run_queued_turns(Snes *s)
{
    if (run_queued_turns(state_of(s), m_platform(s), take_turn_on_the_machine, s) == TURN_WENT_ELSEWHERE)
        return;
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

/* ---- As C, with no machine ------------------------------------------------------------------ */

/* Each turn the event engine's, its commands as C (event/engine.h). */
static bool take_turn(void *ctx, FieldObject obj)
{
    EventAsC *c = ctx;
    EventRunner r = event_runner(c);
    uint16_t at;
    EventTurnEnd end = TURN_OVER;
    TIMED_AS(c->game, RUN_A_TURN, end = event_run_turn(c->game, obj, &r, &at));
    if (end == TURN_OVER)
        return true;
    if (end != TURN_HANDED_BACK)
        program_needs_the_machine(c->game, RUN_A_TURN, "a turn the event engine hands to the original");
    return false;
}

void field_queue_the_due_turns(GameState *g) { queue_due_turns(g); }

bool field_queued_turns_run(GameState *g, Platform *pf)
{
    EventAsC c = { g, pf };
    return run_queued_turns(g, pf, take_turn, &c) != TURN_WENT_ELSEWHERE;
}

/* ---- The guards ------------------------------------------------------------------------------- */

/* A queue the port reads as the original does: empty, or each object in it
 * named by its slot doubled, from the head to the tail. */
enum { LONGEST_QUEUE = FIELD_OBJECTS };

static bool queue_in_the_tables(const GameState *g)
{
    FieldLink at = queue_head(g);
    if (!field_link_is_object(at))
        return true;
    for (unsigned n = 0; n < LONGEST_QUEUE; n++) {
        if (!field_link_names_object(at))
            return false;
        if (field_same_link(at, queue_tail(g)))
            return true;
        at = queued_after(g, field_linked_object(at));
    }
    return false;
}

/* The countdown: its cells reached through a data bank that reaches low RAM;
 * a count of objects the tables hold, at least one (the original's loop runs
 * until its index meets the count doubled, so no objects would take it round
 * 128 slots); and the queue's two ends in the tables, where objects join it. */
static bool ends_in_the_tables(const GameState *g)
{
    FieldLink head = queue_head(g), tail = queue_tail(g);
    return (!field_link_is_object(head) || field_link_names_object(head)) &&
           (!field_link_is_object(tail) || field_link_names_object(tail));
}

bool field_turns_can_be_counted(Snes *s)
{
    if (!field_bank_low_ram(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t count = event_object_count(g);
    bool ok = count != 0 && count <= FIELD_OBJECTS && ends_in_the_tables(g);
    vars_peek(false);
    return ok;
}

/* The turns: on the field's page, its cells through a data bank that reaches
 * low RAM, the queue in the tables. */
bool field_turns_can_run(Snes *s)
{
    if (!field_on_page_low_bank(s))
        return false;
    vars_peek(true);
    bool ok = queue_in_the_tables(state_of(s));
    vars_peek(false);
    return ok;
}
