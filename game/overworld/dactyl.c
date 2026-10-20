/* The dactyls on the 65,000,000 B.C. map: how the party flies over the world
 * of that period, each of the party on a dactyl of its own.
 *
 * - **The leader's dactyl** (overworld_fly_the_dactyl) is the vehicle. It
 *   stands where the party left it; the party walks up and boards it
 *   (vehicles.c), and once all are aboard it takes off and flies as the Epoch
 *   flies (vehicles.c, "In the air"), the map scrolling under it: its
 *   buttons ask the main loop for the menus or the map view, and the confirm
 *   button brings it down where the ground under it is open, the Epoch isn't
 *   parked there, and it isn't over the part of the map no vehicle comes
 *   down in. It can't fly over the highest walls. Each turn it makes turns
 *   the way the party will arrive facing (the location keeps it), which is
 *   how the others know which way it flies.
 * - **The second and third members' dactyls** (overworld_fly_a_following_dactyl)
 *   stand with it and take off with it, drifting apart; in the air they fly
 *   in a V behind it. Every fourth frame each heads anew for its place in the
 *   V (overworld_data.h's formation places), at up to two pixels a frame,
 *   slowing as it comes up to it so as not to pass it, and facing the way it
 *   heads. As the leader's comes down each glides onto the leader's spot in
 *   the time the leader takes to land, and stands there with it.
 * - **Each dactyl's second sprite** (overworld_draw_the_dactyl_s_second_sprite,
 *   overworld_draw_a_following_dactyl_s_second_sprite) is an object of its
 *   own: above the dactyl while it stands, below it in flight.
 *
 * Each dactyl is drawn in its rider's palette. Their state is kept in their
 * slots (menu/object.h), the leader's dactyl's spot and state beside the
 * Epoch's (overworld_vehicle_spot), the others' spots in
 * overworld_following_dactyls, which their second sprites follow. The
 * original works the following dactyls' headings and glides out in the
 * mode's cells and the divider; those it leaves are left once, as
 * scaffolding, where each piece of work ends (the leave_* functions). */
#include "overworld/vehicles.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/menu_data.h"
#include "menu/sprites.h"
#include "original_effects.h"
#include "vars.h"

/* ---- What they share --------------------------------------------------------------------- */

static bool dactyl_is(const GameState *g, uint8_t bits) { return overworld_vehicle_is(g, VEHICLE_DACTYL, bits); }
static void mark_dactyl(GameState *g, uint8_t bits, bool on) { overworld_mark_vehicle(g, VEHICLE_DACTYL, bits, on); }

/* The party has the leader's dactyl out: it rides it, or comes down in it. */
static bool party_has_it_out(const GameState *g) { return dactyl_is(g, VEHICLE_OUT); }
static bool leader_comes_down(const GameState *g) { return dactyl_is(g, DACTYL_COMING_DOWN); }

static WorldSpot leader_s_spot(const GameState *g) { return overworld_vehicle_spot(g, VEHICLE_DACTYL); }

static WorldSpot place(const GameState *g, MenuObject self) { return overworld_walker_spot(g, self); }

static void put(GameState *g, MenuObject self, WorldSpot at, int16_t below)
{
    overworld_put_walker(g, self, (WorldSpot){ at.x, (uint16_t)(at.y + below) });
}

/* Placed on a spot afresh: no fraction of a pixel, no tiles of its own. */
static void place_afresh(GameState *g, MenuObject self, WorldSpot at)
{
    menu_object_set_place(g, self, (MenuPlace){ (uint32_t)at.x << 16, (uint32_t)at.y << 16 });
    set_menu_object_slots_tiles(g, self, 0);
}

/* Come to rest on a whole pixel, not moving. */
static void settle(GameState *g, MenuObject self)
{
    set_menu_object_slots_x_fraction(g, self, 0);
    set_menu_object_slots_y_fraction(g, self, 0);
    overworld_halt(g, self);
}

static void set_step(GameState *g, MenuObject self, uint8_t step) { overworld_set_step(g, self, step); }

static uint8_t frames_on(const GameState *g, MenuObject self) { return menu_object_slots_frames_since_step(g, self); }
static void start_counting(GameState *g, MenuObject self) { set_menu_object_slots_frames_since_step(g, self, 0); }

