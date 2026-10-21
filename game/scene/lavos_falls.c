/* Scenes: Lavos falls to the planet.
 *
 * When the Black Tyrano and Azala are beaten in Tyrano Lair, Azala's last
 * words give way to the scene program's mode 4 (event $FF $24): Lavos, a
 * burning star, comes out of space at the viewer through a field of stars,
 * spinning, then falls away toward the planet, whose edge rises into the
 * corner of the screen.
 *
 * Lavos is a picture on the Mode 7 plane, shown flat (plane.c): channel
 * 0's script brings the camera down toward it and back up, and a frame task
 * turns it a step each frame and works out its matrix, which the vblank sends.
 * The stars are the star field (stars.c), its speed and drift in channel 1's
 * registers: there one frame task turns an angle on and another swings the
 * field round with it, the speed and the drift across the angle's cosine and
 * sine, so the stars sweep past as Lavos turns. As Lavos falls away, the
 * field goes behind the layers and a frame task draws the planet's edge.
 *
 * The setup loads it all: Lavos's picture, its pixels packed two to a byte,
 * spread to Mode 7's byte a pixel by the program's own spread (which moves
 * the stack onto them, docs/TODO.md) and sent into VRAM's high bytes; its map of
 * tiles, 64 to a row, widened to Mode 7's 128 and sent into the low bytes,
 * the rest of the map blank; the planet's sprite tiles, laid out in the order
 * a list gives; the stars' tiles; the colours; then the two channels' scripts
 * started and the stars scattered. */
#include "scene/scene.h"

#include "charge.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

/* ---- Lavos turning ---------------------------------------------------------------------------- */

/* A frame's turn, in the 1024 steps of a whole one. */
enum { TURN_A_FRAME = 0x16 };

// $7E:56A9
/* Lavos turns a step, and the plane's matrix is worked out for it. */
static void turn_lavos(GameState *g)
{
    uint16_t turn = scene_channel_register(g, SCENE_CAMERA_CHANNEL, SCENE_PLANE_TURN);
    set_scene_channel_register(g, SCENE_CAMERA_CHANNEL, SCENE_PLANE_TURN, (uint16_t)(turn + TURN_A_FRAME));
    scene_turn_the_flat_plane(g);
}

/* ---- The stars swinging round ----------------------------------------------------------------- */

/* The angle the field swings round at, in one of channel 1's registers beside
 * the field's speed and drift (scene.h). */
enum { FIELD_ANGLE = 5 };

/* The angle's step a frame, and how far the swing reaches: a 32nd of the
 * sine's 256ths, either way. */
enum { ANGLE_A_FRAME = 2, SWING_SHIFT = 5 };

static uint16_t field(const GameState *g, unsigned reg) { return scene_channel_register(g, SCENE_STAR_FIELD_CHANNEL, reg); }
static void set_field(GameState *g, unsigned reg, uint16_t v) { set_scene_channel_register(g, SCENE_STAR_FIELD_CHANNEL, reg, v); }

// $7E:55F9
static void turn_the_field_s_angle(GameState *g)
{
    uint16_t angle = field(g, FIELD_ANGLE);
    set_field(g, FIELD_ANGLE, (uint16_t)(angle + ANGLE_A_FRAME));
}

// $7E:5855
/* The field's speed the angle's cosine and its drift across the sine, each a
 * 32nd. The program shifts the words as unsigned, so a negative swing's high
 * bits come out clear; the flight takes the low bytes, which are the signed
 * 32nd either way. */
static void swing_the_field(GameState *g)
{
    uint16_t angle = field(g, FIELD_ANGLE);
    set_field(g, SCENE_STAR_FIELD_DRIFT_ACROSS, (uint16_t)((uint16_t)scene_sine(g, angle) >> SWING_SHIFT));
    set_field(g, SCENE_STAR_FIELD_SPEED, (uint16_t)((uint16_t)scene_cosine(g, angle) >> SWING_SHIFT));
}

/* ---- The planet ------------------------------------------------------------------------------- */

/* The planet's edge, a list of sprites placed from the screen's left edge, 112
 * lines down. */
