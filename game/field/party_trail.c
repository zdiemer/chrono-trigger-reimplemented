/* The party walking the map (field/party.h): the leader goes where the
 * player steers it, and the two members follow in its footsteps.
 *
 * Every frame the leader moves, it leaves a footprint on its trail: how far
 * it moved, how the pad walked it (a walk or a run), how its sprite was
 * layered against the map, and the ground under it. The trail keeps the last
 * 128 footprints, round a ring. Each member walks it behind the one ahead of
 * it -- the first behind the leader, the second behind the first -- a
 * footprint a frame: it moves as the leader moved then, is layered as the
 * leader was, and walks, runs or climbs as the leader did. So the members
 * retrace the leader's way exactly, some frames behind.
 *
 * A member keeps its distance. While the leader moves it takes the next
 * footprint only once it is 24 behind the one ahead of it, and while the
 * leader stands it closes up to 16: the party bunches up when the leader
 * stops and strings out again when it sets off. A member that doesn't move
 * on stands, or on a moving floor walks as the leader walked against the
 * floor's pull, or on a climb holds on.
 *
 * What each shows follows the walk: standing, walking or running, facing the
 * way it goes (a diagonal keeps whichever of its two ways the object already
 * faced). On a climb it climbs while it goes up or down and holds still
 * otherwise. The leader running with the pad but held where it is runs on
 * the spot; left standing while the player picks an answer it takes a pose,
 * and when the party has stood about long enough, it fidgets now and then.
 *
 * The trail is kept in bank $7F as seven tables of 128, one for each part of
 * a footprint, and where each of the party is along it on the field's page;
 * the storage functions below are the only code that knows. */
#include "field/party.h"

#include "field/bridge.h"
#include "field/field.h"
#include "field/parts.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "vars.h"

/* ---- Storage: the trail, and what the walk leaves for the party ----------------------- */

enum { TRAIL_FOOTPRINTS = FIELD_TRAIL_GROUND_COUNT };

static uint8_t place_on_trail(const GameState *g, unsigned place) { return field_trail_position(g, place); }
static void set_place_on_trail(GameState *g, unsigned place, uint8_t at) { set_field_trail_position(g, place, at); }

/* The footprint after one, round the ring. */
static uint8_t next_footprint(uint8_t at) { return (uint8_t)((at + 1) % TRAIL_FOOTPRINTS); }

/* How many footprints one place on the trail is ahead of another. */
static uint8_t footprints_between(uint8_t ahead, uint8_t behind) { return (uint8_t)(ahead - behind) % TRAIL_FOOTPRINTS; }

Footprint field_footprint(const GameState *g, uint8_t at)
{
    return (Footprint){
        { (int8_t)field_trail_movement_x(g, at), (int8_t)field_trail_movement_y(g, at) },
        { (int8_t)field_trail_walk_x(g, at), (int8_t)field_trail_walk_y(g, at) },
        { field_trail_lower_priority(g, at), field_trail_upper_priority(g, at) },
        field_trail_ground(g, at),
    };
}

static void leave_footprint(GameState *g, uint8_t at, Footprint f)
{
    set_field_trail_movement_x(g, at, (uint8_t)f.movement.x);
    set_field_trail_movement_y(g, at, (uint8_t)f.movement.y);
    set_field_trail_walk_x(g, at, (uint8_t)f.walk.x);
    set_field_trail_walk_y(g, at, (uint8_t)f.walk.y);
    set_field_trail_lower_priority(g, at, f.priorities.lower_stored);
    set_field_trail_upper_priority(g, at, f.priorities.upper_stored);
    set_field_trail_ground(g, at, f.ground);
}

Velocity field_pad_walk(const GameState *g)
{
    return (Velocity){ (int8_t)field_leader_walk_x(g), (int8_t)field_leader_walk_y(g) };
}

static Velocity leader_movement(const GameState *g)
{
    return (Velocity){ (int8_t)field_leader_movement_x(g), (int8_t)field_leader_movement_y(g) };
}

static bool leader_moves(const GameState *g)
{
    Velocity moved = leader_movement(g);
    return moved.x != 0 || moved.y != 0;
}

static bool is_leader(const GameState *g, FieldObject obj)
{
    return field_same_link(party_member_link(g, PARTY_LEADER), field_link_to(obj));
}

