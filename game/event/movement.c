/* Scripts moving objects about the map: walking to a tile or up to another
 * object, a number of steps in a direction, a jump or a launch, following
 * someone about, and letting an object behave as its state says.
 *
 * A move is a number of steps the field's object update takes one a frame
 * (field/motion.c); a move command stays on itself while one runs, and looks
 * again when it is over. Toward a tile or an object, the command sets off a
 * move's worth of steps at a time -- turning aside from an object in the
 * way -- until the object stands on the tile, next to the other object, or
 * well on screen; a counted move sets off once, for the steps the script
 * gives, and is over when they are taken. On arrival an object can line
 * itself up with the tile's middle or with the other object (object.h,
 * ObjectArrival), which takes steps of its own. A walking object turns to
 * face its way and runs its walking and standing animations; a sliding one
 * keeps its facing and doesn't animate (MoveStyle).
 *
 * The moves are three operations, by where they go: to a tile, toward an
 * object (or a party member's), and in a direction; the table's rows say
 * which opcode walks or slides, counts, or reads its operands from
 * variables. A jump, a launch and following are operations too; letting an
 * object behave as its state says calls the field's own routines, which are
 * still the original's, and is a bridge.
 *
 * What the original's handlers leave in Y and V for the code after them
 * (event/leaves.h) is the field's routines' they called: each move says so
 * where it ends, through the field's record of what those routines leave
 * (field/scratch.h). */
#include "cpu_io.h"
#include "event/bridge.h"
#include "event/leaves.h"
#include "event/script.h"
#include "field/scratch.h"

/* ---- What the original leaves (event/leaves.h, field/scratch.h) --------------------------- */

/* The original works a move's direction out with the field's routine, from
 * two tiles it leaves in cells: the running object's and the one it heads
 * for, where the object it heads for is kept too. */
static void leave_aim(GameState *g, Aim aim, Direction d)
{
    field_leave_aim(g, aim);
    field_leave_direction(g, aim, d);
}

/* Turning aside, the original leaves the new direction in the field's
 * direction cell, which the step's start reads. */
static void leave_turned_aside(GameState *g, Direction turned)
{
    set_field_direction(g, (uint16_t)((field_direction(g) & 0xFF00) | turned));
}

/* What the step's start and the walking animation leave (the Y and V of a
 * move that set off, and the check for objects in the way) is said where
 * they happen, through event/leaves.h: nothing after the move says which way
 * its step started. */

/* ---- Setting off, arriving --------------------------------------------------------------- */

/* A move that meets an object in the way turns aside from it, three
 * sixteenths of a turn (directions being a byte to a turn). */
enum { TURN_ASIDE = 0x30 };

/* Set off in a direction for `steps` steps: facing the way when walking, and
 * turned aside from an object in the way; the object then walks, and the
 * command stays on until the move is over. */
static EventFlow set_off(GameState *g, FieldObject self, MoveStyle style, Direction direction, uint8_t steps)
{
    if (style == WALKING)
        object_set_facing(g, self, facing_of_direction(g, direction));
    bool blocked = object_in_the_way(g, self).blocked;
    ORIGINAL_EFFECT(leave_in_the_way_check(g, self));
    if (blocked) {
        direction = (Direction)(direction + TURN_ASIDE);
        ORIGINAL_EFFECT(leave_turned_aside(g, direction));
        if (style == WALKING)
            object_set_facing(g, self, facing_of_direction(g, direction));
    }
    object_start_walk(g, self, steps);
    StepStart start = object_start_step(g, self, direction);
    if (style == WALKING)
        object_animate_movement(g, self);
    ORIGINAL_EFFECT(leave_set_off(g, self, start, style == WALKING));
    return stay();
}

/* Set off toward a tile, from where the object stands. */
static EventFlow set_off_toward(GameState *g, FieldObject self, MoveStyle style, Aim aim, uint8_t steps)
{
    Direction d = direction_between(g, aim.from, aim.to);
    ORIGINAL_EFFECT(leave_aim(g, aim, d));
    return set_off(g, self, style, d, steps);
}

/* Arrived: a walking object stands. */
static EventFlow arrive(GameState *g, FieldObject self, MoveStyle style)
{
    if (style == WALKING)
        object_stand(g, self);
    return next_command();
}