enum { PLANET_EDGE = 0x56FD, PLANET_LEFT = 0x0000, PLANET_TOP = 0x0070 };

// $7E:56EB
static void draw_the_planet(GameState *g)
{
    set_scene_sprite_offset_x(g, PLANET_LEFT);
    set_scene_sprite_offset_y(g, PLANET_TOP);
    set_scene_sprite_offset_y_high(g, 0);
    TIMED_AS(g, 0xC30D5E, scene_draw_sprite_list(g, PLANET_EDGE, true));
}

/* ---- What channel 1's script calls ------------------------------------------------------------ */

/* Channel 0's script, from where Lavos falls away from the viewer and from
 * where it falls to the planet; and the star field's drawing made to draw
 * every star behind the layers (stars.c). */
enum { LAVOS_GOES_BACK = 0x5635, LAVOS_FALLS_TO_THE_PLANET = 0x5604 };

// $7E:564D
static void send_lavos_back(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    set_scene_channels_script(g, SCENE_CAMERA_CHANNEL, LAVOS_GOES_BACK);
}

// $7E:5640
/* The stars go behind the layers, before the planet's edge, and Lavos falls
 * to the planet. */
static void let_lavos_fall(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    set_scene_stars_depth_branch(g, SCENE_EVERY_STAR_BEHIND);
    set_scene_channels_script(g, SCENE_CAMERA_CHANNEL, LAVOS_FALLS_TO_THE_PLANET);
}

/* ---- The setup -------------------------------------------------------------------------------- */

/* The scene's resources: those unpacked by their place in the program's list,
 * as the unpacker takes it (three bytes an entry), and those copied as they
 * are by their number in it. */
enum { LAVOS_PIXELS = 0x2D, LAVOS_MAP = 0x30, PLANET_TILES = 0x42, STAR_TILES = 0x33 };
enum { SPRITE_COLOURS = 18, LAVOS_COLOURS = 20, PLANET_TILE_ORDER = 21 };

/* Where things go: the unpacker's destinations, VRAM's places, the working
 * palette's colours copied to (Lavos's from colour 0, the sprites' from 128). */
enum {
    LOW_RAM_BUFFER = 0x000C00,
    PLANET_TILES_VRAM = 0x4000, STAR_TILES_VRAM = 0x4600, LAVOS_VRAM = 0x0000,
    LAVOS_COLOURS_AT = 0x2200, SPRITE_COLOURS_AT = 0x2300, LAVOS_COLOURS_BYTES = 0x20, SPRITE_COLOURS_BYTES = 0x40,
};

/* The program's unpacker ($7E:3691): the resource's place in its list in X,
 * unpacked into the destination set. It reads X and the overflow. */
enum { UNPACK = 0x7E3691, UNPACK_IGNORES = DC_A | DC_Y | DC_N | DC_Z | DC_C };

static void unpack(Snes *s, uint32_t site, uint16_t resource)
{
    s->cpu.x = resource;
    m_jsr_dc(s, site, UNPACK, UNPACK_IGNORES);
}

/* Lavos's picture comes packed two pixels to a byte, and Mode 7 takes a byte
 * a pixel: the program's spread ($7E:350E) makes each byte two, from where the
 * unpacker left them in low RAM into the unpack buffer, the packed count in Y
 * and twice it in A. It reads the packed bytes by moving the stack onto them,
 * which shadow mode can't follow inside a port's stretch (S above the entry's
 * reads as a return), so it stays the program's (docs/TODO.md). */
enum { SPREAD = 0x7E350E, SPREAD_IGNORES = DC_X | DC_N | DC_V | DC_Z | DC_C };

static void spread_the_pixels(Snes *s, uint16_t packed)
{
    s->cpu.a = (uint16_t)(2 * packed);
    s->cpu.y = packed;
    m_set_flag(s, CPU_FLAG_M, true);
    m_jsr_dc(s, 0x7E53E1, SPREAD, SPREAD_IGNORES);
}

