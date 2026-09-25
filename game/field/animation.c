/* Field objects: how an object animates.
 *
 * Everything on the map shows one of four things at any moment
 * (ObjectAnimationMode): it animates with its movement, standing, walking or
 * running by how fast it goes; it loops an animation a script chose (an idle
 * bob, a torch flickering); it plays a one-off animation (a nod, a jump of
 * surprise) a number of times and then goes back to what it was doing; or it
 * holds a single frame a script chose. Scripts start and stop these, the
 * object's movement picks the stand, walk and run animations
 * (object_animate_movement), and the object update advances the animation
 * once a frame (object_animation_tick): when the frame's time is up the
 * object is queued for the frame builder, which draws the new frame's tiles
 * and takes the new frame from this file (object_take_next_frame).
 *
 * An animation is four entries a facing in the object's frame table, frame
 * numbers ending in an end marker, and beside it a table of how long each
 * frame shows, ending in 0. Reaching the end of the times starts the
 * animation over, or for a one-off counts a pass.
 *
 * A one-off is counted in half passes, which is how the scripts give its
 * length too (event/animation.c): the end of its times takes a whole pass
 * and the end of its frame table, which an animation whose frame table is
 * shorter than its times reaches first, half of one.
 *
 * The state is kept in the object tables, and the original encodes part of it
 * in-band: while a one-off plays or a frame is held, "was it looping an
 * animation before?" is the looped animation's number $FF. That encoding
 * lives in this file's storage functions and nowhere else. */
#include "field/field.h"
#include "field/object.h"

#include "field/bridge.h"
#include "field/field_data.h"
#include "field/parts.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The end markers of an animation's two tables. */
enum { END_OF_FRAMES = 0xFF, END_OF_TIMES = 0 };

/* ---- Storage: the only code that knows how the state sits in the tables ------------------ */

ObjectAnimationMode object_animation_mode(const GameState *g, FieldObject obj)
{
    return (ObjectAnimationMode)field_object_animation_mode(g, obj);
}

static void set_mode(GameState *g, FieldObject obj, ObjectAnimationMode mode)
{
    set_field_object_animation_mode(g, obj, (uint8_t)mode);
}

/* The animation it loops, or animates its movement with. */
static uint8_t looped_animation(const GameState *g, FieldObject obj) { return field_object_looped_animation(g, obj); }
static void set_looped_animation(GameState *g, FieldObject obj, uint8_t animation)
{
    set_field_object_looped_animation(g, obj, animation);
}

/* The original's in-band marker, in place of the looped animation: nothing to
 * go back to but the object's movement. */
enum { NOTHING_TO_GO_BACK_TO = 0xFF };

static void remember_nothing_to_go_back_to(GameState *g, FieldObject obj)
{
    set_looped_animation(g, obj, NOTHING_TO_GO_BACK_TO);
}

static bool has_loop_to_go_back_to(const GameState *g, FieldObject obj)
{
    return looped_animation(g, obj) != NOTHING_TO_GO_BACK_TO;
}

static uint8_t one_off_animation(const GameState *g, FieldObject obj)
{
    return field_object_one_off_animation(g, obj);
}
static void set_one_off_animation(GameState *g, FieldObject obj, uint8_t animation)
{
    set_field_object_one_off_animation(g, obj, animation);
}

/* The frame a script holds. */
static void set_held_frame(GameState *g, FieldObject obj, uint8_t frame) { set_field_object_static_frame(g, obj, frame); }

/* A one-off's half passes left (above). The last half pass is the one-off
 * played out, holding its frame; none is no one-off. */
enum { NO_ONE_OFF = 0, PLAYED_OUT = 1, HALF_PASSES_PER_PASS = 2 };

static uint8_t half_passes_left(const GameState *g, FieldObject obj)
{
    return field_object_one_off_half_passes(g, obj);
}
static void set_half_passes_left(GameState *g, FieldObject obj, uint8_t n)
{
    set_field_object_one_off_half_passes(g, obj, n);
}

/* Where the object is in its animation: a frame number, and how long it has
 * left on it. An animation that starts over is put a frame before its first,
 * so that its next frame is the first (the frame number wraps). */