/* How long the party has stood about, which the frame wait counts. */
static uint16_t idle_frames(const GameState *g) { return field_idle_frames(g); }
static void set_idle_frames(GameState *g, uint16_t frames) { set_field_idle_frames(g, frames); }

bool party_trail_places_hold(const GameState *g)
{
    for (unsigned place = 0; place < PARTY_PLACES; place++)
        if (place_on_trail(g, place) >= TRAIL_FOOTPRINTS)
            return false;
    return true;
}

/* ---- What the original leaves (field/scratch.h) --------------------------------------- */

/* Whether a moving floor carries the one stepping, which the original notes
 * in a scratch cell, and the gait lookup's horizontal part, which it keeps
 * in another. */
static void leave_carried(GameState *g, bool carried) { original_cell8(g, 0xE5, carried); }
static void leave_lookup_across(GameState *g, uint8_t across) { original_cell8(g, 0xDB, across); }

/* ---- The gait a walk shows ------------------------------------------------------------ */

typedef enum { STANDS, WALKS, RUNS } Gait;

/* How the walk goes on one axis: still, a walk or a run, forward (down, or
 * right) or back. */
typedef struct {
    Gait gait;
    bool forward;
} AxisGait;

static const AxisGait axis_gaits[] = {
    { STANDS, false }, { WALKS, true }, { RUNS, true }, { WALKS, false }, { RUNS, false },
};
enum { AXIS_GAITS = sizeof axis_gaits / sizeof axis_gaits[0] };

/* The original looks the gait up in a table of the two axes' gaits, by one
 * byte it makes of both: each axis's speed (a backward one as 32 more than
 * its size), the vertical's as it is and the horizontal's an eighth of it,
 * run together. The pad's speeds -- still, a walk or a run either way --
 * give an axis's row and column; any other speed muddles the two, and most
 * such land on standing. A member waiting on a moving floor looks up what
 * the floor leaves of the leader's walk, which can be any, so the lookup is
 * kept as it is. */
typedef struct {
    unsigned down, across;   /* an axis gait each, or past them: standing */
} GaitLookup;

static uint8_t lookup_speed(int8_t v) { return v >= 0 ? (uint8_t)v : (uint8_t)(0x20 - v); }

static GaitLookup gait_lookup(GameState *g, Velocity walk)
{
    uint8_t across = (uint8_t)(lookup_speed(walk.x) >> 3);
    ORIGINAL_EFFECT(leave_lookup_across(g, across));
    uint8_t entry = (uint8_t)((lookup_speed(walk.y) | across) >> 1 & 0x3F);
    return (GaitLookup){ entry >> 3, entry & 7 };
}

/* The pose a walk shows: its gait (the faster of its two axes'), and the way
 * it faces. A walk one way faces that way; a diagonal faces either of its
 * two ways, and the object keeps whichever it already faces, or else faces
 * across. */
typedef struct {
    Gait gait;
    Facing facing;
    bool diagonal;
    Facing or_else;
} Pose;

static const Pose STANDING = { STANDS, FACING_UP, false, FACING_UP };

static Pose pose_of(GameState *g, Velocity walk)
{
    GaitLookup l = gait_lookup(g, walk);
    if (l.down >= AXIS_GAITS || l.across >= AXIS_GAITS)
        return STANDING;
    AxisGait down = axis_gaits[l.down], across = axis_gaits[l.across];
    Facing vertical = down.forward ? FACING_DOWN : FACING_UP, horizontal = across.forward ? FACING_RIGHT : FACING_LEFT;
    Gait gait = down.gait > across.gait ? down.gait : across.gait;
    if (down.gait == STANDS)
        return (Pose){ gait, horizontal, false, horizontal };
    if (across.gait == STANDS)
        return (Pose){ gait, vertical, false, vertical };
    return (Pose){ gait, horizontal, true, vertical };
}

static void face_the_way(GameState *g, FieldObject obj, Pose p)
{
    if (p.diagonal && object_facing_is_one(g, obj)) {
        Facing now = object_facing(g, obj);
        if (now == p.facing || now == p.or_else)
            return;
    }
    object_set_facing(g, obj, p.facing);
}

/* On the move, the party isn't standing about. */
static void count_as_on_the_move(GameState *g) { set_idle_frames(g, 0); }

/* The leader running with the pad but held where it is (against a wall, a
 * shut door) runs on the spot; not where a moving floor carries it. */