/* The planet's sprite tiles are laid out in VRAM in the order their list
 * gives: each of the list's 96 entries names one of the tiles unpacked, sent
 * whole, sixteen words, to the next place. */
enum { TILES_LAID_OUT = 96, TILE_WORDS = 16 };

// $7E:54EA
static void lay_out_the_planet_tiles(GameState *g, Platform *pf)
{
    for (unsigned n = 0; n < TILES_LAID_OUT; n++) {
        unsigned tile = scene_planet_tile_order(g, n);
        for (unsigned w = 0; w < TILE_WORDS; w++)
            vid_write_vram(pf, PF_AT(0x7E5516), scene_planet_tiles(g, tile * TILE_WORDS + w));
    }
}

/* It reads the list through the work RAM port, which it points at the list
 * in work RAM's lower bank (the port's bank 0), and counts the tiles down in
 * the last word of the second bank. */
enum { WORK_RAM_LOWER_BANK = 0 };

static void leave_the_tiles_laid_out(GameState *g)
{
    hw_port_seek(g, (uint16_t)(RAM_SCENE_PLANET_TILE_ORDER + TILES_LAID_OUT), WORK_RAM_LOWER_BANK);
    set_scene_planet_tiles_left(g, 0);
}

/* Lavos's map: its seven rows of 64 tiles, widened to the plane's 128
 * (screen.c), into the low bytes of VRAM's first words, and the rest of the
 * plane's map the blank tile. */
enum { LAVOS_ROWS = 7, PLANE_ROW_TILES = 128 };
enum { LAVOS_ROWS_BYTES = LAVOS_ROWS * PLANE_ROW_TILES, PLANE_MAP_BYTES = PLANE_ROW_TILES * PLANE_ROW_TILES };

static void send_the_map(Platform *pf)
{
    vid_send_vram_half(pf, PF_AT(0x7E5470), LAVOS_VRAM, PF_GAME(SCENE_PLANE_MAP), LAVOS_ROWS_BYTES, VID_VRAM_LOW_BYTES,
                       SCENE_VRAM_LOW_BYTES);
    scene_blank_past_the_plane(pf, PF_AT(0x7E5476), PLANE_MAP_BYTES - LAVOS_ROWS_BYTES);
}

/* A resource copied as it is into the first bank: the setup moves the
 * unpacker's destination's bank there first. */
static void copy_into_the_first_bank(GameState *g, unsigned resource, uint16_t to, uint16_t bytes)
{
    set_decompress_dest(g, decompress_dest(g) & 0xFFFF);
    scene_copy_resource_to(g, scene_program_own(), resource, to, bytes);
}

/* Lavos's picture: its pixels. */
static void load_lavos(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    vid_start_vram_uploads(pf, PF_AT(0x7E53BD), SCENE_VRAM_WHOLE_WORDS);
    vid_set_vram_address(pf, PF_AT(0x7E53C3), LAVOS_VRAM);
    set_decompress_dest(g, LOW_RAM_BUFFER);
    unpack(s, 0x7E53D2, LAVOS_PIXELS);
    uint16_t packed = decompress_length(g);
    set_decompress_length(g, (uint16_t)(2 * packed));
    spread_the_pixels(s, packed);
    set_decompress_dest(g, SCENE_UNPACK_BUFFER);
    scene_choose_transfer(g, scene_pixels_into_high_bytes);
    scene_send_unpacked_by(g, pf, scene_pixels_into_high_bytes);
}

/* The planet's tiles and the stars'. */
static void load_the_sprites(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    copy_into_the_first_bank(g, PLANET_TILE_ORDER, (uint16_t)LOW_RAM_BUFFER, TILES_LAID_OUT);
    set_decompress_dest(g, SCENE_UNPACK_BUFFER);
    unpack(s, 0x7E541B, PLANET_TILES);
    vid_set_vram_address(pf, PF_AT(0x7E5421), PLANET_TILES_VRAM);
    lay_out_the_planet_tiles(g, pf);
    ORIGINAL_EFFECT(leave_the_tiles_laid_out(g));
    scene_choose_transfer(g, scene_whole_words);
    vid_set_vram_address(pf, PF_AT(0x7E5434), STAR_TILES_VRAM);
    unpack(s, 0x7E543A, STAR_TILES);
    scene_send_unpacked_by(g, pf, scene_whole_words);
}

