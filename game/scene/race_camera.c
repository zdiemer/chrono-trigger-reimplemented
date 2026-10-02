/* Scenes: the jet bike race's camera.
 *
 * The camera stands behind the bikes and looks along the track, which is the
 * Mode 7 plane: channel 0's script sets where it is and how it is turned and
 * pitched (race.h names its registers), and these tasks work out what follows.
 * Every frame it moves along the track by its speed, and its position on the
 * plane is its place plus its distance turned by its angle; the flat layers
 * above the horizon scroll across by the angle, BG3 by half of it and BG2 by a
 * quarter, and the horizon's line splits the per-line tables between them and
 * the plane. When the camera's height, angle or pitch changes, the plane's
 * perspective is built again (still the original's, at $7E:357B), and the
 * distances the bikes are scaled by are forgotten.
 *
 * Which way the control pad steers depends on the camera: the pad's directions
 * are given to the lanes by the eighth of a turn it faces. In the attract
 * demo the controller doesn't steer: L and R turn the camera, X and A raise and
 * lower it. After Johnny's win the camera follows him along, and after Crono's
 * it settles back to where it began. */
#include "scene/race.h"

#include "charge.h"
#include "original_effects.h"
#include "platform/input.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/scene_data.h"
#include "vars.h"

/* Compared as the race compares: by the sign of the difference. */
static bool at_least(uint16_t a, uint16_t b) { return (int16_t)(a - b) >= 0; }

/* ---- Turning: the sine ------------------------------------------------------------------------ */

enum { TURN = 0x400, HALF_TURN = 0x200, QUARTER_TURN = 0x100 };

// $7E:3465
int16_t race_sine(const GameState *g, uint16_t angle)
{
    uint16_t step = angle & (TURN - 1);
    int16_t magnitude = scene_sine_magnitude(g, step);
    return step < HALF_TURN ? (int16_t)-magnitude : magnitude;
}

// $7E:3461
int16_t race_cosine(const GameState *g, uint16_t angle) { return race_sine(g, (uint16_t)(angle + QUARTER_TURN)); }

/* ---- The camera moving along ------------------------------------------------------------------ */

/* The plane's rotation center from the view's position: $7F across, $E0 down. */
enum { CENTER_ACROSS = 0x7F, CENTER_DOWN = 0xE0, PLACE_LOOP = TURN - 1 };

// $7E:428B
/* The camera moves along the track by its speed, and the view's position and
 * the plane's rotation center follow it. */
static void move_along(GameState *g)
{
    uint16_t place = (uint16_t)(race_camera(g, CAMERA_PLACE) + race_camera(g, CAMERA_SPEED)) & PLACE_LOOP;
    set_race_camera(g, CAMERA_PLACE, place);
    uint16_t across = (uint16_t)(place + scene_race_camera_offset(g, 0));
    set_scene_race_view_position(g, 0, across);
    set_scene_race_center_x(g, (uint16_t)(across + CENTER_ACROSS));
    uint16_t down = (uint16_t)(race_channel_1(g, CAMERA_SIDE) + scene_race_camera_offset(g, 1));
    set_scene_race_view_position(g, 1, down);
    set_scene_race_center_y(g, (uint16_t)(down + CENTER_DOWN));
}

// $7E:4447
/* The camera's offset from its place, its distance turned by its angle, and
 * what the flat layers and the horizon make of its angle and pitch. */
enum { HORIZON_BASE = 0x17, BG3_HORIZON_LIFT = 2 };

static uint16_t turned_distance(GameState *g, int16_t factor)
{
    uint32_t product = scene_multiply_signed16(g, scene_program_channel(0), (uint16_t)factor, race_camera(g, CAMERA_DISTANCE));
    return (uint16_t)(product >> 8);
}

static void aim_camera(GameState *g)
{
    uint16_t angle = race_camera(g, CAMERA_ANGLE);
    set_scene_race_camera_offset(g, 1, turned_distance(g, race_cosine(g, angle)));
    set_scene_race_camera_offset(g, 0, turned_distance(g, race_sine(g, angle)));
    uint16_t horizon = (uint16_t)(HORIZON_BASE - race_camera(g, CAMERA_PITCH));
    set_scene_race_horizon(g, horizon);
    set_scene_race_bg3_scroll_y(g, (uint16_t)(horizon - BG3_HORIZON_LIFT));
    uint16_t turn = (uint16_t)-angle;
    set_scene_race_plane_turn(g, turn);
    set_scene_race_bg3_scroll_x(g, turn >> 1);
    set_scene_race_bg2_scroll_x(g, turn >> 2);
}