enum { BEFORE_THE_FIRST_FRAME = 0xFF };

void object_restart_animation(GameState *g, FieldObject obj)
{
    set_field_object_animation_frame(g, obj, 0);
    object_end_frame_time(g, obj);
}

void object_end_frame_time(GameState *g, FieldObject obj) { set_field_object_frame_delay(g, obj, 0); }

/* ---- What a script does with it ------------------------------------------------------- */

/* Leave what it was doing for a one-off animation or a held frame. An object
 * animating with its movement has nothing to go back to but its movement; a
 * looping one goes back to its loop. */
static void interrupt_with(GameState *g, FieldObject obj, ObjectAnimationMode mode)
{
    object_restart_animation(g, obj);
    if (object_animation_mode(g, obj) == ANIMATE_WITH_MOVEMENT)
        remember_nothing_to_go_back_to(g, obj);
    set_mode(g, obj, mode);
}

/* Start the object animating in a mode from the first frame, dropping any
 * one-off it was playing. */
static void start_afresh(GameState *g, FieldObject obj, ObjectAnimationMode mode)
{
    set_mode(g, obj, mode);
    set_half_passes_left(g, obj, NO_ONE_OFF);
    object_restart_animation(g, obj);
}

/* Loop an animation from its first frame, dropping whatever the object was
 * doing. */
void object_loop_animation(GameState *g, FieldObject obj, uint8_t animation)
{
    set_looped_animation(g, obj, animation);
    start_afresh(g, obj, ANIMATE_LOOP);
}

/* Stop any animation a script gave the object: it stands, and walks and runs
 * again as it moves. */
void object_stop_animating(GameState *g, FieldObject obj)
{
    set_looped_animation(g, obj, ANIMATION_STAND);
    start_afresh(g, obj, ANIMATE_WITH_MOVEMENT);
}

/* Play a one-off animation (a gesture, a reaction) for `half_passes` half
 * passes; then it holds its last frame until the script that started it
 * finishes it. The count runs down two a pass and one where the frame table
 * ends first, and the one-off is played out on its last half pass. */
void object_play_animation(GameState *g, FieldObject obj, uint8_t animation, uint8_t half_passes)
{
    set_one_off_animation(g, obj, animation);
    interrupt_with(g, obj, ANIMATE_PLAY);
    set_half_passes_left(g, obj, half_passes);
}

bool object_playing_animation(const GameState *g, FieldObject obj, uint8_t animation)
{
    return half_passes_left(g, obj) != NO_ONE_OFF && animation == one_off_animation(g, obj);
}

bool object_animation_played(const GameState *g, FieldObject obj) { return half_passes_left(g, obj) == PLAYED_OUT; }

/* A one-off is over: back to the loop the object had, or to animating with its
 * movement, standing. */
void object_finish_animation(GameState *g, FieldObject obj)
{
    set_half_passes_left(g, obj, NO_ONE_OFF);
    object_restart_animation(g, obj);
    if (has_loop_to_go_back_to(g, obj)) {
        set_mode(g, obj, ANIMATE_LOOP);
    } else {
        set_looped_animation(g, obj, ANIMATION_STAND);
        set_mode(g, obj, ANIMATE_WITH_MOVEMENT);
    }
}

/* Hold one frame a script chose, until the object moves again. */
void object_show_frame(GameState *g, FieldObject obj, uint8_t frame)
{
    set_held_frame(g, obj, frame);
    interrupt_with(g, obj, ANIMATE_SHOW_FRAME);
}

/* An object that leaves view while playing a one-off animation is cut to its
 * last pass, so the script waiting on it isn't held up by what nobody sees. */
void object_hurry_animation(GameState *g, FieldObject obj)
{
    if (half_passes_left(g, obj) != NO_ONE_OFF)
        set_half_passes_left(g, obj, HALF_PASSES_PER_PASS);
}

/* ---- Animating with movement ------------------------------------------------------------ */

/* For the original's code and shadow mode (field/scratch.h): the speed's sum
 * keeps its first part at $D9. */