static bool runs_on_the_spot(const GameState *g, FieldObject obj, bool carried)
{
    return !leader_moves(g) && is_leader(g, obj) && !carried;
}

/* Standing: the leader takes a pose while the player picks an answer. With
 * the party in the player's hands, once it has stood about long enough it
 * may fidget, and then may again every 128 frames, keeping between times
 * whatever it shows; otherwise it stands. */
enum { FIDGET_AFTER = 0x2FF, FIDGET_EVERY = 0x80, HAS_FIDGETED = 0x8000, FIDGET_CHANCE = 8 };

static void stand_or_fidget(GameState *g, FieldObject obj)
{
    if (dialog_choice_active(g) == DIALOG_CHOOSING && is_leader(g, obj)) {
        object_show_movement(g, obj, ANIMATION_CHOOSING);
        return;
    }
    uint8_t shown = ANIMATION_STAND;
    if (field_player_control(g) != 0) {
        uint16_t idle = idle_frames(g);
        if (idle > FIDGET_AFTER && (idle % FIDGET_EVERY) != FIDGET_EVERY - 1)
            return;
        if (idle >= FIDGET_AFTER) {
            set_idle_frames(g, (uint16_t)(idle | HAS_FIDGETED));
            if (field_draw_random(g) < FIDGET_CHANCE)
                shown = ANIMATION_FIDGET;
        }
    }
    object_show_movement(g, obj, shown);
}

// $C0:9FF7 (the gaits' table) and the poses' routines $C0:A079-$C0:A26A
/* The object shows its pose: it stands (or fidgets), or faces its way and
 * walks or runs, which counts as the party being on the move. `carried`: a
 * moving floor carries it, where a run against something is no run on the
 * spot. */
static void take_pose(GameState *g, FieldObject obj, Pose p, bool carried)
{
    if (p.gait == STANDS) {
        stand_or_fidget(g, obj);
        return;
    }
    face_the_way(g, obj, p);
    count_as_on_the_move(g);
    if (p.gait == WALKS)
        object_show_movement(g, obj, ANIMATION_WALK);
    else
        object_show_movement(g, obj, runs_on_the_spot(g, obj, carried) ? ANIMATION_RUN_ON_THE_SPOT : ANIMATION_RUN);
}

/* On a climb: climbing while it goes up or down, holding still otherwise. */
static void climb(GameState *g, FieldObject obj, bool going)
{
    if (going)
        object_show_movement(g, obj, ANIMATION_CLIMB);
    else
        object_restart_animation(g, obj);
}

/* ---- The leader ------------------------------------------------------------------------ */

// $C0:9E29, and its gait $C0:9E84
void party_leader_steps(GameState *g, FieldObject leader)
{
    LeaderGround ground = field_leader_ground(g);
    Velocity walk = field_pad_walk(g);
    ORIGINAL_EFFECT(leave_carried(g, ground != 0 && !(ground & GROUND_CLIMB)));
    if (ground & GROUND_CLIMB)
        climb(g, leader, walk.y != 0);
    else
        take_pose(g, leader, pose_of(g, walk), ground != 0);
    Velocity moved = leader_movement(g);
    if (!leader_moves(g)) {
        object_set_step(g, leader, (Velocity){ 0, 0 });
        return;
    }
    uint8_t newest = next_footprint(place_on_trail(g, PARTY_LEADER));
    set_place_on_trail(g, PARTY_LEADER, newest);
    leave_footprint(g, newest, (Footprint){ moved, walk, object_sprite_priorities(g, leader), ground });
    object_set_step(g, leader, moved);
}

/* ---- The members ----------------------------------------------------------------------- */

enum { KEEPS_BEHIND_A_MOVING_LEADER = 24, CLOSES_UP_TO = 16 };

bool party_member_moves_on(const GameState *g, unsigned place)
{
    uint8_t keeps = leader_moves(g) ? KEEPS_BEHIND_A_MOVING_LEADER : CLOSES_UP_TO;
    return footprints_between(place_on_trail(g, place - 1), place_on_trail(g, place)) >= keeps;
}

/* A floor pulls at half a walking step or at one and a half; the pull the
 * footprint's movement shows is rounded away from nothing to whole steps. */
enum { HALF_A_STEP = 0x08 };

int8_t field_floor_pull(int8_t movement)
{
    if (!((uint8_t)movement & HALF_A_STEP))
        return movement;
    return (int8_t)(movement < 0 ? movement - HALF_A_STEP : movement + HALF_A_STEP);
}