/* After Johnny's win the camera follows the bikes out: the plane turns with
 * how far along the track the camera is, its place and the two bikes' mean
 * distance, and the flat layers scroll by a quarter of that and by three
 * eighths (one more where the quarter's lowest bit, shifted out last, was set). */
enum { FOLLOW_BASE = 0x88 };

// $7E:449C
static void follow_finish(GameState *g)
{
    uint16_t far = (uint16_t)(scene_race_place(g) >> 8) >> 2;
    uint16_t place = race_camera(g, CAMERA_PLACE);
    uint32_t sum = (uint32_t)(place >> 1) + far + (place & 1);
    uint16_t along = (uint16_t)(sum + FOLLOW_BASE + (sum >> 16));
    uint16_t turn = (uint16_t)((uint16_t)-along + scene_race_scenery_drift(g));
    set_scene_race_plane_turn(g, turn);
    set_scene_race_bg2_scroll_x(g, turn >> 2);
    uint16_t quarter = turn >> 2;
    set_scene_race_bg3_scroll_x(g, (uint16_t)((quarter >> 1) + quarter + (quarter & 1)));
    set_scene_call_first(g, scene_program_own(), quarter);
}

/* ---- The horizon ------------------------------------------------------------------------------ */

// $7E:44FE
/* The horizon splits the per-line tables: the lines above it (half the pitch)
 * and the lines from it (the other half, with the odd line), in the tables
 * for the BG mode, the color math and BG1's scroll. Then the road strip goes
 * to VRAM. */
static void set_horizon(GameState *g)
{
    uint8_t pitch = (uint8_t)race_camera(g, CAMERA_PITCH);
    uint8_t above = pitch >> 1, below = (uint8_t)(above + (pitch & 1));
    set_scene_race_line_counts(g, 0, above);
    set_scene_race_math_line_counts(g, 0, above);
    set_scene_race_scroll_line_counts(g, 0, above);
    set_scene_race_line_counts(g, 1, below);
    set_scene_race_math_line_counts(g, 1, below);
    set_scene_race_scroll_line_counts(g, 1, below);
}

/* ---- The perspective --------------------------------------------------------------------------- */

/* The camera the perspective is built for, as scene_race_perspective_camera
 * keeps it. */
static const unsigned perspective_registers[] = { CAMERA_HEIGHT, CAMERA_ANGLE, CAMERA_PITCH, CAMERA_PLANE_TOP, CAMERA_MIRRORED };
enum { PERSPECTIVE_REGISTERS = sizeof perspective_registers / sizeof perspective_registers[0] };

/* The original clears the divisors with a block move of 255 bytes after
 * storing the first word. */
enum { DIVISORS = 0x100, DIVISORS_MOVED = DIVISORS - 1 };

static void forget_divisors(GameState *g)
{
    uint32_t mark = 0;
    ORIGINAL_EFFECT(mark = charge_uncounted_begin(g));
    for (unsigned i = 0; i < DIVISORS; i++)
        set_scene_race_divisors(g, i, 0);
    ORIGINAL_EFFECT(charge_uncounted_end(g, mark));
    ORIGINAL_EFFECT(charge_block_move(g, DIVISORS_MOVED));
}

// $7E:42CE
/* Whether the camera has changed since the perspective was last built; if it
 * has, it is kept as the perspective's, and a new height forgets the bikes'
 * divisors. */
static bool camera_changed(GameState *g)
{
    unsigned first = PERSPECTIVE_REGISTERS;
    for (unsigned i = 0; i < PERSPECTIVE_REGISTERS && first == PERSPECTIVE_REGISTERS; i++)
        if (race_camera(g, perspective_registers[i]) != scene_race_perspective_camera(g, i))
            first = i;
    if (first == PERSPECTIVE_REGISTERS)
        return false;
    if (first == 0) {
        set_scene_race_perspective_camera(g, 0, race_camera(g, CAMERA_HEIGHT));
        forget_divisors(g);
    }
    for (unsigned i = first ? first : 1; i < PERSPECTIVE_REGISTERS; i++)
        set_scene_race_perspective_camera(g, i, race_camera(g, perspective_registers[i]));
    return true;
}

