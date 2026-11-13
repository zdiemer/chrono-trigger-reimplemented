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
#include "charge.h"
#include "field/party.h"

#include "field/bridge.h"
#include "field/field.h"
#include "field/parts.h"
#include "vars.h"

/* ---- Storage: the trail, and what the walk leaves for the party ----------------------- */

enum { TRAIL_FOOTPRINTS = FIELD_TRAIL_GROUND_COUNT };

static uint8_t place_on_trail(const GameState *g, PartyPlace place) { return field_trail_position(g, place); }
static void set_place_on_trail(GameState *g, PartyPlace place, uint8_t at) { set_field_trail_position(g, place, at); }

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
        { field_trail_ground(g, at) },
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
    set_field_trail_ground(g, at, f.ground.stored);
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

LeaderGround leader_ground(const GameState *g) { return (LeaderGround){ field_leader_ground(g) }; }
void set_leader_ground(GameState *g, LeaderGround ground) { set_field_leader_ground(g, ground.stored); }

PartyPlace object_party_place(const GameState *g, FieldObject obj)
{
    switch (object_role(g, obj).kind) {
    case ROLE_PARTY_1: return PARTY_FIRST_MEMBER;
    case ROLE_PARTY_2: return PARTY_SECOND_MEMBER;
    default: return PARTY_LEADER;
    }
}

/* How long the party has stood about: the frames the frame wait counts, and
 * whether the leader has fidgeted since the party last moved, kept as one
 * word with the flag its top bit. */
typedef struct {
    uint16_t frames;
    bool fidgeted;
} IdleTime;
enum { FIDGETED_BIT = 0x8000 };

static IdleTime idle_time(const GameState *g)
{
    uint16_t stored = field_idle_frames(g);
    return (IdleTime){ (uint16_t)(stored & ~FIDGETED_BIT), (stored & FIDGETED_BIT) != 0 };
}
static void mark_fidgeted(GameState *g) { set_field_idle_frames(g, (uint16_t)(field_idle_frames(g) | FIDGETED_BIT)); }

bool party_trail_places_hold(const GameState *g)
{
    for (PartyPlace place = 0; place < PARTY_PLACES; place++)
        if (place_on_trail(g, place) >= TRAIL_FOOTPRINTS)
            return false;
    return true;
}

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