/* Lining up on arrival takes a step at a time; a walking object walks it. */
static EventFlow keep_lining_up(GameState *g, FieldObject self, MoveStyle style)
{
    if (style == WALKING) {
        object_animate_movement(g, self);
        ORIGINAL_EFFECT(leave_walk_overflow(g, self));
    }
    return stay();
}

/* A counted move's progress: none started, one running, or one just over
 * (whose mark is then taken away). */
typedef enum { MOVE_FRESH, MOVE_RUNNING, MOVE_DONE } CountedMove;

static CountedMove counted_move(GameState *g, FieldObject self)
{
    if (!object_scripted_move_marked(g, self))
        return MOVE_FRESH;
    if (object_moving(g, self))
        return MOVE_RUNNING;
    object_unmark_scripted_move(g, self);
    return MOVE_DONE;
}

/* The steps a move sets off for: the script's, for a counted move, or the
 * object's move length; and a counted move is marked. */
static uint8_t steps_to_set_off(GameState *g, FieldObject self, bool counted, uint8_t steps)
{
    if (!counted)
        return object_move_length(g, self);
    object_mark_scripted_move(g, self);
    return steps;
}

/* ---- To a tile --------------------------------------------------------------------------- */

// $C0:4F61
/* Walk or slide to a tile, a move's worth of steps at a time, turning aside
 * from what's in the way, and stay until there -- lined up with the tile's
 * middle, where the object asks. A counted move sets off once, for the steps
 * given, and is over when they are taken or it stands on the tile. */
static EventFlow move_to_tile(GameState *g, FieldObject self, uint8_t tile_x, uint8_t tile_y, MoveStyle style,
                              bool counted, uint8_t steps)
{
    bool fresh = !counted || !object_scripted_move_marked(g, self);
    if (object_moving(g, self) && (!counted || !fresh))
        return stay();
    if (fresh) {
        Aim aim = { object_tile(g, self), { tile_x, tile_y } };
        ORIGINAL_EFFECT(field_leave_aim(g, aim));
        if (aim.from.x != aim.to.x || aim.from.y != aim.to.y)
            return set_off_toward(g, self, style, aim, steps_to_set_off(g, self, counted, steps));
        if (object_lines_up(g, self, LINE_UP_ON_THE_TILE) && object_line_up_with_tile(g, self))
            return keep_lining_up(g, self, style);
    }
    if (counted)
        object_unmark_scripted_move(g, self);
    return arrive(g, self, style);
}
EVENT_OPERATION(move_to_tile, self, tile_x, tile_y, style, counted, steps)

/* ---- Toward an object --------------------------------------------------------------------- */

// $C0:5215
/* Walk or slide up to another object (or a party member), a move's worth of
 * steps at a time, turning aside from what's in the way, until next to it --
 * lined up with it, where the object asks -- or, running in to join a scene,
 * until well on screen. A disabled object is none to walk to. A counted move
 * sets off once, for the steps given. */
static EventFlow move_toward(GameState *g, FieldObject self, FieldObject target, MoveStyle style, bool counted,
                             uint8_t steps, WalkEnd until)
{
    if (counted) {
        switch (counted_move(g, self)) {
        case MOVE_RUNNING: return stay();
        case MOVE_DONE: return next_command();
        default: break;
        }
    } else if (object_moving(g, self)) {
        return stay();
    }
    if (!object_disabled(g, target)) {
        Aim aim = { object_tile(g, self), object_tile(g, target) };
        ORIGINAL_EFFECT(field_leave_aim(g, aim));
        ORIGINAL_EFFECT(field_leave_target_object(g, target));
        if (!tiles_next_to(aim.from, aim.to)) {
            if (until == WELL_ON_SCREEN) {
                ORIGINAL_EFFECT(keep_screen_edges(g));
                if (tile_well_on_screen(g, aim.from))
                    return arrive(g, self, style);
            }
            return set_off_toward(g, self, style, aim, steps_to_set_off(g, self, counted, steps));
        }
        if (until == NEXT_TO_IT && object_lines_up(g, self, LINE_UP_ON_THE_OBJECT) &&
            object_line_up_with(g, self, target))
            return keep_lining_up(g, self, style);
    }
    if (counted)
        object_unmark_scripted_move(g, self);
    else if (style == SLIDING)
        return next_command();
    return arrive(g, self, style);
}
EVENT_OPERATION(move_toward, self, target, style, counted, steps, until)