static Facing facing(const GameState *g, MenuObject self) { return (Facing)menu_object_slots_facing(g, self); }

static void give_animation(GameState *g, MenuObject self, uint8_t animation)
{
    menu_give_sprite_script(g, self, animation);
}

static void animate(GameState *g) { overworld_animate(g); }

/* A dactyl is drawn in its rider's palette, under the party standing and
 * over it in the air; its second sprite over the party, in the first
 * palette. */
static void draw(GameState *g, MenuObject self, PartyPlace rider, bool over_the_party)
{
    overworld_draw_vehicle(g, self, overworld_member_palette(rider), over_the_party);
}

static void raise_over_the_party(GameState *g, MenuObject self)
{
    set_menu_object_slots_look(g, self, (uint8_t)(menu_object_slots_look(g, self) | OVERWORLD_PRIORITY_3));
}

static void lower_under_the_party(GameState *g, MenuObject self)
{
    uint8_t look = menu_object_slots_look(g, self);
    set_menu_object_slots_look(g, self, (uint8_t)((look & ~MENU_LOOK_PRIORITY) | OVERWORLD_PRIORITY_2));
}

enum { SECOND_SPRITE_LOOK = MENU_LOOK_ON_THE_MAP | OVERWORLD_PRIORITY_3 };

/* ---- The leader's dactyl ------------------------------------------------------------------- */

/* The ground under it: the four tiles round a spot, as a walker's feet stand
 * (it never looks at the two below them, which its test for open ground
 * reads as its slot holds them). */
static void note_the_ground(GameState *g, MenuObject self, WorldSpot at)
{
    overworld_note_the_ground_under(g, VEHICLE_DACTYL, self, at);
}

// $C2:4CDA
/* As the map starts it is placed where it stands, facing down: on the
 * ground, or in the air where the party has it out. */
static void set_down(GameState *g, MenuObject self)
{
    bool out = party_has_it_out(g);
    place_afresh(g, self, leader_s_spot(g));
    set_menu_object_slots_facing(g, self, FACING_DOWN);
    set_menu_object_slots_facing_before(g, self, FACING_DOWN);
    set_step(g, self, out ? DACTYL_FLIES : DACTYL_WAITS_FOR_THE_PARTY);
    draw(g, self, PARTY_LEADER, out);
    overworld_set_vehicle_flying(g, VEHICLE_DACTYL, out);
    give_animation(g, self, out ? overworld_dactyl_flying_animation(g, FACING_DOWN) : DACTYL_STANDS);
    animate(g);
}

// $C2:4D79
static void rise(GameState *g, MenuObject self)
{
    if (!overworld_vehicle_rises(g, VEHICLE_DACTYL, self))
        overworld_vehicle_takes_to_the_air(g, VEHICLE_DACTYL, self);
}

// $C2:4D37
/* Standing, it waits for the party to board; all aboard, it takes off. */
static void wait_for_the_party(GameState *g, MenuObject self)
{
    if (overworld_party_travel(g) != TRAVEL_ON_A_DACTYL) {
        animate(g);
        return;
    }
    mark_dactyl(g, VEHICLE_OUT, true);
    set_step(g, self, DACTYL_RISES);
    note_the_ground(g, self, place(g, self));
    overworld_vehicle_climb(g, VEHICLE_DACTYL, self, -1);
    give_animation(g, self, DACTYL_TAKES_OFF);
    rise(g, self);
}

// $C2:4CC5
void overworld_fly_the_dactyl(GameState *g, MenuObject self)
{
    switch ((DactylStep)overworld_step(g, self)) {
    case DACTYL_SETS_DOWN: set_down(g, self); break;
    case DACTYL_WAITS_FOR_THE_PARTY: wait_for_the_party(g, self); break;
    case DACTYL_RISES: rise(g, self); break;
    case DACTYL_FLIES: overworld_vehicle_flies(g, VEHICLE_DACTYL, self); break;
    case DACTYL_FLIES_A_TILE: overworld_vehicle_flies_a_tile(g, VEHICLE_DACTYL, self); break;
    default: overworld_vehicle_comes_down(g, VEHICLE_DACTYL, self); break;
    }
}

/* ---- The second and third members' dactyls --------------------------------------------------- */

/* Whose a following dactyl is: the second member's (0) or the third's (1),
 * from the slot's party place. */