static void load_lavos_s_map(Snes *s)
{
    GameState *g = state_of(s);
    vid_set_vram_address(m_platform(s), PF_AT(0x7E5444), LAVOS_VRAM);
    unpack(s, 0x7E544A, LAVOS_MAP);
    scene_widen_the_plane_map(g);
    send_the_map(m_platform(s));
}

/* The colours: Lavos's, the sprites' (the planet's and the stars'), and the
 * clear colour black. */
static void load_the_colours(GameState *g)
{
    copy_into_the_first_bank(g, LAVOS_COLOURS, LAVOS_COLOURS_AT, LAVOS_COLOURS_BYTES);
    copy_into_the_first_bank(g, SPRITE_COLOURS, SPRITE_COLOURS_AT, SPRITE_COLOURS_BYTES);
    set_palette_working(g, 0, 0);
}

/* The two channels' scripts: channel 0 brings Lavos in, channel 1 swings the
 * stars and calls the fall. */
enum { LAVOS_SCRIPT = 0x552A, FIELD_SCRIPT = 0x5568 };

/* The script's first step ($7E:3749), called with 16-bit A and the setup's
 * data bank kept under it. It reads Y and the overflow: Y past the sprites'
 * colours, as their copy left it, and the overflow as the setup was entered
 * with it. */
enum { FIRST_STEP = 0x7E3749, FIRST_STEP_IGNORES = DC_A | DC_X | DC_N | DC_Z | DC_C, SCENE_PAGE = 0x0000 };

static void first_script_step(Snes *s)
{
    m_push8(s, s->cpu.db);
    m_set_flag(s, CPU_FLAG_M, false);
    s->cpu.y = (uint16_t)(SPRITE_COLOURS_AT + SPRITE_COLOURS_BYTES);
    m_jsr_dc(s, 0x7E54B4, FIRST_STEP, FIRST_STEP_IGNORES);
    s->cpu.db = m_pull8(s);
    m_set_flag(s, CPU_FLAG_M, true);
    s->cpu.d = SCENE_PAGE;
}

/* The screen until the script brings the plane in: the sprites alone on the
 * main screen, nothing on the sub screen. Then the layers: Mode 7, the plane
 * clear outside its map; BG1's and BG2's maps at $6000 and $6400, BG3's at
 * $6800, all four layers' tiles at $5000 (only the plane shows in Mode 7);
 * color math taking the sub screen; and the screen's fades a step every six
 * frames. */
enum { FADE_FRAMES = 6, LAYER_TILES_VRAM = 0x5000 };

static const VidBg bg1_map = { VID_TILEMAP(0x6000, 32, 32) }, bg2_map = { VID_TILEMAP(0x6400, 32, 32) },
                   bg3_map = { VID_TILEMAP(0x6800, 32, 32) };

static void show_the_sprites_alone(Platform *pf)
{
    vid_set_screens(pf, PF_AT(0x7E54B0), VID_OBJ_BIT, 0, VID_SCREEN_MAIN | VID_SCREEN_SUB);
}

static void set_up_the_layers(GameState *g, Platform *pf)
{
    vid_set_mode(pf, PF_AT(0x7E54C3), &(VidMode){ .mode = 7 });
    vid_set_mode7(pf, PF_AT(0x7E54C8), &(VidMode7){ .outside = VID_M7_TRANSPARENT }, VID_M7_SELECT);
    set_scene_fade_period(g, FADE_FRAMES);
    set_scene_fade_countdown(g, FADE_FRAMES);
    vid_set_bg_map(pf, PF_AT(0x7E54D4), VID_BG1, &bg1_map);
    vid_set_bg_map(pf, PF_AT(0x7E54D4), VID_BG2, &bg2_map);
    vid_set_bg_map(pf, PF_AT(0x7E54D9), VID_BG3, &bg3_map);
    vid_set_bg_chars(pf, PF_AT(0x7E54DE), VID_BG1, LAYER_TILES_VRAM, LAYER_TILES_VRAM);
    vid_set_bg_chars(pf, PF_AT(0x7E54E1), VID_BG3, LAYER_TILES_VRAM, LAYER_TILES_VRAM);
    vid_set_color_math(pf, PF_AT(0x7E54E6), &(VidColorMath){ .sub_screen = true }, VID_MATH_CONTROL);
}