/* ---- The camera in the demo --------------------------------------------------------------------- */

/* How fast the camera turns: building up a step a frame while L or R is held,
 * to at most 12, and dying away a step a frame; the camera turns by a quarter
 * of it. */
enum { TURNING_MAX = 12, TURNING_SHIFT = 2 };

static uint16_t ramp(uint16_t turning, bool held)
{
    if (held) {
        uint16_t faster = (uint16_t)(turning + 1);
        return at_least(faster, TURNING_MAX) ? TURNING_MAX : faster;
    }
    return turning != 0 ? (uint16_t)(turning - 1) : 0;
}

/* The pad's directions, given to the lanes by the eighth of a turn the
 * camera faces. */
enum { EIGHTH_SHIFT = 7, HALF_EIGHTH = 0x40, EIGHTHS = 8 };

static void give_directions(GameState *g)
{
    unsigned eighth = (uint16_t)(race_camera(g, CAMERA_ANGLE) + HALF_EIGHTH) >> EIGHTH_SHIFT & (EIGHTHS - 1);
    set_scene_race_steer_buttons(g, 0, scene_race_steering_by_view(g, eighth));
    set_scene_race_steer_buttons(g, 1, scene_race_steering_by_view(g, EIGHTHS + eighth));
}

/* X raises the camera and A lowers it, a step every other frame, between 96
 * and 127 high: its distance and pitch go with it. */
enum { HIGHEST = 0x7F, LOWEST = 0x60 };

static bool odd_frame(const GameState *g) { return scene_frame_counter(g) & 1; }


static void raise_camera(GameState *g, int16_t by)
{
    set_race_camera(g, CAMERA_DISTANCE, (uint16_t)(race_camera(g, CAMERA_DISTANCE) + by));
    set_race_camera(g, CAMERA_HEIGHT, (uint16_t)(race_camera(g, CAMERA_HEIGHT) + by));
    set_race_camera(g, CAMERA_PITCH, (uint16_t)(race_camera(g, CAMERA_PITCH) + by));
}

static bool raising(const GameState *g) { return (scene_race_buttons(g) & IN_BUTTON_X) && odd_frame(g); }
static bool lowering(const GameState *g) { return (scene_race_buttons(g) & IN_BUTTON_A) && odd_frame(g); }

// $7E:4339
/* The pad's directions for the way the camera faces; in the demo, first the
 * shoulder buttons' turn of the camera, and after, the camera's height. */
static void turn_camera(GameState *g)
{
    if (scene_race_demo(g) != 0) {
        PadBits pad = scene_race_buttons(g);
        uint16_t one_way = ramp(scene_race_turning(g, 0), pad & IN_BUTTON_R);
        uint16_t other_way = ramp(scene_race_turning(g, 1), pad & IN_BUTTON_L);
        set_scene_race_turning(g, 0, one_way);
        set_scene_race_turning(g, 1, other_way);
        SceneProgramPage camera = scene_program_channel(0);
        set_scene_call_first(g, camera, one_way >> TURNING_SHIFT);
        set_scene_call_second(g, camera, other_way >> TURNING_SHIFT);
        set_race_camera(g, CAMERA_ANGLE, (uint16_t)(race_camera(g, CAMERA_ANGLE) + (one_way >> TURNING_SHIFT) -
                                                   (other_way >> TURNING_SHIFT)));
    }
    give_directions(g);
    uint16_t height = race_camera(g, CAMERA_HEIGHT);
    if (raising(g)) {
        if (!at_least(height, HIGHEST))
            raise_camera(g, 1);
        return;
    }
    if (lowering(g) && at_least(height, LOWEST))
        raise_camera(g, -1);
}

/* What the camera's height control leaves in A, from the controller, the
 * frame and the height before it ran: the height where it tried a step, the
 * frame count halved where A waited for the odd frame, the controller where
 * neither button was acted on. */
