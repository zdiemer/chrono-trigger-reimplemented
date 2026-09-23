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
 * and asks this file which frame that is (object_current_frame).
 *
 * An animation is four entries a facing in the object's frame table, frame
 * numbers ending in an end marker, and beside it a table of how long each
 * frame shows, ending in 0. Reaching the end of the times starts the
 * animation over, or for a one-off counts a pass.
 *
 * The state is kept in the object tables, and the original encodes part of it
 * in-band: while a one-off plays or a frame is held, "was it looping an
 * animation before?" is the looped animation's number $FF, and a one-off's
 * count of passes is kept in half passes. Those encodings live in this file's
 * storage functions and nowhere else. */
#include "field/field.h"
#include "field/object.h"

#include "field/bridge.h"
#include "field/field_data.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

enum { FRAMES_PER_ANIMATION = 4 };

/* The end markers of an animation's two tables. */
enum { END_OF_FRAMES = 0xFF, END_OF_TIMES = 0 };

/* ---- Storage: the only code that knows how the state sits in the tables ------------------ */

ObjectAnimationMode object_animation_mode(const GameState *g, FieldObject obj)
{
    return (ObjectAnimationMode)field_object_animation_mode_mode(g, obj);
}

static void set_mode(GameState *g, FieldObject obj, ObjectAnimationMode mode)
{
    set_field_object_animation_mode_mode(g, obj, (uint8_t)mode);
}

/* The animation it loops, or animates its movement with. */
static uint8_t looped_animation(const GameState *g, FieldObject obj) { return field_object_animation_number(g, obj); }
static void set_looped_animation(GameState *g, FieldObject obj, uint8_t animation)
{
    set_field_object_animation_number(g, obj, animation);
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
    return field_object_animation_mode_timed_animation(g, obj);
}

/* A one-off's passes left, which the original counts in half passes: the end
 * of the times takes a whole pass, and the end of the frame table, which an
 * animation whose frame table is shorter than its times reaches first, half
 * of one. The last half pass is the one-off played out, holding its frame. */
enum { NO_ONE_OFF = 0, PLAYED_OUT = 1, HALF_PASSES_PER_PASS = 2 };

static uint8_t half_passes_left(const GameState *g, FieldObject obj)
{
    return field_object_move_state_animation_count(g, obj);
}
static void set_half_passes_left(GameState *g, FieldObject obj, uint8_t n)
{
    set_field_object_move_state_animation_count(g, obj, n);
}

/* Where the object is in its animation: a frame number, and how long it has
 * left on it. An animation that starts over is put a frame before its first,
 * so that its next frame is the first (the frame number wraps). */
enum { BEFORE_THE_FIRST_FRAME = 0xFF };

void object_restart_animation(GameState *g, FieldObject obj)
{
    set_field_object_animation_frame(g, obj, 0);
    set_field_object_frame_delay(g, obj, 0);
}

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
    set_field_object_frame_delay(g, obj, 0);
    set_half_passes_left(g, obj, NO_ONE_OFF);
    set_field_object_animation_frame(g, obj, 0);
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

/* Play a one-off animation (a gesture, a reaction) `passes` times through;
 * then it holds its last frame until the script that started it finishes it. */
void object_play_animation(GameState *g, FieldObject obj, uint8_t animation, uint8_t passes)
{
    set_field_object_animation_mode_timed_animation(g, obj, animation);
    interrupt_with(g, obj, ANIMATE_PLAY);
    set_half_passes_left(g, obj, (uint8_t)(passes * HALF_PASSES_PER_PASS));
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
    set_field_object_static_frame(g, obj, frame);
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
static void animate_movement_with(GameState *g, FieldObject obj, uint8_t animation)
{
    if (looped_animation(g, obj) == animation)
        return;
    set_looped_animation(g, obj, animation);
    object_restart_animation(g, obj);
}

static uint8_t magnitude(uint8_t step) { return (int8_t)step < 0 ? (uint8_t)-step : step; }

/* How fast it moves, the two axes' steps added (a byte, as the original). */
static uint8_t movement_speed(const GameState *g, FieldObject obj)
{
    return (uint8_t)(magnitude((uint8_t)field_object_velocity_y(g, obj)) +
                     magnitude((uint8_t)field_object_velocity_x(g, obj)));
}

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
    leave_speed_part(g, magnitude((uint8_t)field_object_velocity_x(g, obj)));
    uint8_t speed = movement_speed(g, obj);
    MovementAnimation choice = speed == 0                                           ? MOVEMENT_STAND
                               : speed >= RUNNING_SPEED && object_may_run(g, obj) ? MOVEMENT_RUN
                                                                                    : MOVEMENT_WALK;
    static const uint8_t animations[] = { 0, ANIMATION_STAND, ANIMATION_WALK, ANIMATION_RUN };
    animate_movement_with(g, obj, animations[choice]);
    return choice;
}

