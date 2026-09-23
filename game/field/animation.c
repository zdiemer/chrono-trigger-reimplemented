/* Field objects: how they animate.
 *
 * An object animates in one of four ways (ObjectAnimationMode). Most of the
 * time it animates with its movement: the field has it stand, walk or run by
 * how fast it moves. A script can instead have it loop an animation, play a
 * one-off animation (a gesture, a reaction) a number of times and then go
 * back to what it was doing, or hold a single frame; stopping it puts it back
 * to animating with its movement.
 *
 * An animation is four entries a facing in the object's frame table, frame
 * numbers ending in END_OF_FRAMES, and beside it a table of how long each
 * frame shows, ending in 0. Every frame the object update ticks each object
 * in view: when its frame's time is up, the object is queued for the frame
 * builder (which draws the new frame's tiles) and moves to its next frame,
 * wrapping to the first at the end. A one-off animation counts its plays down
 * at each end it reaches, in both tables, and holds its last frame when they
 * are used up; the script that started it is waiting for that. */
#include "field/field.h"
#include "field/object.h"

#include "field/bridge.h"
#include "field/field_data.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

enum { FRAMES_PER_ANIMATION = 4 };
enum { END_OF_FRAMES = 0xFF, END_OF_DELAYS = 0 };

/* After a one-off animation or a held frame, the object goes back to looping
 * its animation, or where it had none, to animating with its movement: the
 * game keeps that as this in place of the loop's animation. */
enum { RESUME_MOVEMENT = 0xFF };

/* The countdown of a one-off animation, in the units the scripts give it: the
 * end of the frame table takes one off, the end of the delays two, and at
 * PLAYS_DONE it has played out and holds its last frame; PLAYS_LAST plays out
 * at the next end of its delays. PLAYS_NONE is none playing. */
enum { PLAYS_NONE = 0, PLAYS_DONE = 1, PLAYS_LAST = 2, PLAYS_PER_DELAY_END = 2 };

/* The objects the field's queue puts first when their frame is due: the
 * party's leader always, the other two when the frame builder asks. */
enum { QUEUE_PARTY_FIRST = 2 };   /* field_animation_queue_front */

/* ---- The object's animation state ------------------------------------------------------- */

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

/* Whether it goes back to its movement after a one-off or a held frame,
 * rather than to a loop. */
static bool resumes_movement(const GameState *g, FieldObject obj) { return looped_animation(g, obj) == RESUME_MOVEMENT; }
static uint8_t one_off_animation(const GameState *g, FieldObject obj)
{
    return field_object_animation_mode_timed_animation(g, obj);
}

static uint8_t plays_left(const GameState *g, FieldObject obj) { return field_object_move_state_animation_count(g, obj); }
static void set_plays_left(GameState *g, FieldObject obj, uint8_t n) { set_field_object_move_state_animation_count(g, obj, n); }

/* The animation the tick times: a loop's, or the one-off's (a held frame
 * keeps timing the last one-off). */
static uint8_t showing_animation(const GameState *g, FieldObject obj)
{
    ObjectAnimationMode mode = object_animation_mode(g, obj);
    return mode == ANIMATE_WITH_MOVEMENT || mode == ANIMATE_LOOP ? looped_animation(g, obj) : one_off_animation(g, obj);
}

/* From the animation's first frame, with a full frame's time. */
static void restart(GameState *g, FieldObject obj)
{
    set_field_object_frame_delay(g, obj, 0);
    set_plays_left(g, obj, PLAYS_NONE);
    set_field_object_animation_frame(g, obj, 0);
}

static void start(GameState *g, FieldObject obj, ObjectAnimationMode mode)
{
    set_mode(g, obj, mode);
    restart(g, obj);
}

/* Leave what it was doing for a one-off animation or a held frame, and
 * remember whether it was looping one to go back to. */