/* ---- In a direction ----------------------------------------------------------------------- */

// $C0:4E73
/* Walk or slide a number of steps in a direction, across tiles, and stay
 * until they are taken. */
static EventFlow move_in_direction(GameState *g, FieldObject self, Direction direction, uint8_t steps, MoveStyle style)
{
    switch (counted_move(g, self)) {
    case MOVE_RUNNING: return stay();
    case MOVE_DONE: return arrive(g, self, style);
    default: break;
    }
    ORIGINAL_EFFECT(set_field_direction(g, direction));
    object_start_move(g, self, steps, CROSS_ALWAYS);
    if (style == WALKING)
        object_set_facing(g, self, facing_of_direction(g, direction));
    object_mark_scripted_move(g, self);
    StepStart start = object_start_step(g, self, direction);
    if (style == WALKING)
        object_animate_movement(g, self);
    ORIGINAL_EFFECT(leave_set_off(g, self, start, style == WALKING));
    return stay();
}
EVENT_OPERATION(move_in_direction, self, direction, steps, style)

/* ---- With a velocity of its own ----------------------------------------------------------- */

/* A move with its own velocity is under way, or just over (it has landed);
 * none, to start one. */
typedef enum { NO_LAUNCH, LAUNCHED, LANDED } Launch;

static Launch launch_state(GameState *g, FieldObject self)
{
    if (!object_scripted_move_marked(g, self))
        return NO_LAUNCH;
    if (object_moving(g, self))
        return LAUNCHED;
    object_land(g, self);
    return LANDED;
}

// $C0:4E25
/* Launch the object at a speed (the low bytes of its velocities), with an
 * acceleration, for a number of steps: a fall, a throw, a leap. Waits until
 * it lands. */
static EventFlow launch(GameState *g, FieldObject self, uint8_t velocity_x, uint8_t velocity_y, uint8_t acceleration,
                        uint8_t steps)
{
    switch (launch_state(g, self)) {
    case LAUNCHED: return stay();
    case LANDED: return next_command();
    default: break;
    }
    object_launch(g, self, velocity_x, velocity_y, acceleration, steps);
    return stay();
}
EVENT_OPERATION(launch, self, velocity_x, velocity_y, acceleration, steps)

/* The steps to cover the tiles between `own` and `target` along an axis at a
 * velocity (its low byte): distance * 256 / |velocity|. False if the velocity
 * is zero. The original divides in hardware, which it leaves holding the
 * quotient. */
static bool steps_along(GameState *g, uint8_t own, uint8_t target, int8_t velocity, uint8_t *steps)
{
    uint8_t distance = target >= own ? (uint8_t)(target - own) : (uint8_t)(own - target);
    uint16_t dividend = (uint16_t)(distance << 8);
    ORIGINAL_EFFECT(hw_set_dividend(g, dividend));
    if (velocity == 0)
        return false;
    uint8_t speed = velocity < 0 ? (uint8_t)-velocity : (uint8_t)velocity;
    ORIGINAL_EFFECT(hw_divide_by(g, speed));
    *steps = (uint8_t)(dividend / speed);
    return true;
}

/* A jump's steps: as many as the horizontal distance takes at the object's
 * horizontal velocity, or failing that the vertical, or failing both the
 * height's. */
static uint8_t jump_steps(GameState *g, FieldObject self, Aim aim, uint8_t height)
{
    uint8_t steps = height;
    Velocity v = object_velocity(g, self);
    if (aim.from.x != aim.to.x && steps_along(g, aim.from.x, aim.to.x, v.x, &steps))
        return steps;
    if (aim.from.y != aim.to.y && steps_along(g, aim.from.y, aim.to.y, v.y, &steps))
        return steps;
    return height;
}

/* For the original's code and shadow mode: the jump keeps its height in
 * event_count's low byte across its calls, and multiplies the steps by the
 * height in hardware, which it leaves holding the product. */