static unsigned follower(const GameState *g, MenuObject self)
{
    return (unsigned)(menu_object_slots_party_place(g, self) - PARTY_FIRST_MEMBER);
}

static PartyPlace rider(const GameState *g, MenuObject self) { return (PartyPlace)(PARTY_FIRST_MEMBER + follower(g, self)); }

static WorldSpot follower_s_spot(const GameState *g, unsigned f)
{
    return (WorldSpot){ overworld_following_dactyls_x(g, f), overworld_following_dactyls_y(g, f) };
}

// $C2:5605
/* Where it is is where its second sprite follows it to. */
static void keep_its_spot(GameState *g, MenuObject self)
{
    WorldSpot at = place(g, self);
    unsigned f = follower(g, self);
    set_overworld_following_dactyls_x(g, f, at.x);
    set_overworld_following_dactyls_y(g, f, at.y);
}

static uint8_t takes_off(const GameState *g, MenuObject self)
{
    return follower(g, self) == 0 ? SECOND_S_DACTYL_TAKES_OFF : THIRD_S_DACTYL_TAKES_OFF;
}

/* Taking off, the second member's drifts right and the third's left. */
static int8_t drift(const GameState *g, MenuObject self) { return follower(g, self) == 0 ? 1 : -1; }

enum { HALF_A_PIXEL = MENU_POSITION_UNITS / 2 };

// $C2:5470
/* It shows its flying animation the way it faces, where that is not the way
 * it faced before. The way it faced before is the original's in-band
 * "shown afresh" ($FF) where it is to show it whatever way it faces. */
enum { SHOWN_AFRESH = 0xFF };

static void show_its_way(GameState *g, MenuObject self)
{
    Facing way = facing(g, self);
    if (way != (Facing)menu_object_slots_facing_before(g, self))
        give_animation(g, self, overworld_following_dactyl_flying_animation(g, follower(g, self), way));
}

static void show_its_way_afresh(GameState *g, MenuObject self)
{
    set_menu_object_slots_facing_before(g, self, SHOWN_AFRESH);
    show_its_way(g, self);
}

static void turn(GameState *g, MenuObject self, Facing way)
{
    set_menu_object_slots_facing_before(g, self, facing(g, self));
    set_menu_object_slots_facing(g, self, way);
    show_its_way(g, self);
}

// $C2:531D
/* Its place in the V: beside and behind the leader's dactyl by the way the
 * leader flies, on the grid of 8x8 tiles, round the map. */
static WorldSpot place_in_the_formation(const GameState *g, MenuObject self)
{
    enum { ON_THE_GRID = (uint16_t)~(TILE_PIXELS - 1) };
    uint8_t way = overworld_arrival_facing(g);
    unsigned f = follower(g, self);
    WorldSpot leader = leader_s_spot(g);
    int16_t x = (int16_t)((leader.x + overworld_formation_place(g, way, f, MENU_ACROSS)) & ON_THE_GRID);
    if (x < 0)
        x = (int16_t)(x + MENU_MAP_WIDTH);
    else if (x >= MENU_MAP_WIDTH)
        x = (int16_t)(x - MENU_MAP_WIDTH);
    uint16_t y = (uint16_t)((leader.y + overworld_formation_place(g, way, f, MENU_DOWN)) & (MENU_MAP_HEIGHT - TILE_PIXELS));
    return (WorldSpot){ (uint16_t)x, y };
}

/* How far, and which way, `to` lies from `from` along an axis the map is
 * `round` long, the shorter way round. */
static int16_t offset_round(uint16_t from, uint16_t to, uint16_t round)
{
    int16_t d = (int16_t)(to - from);
    uint16_t far = (uint16_t)(d < 0 ? -d : d);
    if (far >= round / 2)
        d = (int16_t)(d < 0 ? d + round : d - round);
    return d;
}

static uint16_t magnitude(int16_t d) { return (uint16_t)(d < 0 ? -d : d); }

/* The heading's table goes a quarter turn from across to down
 * (menu_arctangent); the headings round the turn take it from either axis. */
enum { HALF_TURN = MENU_OBJECT_TURN / 2 };

typedef struct {
    bool there;      /* within the table's first step of it both ways */
    uint8_t angle;   /* the way to it, in MENU_OBJECT_TURN steps from across */
} Heading;