static GaitLookup gait_lookup(Velocity walk)
{
    uint8_t across = (uint8_t)(lookup_speed(walk.x) >> 3);
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

static Pose pose_of(Velocity walk)
{
    GaitLookup l = gait_lookup(walk);
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
void count_as_on_the_move(GameState *g) { set_field_idle_frames(g, 0); }

/* The count is ten bits; the bits between it and the flag stay clear. */
enum { IDLE_FRAMES_COUNTED = 0x03FF };
void party_stands_another_frame(GameState *g)
{
    uint16_t stored = (uint16_t)(field_idle_frames(g) + 1);
    set_field_idle_frames(g, (uint16_t)(stored & (FIDGETED_BIT | IDLE_FRAMES_COUNTED)));
}

/* The leader running with the pad but held where it is (against a wall, a
 * shut door) runs on the spot; not where a moving floor carries it. */
static bool runs_on_the_spot(const GameState *g, FieldObject obj, bool carried)
{
    return !leader_moves(g) && is_leader(g, obj) && !carried;
}

/* What a pose shows goes on the one in the member's place, an object, or
 * the slot an emptied place's marker names past the tables, which the
 * original writes all the same (step_the_place). */
static void show_movement(GameState *g, FieldObject obj, uint8_t animation)
{
    if (field_names_object(field_object_offset(obj)))
        object_show_movement(g, obj, animation);
    else
        object_show_movement_past_the_tables(g, obj, animation);
}

static void restart_animation(GameState *g, FieldObject obj)
{
    if (field_names_object(field_object_offset(obj)))
        object_restart_animation(g, obj);
    else
        object_restart_animation_past_the_tables(g, obj);
}

/* Standing: the leader takes a pose while the player picks an answer. With
 * the party in the player's hands, once it has stood about long enough it
 * may fidget, and then may again every 128 frames, keeping between times
 * whatever it shows; otherwise it stands. */
enum { FIDGET_AFTER = 0x2FF, FIDGET_EVERY = 0x80, FIDGET_CHANCE = 8 };

static void stand_or_fidget(GameState *g, FieldObject obj)
{
    if (dialog_choice_active(g) == DIALOG_CHOOSING && is_leader(g, obj)) {
        show_movement(g, obj, ANIMATION_CHOOSING);
        return;
    }
    uint8_t shown = ANIMATION_STAND;
    if (party_is_the_players(g)) {
        IdleTime idle = idle_time(g);
        bool fidgets_now_and_then = idle.fidgeted || idle.frames > FIDGET_AFTER;
        if (fidgets_now_and_then && idle.frames % FIDGET_EVERY != FIDGET_EVERY - 1)
            return;
        if (fidgets_now_and_then || idle.frames == FIDGET_AFTER) {
            mark_fidgeted(g);
            if (field_draw_random(g) < FIDGET_CHANCE)
                shown = ANIMATION_FIDGET;
        }
    }
    show_movement(g, obj, shown);
}

// $C0:9FF7 (the gaits' table) and the poses' routines $C0:A079-$C0:A26A
/* The pose shows: one stands (or fidgets), or the one stepping faces its
 * way and the one shown walks or runs, which counts as the party being on
 * the move. They are the same object but for a member waiting in a place a
 * scene gave to someone else (party_member_follows). `carried`: a moving
 * floor carries it, where a run against something is no run on the spot. */
static void take_pose(GameState *g, FieldObject turns, FieldObject shows, Pose p, bool carried)
{
    charge_accesses(g, 1); /* the pose's horizontal gait selection */
    if (p.gait == STANDS) {
        stand_or_fidget(g, shows);
        return;
    }
    face_the_way(g, turns, p);
    count_as_on_the_move(g);
    if (p.gait == WALKS)
        show_movement(g, shows, ANIMATION_WALK);
    else
        show_movement(g, shows, runs_on_the_spot(g, shows, carried) ? ANIMATION_RUN_ON_THE_SPOT : ANIMATION_RUN);
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

enum { LEADER_STEPS = 0xC09E29 };

// $C0:9E29, and its gait $C0:9E84
static void leader_steps(GameState *g, FieldObject leader)
{
    LeaderGround ground = leader_ground(g);
    GroundKind kind = ground_kind(ground);
    Velocity walk = field_pad_walk(g);
    charge_accesses(g, 1); /* moving-floor selection */
    if (kind == GROUND_CLIMB)
        climb(g, leader, walk.y != 0);
    else
        take_pose(g, leader, leader, pose_of(walk), kind == GROUND_MOVING_FLOOR);
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

void party_leader_steps(GameState *g, FieldObject leader) { TIMED_AS(g, LEADER_STEPS, leader_steps(g, leader)); }

/* ---- The members ----------------------------------------------------------------------- */

enum { KEEPS_BEHIND_A_MOVING_LEADER = 24, CLOSES_UP_TO = 16 };

bool party_member_moves_on(const GameState *g, PartyPlace place)
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
 * was. What it shows is shown on the one in its place of the party
 * (party_member_follows); the member turns its own way. */
// $C0:9F6F
static void wait_on_the_trail(GameState *g, FieldObject member, FieldObject in_the_place, Footprint f)
{
    GroundKind kind = ground_kind(f.ground);
    if (kind == GROUND_PLAIN) {
        stand_or_fidget(g, in_the_place);
        return;
    }
    if (kind == GROUND_CLIMB) {
        if (f.walk.y != 0 && f.movement.y != 0 && f.movement.y != f.walk.y)
            return;
        restart_animation(g, in_the_place);
        return;
    }
    Velocity against = { (int8_t)(f.walk.x - field_floor_pull(f.movement.x)),
                         (int8_t)(f.walk.y - field_floor_pull(f.movement.y)) };
    take_pose(g, member, in_the_place, pose_of(against), false);
}

/* A member's step moves the object its place of the party names; moving on,
 * the member whose turn it is shows its pose, and waiting, the one in the
 * place shows it while the member turns. They are the same object but where
 * a scene changes the party under a member still stepping: Robo, staying
 * behind at Fiona's house, steps as the party's third while the place
 * already names who took it; and for a moment the place is emptied (the Prophet's
 * ending, frames 119033-119035, as the party is taken off the map for its
 * first vignette). The original then steps the slot the empty place's
 * marker names, past the tables, which holds nobody: its step lands on the
 * next tables' bytes, the first object's vertical step and its speed, which
 * the game goes on with, so the port makes the same writes. */
static void step_the_place(GameState *g, PartyPlace place, Velocity step)
{
    FieldLink link = party_member_link(g, place);
    if (field_link_is_object(link))
        object_set_step(g, field_linked_object(link), step);
    else
        object_set_step_past_the_tables(g, field_linked_object(link), step);
}

static void layer_the_place(GameState *g, PartyPlace place, SpritePriorities priorities)
{
    FieldLink link = party_member_link(g, place);
    if (field_link_is_object(link))
        object_take_sprite_priorities(g, field_linked_object(link), priorities);
}

/* A place steps an object, or with the game's marker for an emptied one,
 * the slot past the tables (step_the_place, wait_on_the_trail). */
bool party_place_steps(const GameState *g, PartyPlace place)
{
    FieldLink link = party_member_link(g, place);
    return field_link_names_object(link) || field_same_link(link, field_no_link());
}

enum { FIRST_MEMBER_FOLLOWS = 0xC0A26B, SECOND_MEMBER_FOLLOWS = 0xC0A2CE };

// $C0:A26B (the first member), $C0:A2CE (the second), and their gait $C0:9ED1, $C0:9F20
static void member_follows(GameState *g, FieldObject member, PartyPlace place)
{
    charge_accesses(g, 1); /* the member is not carried */
    step_the_place(g, place, (Velocity){ 0, 0 });
    if (!party_member_moves_on(g, place)) {
        FieldObject in_the_place = field_linked_object(party_member_link(g, place));
        wait_on_the_trail(g, member, in_the_place, field_footprint(g, place_on_trail(g, place)));
        return;
    }
    uint8_t next = next_footprint(place_on_trail(g, place));
    set_place_on_trail(g, place, next);
    Footprint f = field_footprint(g, next);
    step_the_place(g, place, f.movement);
    layer_the_place(g, place, f.priorities);
    if (ground_kind(f.ground) == GROUND_CLIMB)
        climb(g, member, f.walk.y != 0);
    else
        take_pose(g, member, member, pose_of(f.walk), false);
}

void party_member_follows(GameState *g, FieldObject member, PartyPlace place)
{
    TIMED_AS(g, place == PARTY_FIRST_MEMBER ? FIRST_MEMBER_FOLLOWS : SECOND_MEMBER_FOLLOWS,
             member_follows(g, member, place));
}

/* ---- The party regrouping --------------------------------------------------------------- */

/* The footprint the members pick the trail up at is wiped, so that they
 * stand where they are: the original wipes its horizontal half and its
 * ground and leaves its vertical half, which the gate keeps. */
// $C0:327B, $C0:3477
void party_trail_regroup(GameState *g)
{
    uint8_t newest = place_on_trail(g, PARTY_LEADER);
    for (PartyPlace place = PARTY_FIRST_MEMBER; place < PARTY_PLACES; place++)
        set_place_on_trail(g, place, newest);
    set_field_trail_movement_x(g, newest, 0);
    set_field_trail_walk_x(g, newest, 0);
    set_field_trail_ground(g, newest, 0);
}

/* ---- A map's start, and after a battle ---------------------------------------------------- */

// $C0:9DC3
/* The whole party at the first footprint, which says it moved and walked no
 * way. */
void party_trail_rejoin(GameState *g)
{
    for (PartyPlace place = PARTY_LEADER; place < PARTY_PLACES; place++)
        set_place_on_trail(g, place, 0);
}

void party_trail_start(GameState *g)
{
    party_trail_rejoin(g);
    set_field_trail_movement_x(g, 0, 0);
    set_field_trail_movement_y(g, 0, 0);
    set_field_trail_walk_x(g, 0, 0);
    set_field_trail_walk_y(g, 0, 0);
}

// $C0:0384
void party_trail_catch_up(GameState *g)
{
    uint8_t newest = place_on_trail(g, PARTY_LEADER);
    set_place_on_trail(g, PARTY_FIRST_MEMBER, newest);
    set_place_on_trail(g, PARTY_SECOND_MEMBER, newest);
}

void party_place_layered(GameState *g, PartyPlace place, uint8_t upper, uint8_t lower)
{
    FieldLink link = party_member_link(g, place);
    if (field_link_is_object(link))
        object_set_sprite_priorities(g, field_linked_object(link), upper, lower);
}