static void leave_jump(GameState *g, uint8_t steps, uint8_t height)
{
    set_event_count(g, (uint16_t)((event_count(g) & 0xFF00) | height));
    hw_set_multiplicand(g, steps);
    hw_multiply_by(g, height);
}

// $C0:4D06
/* Jump to a tile: an arc as high as the command says, at the object's speed,
 * landing on the tile. Waits until it lands. */
static EventFlow jump_to_tile(GameState *g, FieldObject self, uint8_t tile_x, uint8_t tile_y, uint8_t height)
{
    switch (launch_state(g, self)) {
    case LAUNCHED: return stay();
    case LANDED: return next_command();
    default: break;
    }
    Aim aim = { object_tile(g, self), { tile_x, tile_y } };
    Direction d = direction_between(g, aim.from, aim.to);
    ORIGINAL_EFFECT(leave_aim(g, aim, d));
    object_head(g, self, d);
    uint8_t steps = jump_steps(g, self, aim, height);
    object_jump(g, self, steps, height);
    ORIGINAL_EFFECT(leave_jump(g, steps, height));
    return stay();
}
EVENT_OPERATION(jump_to_tile, self, tile_x, tile_y, height)

/* ---- Following ----------------------------------------------------------------------------- */

// $C0:3546
/* Follow another object about for good: walk up to it every turn. */
static EventFlow follow(GameState *g, FieldObject self, FieldObject target)
{
    move_toward(g, self, target, WALKING, false, 0, NEXT_TO_IT);
    return stay();
}
EVENT_OPERATION(follow, self, target)

// $C0:354F
/* Follow a party member about for good: walk up to them every turn, and go
 * on with the object's turn this frame when the walk would have (arrived). */
static EventFlow follow_party_member(GameState *g, FieldObject self, FieldObject target)
{
    EventFlow walk = move_toward(g, self, target, WALKING, false, 0, NEXT_TO_IT);
    return walk.kind == FLOW_NEXT ? run_again() : stay();
}
EVENT_OPERATION(follow_party_member, self, target)

/* ---- Behaving as the state says ------------------------------------------------------------ */

enum { STATE_ROUTINES = 3 };
/* What the state routines leave that the command doesn't hand on. */
enum { STATE_DONT_CARE = DC_A | DC_X | DC_N | DC_Z };

// $C0:3002
/* Let the object behave as its state says, a step at a time, for as long as
 * the script stays here: the field's own routines for a wandering
 * townsperson, a party member following the leader (state 0 $C0:9E29, and
 * then $C0:3154 if $C0:5B8D accepts its facing; 1 $C0:A26B; 2 $C0:A2CE).
 * Nothing happens while field_state_routine_hold is set. The routines are
 * the original's, called with 8-bit index registers. */
void event_cmd_behave(Snes *s)
{
    static const uint32_t sites[STATE_ROUTINES] = { 0xC03047, 0xC03037, 0xC03027 };
    static const uint32_t routines[STATE_ROUTINES] = { 0xC09E29, 0xC0A26B, 0xC0A2CE };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    if (field_state_routine_hold(g) != 0) {
        event_return(s, (EventStep){ at, false });
        return;
    }
    ORIGINAL_EFFECT(event_keep_offset(g, at));
    s->cpu.p |= CPU_FLAG_X;   /* 8-bit index registers, which clears their high bytes */
    s->cpu.x &= 0x00FF;
    s->cpu.y &= 0x00FF;
    FieldObject self = object_current(g);
    ObjectRole role = object_role(g, self);
    if (object_is_party_member(g, self)) {
        object_start_one_step(g, self);
        if (!bridge_call(s, sites[role.kind], routines[role.kind], STATE_DONT_CARE, event_machine(s, true)))
            return;
        if (role.kind == ROLE_LEADER) {
            if (!bridge_call(s, 0xC0304F, 0xC05B8D, STATE_DONT_CARE, event_machine(s, true)))
                return;
            if (m_flag(s, CPU_FLAG_C) && !bridge_call(s, 0xC03054, 0xC03154, STATE_DONT_CARE, event_machine(s, true)))
                return;
        }
    }
    s->cpu.p &= (uint8_t)~CPU_FLAG_X;
    event_return(s, (EventStep){ at, false });
}