static uint8_t arctangent_between(const GameState *g, int16_t across, int16_t down)
{
    return menu_arctangent(g, magnitude(down) / MENU_ARCTANGENT_STEP, magnitude(across) / MENU_ARCTANGENT_STEP);
}

// $C2:229D
/* The way from one spot to another, round the map. */
static Heading heading(const GameState *g, WorldSpot from, WorldSpot to)
{
    int16_t across = offset_round(from.x, to.x, MENU_MAP_WIDTH), down = offset_round(from.y, to.y, MENU_MAP_HEIGHT);
    uint8_t t = arctangent_between(g, across, down);
    uint8_t angle;
    if (across > 0)
        angle = down > 0 ? t : (uint8_t)(0u - t);
    else
        angle = down > 0 ? (uint8_t)(HALF_TURN - t) : (uint8_t)(HALF_TURN + t);
    bool there = magnitude(across) < MENU_ARCTANGENT_STEP && magnitude(down) < MENU_ARCTANGENT_STEP;
    return (Heading){ there, angle };
}

/* Up to two pixels a frame along the heading: the sine's 128 is two pixels. */
enum { TOP_SPEED = 2, SINE_PEAK = 128, SPEED_UNIT = TOP_SPEED * MENU_POSITION_UNITS / SINE_PEAK };

static void set_off_along(GameState *g, MenuObject self, uint8_t angle)
{
    int32_t across = menu_object_cosine(g, angle) * SPEED_UNIT, down = menu_object_sine(g, angle) * SPEED_UNIT;
    menu_object_set_velocity(g, self, (MenuPlace){ (uint32_t)across, (uint32_t)down });
}

/* The way from the place to the dactyl as the original keeps it, turned
 * round where the place is nearer the other way round the map. */
static uint16_t kept_way_back(uint16_t from, uint16_t to, uint16_t round)
{
    uint16_t back = (uint16_t)(from - to);
    return magnitude((int16_t)back) >= round / 2 ? (uint16_t)(0u - back) : back;
}

/* What working the heading out leaves in the mode's cells: the way back
 * from the place down, how far down it is the shorter way, the way back
 * across, the table's angle, the heading, where the dactyl is down and the
 * place. */
static void leave_heading(GameState *g, MenuObject self, Heading h)
{
    enum { DOWN = 0x00, HOW_FAR_DOWN = 0x02, ACROSS = 0x04, TABLE_ANGLE = 0x06, ANGLE = 0x08, WHERE_DOWN = 0x0A,
           PLACE_ACROSS = 0x0C, PLACE_DOWN = 0x0E };
    WorldSpot at = place(g, self), goal = overworld_goal(g, self);
    int16_t across = offset_round(at.x, goal.x, MENU_MAP_WIDTH), down = offset_round(at.y, goal.y, MENU_MAP_HEIGHT);
    menu_object_leave_cell16(g, DOWN, kept_way_back(at.y, goal.y, MENU_MAP_HEIGHT));
    menu_object_leave_cell16(g, HOW_FAR_DOWN, magnitude(down));
    menu_object_leave_cell16(g, ACROSS, kept_way_back(at.x, goal.x, MENU_MAP_WIDTH));
    menu_object_leave_cell16(g, TABLE_ANGLE, arctangent_between(g, across, down));
    menu_object_leave_cell16(g, ANGLE, h.angle);
    menu_object_leave_cell16(g, WHERE_DOWN, at.y);
    menu_object_leave_cell16(g, PLACE_ACROSS, goal.x);
    menu_object_leave_cell16(g, PLACE_DOWN, goal.y);
}

/* How far a spot lies from another along an axis, as the following dactyls
 * measure it slowing down: the shorter way round, though across they count
 * the way round as if the map were MENU_MAP_HEIGHT long. Near the edge where
 * the map comes round, across, they don't slow. */
static uint16_t distance_slowing(uint16_t from, uint16_t to, uint16_t half)
{
    uint16_t far = magnitude((int16_t)(to - from));
    return far >= half ? (uint16_t)(MENU_MAP_HEIGHT - far) : far;
}

/* Where it is fewer whole pixels from its place than it moves a frame, it
 * moves just that far. */