// $7E:53BB
/* The scene's setup, from the program's mode table (mode 4). */
static void set_up_lavos_falls(Snes *s)
{
    GameState *g = state_of(s);
    load_lavos(s);
    load_the_sprites(s);
    load_lavos_s_map(s);
    load_the_colours(g);
    set_scene_channels_script(g, SCENE_CAMERA_CHANNEL, LAVOS_SCRIPT);
    set_scene_channels_script(g, SCENE_STAR_FIELD_CHANNEL, FIELD_SCRIPT);
    show_the_sprites_alone(m_platform(s));
    first_script_step(s);
    scene_bridge_scatter_stars(s, s->cpu.a);
    set_up_the_layers(g, m_platform(s));
}

/* ---- Entries ---------------------------------------------------------------------------------- */

/* The routines channel 1's script calls, as C (scene/script.c). */
const SceneScriptRoutine scene_lavos_routines[] = {
    [SCENE_ROW_SEND_LAVOS_BACK] = { SCENE_ROUTINE_SEND_LAVOS_BACK, send_lavos_back, scene_send_lavos_back_present, false },
    [SCENE_ROW_LET_LAVOS_FALL] = { SCENE_ROUTINE_LET_LAVOS_FALL, let_lavos_fall, scene_let_lavos_fall_present, false },
};
const unsigned scene_lavos_routine_count = sizeof scene_lavos_routines / sizeof scene_lavos_routines[0];

#if ORIGINAL_EFFECTS
/* Each leaves A as its last load left it: channel 0's script's place. */
static uint16_t leaves_the_script_place(uint16_t called_with)
{
    return called_with == SCENE_ROUTINE_LET_LAVOS_FALL ? LAVOS_FALLS_TO_THE_PLANET : LAVOS_GOES_BACK;
}

const SceneRoutineLeaves scene_lavos_routine_leaves[] = {
    [SCENE_ROW_SEND_LAVOS_BACK] = { leaves_the_script_place, NULL },
    [SCENE_ROW_LET_LAVOS_FALL] = { leaves_the_script_place, NULL },
};
#endif

void scene_send_lavos_back(Snes *s) { scene_script_routine_alone(s, &scene_lavos_routines[SCENE_ROW_SEND_LAVOS_BACK]); }
void scene_let_lavos_fall(Snes *s) { scene_script_routine_alone(s, &scene_lavos_routines[SCENE_ROW_LET_LAVOS_FALL]); }

/* Entered with 8-bit A; back, the overflow is the scatter's last draw's,
 * which the program reads, and the direct page the scene's. */
bool scene_lavos_falls_set_up_scene(Snes *s)
{
    set_up_lavos_falls(s);
    return true;
}

void scene_lavos_falls_set_up(Snes *s)
{
    scene_lavos_falls_set_up_scene(s);
    m_rts(s);
}

/* The frame's tasks, 16-bit, each leaving A as its last load left it: the
 * matrix's last entry the turn wrote, the angle, the speed. */
void scene_turn_lavos(Snes *s)
{
    GameState *g = state_of(s);
    turn_lavos(g);
    ORIGINAL_EFFECT(s->cpu.a = scene_channel_register(g, SCENE_CAMERA_CHANNEL, SCENE_PLANE_C));
    m_rts(s);
}

void scene_turn_the_field_s_angle(Snes *s)
{
    GameState *g = state_of(s);
    turn_the_field_s_angle(g);
    ORIGINAL_EFFECT(s->cpu.a = field(g, FIELD_ANGLE));
    m_rts(s);
}