static void interrupt_with(GameState *g, FieldObject obj, ObjectAnimationMode mode)
{
    set_field_object_animation_frame(g, obj, 0);
    set_field_object_frame_delay(g, obj, 0);
    if (object_animation_mode(g, obj) == ANIMATE_WITH_MOVEMENT)
        set_field_object_animation_number(g, obj, RESUME_MOVEMENT);
    set_mode(g, obj, mode);
}

/* ---- What a script does with it ------------------------------------------------------- */

void object_loop_animation(GameState *g, FieldObject obj, uint8_t animation)
{
    set_field_object_animation_number(g, obj, animation);
    start(g, obj, ANIMATE_LOOP);
}

void object_stop_animating(GameState *g, FieldObject obj)
{
    set_field_object_animation_number(g, obj, ANIMATION_STAND);
    start(g, obj, ANIMATE_WITH_MOVEMENT);
}

void object_play_animation(GameState *g, FieldObject obj, uint8_t animation, uint8_t plays)
{
    set_field_object_animation_mode_timed_animation(g, obj, animation);
    interrupt_with(g, obj, ANIMATE_PLAY);
    set_plays_left(g, obj, plays);
}

bool object_playing_animation(const GameState *g, FieldObject obj, uint8_t animation)
{
    return plays_left(g, obj) != PLAYS_NONE && animation == one_off_animation(g, obj);
}

bool object_animation_played(const GameState *g, FieldObject obj) { return plays_left(g, obj) == PLAYS_DONE; }

/* Back to the loop it had, or to animating with its movement. */
void object_finish_animation(GameState *g, FieldObject obj)
{
    set_plays_left(g, obj, PLAYS_NONE);
    set_field_object_frame_delay(g, obj, 0);
    set_field_object_animation_frame(g, obj, 0);
    ObjectAnimationMode back = ANIMATE_LOOP;
    if (resumes_movement(g, obj)) {
        set_field_object_animation_number(g, obj, ANIMATION_STAND);
        back = ANIMATE_WITH_MOVEMENT;
    }
    set_mode(g, obj, back);
}

void object_show_frame(GameState *g, FieldObject obj, uint8_t frame)
{
    set_field_object_static_frame(g, obj, frame);
    interrupt_with(g, obj, ANIMATE_SHOW_FRAME);
}

/* An object that leaves view while playing a one-off animation is cut to its
 * last play. */
void object_hurry_animation(GameState *g, FieldObject obj)
{
    if (plays_left(g, obj) != PLAYS_NONE)
        set_plays_left(g, obj, PLAYS_LAST);
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
    set_field_object_animation_number(g, obj, animation);
    set_field_object_animation_frame(g, obj, 0);
    set_field_object_frame_delay(g, obj, 0);
}

static uint8_t magnitude(uint8_t v) { return (v & 0x80) ? (uint8_t)-v : v; }

/* How fast it moves, the two axes' steps added (a byte, as the original). */
static uint8_t movement_speed(const GameState *g, FieldObject obj)
{
    return (uint8_t)(magnitude((uint8_t)field_object_velocity_y(g, obj)) +
                     magnitude((uint8_t)field_object_velocity_x(g, obj)));
}

enum { RUNNING_SPEED = 0x20 };

// $C0:5614
/* A held frame gives way to the loop the object had, or to its movement; a
 * loop or a one-off animation keeps the animation. Otherwise it stands,
 * walks or runs by its speed (objects past the party's states only walk). */
MovementAnimation object_animate_movement(GameState *g, FieldObject obj)
{
    ObjectAnimationMode mode = object_animation_mode(g, obj);
    if (mode == ANIMATE_SHOW_FRAME) {
        if (!resumes_movement(g, obj)) {
            set_mode(g, obj, ANIMATE_LOOP);
            return MOVEMENT_KEPT_BY_SCRIPT;
        }
        set_mode(g, obj, ANIMATE_WITH_MOVEMENT);
    } else if (mode != ANIMATE_WITH_MOVEMENT) {
        return MOVEMENT_KEPT_BY_SCRIPT;
    }
    leave_speed_part(g, magnitude((uint8_t)field_object_velocity_x(g, obj)));
    uint8_t speed = movement_speed(g, obj);
    MovementAnimation choice = speed == 0 ? MOVEMENT_STAND
                               : speed >= RUNNING_SPEED && object_role(g, obj) < ROLE_WALKS_ONLY
                                   ? MOVEMENT_RUN
                                   : MOVEMENT_WALK;
    static const uint8_t animations[] = { 0, ANIMATION_STAND, ANIMATION_WALK, ANIMATION_RUN };
    animate_movement_with(g, obj, animations[choice]);
    return choice;
}