static uint16_t left_by_height_control(PadBits pad, uint16_t frame, uint16_t height)
{
    if ((pad & IN_BUTTON_X) && (frame & 1))
        return height;
    if (!(pad & IN_BUTTON_A))
        return pad;
    if (!(frame & 1))
        return frame >> 1;
    return height;
}

/* ---- After Crono's finish ------------------------------------------------------------------- */

/* The camera settles back, a step every other pair of frames: its height to
 * $A0, its pitch to $38 and its distance to $90. */
enum { SETTLED_HEIGHT = 0xA0, SETTLED_PITCH = 0x38, SETTLED_DISTANCE = 0x90, SETTLE_PAIRS = 2 };

static void settle(GameState *g, unsigned reg, uint16_t to)
{
    uint16_t at = race_camera(g, reg);
    if (at != to)
        set_race_camera(g, reg, at_least(at, to) ? (uint16_t)(at - 1) : (uint16_t)(at + 1));
}

// $7E:416D
static bool settle_camera(GameState *g)
{
    if (scene_frame_counter(g) & SETTLE_PAIRS)
        return false;
    settle(g, CAMERA_HEIGHT, SETTLED_HEIGHT);
    settle(g, CAMERA_PITCH, SETTLED_PITCH);
    settle(g, CAMERA_DISTANCE, SETTLED_DISTANCE);
    return true;
}

/* ---- The controller ---------------------------------------------------------------------------- */

// $7E:42BD
/* The race keeps the controller of this frame and the last. The original
 * rotates the three words, which leaves the scene's copy of the controller
 * holding the one from two frames ago until the next read; and the controller
 * passes through the arithmetic's first cell on the way. */
static void keep_controller(GameState *g)
{
    PadBits now = scene_joypad(g);
    set_scene_call_first(g, scene_program_own(), now);
    set_scene_joypad(g, scene_race_buttons_before(g));
    set_scene_race_buttons_before(g, scene_race_buttons(g));
    set_scene_race_buttons(g, now);
}

/* ---- The opening ------------------------------------------------------------------------------- */

// $7E:3448
/* The next of the game's random bytes ($C0:FE00), for the scene program. */
static uint8_t random_byte(GameState *g)
{
    uint8_t step = (uint8_t)(scene_random_step(g) + 1);
    set_scene_random_step(g, step);
    return scene_random_byte(g, step);
}

// $7E:3F59
/* Channel 0's script calls this to pick one of the race's two openings at
 * random: where the script goes on. */
enum { OPENING_PICK = 2 };

static void pick_opening(GameState *g)
{
    unsigned which = (random_byte(g) & OPENING_PICK) / OPENING_PICK;
    set_scene_channels_script(g, 0, scene_race_openings(g, which));
}

/* ---- Entries ---------------------------------------------------------------------------------- */

/* The camera's tasks are guarded by hashes of their code, on the scene's page
 * with a data bank reaching low RAM; those that use the sine, its code too. */
static bool on_scene(Snes *s) { return scene_on_scene_page_low_bank(s); }
static bool sine_present(Snes *s) { return m_code_matches(s, 0x7E3461, 0x19, 0xA2486F18B991C163ull); }

bool scene_race_settle_camera_present(Snes *s) { return on_scene(s) && m_code_matches(s, 0x7E416D, 0x39, 0x8A6D62B6310A5059ull); }
bool scene_race_move_along_present(Snes *s) { return on_scene(s) && m_code_matches(s, 0x7E428B, 0x32, 0x89E4300D2BB717EAull); }
bool scene_race_keep_controller_present(Snes *s) { return on_scene(s) && m_code_matches(s, 0x7E42BD, 0x11, 0x4BD0D004BE797B4Full); }
bool scene_race_check_perspective_present(Snes *s) { return on_scene(s) && m_code_matches(s, 0x7E42CE, 0x4B, 0x9F76AD7C9F8AFB23ull); }
bool scene_race_turn_camera_present(Snes *s) { return on_scene(s) && m_code_matches(s, 0x7E4339, 0xB9, 0x3E2508299E81D23Full); }

/* The camera's turn multiplies on channel 0's page, through the program's
 * signed multiply. */