static void leave_speed_part(GameState *g, uint8_t v) { original_cell8(g, 0xD9, v); }

/* Switch the object to a movement animation, from its first frame, unless it
 * is already showing that one. */
void object_show_movement(GameState *g, FieldObject obj, uint8_t animation)
{
    if (looped_animation(g, obj) == animation)
        return;
    set_looped_animation(g, obj, animation);
    object_restart_animation(g, obj);
}

static uint8_t magnitude(int8_t step) { return step < 0 ? (uint8_t)-step : (uint8_t)step; }

/* How fast it moves, the two axes' steps added (a byte, as the original). */
static uint8_t movement_speed(Velocity v) { return (uint8_t)(magnitude(v.y) + magnitude(v.x)); }

enum { RUNNING_SPEED = 0x20 };

// $C0:5614
/* Every step of a move: a held frame gives way to the loop the object had,
 * or to its movement; a loop or a one-off keeps its animation. Otherwise the
 * object stands, walks, or where it may run and goes fast enough, runs. */
MovementAnimation object_animate_movement(GameState *g, FieldObject obj)
{
    ObjectAnimationMode mode = object_animation_mode(g, obj);
    if (mode == ANIMATE_SHOW_FRAME) {
        if (has_loop_to_go_back_to(g, obj)) {
            set_mode(g, obj, ANIMATE_LOOP);
            return MOVEMENT_KEPT_BY_SCRIPT;
        }
        set_mode(g, obj, ANIMATE_WITH_MOVEMENT);
    } else if (mode != ANIMATE_WITH_MOVEMENT) {
        return MOVEMENT_KEPT_BY_SCRIPT;
    }
    Velocity v = object_velocity(g, obj);
    ORIGINAL_EFFECT(leave_speed_part(g, magnitude(v.x)));
    uint8_t speed = movement_speed(v);
    MovementAnimation choice = speed == 0                                           ? MOVEMENT_STAND
                               : speed >= RUNNING_SPEED && object_may_run(g, obj) ? MOVEMENT_RUN
                                                                                    : MOVEMENT_WALK;
    static const uint8_t animations[] = { [MOVEMENT_STAND] = ANIMATION_STAND, [MOVEMENT_WALK] = ANIMATION_WALK,
                                          [MOVEMENT_RUN] = ANIMATION_RUN };
    object_show_movement(g, obj, animations[choice]);
    return choice;
}

// $C0:568B
/* A move is over: an object animating with its movement stands. */
void object_stand(GameState *g, FieldObject obj)
{
    if (object_animation_mode(g, obj) == ANIMATE_WITH_MOVEMENT)
        object_show_movement(g, obj, ANIMATION_STAND);
}

/* ---- The frame, every frame -------------------------------------------------------------- */

/* For the original's code and shadow mode (field/scratch.h): the tick keeps
 * the animation's first time entry at $C1, and the frame read the frame at
 * $D9, on the way to their sums. */
static void leave_first_time_entry(GameState *g, uint16_t v) { original_cell16(g, 0xC1, v); }
static void leave_frame_sum_part(GameState *g, uint16_t v) { original_cell16(g, 0xD9, v); }

/* Count the frame's time down: true when it is up. */
static bool frame_due(GameState *g, FieldObject obj)
{
    uint8_t delay = field_object_frame_delay(g, obj);
    if (delay == 0)
        return true;
    set_field_object_frame_delay(g, obj, (uint8_t)(delay - 1));
    return delay == 1;
}

/* The frame builder's queue of objects whose frame is due, a list of links
 * from its head to its tail. Its storage: */
static FieldLink queue_head(const GameState *g) { return field_link_stored(field_animation_queue_head(g)); }
static FieldLink queue_tail(const GameState *g) { return field_link_stored(field_animation_queue_tail(g)); }
static void set_queue_head(GameState *g, FieldLink l) { set_field_animation_queue_head(g, field_link_byte(l)); }
static void set_queue_tail(GameState *g, FieldLink l) { set_field_animation_queue_tail(g, field_link_byte(l)); }
static FieldLink queued_after(const GameState *g, FieldObject obj)
{
    return field_link_stored(field_object_animation_link(g, obj));
}
static void set_queued_after(GameState *g, FieldObject obj, FieldLink l)
{
    set_field_object_animation_link(g, obj, field_link_byte(l));
}