/* The moves that set off, following included, look for an object in the
 * way, over the object slots (object_search_fits). */
static bool can_look_its_way(const GameState *g) { return object_search_fits(g); }
EVENT_CHECK_TAKING_NOTHING(can_look_its_way)

/* A sliding move looks the way the object faces, which the original looks
 * up in a table of the four facings: an object facing none of them would
 * read past it, which the guard leaves to the original. */
static bool can_slide_its_way(const GameState *g, FieldObject self)
{
    return object_facing_is_one(g, self) && object_search_fits(g);
}
EVENT_CHECK(can_slide_its_way, self)

/* The moves' guards are their operands': an object, or a party member whose
 * object is one. */
#define TO_A_TILE .tile_x = OPERAND(1), .tile_y = OPERAND(2)
#define TO_A_TILE_FROM_VARIABLES .tile_x = VARIABLE_BYTE(1), .tile_y = VARIABLE_BYTE(2)
#define WALKS .style = CONSTANT(WALKING)
#define WALKS_LOOKING .style = CONSTANT(WALKING), CHECK(can_look_its_way)
#define SLIDES .style = CONSTANT(SLIDING), CHECK(can_slide_its_way)

EVENT_OPCODES(event_movement_opcodes,
    { 0x96, OPERATION(move_to_tile), TO_A_TILE, WALKS_LOOKING, .length = 3 },
    { 0x97, OPERATION(move_to_tile), TO_A_TILE_FROM_VARIABLES, WALKS_LOOKING, .length = 3 },
    { 0xA0, OPERATION(move_to_tile), TO_A_TILE, SLIDES, .length = 3 },
    { 0xA1, OPERATION(move_to_tile), TO_A_TILE_FROM_VARIABLES, SLIDES, .length = 3 },
    { 0x9A, OPERATION(move_to_tile), TO_A_TILE, WALKS_LOOKING, .counted = CONSTANT(true), .steps = OPERAND(3), .length = 4 },
    { 0x94, OPERATION(move_toward), .target = OBJECT(1), WALKS_LOOKING, .length = 2 },
    { 0x95, OPERATION(move_toward), .target = MEMBER_OBJECT(1), WALKS_LOOKING, .length = 2 },
    { 0x9E, OPERATION(move_toward), .target = OBJECT(1), SLIDES, .length = 2 },
    { 0x9F, OPERATION(move_toward), .target = MEMBER_OBJECT(1), SLIDES, .length = 2 },
    { 0x98, OPERATION(move_toward), .target = OBJECT(1), WALKS_LOOKING, .counted = CONSTANT(true), .steps = OPERAND(2),
      .length = 3 },
    { 0x99, OPERATION(move_toward), .target = MEMBER_OBJECT(1), WALKS_LOOKING, .counted = CONSTANT(true),
      .steps = OPERAND(2), .length = 3 },
    { 0x8F, OPERATION(move_toward), .target = MEMBER_OBJECT(1), WALKS_LOOKING, .until = CONSTANT(WELL_ON_SCREEN),
      .length = 2 },
    { 0x92, OPERATION(move_in_direction), .direction = OPERAND(1), .steps = OPERAND(2), WALKS, .length = 3 },
    { 0x9C, OPERATION(move_in_direction), .direction = OPERAND(1), .steps = OPERAND(2),
      .style = CONSTANT(SLIDING), .length = 3 },
    { 0x9D, OPERATION(move_in_direction), .direction = VARIABLE_BYTE(1), .steps = VARIABLE_BYTE(2),
      .style = CONSTANT(SLIDING), .length = 3 },
    { 0x7A, OPERATION(jump_to_tile), TO_A_TILE, .height = OPERAND(3), .length = 4 },
    { 0x7B, OPERATION(launch), .velocity_x = OPERAND(1), .velocity_y = OPERAND(2), .acceleration = OPERAND(3),
      .steps = OPERAND(4), .length = 5 },
    { 0xB5, OPERATION(follow), .target = OBJECT(1), .length = 2, CHECK(can_look_its_way) },
    { 0xB6, OPERATION(follow_party_member), .target = MEMBER_OBJECT(1), .length = 2, CHECK(can_look_its_way) },
    { 0xB0, .length = 1 });