static uint32_t slowed(uint32_t velocity, uint16_t distance)
{
    bool back = (int32_t)velocity < 0;
    uint16_t pixels = (uint16_t)((back ? 0u - velocity : velocity) >> 16);
    if (distance >= pixels)
        return velocity;
    uint32_t v = (uint32_t)distance << 16;
    return back ? 0u - v : v;
}

/* What slowing leaves in the mode's cells: the whole pixels it moved down a
 * frame, and whether up. */
static void leave_slowing(GameState *g, uint32_t down)
{
    enum { PIXELS = 0x08, BACK = 0x0A };
    bool back = (int32_t)down < 0;
    menu_object_leave_cell16(g, PIXELS, (uint16_t)((back ? 0u - down : down) >> 16));
    menu_object_leave_cell16(g, BACK, back);
}

// $C2:528A
/* Coming up to its place it slows so as not to pass it. */
static void slow_so_as_not_to_pass(GameState *g, MenuObject self)
{
    MenuPlace v = menu_object_velocity(g, self);
    WorldSpot at = place(g, self), goal = overworld_goal(g, self);
    uint32_t down = v.y;
    v.x = slowed(v.x, distance_slowing(at.x, goal.x, MENU_MAP_WIDTH / 2));
    v.y = slowed(v.y, distance_slowing(at.y, goal.y, MENU_MAP_HEIGHT / 2));
    menu_object_set_velocity(g, self, v);
    ORIGINAL_EFFECT(leave_slowing(g, down));
}

/* Gliding down onto a spot it comes there in the frames the leader takes to
 * land, its speed worked out in 256ths of a pixel. */
enum { GLIDE_FRAMES = 0x30, GLIDE_UNIT = 256 };

static uint32_t glide(int16_t distance)
{
    uint32_t v = (uint32_t)(magnitude(distance) * GLIDE_UNIT / GLIDE_FRAMES) << 8;
    return distance < 0 ? 0u - v : v;
}

/* What the glide leaves: the way to the spot across and down, and the
 * divider as it worked the speed down out. */
static void leave_glide(GameState *g, int16_t across, int16_t down)
{
    enum { ACROSS = 0x08, DOWN = 0x0A };
    menu_object_leave_cell16(g, ACROSS, (uint16_t)across);
    menu_object_leave_cell16(g, DOWN, (uint16_t)down);
    hw_set_dividend(g, (uint16_t)(magnitude(down) * GLIDE_UNIT));
    hw_divide_by(g, GLIDE_FRAMES);
}

// $C2:53D3
static void glide_to(GameState *g, MenuObject self, WorldSpot spot)
{
    overworld_set_goal(g, self, spot);
    WorldSpot at = place(g, self);
    int16_t across = (int16_t)(spot.x - at.x), down = (int16_t)(spot.y - at.y);
    menu_object_set_velocity(g, self, (MenuPlace){ glide(across), glide(down) });
    ORIGINAL_EFFECT(leave_glide(g, across, down));
}

// $C2:50AC
/* As the map starts it is placed on the leader's spot, facing down: on the
 * ground, or in the air where the party has the leader's out. */
static void follower_sets_down(GameState *g, MenuObject self)
{
    place_afresh(g, self, leader_s_spot(g));
    set_menu_object_slots_facing(g, self, FACING_DOWN);
    set_menu_object_slots_facing_before(g, self, FACING_DOWN);
    draw(g, self, rider(g, self), false);
    if (!party_has_it_out(g)) {
        set_step(g, self, FOLLOWER_WAITS_FOR_THE_PARTY);
        give_animation(g, self, DACTYL_STANDS);
        return;
    }
    set_step(g, self, FOLLOWER_HEADS_FOR_ITS_PLACE);
    raise_over_the_party(g, self);
    show_its_way_afresh(g, self);
    keep_its_spot(g, self);
    animate(g);
}

// $C2:5227
/* A frame towards its place. */
static void fly_a_frame_to_its_place(GameState *g, MenuObject self)
{
    slow_so_as_not_to_pass(g, self);
    overworld_walk_a_frame(g, self);
    keep_its_spot(g, self);
    animate(g);
}

/* Gliding down, the original takes the way it faced before from the mode's
 * cell where it meant its own slot's, which holds a byte of the map's
 * scroll. Nothing reads it: it is set again (shown afresh as it takes off,
 * or facing down as the map starts) before the dactyl next shows its way. */