void scene_swing_the_field(Snes *s)
{
    GameState *g = state_of(s);
    swing_the_field(g);
    ORIGINAL_EFFECT(s->cpu.a = field(g, SCENE_STAR_FIELD_SPEED));
    m_rts(s);
}

/* Y is the next free sprite entry, and A as the list leaves it (sprites.c),
 * called with the list's place. */
void scene_draw_the_planet(Snes *s)
{
    GameState *g = state_of(s);
    draw_the_planet(g);
    s->cpu.y = scene_sprite_index(g);
    ORIGINAL_EFFECT(s->cpu.a = scene_clipped_list_leaves_a(g, PLANET_EDGE, PLANET_EDGE));
    m_rts(s);
}

/* ---- The guards ------------------------------------------------------------------------------- */

/* The program's sine and multiply, which the turn runs as C. */
static bool sine_present(Snes *s) { return m_code_matches(s, 0x7E3461, 0x19, 0xA2486F18B991C163ull); }
static bool multiply_present(Snes *s) { return m_code_matches(s, 0x7E31BF, 0x4A, 0x479627EBF1311CAFull); }

/* The turn adds the carry the list hands it, which the list's loop leaves
 * clear at the task's slot: the guard turns away a turn entered with it set. */
bool scene_turn_lavos_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && !m_flag(s, CPU_FLAG_C) &&
           m_code_matches(s, 0x7E56A9, 0x42, 0x1954DBB8276A19AEull) && sine_present(s) && multiply_present(s);
}

/* The angle's turn reaches channel 1's registers through the data bank and
 * adds with the carry the list leaves clear, as the turn does. */
bool scene_turn_the_field_s_angle_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && !m_flag(s, CPU_FLAG_C) &&
           m_code_matches(s, 0x7E55F9, 0x0B, 0xAF9A989D6EC7E462ull);
}

bool scene_swing_the_field_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E5855, 0x1D, 0xB1A7FF9566C6DC3Bull) && sine_present(s);
}

/* The planet's list, its 28 sprites with room for them in the tables. */
enum { PLANET_SPRITES = 28 };

bool scene_draw_the_planet_present(Snes *s)
{
    if (!scene_on_scene_page_io_bank(s) || !m_code_matches(s, 0x7E56EB, 0x12, 0x9F41A077BE5302E5ull) ||
        !m_code_matches(s, 0x7E56FD, 0x71, 0xEFBCBE84C2B5A9B6ull))
        return false;
    vars_peek(true);
    bool fits = scene_sprites_fit(state_of(s), PLANET_SPRITES);
    vars_peek(false);
    return fits;
}

bool scene_send_lavos_back_present(Snes *s) { return m_code_matches(s, 0x7E564D, 0x07, 0x5A6A85F804B87658ull); }
bool scene_let_lavos_fall_present(Snes *s) { return m_code_matches(s, 0x7E5640, 0x0D, 0x521AA074E2851935ull); }

/* The setup's code and the program's routines it runs inline: the spread of
 * the pixels, the laying out of the tiles, the widening of the map, the
 * blanking, the copies of resources, the transfer and the scatter. Its stores
 * reach the registers and low RAM through the data bank. */
bool scene_lavos_falls_set_up_present(Snes *s)
{
    return scene_on_scene_page(s) && m_bank_reaches_io(s) &&
           m_code_matches(s, 0x7E53BB, 0x12F, 0x9981100ED065A3B0ull) &&
           m_code_matches(s, 0x7E350E, 0x2E, 0xA6DAAD20AD0C7ACEull) &&
           m_code_matches(s, 0x7E54EA, 0x40, 0x3448FBAF895AB47Bull) &&
           m_code_matches(s, 0x7E37CA, 0x29, 0x94A78C10CD6A42C5ull) &&
           m_code_matches(s, 0x7E90A7, 0x1A, 0x841D0D5589A89675ull) &&
           m_code_matches(s, 0x7E36AA, 0x22, 0xCCA1DE26086A09CBull) &&
           m_code_matches(s, 0x7E38E7, 0x24, 0xDD4B67DA62CC8E67ull) && scene_stars_scatter_code_present(s);
}