// $C0:568B
void object_stand(GameState *g, FieldObject obj)
{
    if (object_animation_mode(g, obj) == ANIMATE_WITH_MOVEMENT)
        animate_movement_with(g, obj, ANIMATION_STAND);
}

/* ---- The frame, every frame -------------------------------------------------------------- */

/* For the original's code and shadow mode (field/scratch.h): the tick keeps
 * the animation's first delay entry at $C1, and the frame read the facing
 * table's size, then the frame, at $D9, on the way to their sums. */
static void leave_first_delay_entry(GameState *g, uint16_t v) { original_cell16(g, 0xC1, v); }
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

/* The frame builder's queue, of links. */
static bool queued_last(const GameState *g, FieldObject obj)
{
    return field_animation_queue_tail(g) == field_object_link(obj);
}

static void start_queue_with(GameState *g, FieldObject obj)
{
    set_field_animation_queue_tail(g, field_object_link(obj));
    set_field_animation_queue_head(g, field_object_link(obj));
}

/* Put the object at the front of the queue. False if it is there already. */
static bool queue_first(GameState *g, FieldObject obj)
{
    uint8_t head = field_animation_queue_head(g);
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
    if (queued_last(g, obj))
        return false;
    uint8_t tail = field_animation_queue_tail(g);
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

/* Queue the object for the frame builder: the leader first, and the rest of
 * the party first when the builder has asked; a disabled object not at all. */
static bool queue_for_frame(GameState *g, FieldObject obj)
{
    if (object_disabled(g, obj))
        return false;
    uint8_t role = object_role(g, obj);
    bool first = role == ROLE_LEADER;
    if ((role == ROLE_PARTY_1 || role == ROLE_PARTY_2) && field_animation_queue_front(g) == QUEUE_PARTY_FIRST) {
        set_field_animation_queue_front(g, 0);
        first = true;
    }
    return first ? queue_first(g, obj) : queue_last(g, obj);
}

/* A one-off animation reached the end of its delays: another play from its
 * first frame, or on the last, hold the frame it is on. */
static void count_a_play(GameState *g, FieldObject obj)
{
    uint8_t plays = plays_left(g, obj);
    if (plays != PLAYS_DONE && plays != PLAYS_LAST) {
        set_plays_left(g, obj, (uint8_t)(plays - PLAYS_PER_DELAY_END));
        set_field_object_animation_frame(g, obj, 0);
        return;
    }
    if (plays == PLAYS_LAST)
        set_plays_left(g, obj, PLAYS_DONE);
    set_field_object_animation_frame(g, obj, (uint8_t)(field_object_animation_frame(g, obj) - 1));
}

/* Where the animation's delays start in the object's delay table. */
static uint32_t first_delay_entry(const GameState *g, FieldObject obj)
{
    return (uint32_t)showing_animation(g, obj) * FRAMES_PER_ANIMATION + field_object_delay_table(g, obj);
}

/* On to the next frame, with its time; at the end of the animation, back to
 * the first. */
static void next_frame(GameState *g, FieldObject obj)
{
    uint8_t frame = (uint8_t)(field_object_animation_frame(g, obj) + 1);
    set_field_object_animation_frame(g, obj, frame);
    uint16_t first = (uint16_t)first_delay_entry(g, obj);
    leave_first_delay_entry(g, first);
    uint8_t delay = field_animation_delay(g, (uint16_t)(first + frame));
    if (delay != END_OF_DELAYS) {
        set_field_object_frame_delay(g, obj, delay);
        return;
    }
    set_field_object_frame_delay(g, obj, field_animation_delay(g, first));
    if (object_animation_mode(g, obj) == ANIMATE_PLAY)
        count_a_play(g, obj);
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
 * $81, which read as below 2) one. */
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

/* What reading the frame leaves for the entry's wrapper: the plays left where
 * a one-off counted one (the original returns them where the frame would
 * be), and whether the animation started over. */
typedef struct {
    uint8_t plays_counted;
    bool started_over;
} FrameTrace;

/* The frame at the object's place in its animation. The entry's address is
 * kept in field_frame_entry_address, where the frame builder goes on from. At
 * the animation's end, a one-off counts a play and there is no frame; any
 * other animation starts over, its frame set one before the first so that the
 * tick's next frame is the first. */
static ObjectFrame current_frame(GameState *g, FieldObject obj, FrameTrace *t)
{
    *t = (FrameTrace){ 0, false };
    if (tables_on(field_object_facing(g, obj)) == 3)
        leave_frame_sum_part(g, field_object_facing_table_size(g, obj));
    uint16_t table = facing_frame_table(g, obj);
    uint8_t frame = field_object_animation_frame(g, obj);
    leave_frame_sum_part(g, frame);
    bool one_off = object_animation_mode(g, obj) == ANIMATE_PLAY;
    uint8_t animation = one_off ? one_off_animation(g, obj) : looped_animation(g, obj);
    uint16_t entry = (uint16_t)(animation * FRAMES_PER_ANIMATION + frame + table);
    ObjectFrame r = { true, frame_entry(g, entry) };
    if (r.frame != END_OF_FRAMES) {
        set_field_frame_entry_address(g, entry);
    } else if (one_off) {
        set_field_frame_entry_address(g, entry);
        t->plays_counted = (uint8_t)(plays_left(g, obj) - 1);
        if (t->plays_counted != PLAYS_NONE)
            set_plays_left(g, obj, t->plays_counted);
        r = (ObjectFrame){ false, 0 };
    } else {
        uint16_t first = (uint16_t)(entry - frame);
        set_field_frame_entry_address(g, first);
        set_field_object_animation_frame(g, obj, END_OF_FRAMES);
        r.frame = frame_entry(g, first);
        t->started_over = true;
    }
    return r;
}

// $C0:CB3A
ObjectFrame object_current_frame(GameState *g, FieldObject obj)
{
    FrameTrace t;
    return current_frame(g, obj, &t);
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
 * delay table at the top of bank $E4, would; no object the game makes has
 * one. */
enum { BANK_SIZE = 0x10000 };

static bool link_or_none(uint8_t link) { return !field_link_is_object(link) || !(link & 1); }

bool field_tick_is_exact(const GameState *g, unsigned obj)
{
    return first_delay_entry(g, (FieldObject)obj) < BANK_SIZE && link_or_none(field_animation_queue_head(g)) &&
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

/* The object in X (8-bit). The carry says whether there is a frame, A holds
 * it with the high byte the entry's address left, and the overflow is that of
 * the entry's last sum: the table and the place in it, or the frame taken
 * back off where the animation started over. */
void field_read_animation_frame(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject obj = (FieldObject)field_object_in_x(s);
    FrameTrace t;
    ObjectFrame r = current_frame(g, obj, &t);
    uint16_t entry = field_frame_entry_address(g);
    s->cpu.a = (uint16_t)((entry & 0xFF00) | (r.has_frame ? r.frame : t.plays_counted));
    m_set_flag(s, CPU_FLAG_C, r.has_frame);
    bool overflow;
    if (t.started_over) {
        uint16_t frame = original_cell_value16(g, 0xD9);
        overflow = field_subtract_overflows16((uint16_t)(entry + frame), frame);
    } else {
        uint16_t table = facing_frame_table(g, obj);
        overflow = field_add_overflows16((uint16_t)(entry - table), table);
    }
    m_set_flag(s, CPU_FLAG_V, overflow);
    m_rts(s);
}