static void leave_a_stray_way_faced(GameState *g, MenuObject self)
{
    set_menu_object_slots_facing_before(g, self, (uint8_t)menu_object_shadows_bg2_scroll_y(g));
}

// $C2:51AC
/* The leader's comes down: it glides down onto the leader's spot. */
static void glide_down_after_the_leader(GameState *g, MenuObject self)
{
    set_step(g, self, FOLLOWER_COMES_DOWN);
    start_counting(g, self);
    ORIGINAL_EFFECT(set_menu_object_slots_dactyl_landing_cleared(g, self, 0));
    put(g, self, place(g, self), -VEHICLE_HEIGHT_IN_THE_AIR);
    glide_to(g, self, leader_s_spot(g));
    ORIGINAL_EFFECT(leave_a_stray_way_faced(g, self));
    set_menu_object_slots_facing(g, self, FACING_DOWN);
    give_animation(g, self, takes_off(g, self));
    animate(g);
}

// $C2:51A5, $C2:5374, $C2:545C
/* It heads for its place in the V, facing the way it heads; there already,
 * it hangs where it is facing the way the leader flies. */
static void head_for_its_place(GameState *g, MenuObject self)
{
    if (leader_comes_down(g)) {
        glide_down_after_the_leader(g, self);
        return;
    }
    overworld_set_goal(g, self, place_in_the_formation(g, self));
    overworld_halt(g, self);
    Heading h = heading(g, place(g, self), overworld_goal(g, self));
    if (h.there) {
        turn(g, self, overworld_arrival_facing(g));
    } else {
        set_off_along(g, self, h.angle);
        turn(g, self, (Facing)menu_facing_of_angle(g, h.angle));
    }
    ORIGINAL_EFFECT(leave_heading(g, self, h));
    set_step(g, self, FOLLOWER_FLIES_TO_ITS_PLACE);
    start_counting(g, self);
    fly_a_frame_to_its_place(g, self);
}

/* It heads anew every few frames. */
enum { FRAMES_A_HEADING = 4 };

static void fly_to_its_place(GameState *g, MenuObject self)
{
    if (frames_on(g, self) < FRAMES_A_HEADING) {
        fly_a_frame_to_its_place(g, self);
        return;
    }
    set_step(g, self, FOLLOWER_HEADS_FOR_ITS_PLACE);
    head_for_its_place(g, self);
}

// $C2:514C
/* Rising with the leader's, drifting aside; risen, it heads for its place. */
static void follower_rises(GameState *g, MenuObject self)
{
    if (frames_on(g, self) == VEHICLE_OVER_THE_PARTY_AFTER)
        raise_over_the_party(g, self);
    if (frames_on(g, self) < VEHICLE_RISING_FRAMES) {
        overworld_walk_a_frame(g, self);
        keep_its_spot(g, self);
        animate(g);
        return;
    }
    set_step(g, self, FOLLOWER_HEADS_FOR_ITS_PLACE);
    raise_over_the_party(g, self);
    put(g, self, place(g, self), VEHICLE_HEIGHT_IN_THE_AIR);
    overworld_vehicle_stop(g, self);
    show_its_way_afresh(g, self);
    head_for_its_place(g, self);
}

// $C2:5110
/* Standing, it waits for the party to board the leader's; then it takes off,
 * a pixel a frame up. */
static void follower_waits(GameState *g, MenuObject self)
{
    if (overworld_party_travel(g) != TRAVEL_ON_A_DACTYL)
        return;
    set_step(g, self, FOLLOWER_RISES);
    start_counting(g, self);
    menu_object_set_velocity(g, self, (MenuPlace){ (uint32_t)(int32_t)(drift(g, self) * HALF_A_PIXEL),
                                                   (uint32_t)-MENU_POSITION_UNITS });
    give_animation(g, self, takes_off(g, self));
    follower_rises(g, self);
}

// $C2:5249
/* Gliding down; down, it stands. */
static void follower_comes_down(GameState *g, MenuObject self)
{
    if (frames_on(g, self) < VEHICLE_COMING_DOWN_FRAMES) {
        overworld_walk_a_frame(g, self);
        keep_its_spot(g, self);
        animate(g);
        return;
    }
    set_step(g, self, FOLLOWER_WAITS_FOR_THE_PARTY);
    lower_under_the_party(g, self);
    settle(g, self);
    animate(g);
}