static void start_queue_with(GameState *g, FieldObject obj)
{
    set_queue_tail(g, field_link_to(obj));
    set_queue_head(g, field_link_to(obj));
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

/* Put the object at the back of the queue. False if it is queued already. */
static bool queue_last(GameState *g, FieldObject obj)
{
    FieldLink tail = queue_tail(g);
    if (field_same_link(tail, field_link_to(obj)))
        return false;
    if (field_link_is_object(tail) && field_link_is_object(queued_after(g, obj)))
        return false;
    if (!field_link_is_object(tail)) {
        start_queue_with(g, obj);
    } else {
        set_queued_after(g, field_linked_object(tail), field_link_to(obj));
        set_queue_tail(g, field_link_to(obj));
    }
    return true;
}

/* The frame builder asks for the rest of the party first, once, with this. */
enum { PARTY_FIRST_ASKED = 2 };   /* field_animation_queue_front */

/* Queue the object for the frame builder: the leader first, and the rest of
 * the party first when the builder has asked, so the party's frames are
 * never late; a disabled object not at all. */
static bool queue_for_frame(GameState *g, FieldObject obj)
{
    ObjectRole role = object_role(g, obj);
    if (role.disabled)
        return false;
    bool first = role.kind == ROLE_LEADER;
    if ((role.kind == ROLE_PARTY_1 || role.kind == ROLE_PARTY_2) &&
        field_animation_queue_front(g) == PARTY_FIRST_ASKED) {
        set_field_animation_queue_front(g, 0);
        first = true;
    }
    return first ? queue_first(g, obj) : queue_last(g, obj);
}

/* Back a frame, onto the one it has just passed: a played-out one-off holds
 * its last frame. */
static void hold_last_frame(GameState *g, FieldObject obj)
{
    set_field_object_animation_frame(g, obj, (uint8_t)(field_object_animation_frame(g, obj) - 1));
}

/* A one-off reached the end of its times: another pass from its first frame,
 * or on the last, the one-off is played out and holds the frame it is on. (A
 * one-off with no count, which the game never starts, counts round from 255.) */
static void count_a_pass(GameState *g, FieldObject obj)
{
    uint8_t left = half_passes_left(g, obj);
    if (left == PLAYED_OUT || left == HALF_PASSES_PER_PASS) {
        set_half_passes_left(g, obj, PLAYED_OUT);
        hold_last_frame(g, obj);
        return;
    }
    set_half_passes_left(g, obj, (uint8_t)(left - HALF_PASSES_PER_PASS));
    set_field_object_animation_frame(g, obj, 0);
}

/* The animation the object is on: a one-off's while it plays one, its loop's
 * (or its movement's) otherwise. A held frame is the one exception, and the
 * original's: the tick goes on timing the last one-off, while the frame
 * builder reads the loop's frames. */
typedef enum { ITS_TIMES, ITS_FRAMES } AnimationPart;

static uint8_t current_animation(const GameState *g, FieldObject obj, AnimationPart part)
{
    ObjectAnimationMode mode = object_animation_mode(g, obj);
    bool looping = part == ITS_FRAMES ? mode != ANIMATE_PLAY : mode == ANIMATE_WITH_MOVEMENT || mode == ANIMATE_LOOP;
    return looping ? looped_animation(g, obj) : one_off_animation(g, obj);
}

/* Where the animation's times start in the object's table of times, without
 * the 16 bits' wrap (for the guard, below). */
static uint32_t first_time_entry(const GameState *g, FieldObject obj)
{
    return (uint32_t)current_animation(g, obj, ITS_TIMES) * FIELD_FRAMES_A_ANIMATION + field_object_delay_table(g, obj);
}

/* On to the next frame, with its time; at the end of the times, the
 * animation starts over, or a one-off counts a pass. */
static void next_frame(GameState *g, FieldObject obj)
{
    uint8_t frame = (uint8_t)(field_object_animation_frame(g, obj) + 1);
    set_field_object_animation_frame(g, obj, frame);
    uint16_t table = field_object_delay_table(g, obj);
    uint8_t animation = current_animation(g, obj, ITS_TIMES);
    ORIGINAL_EFFECT(leave_first_time_entry(g, field_animation_place(table, animation, 0)));
    uint8_t time = field_animation_frame_time(g, field_animation_place(table, animation, frame));
    if (time != END_OF_TIMES) {
        set_field_object_frame_delay(g, obj, time);
        return;
    }
    set_field_object_frame_delay(g, obj, field_animation_frame_time(g, field_animation_place(table, animation, 0)));
    if (object_animation_mode(g, obj) == ANIMATE_PLAY)
        count_a_pass(g, obj);
    else
        set_field_object_animation_frame(g, obj, 0);
}

// $C0:C98A
void object_animation_tick(GameState *g, FieldObject obj)
{
    if (frame_due(g, obj) && queue_for_frame(g, obj))
        next_frame(g, obj);
}

/* ---- The frame it shows ------------------------------------------------------------------ */

/* The object's frame table for its facing, in the bank the frame builder
 * names: a table a facing, in the facings' order. */
static FieldFrameTable facing_frame_table(const GameState *g, FieldObject obj)
{
    uint16_t size = field_object_facing_table_size(g, obj);
    uint16_t facing_table = (uint16_t)(object_facing(g, obj) * size + field_object_frame_table(g, obj));
    return (FieldFrameTable){ field_frame_entry_bank(g), facing_table };
}

/* What the object shows next, found without changing anything: the frame
 * at its place in its animation; or at the end of the frame table, a one-off
 * with none, and any other animation starting over. `place` is where in the
 * frame table that is, the first frame's place for an animation starting
 * over, whose frame the taker reads once the animation is put back before
 * its start (the original's order: a table in work RAM can hold the frame
 * number it rewinds). */
typedef enum { A_FRAME, END_OF_A_ONE_OFF, STARTING_OVER } FrameFound;
typedef struct {
    FrameFound found;
    uint8_t frame;
    uint16_t place;
} NextFrame;

static NextFrame find_next_frame(const GameState *g, FieldObject obj)
{
    uint8_t frame = field_object_animation_frame(g, obj);
    bool one_off = object_animation_mode(g, obj) == ANIMATE_PLAY;
    uint8_t animation = current_animation(g, obj, ITS_FRAMES);
    /* Looked up last, just before its frame is read, as the original does: a
     * table pointing where nothing answers reads the last byte on the bus,
     * the table's bank (fuzzing found it; no object the game makes does). */
    FieldFrameTable table = facing_frame_table(g, obj);
    uint16_t place = field_animation_place(table.offset, animation, frame);
    uint8_t shown = field_animation_frame_number(g, table, place);
    if (shown != END_OF_FRAMES)
        return (NextFrame){ A_FRAME, shown, place };
    if (one_off)
        return (NextFrame){ END_OF_A_ONE_OFF, 0, place };
    return (NextFrame){ STARTING_OVER, 0, (uint16_t)(place - frame) };
}

// $C0:CB3A
/* The frame builder takes the object's next frame to draw, and goes on
 * reading the frame table from where it is (field_frame_entry_address, which
 * the builder keeps). At the end of a one-off's frames it counts half a pass
 * and there is no frame to draw; any other animation starts over. */
ObjectFrame object_take_next_frame(GameState *g, FieldObject obj)
{
    ORIGINAL_EFFECT(leave_frame_sum_part(g, field_object_animation_frame(g, obj)));
    NextFrame next = find_next_frame(g, obj);
    set_field_frame_entry_address(g, next.place);
    switch (next.found) {
    case END_OF_A_ONE_OFF: {
        uint8_t left = (uint8_t)(half_passes_left(g, obj) - 1);
        if (left != NO_ONE_OFF)
            set_half_passes_left(g, obj, left);
        return (ObjectFrame){ false, 0 };
    }
    case STARTING_OVER:
        set_field_object_animation_frame(g, obj, BEFORE_THE_FIRST_FRAME);
        return (ObjectFrame){ true, field_animation_frame_number(g, facing_frame_table(g, obj), next.place) };
    default:
        return (ObjectFrame){ true, next.frame };
    }
}

/* For the derivations (field/parts.h, field/leaves.c): the start of the
 * object's frame table for its facing, where its current animation's first
 * frame is in it, and whether it was put a frame before its first. */
uint16_t field_facing_frame_table(const GameState *g, FieldObject obj) { return facing_frame_table(g, obj).offset; }

uint16_t field_first_frame_place(const GameState *g, FieldObject obj)
{
    return field_animation_place(facing_frame_table(g, obj).offset, current_animation(g, obj, ITS_FRAMES), 0);
}

bool field_frame_before_the_first(uint8_t frame) { return frame == BEFORE_THE_FIRST_FRAME; }

bool field_animation_rewound(const GameState *g, FieldObject obj)
{
    return field_frame_before_the_first(field_object_animation_frame(g, obj));
}

/* ---- Entries --------------------------------------------------------------------------- */

/* On the field's page, the current object's (16-bit X). */
void field_update_walk_animation(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject obj = field_current_object_slot(s);
    if (object_animate_movement(g, obj) != MOVEMENT_KEPT_BY_SCRIPT)
        m_set_flag(s, CPU_FLAG_V, field_walk_overflow(g, obj));
    m_rts(s);
}

void field_stand_animation(Snes *s)
{
    object_stand(state_of(s), field_current_object_slot(s));
    m_rts(s);
}

/* The guards of the tick and the frame read: the object is one, the frame
 * builder's queue holds objects, and the object's animation tables add up
 * without carrying past 16 bits, where the original carries each sum into
 * the next and the port doesn't. A frame table three facing tables on, or a
 * table of times at the top of bank $E4, would; no object the game makes has
 * one. */
enum { BANK_SIZE = 0x10000 };

static bool none_or_an_object(FieldLink link) { return !field_link_is_object(link) || field_names_object(field_link_byte(link)); }

bool field_tick_is_exact(const GameState *g, unsigned obj)
{
    return first_time_entry(g, (FieldObject)obj) < BANK_SIZE && none_or_an_object(queue_head(g)) &&
           none_or_an_object(queue_tail(g));
}

bool field_object_delays_in_bank(Snes *s)
{
    if (!field_current_object_low_bank(s))
        return false;
    vars_peek(true);
    bool ok = field_tick_is_exact(state_of(s), field_current_object_slot(s));
    vars_peek(false);
    return ok;
}

bool field_object_frames_in_bank(Snes *s)
{
    if (!field_object_x_low_bank(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_object_in_x(s);
    bool ok = object_facing_is_one(g, obj) &&
              (object_facing(g, obj) != FACING_RIGHT || 3u * field_object_facing_table_size(g, obj) < BANK_SIZE);
    vars_peek(false);
    return ok;
}

/* The current object's (8-bit X). */
void field_tick_object_animation(Snes *s)
{
    object_animation_tick(state_of(s), field_current_object_slot(s));
    m_rts(s);
}

/* The object in X (8-bit). The carry says whether there is a frame; A holds
 * it (or, for a one-off at its end, the half passes it counted down to) with
 * the high byte of the entry's address; and the overflow is the frame
 * read's (field/leaves.c). */
void field_read_animation_frame(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_object_in_x(s);
    uint8_t frame_before = field_object_animation_frame(g, obj);
    uint8_t half_passes_before = half_passes_left(g, obj);
    ObjectFrame r = object_take_next_frame(g, obj);
    uint16_t entry = field_frame_entry_address(g);
    uint8_t shown = r.has_frame ? r.frame : (uint8_t)(half_passes_before - 1);
    s->cpu.a = (uint16_t)((entry & 0xFF00) | shown);
    m_set_flag(s, CPU_FLAG_C, r.has_frame);
    vars_peek(true);
    m_set_flag(s, CPU_FLAG_V, field_frame_read_overflow(g, obj, r.has_frame, frame_before));
    vars_peek(false);
    m_rts(s);
}