/* A member that doesn't move on this frame, on its own footprint: it stands;
 * on a moving floor it walks as the leader walked against the floor's pull;
 * on a climb it holds still, but where the leader climbed at the climb's
 * pace against a faster pad (a run slowed to a walk), where it goes on as it
 * was. */
// $C0:9F6F
static void wait_on_the_trail(GameState *g, FieldObject member, Footprint f)
{
    if (f.ground == 0) {
        stand_or_fidget(g, member);
        return;
    }
    if (f.ground & GROUND_CLIMB) {
        if (f.walk.y != 0 && f.movement.y != 0 && f.movement.y != f.walk.y)
            return;
        object_restart_animation(g, member);
        return;
    }
    Velocity against = { (int8_t)(f.walk.x - field_floor_pull(f.movement.x)),
                         (int8_t)(f.walk.y - field_floor_pull(f.movement.y)) };
    take_pose(g, member, pose_of(g, against), false);
}

// $C0:A26B (the first member), $C0:A2CE (the second), and their gait $C0:9ED1, $C0:9F20
void party_member_follows(GameState *g, FieldObject member, unsigned place)
{
    ORIGINAL_EFFECT(leave_carried(g, false));
    object_set_step(g, member, (Velocity){ 0, 0 });
    if (!party_member_moves_on(g, place)) {
        wait_on_the_trail(g, member, field_footprint(g, place_on_trail(g, place)));
        return;
    }
    uint8_t next = next_footprint(place_on_trail(g, place));
    set_place_on_trail(g, place, next);
    Footprint f = field_footprint(g, next);
    object_set_step(g, member, f.movement);
    object_take_sprite_priorities(g, member, f.priorities);
    if (f.ground & GROUND_CLIMB)
        climb(g, member, f.walk.y != 0);
    else
        take_pose(g, member, pose_of(g, f.walk), false);
}

/* ---- The party regrouping --------------------------------------------------------------- */

/* The footprint the members pick the trail up at is wiped, so that they
 * stand where they are: the original wipes its horizontal half and its
 * ground and leaves its vertical half, which the gate keeps. */
// $C0:327B, $C0:3477
void party_trail_regroup(GameState *g)
{
    uint8_t newest = place_on_trail(g, PARTY_LEADER);
    for (unsigned place = PARTY_LEADER + 1; place < PARTY_PLACES; place++)
        set_place_on_trail(g, place, newest);
    set_field_trail_movement_x(g, newest, 0);
    set_field_trail_walk_x(g, newest, 0);
    set_field_trail_ground(g, newest, 0);
}

/* ---- The entries ----------------------------------------------------------------------- */

void field_leader_steps(Snes *s)
{
    GameState *g = state_of(s);
    FieldObject leader = object_current(g);
    party_leader_steps(g, leader);
    s->cpu.y = field_object_offset(leader);
    m_set_flag(s, CPU_FLAG_V, field_leader_step_overflow(g, m_flag(s, CPU_FLAG_V)));
    m_rts(s);
}

static void member_follows(Snes *s, unsigned place)
{
    GameState *g = state_of(s);
    FieldObject member = object_current(g);
    vars_peek(true);
    bool moved_on = party_member_moves_on(g, place);
    vars_peek(false);
    party_member_follows(g, member, place);
    s->cpu.y = field_object_offset(member);
    m_set_flag(s, CPU_FLAG_V, field_member_step_overflow(g, place, moved_on));
    m_rts(s);
}

void field_first_member_follows(Snes *s) { member_follows(s, 1); }
void field_second_member_follows(Snes *s) { member_follows(s, 2); }

/* The guards. The leader's step is the current object's, whichever it is;
 * a member's moves the object in its place of the party and animates the
 * current one, which the game has the same. Both read the random table
 * through the data bank, and the trail by the places on it. */
bool field_leader_can_step(Snes *s) { return field_current_object_bank_zero(s); }

static bool member_can_follow(Snes *s, unsigned place)
{
    if (!field_current_object_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = field_same_link(party_member_link(g, place), field_link_stored((uint8_t)field_current_object(g))) &&
              party_trail_places_hold(g);
    vars_peek(false);
    return ok;
}

bool field_first_member_can_follow(Snes *s) { return member_can_follow(s, 1); }
bool field_second_member_can_follow(Snes *s) { return member_can_follow(s, 2); }