// $C2:5097
void overworld_fly_a_following_dactyl(GameState *g, MenuObject self)
{
    switch ((FollowingDactylStep)overworld_step(g, self)) {
    case FOLLOWER_SETS_DOWN: follower_sets_down(g, self); break;
    case FOLLOWER_WAITS_FOR_THE_PARTY: follower_waits(g, self); break;
    case FOLLOWER_RISES: follower_rises(g, self); break;
    case FOLLOWER_HEADS_FOR_ITS_PLACE: head_for_its_place(g, self); break;
    case FOLLOWER_FLIES_TO_ITS_PLACE: fly_to_its_place(g, self); break;
    default: follower_comes_down(g, self); break;
    }
}

/* The heading's table reaches 32 of its steps each way, and the glide's
 * speed is worked out for under 256 pixels: a following dactyl keeps far
 * nearer than that to its place and to the leader's. */
enum { NEAR = 96, NEAR_THE_LEADER = 128 };

static bool near(int16_t across, int16_t down, uint16_t within)
{
    return magnitude(across) < within && magnitude(down) < within;
}

static bool on_the_map(WorldSpot at) { return at.x < MENU_MAP_WIDTH && at.y < MENU_MAP_HEIGHT; }

static bool near_the_leader(const GameState *g, WorldSpot at)
{
    WorldSpot leader = leader_s_spot(g);
    return near((int16_t)(leader.x - at.x), (int16_t)(leader.y - at.y), NEAR_THE_LEADER);
}

bool overworld_following_dactyl_keeps_near(const GameState *g, MenuObject self)
{
    FollowingDactylStep step = (FollowingDactylStep)overworld_step(g, self);
    if (step < FOLLOWER_RISES || step > FOLLOWER_FLIES_TO_ITS_PLACE)
        return true;
    WorldSpot at = place(g, self), goal = place_in_the_formation(g, self);
    if (!on_the_map(at) || !on_the_map(leader_s_spot(g)))
        return false;
    if (leader_comes_down(g))
        return near_the_leader(g, at);
    return near(offset_round(at.x, goal.x, MENU_MAP_WIDTH), offset_round(at.y, goal.y, MENU_MAP_HEIGHT), NEAR);
}

/* ---- The second sprites ------------------------------------------------------------------------ */

enum { SECOND_SPRITE_STARTS = 0, SECOND_SPRITE_FOLLOWS = 1 };

// $C2:504B
/* The leader's dactyl's, which follows it: above it while it stands, below
 * it in flight. */
void overworld_draw_the_dactyl_s_second_sprite(GameState *g, MenuObject self)
{
    if (overworld_step(g, self) == SECOND_SPRITE_STARTS) {
        overworld_set_step(g, self, SECOND_SPRITE_FOLLOWS);
        set_menu_object_slots_look(g, self, SECOND_SPRITE_LOOK);
        set_menu_object_slots_tiles(g, self, 0);
    }
    bool flying = overworld_dactyl_flying(g);
    put(g, self, leader_s_spot(g), overworld_dactyl_second_sprite_below(g, flying));
    give_animation(g, self, overworld_dactyl_second_sprite(g, flying));
    animate(g);
}

/* A following dactyl's, which follows the dactyl of the same party place:
 * above it standing, below it in flight. */
static WorldSpot its_dactyl_s_spot(const GameState *g, MenuObject self) { return follower_s_spot(g, follower(g, self)); }

static void follow_its_dactyl(GameState *g, MenuObject self)
{
    put(g, self, its_dactyl_s_spot(g, self), VEHICLE_HEIGHT_IN_THE_AIR);
    menu_object_wrap_onto_map(g, self);
    animate(g);
}

// $C2:556D
/* In flight it follows its dactyl; as the leader's comes down it glides
 * down to stand above the leader's spot. */
static void second_sprite_follows(GameState *g, MenuObject self)
{
    if (!leader_comes_down(g)) {
        follow_its_dactyl(g, self);
        return;
    }
    set_step(g, self, FOLLOWERS_SPRITE_COMES_DOWN);
    start_counting(g, self);
    WorldSpot leader = leader_s_spot(g);
    put(g, self, place(g, self), -2 * VEHICLE_HEIGHT_IN_THE_AIR);
    glide_to(g, self, (WorldSpot){ leader.x, (uint16_t)(leader.y - VEHICLE_HEIGHT_IN_THE_AIR) });
    give_animation(g, self, VEHICLE_SECOND_SPRITE_STANDING);
    animate(g);
}