// $C0:568B
/* A move is over: an object animating with its movement stands. */
void object_stand(GameState *g, FieldObject obj)
{
    if (object_animation_mode(g, obj) == ANIMATE_WITH_MOVEMENT)
        animate_movement_with(g, obj, ANIMATION_STAND);
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
 * from its head to its tail. */
static void start_queue_with(GameState *g, FieldObject obj)
{
    set_field_animation_queue_tail(g, field_object_link(obj));
    set_field_animation_queue_head(g, field_object_link(obj));
}

/* Put the object at the front of the queue. False if it is there already. */
static bool queue_first(GameState *g, FieldObject obj)
{
    FieldLink head = field_animation_queue_head(g);
    if (head == field_object_link(obj))
        return false;
    if (!field_link_is_object(head)) {
        start_queue_with(g, obj);
    } else {
        set_field_object_animation_link(g, obj, head);
        set_field_animation_queue_head(g, field_object_link(obj));
    }
    return true;
}

/* Put the object at the back of the queue. False if it is queued already. */
static bool queue_last(GameState *g, FieldObject obj)
{
    FieldLink tail = field_animation_queue_tail(g);
    if (tail == field_object_link(obj))
        return false;
    if (field_link_is_object(tail) && field_link_is_object(field_object_animation_link(g, obj)))
        return false;
    if (!field_link_is_object(tail)) {
        start_queue_with(g, obj);
    } else {
        set_field_object_animation_link(g, field_object_of_link(tail), field_object_link(obj));
        set_field_animation_queue_tail(g, field_object_link(obj));
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

/* The animation the tick times: a loop's, or the one-off's (a held frame
 * keeps timing the last one-off). */
static uint8_t timed_animation(const GameState *g, FieldObject obj)
{
    ObjectAnimationMode mode = object_animation_mode(g, obj);
    return mode == ANIMATE_WITH_MOVEMENT || mode == ANIMATE_LOOP ? looped_animation(g, obj) : one_off_animation(g, obj);
}

/* Where the animation's times start in the object's table of times. */
static uint32_t first_time_entry(const GameState *g, FieldObject obj)
{
    return (uint32_t)timed_animation(g, obj) * FRAMES_PER_ANIMATION + field_object_delay_table(g, obj);
}

/* On to the next frame, with its time; at the end of the times, the
 * animation starts over, or a one-off counts a pass. */
static void next_frame(GameState *g, FieldObject obj)
{
    uint8_t frame = (uint8_t)(field_object_animation_frame(g, obj) + 1);
    set_field_object_animation_frame(g, obj, frame);
    uint16_t first = (uint16_t)first_time_entry(g, obj);
    leave_first_time_entry(g, first);
    uint8_t time = field_animation_delay(g, (uint16_t)(first + frame));
    if (time != END_OF_TIMES) {
        set_field_object_frame_delay(g, obj, time);
        return;
    }
    set_field_object_frame_delay(g, obj, field_animation_delay(g, first));
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

/* How many facing tables on from the object's frame table its facing's are:
 * facing 0 none, 2 two, 3 and on three, the rest (1, and the facings past
 * $81, which the original's signed compare takes as below 2) one. */
static unsigned tables_on(uint8_t facing)
{
    if (facing == 0)
        return 0;
    if (facing == 2)
        return 2;
    return (int8_t)(facing - 2) < 0 ? 1 : 3;
}

static uint16_t facing_frame_table(const GameState *g, FieldObject obj)
{
    unsigned on = tables_on(field_object_facing(g, obj));
    uint16_t table = field_object_frame_table(g, obj);
    return on == 0 ? table : (uint16_t)(on * field_object_facing_table_size(g, obj) + table);
}

/* The frame table's entry at an address in the bank the frame builder set. */
static uint8_t frame_entry(const GameState *g, uint16_t address)
{
    return field_animation_frame_entry(g, (uint32_t)field_frame_entry_bank(g) << 16 | address);
}

/* For the entry's wrapper (field/scratch.h, "What a wrapper can't work out"):
 * whether reading the frame started the animation over, which decides the
 * overflow the original's callers see. Written only by record_started_over,
 * read only by field_read_animation_frame. */
static struct {
    bool started_over;
} frame_read_leaves;

static void record_started_over(void) { frame_read_leaves.started_over = true; }

// $C0:CB3A
/* The frame at the object's place in its animation. Where the frame builder
 * reads it from is kept in field_frame_entry_address, where the builder goes
 * on from. At the end of the frame table, a one-off counts half a pass and
 * there is no frame; any other animation starts over. */
ObjectFrame object_current_frame(GameState *g, FieldObject obj)
{
    uint16_t table = facing_frame_table(g, obj);
    uint8_t frame = field_object_animation_frame(g, obj);
    leave_frame_sum_part(g, frame);
    bool one_off = object_animation_mode(g, obj) == ANIMATE_PLAY;
    uint8_t animation = one_off ? one_off_animation(g, obj) : looped_animation(g, obj);
    uint16_t entry = (uint16_t)(animation * FRAMES_PER_ANIMATION + frame + table);
    uint8_t shown = frame_entry(g, entry);
    if (shown != END_OF_FRAMES) {
        set_field_frame_entry_address(g, entry);
        return (ObjectFrame){ true, shown };
    }
    if (one_off) {
        set_field_frame_entry_address(g, entry);
        uint8_t left = (uint8_t)(half_passes_left(g, obj) - 1);
        if (left != NO_ONE_OFF)
            set_half_passes_left(g, obj, left);
        return (ObjectFrame){ false, 0 };
    }
    uint16_t first = (uint16_t)(entry - frame);
    set_field_frame_entry_address(g, first);
    set_field_object_animation_frame(g, obj, BEFORE_THE_FIRST_FRAME);
    record_started_over();
    return (ObjectFrame){ true, frame_entry(g, first) };
}

/* ---- Entries --------------------------------------------------------------------------- */

/* On the field's page, the current object's (16-bit X). The overflow of the
 * speed's sum is read after, where the speed was looked at. */
void field_update_walk_animation(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject obj = field_current_object_slot(s);
    if (object_animate_movement(g, obj) != MOVEMENT_KEPT_BY_SCRIPT)
        m_set_flag(s, CPU_FLAG_V, field_add_overflows8(magnitude((uint8_t)field_object_velocity_y(g, obj)),
                                                       magnitude((uint8_t)field_object_velocity_x(g, obj))));
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

static bool link_or_none(FieldLink link) { return !field_link_is_object(link) || !(link & 1); }

bool field_tick_is_exact(const GameState *g, unsigned obj)
{
    return first_time_entry(g, (FieldObject)obj) < BANK_SIZE && link_or_none(field_animation_queue_head(g)) &&
           link_or_none(field_animation_queue_tail(g));
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
    bool ok = tables_on(field_object_facing(g, obj)) != 3 || 3u * field_object_facing_table_size(g, obj) < BANK_SIZE;
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
 * the high byte of the entry's address; and the overflow is that of the
 * entry's last sum: the table and the place in it, or the frame taken back
 * off where the animation started over. */
void field_read_animation_frame(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_object_in_x(s);
    uint8_t frame_before = field_object_animation_frame(g, obj);
    uint8_t half_passes_before = half_passes_left(g, obj);
    frame_read_leaves.started_over = false;
    ObjectFrame r = object_current_frame(g, obj);
    uint16_t entry = field_frame_entry_address(g);
    uint8_t shown = r.has_frame ? r.frame : (uint8_t)(half_passes_before - 1);
    s->cpu.a = (uint16_t)((entry & 0xFF00) | shown);
    m_set_flag(s, CPU_FLAG_C, r.has_frame);
    bool overflow;
    if (frame_read_leaves.started_over) {
        overflow = field_subtract_overflows16((uint16_t)(entry + frame_before), frame_before);
    } else {
        uint16_t table = facing_frame_table(g, obj);
        overflow = field_add_overflows16((uint16_t)(entry - table), table);
    }
    m_set_flag(s, CPU_FLAG_V, overflow);
    m_rts(s);
}