bool scene_race_aim_camera_present(Snes *s)
{
    return on_scene(s) && sine_present(s) && m_code_matches(s, 0x7E4447, 0x55, 0x5596D8A2F4B871F1ull) &&
           m_code_matches(s, 0x7E33D6, 0x37, 0xCB3F0FAD1C9F8EA9ull) && m_code_matches(s, 0x7E31BF, 74, 0x479627EBF1311CAFull);
}

bool scene_race_follow_finish_present(Snes *s) { return on_scene(s) && m_code_matches(s, 0x7E449C, 0x32, 0x66215BBFE736EED8ull); }
bool scene_race_set_horizon_present(Snes *s)
{
    return on_scene(s) && m_code_matches(s, 0x7E44FE, 0x21, 0x7880D3E3796835BFull) && scene_race_upload_strip_present(s);
}

/* Called by channel 0's script, on its page, with the data bank on the
 * program's (where its table of openings is). */
bool scene_race_pick_opening_present(Snes *s)
{
    SceneChannel ch;
    return scene_channel_at(m_direct_page(s), &ch) && ch == 0 && s->cpu.db == SCENE_LOWER_WORK_RAM &&
           m_code_matches(s, 0x7E3F59, 0x0D, 0xFEC1A17AFFF3EF75ull) && m_code_matches(s, 0x7E3448, 0x19, 0x092856CFEC289766ull);
}

/* The tasks below set the direct page on channel 0's record for their work,
 * which the list's code puts back; A's high byte, which the list's code may
 * hand on, is what each leaves there last. */
enum { CHANNEL_0_PAGE = 0x0A00 };

void scene_race_settle_camera(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t distance_before = race_camera(g, CAMERA_DISTANCE);
    uint16_t frame = scene_frame_counter(g);
    bool settled = settle_camera(g);
    ORIGINAL_EFFECT(s->cpu.a = settled ? distance_before : (uint16_t)(frame >> 2));
    m_rts(s);
}

void scene_race_move_along(Snes *s)
{
    move_along(state_of(s));
    ORIGINAL_EFFECT(s->cpu.a = scene_race_center_y(state_of(s)));
    m_rts(s);
}

void scene_race_keep_controller(Snes *s)
{
    GameState *g = state_of(s);
    keep_controller(g);
    ORIGINAL_EFFECT(s->cpu.a = scene_race_buttons(g));
    m_rts(s);
}

/* The perspective is the original's ($7E:357B), on channel 0's page; it reads
 * none of the registers but the page. Unchanged, A is the last register
 * compared. */
enum { BUILD_PERSPECTIVE = 0x7E357B };

void scene_race_check_perspective(Snes *s)
{
    GameState *g = state_of(s);
    if (!camera_changed(g)) {
        ORIGINAL_EFFECT(s->cpu.a = race_camera(g, CAMERA_MIRRORED));
        m_rts(s);
        return;
    }
    s->cpu.d = CHANNEL_0_PAGE;
    m_stop_at_own(s, BUILD_PERSPECTIVE, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
}

void scene_race_turn_camera(Snes *s)
{
    GameState *g = state_of(s);
    PadBits pad = scene_race_buttons(g);
    uint16_t frame = scene_frame_counter(g), height = race_camera(g, CAMERA_HEIGHT);
    turn_camera(g);
    ORIGINAL_EFFECT(s->cpu.a = left_by_height_control(pad, frame, height));
    m_rts(s);
}

void scene_race_aim_camera(Snes *s)
{
    GameState *g = state_of(s);
    aim_camera(g);
    ORIGINAL_EFFECT(s->cpu.a = scene_race_bg2_scroll_x(g));
    m_rts(s);
}

/* Vblank tasks: nothing they leave is read but the accumulator's width (the
 * follower works in 16 bits, and returns so). */
void scene_race_follow_finish(Snes *s)
{
    follow_finish(state_of(s));
    m_set_flag(s, CPU_FLAG_M, false);
    m_rts(s);
}

void scene_race_set_horizon(Snes *s)
{
    set_horizon(state_of(s));
    TIMED_AS(state_of(s), 0x7E5261, race_upload_road_strip(m_platform(s)));
    m_rts(s);
}

/* The script's call: what it leaves in A and X isn't read. */
void scene_race_pick_opening(Snes *s)
{
    pick_opening(state_of(s));
    m_rts(s);
}