// $C2:5542
/* Rising with its dactyl, a half pixel a frame and drifting aside as it
 * does; risen, it follows it. */
static void second_sprite_rises(GameState *g, MenuObject self)
{
    if (frames_on(g, self) < VEHICLE_RISING_FRAMES) {
        overworld_walk_a_frame(g, self);
        animate(g);
        return;
    }
    set_step(g, self, FOLLOWERS_SPRITE_FOLLOWS);
    put(g, self, place(g, self), 2 * VEHICLE_HEIGHT_IN_THE_AIR);
    give_animation(g, self, VEHICLE_SECOND_SPRITE_IN_FLIGHT);
    second_sprite_follows(g, self);
}

// $C2:550E
/* Standing, it waits with its dactyl for the party to board the leader's;
 * then it takes off. */
static void second_sprite_waits(GameState *g, MenuObject self)
{
    if (overworld_party_travel(g) != TRAVEL_ON_A_DACTYL)
        return;
    set_step(g, self, FOLLOWERS_SPRITE_RISES);
    start_counting(g, self);
    menu_object_set_velocity(g, self, (MenuPlace){ (uint32_t)(int32_t)(drift(g, self) * HALF_A_PIXEL),
                                                   (uint32_t)-HALF_A_PIXEL });
    second_sprite_rises(g, self);
}

// $C2:54A7
/* As the map starts it is placed with its dactyl: above the leader's spot on
 * the ground, or below its dactyl where the party has the leader's out. */
static void second_sprite_sets_down(GameState *g, MenuObject self)
{
    set_menu_object_slots_look(g, self, SECOND_SPRITE_LOOK);
    if (party_has_it_out(g)) {
        set_step(g, self, FOLLOWERS_SPRITE_FOLLOWS);
        give_animation(g, self, VEHICLE_SECOND_SPRITE_IN_FLIGHT);
        put(g, self, its_dactyl_s_spot(g, self), 2 * VEHICLE_HEIGHT_IN_THE_AIR);
        set_menu_object_slots_tiles(g, self, 0);
        return;
    }
    set_step(g, self, FOLLOWERS_SPRITE_WAITS_FOR_THE_PARTY);
    put(g, self, leader_s_spot(g), -VEHICLE_HEIGHT_IN_THE_AIR);
    set_menu_object_slots_tiles(g, self, 0);
    give_animation(g, self, VEHICLE_SECOND_SPRITE_STANDING);
    second_sprite_waits(g, self);
}

// $C2:55D1
/* Gliding down; down, it stands. */
static void second_sprite_comes_down(GameState *g, MenuObject self)
{
    if (frames_on(g, self) < VEHICLE_COMING_DOWN_FRAMES) {
        overworld_walk_a_frame(g, self);
        animate(g);
        return;
    }
    set_step(g, self, FOLLOWERS_SPRITE_WAITS_FOR_THE_PARTY);
    settle(g, self);
    animate(g);
}

// $C2:5494
void overworld_draw_a_following_dactyl_s_second_sprite(GameState *g, MenuObject self)
{
    switch ((FollowingSecondSpriteStep)overworld_step(g, self)) {
    case FOLLOWERS_SPRITE_SETS_DOWN: second_sprite_sets_down(g, self); break;
    case FOLLOWERS_SPRITE_WAITS_FOR_THE_PARTY: second_sprite_waits(g, self); break;
    case FOLLOWERS_SPRITE_RISES: second_sprite_rises(g, self); break;
    case FOLLOWERS_SPRITE_FOLLOWS: second_sprite_follows(g, self); break;
    default: second_sprite_comes_down(g, self); break;
    }
}

bool overworld_following_second_sprite_keeps_near(const GameState *g, MenuObject self)
{
    FollowingSecondSpriteStep step = (FollowingSecondSpriteStep)overworld_step(g, self);
    if (!leader_comes_down(g) || (step != FOLLOWERS_SPRITE_RISES && step != FOLLOWERS_SPRITE_FOLLOWS))
        return true;
    return near_the_leader(g, place(g, self));
}
