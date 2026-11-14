/* The End of Time's gate screen: the swirl of light behind the gate a
 * party member steps into.
 *
 * The swirl is BG3: its graphics unpacked from the ROM, and a tilemap of six
 * rows of sixteen tiles, each row twice across, repeated down the map
 * (menu_draw_gate_screen). The screen starts (menu_start_gate_screen) by
 * moving BG3's tilemap to its own place, adding the screen's frame task to
 * the menus' tasks and setting the screen's state up from the ROM. Every
 * frame the task turns the swirl (menu_turn_gate_swirl): the layer drifts a
 * pixel right and up every fourth frame, and each of sixteen bands of lines
 * is scrolled across by its place on a wave that moves on a step every frame
 * and back one every fourth, which the screen's per-line effect applies. */
#include "menu/gate_screen.h"

#include "charge.h"
#include "core/program.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "overworld/setup.h"
#include "platform/video.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

/* ---- Drawing it ------------------------------------------------------------------------------- */

/* The swirl's tilemap: 32 tiles by 32; a row's sixteen tiles follow each
 * other from its first, and come twice across. */
enum { MAP_TILES = 32, ROW_TILES = 16 };

/* BG3's scroll (menu_scroll's third layer): the swirl starts a line above
 * the top, wrapped round the layer's 512 lines. */
enum { SWIRL_LAYER = 2, SWIRL_START_Y = 0x01FF };

/* Its colours: eight in the palette's second row, from its ninth. */
enum { SWIRL_PALETTE_FROM = 24 };

static void lay_out_swirl(GameState *g)
{
    for (unsigned row = 0; row < MAP_TILES; row++) {
        uint16_t first = menu_gate_row_first_tile(g, row % MENU_GATE_ROWS);
        for (unsigned column = 0; column < MAP_TILES; column++)
            set_menu_gate_tilemap(g, row * MAP_TILES + column, (uint16_t)(first + column % ROW_TILES));
    }
}

static void load_swirl_colours(GameState *g)
{
    for (unsigned i = 0; i < MENU_GATE_PALETTE_COLOURS; i++)
        set_menu_palette(g, SWIRL_PALETTE_FROM + i, menu_gate_palette_colour(g, i));
    for (unsigned i = 0; i < MENU_GATE_COLOURS; i++)
        set_menu_gate_colours(g, i, menu_gate_colour(g, i));
}

/* What the original's drawing leaves in its cells: its rows counted out, and
 * where the row after the sixth would go. */
enum { ROW_BYTES = MAP_TILES * 2 };

// $FF:F677
/* Once the swirl's graphics are unpacked: its tilemap, its colours, and the
 * layer's scroll. */
void menu_draw_gate_swirl(GameState *g)
{
    set_menu_scroll_y(g, SWIRL_LAYER, SWIRL_START_Y);
    lay_out_swirl(g);
    load_swirl_colours(g);
}

/* ---- Turning it ------------------------------------------------------------------------------- */

/* The screen's state (menu_gate_screen_state): the wave's phase, then the
 * frames counted; the setup after them. */
enum { PHASE = 0, FRAMES = 1, GATE_SCREEN_COUNTS = 2 };

/* Every fourth frame the layer drifts and the wave steps back. */
enum { DRIFT_EVERY = 4, PHASE_STEPS = 32, BANDS = 16, BAND_STEP = 2 };

/* The original's sum carries the frame count's top bit into the first band:
 * for 128 frames out of every 256 the top band sits a pixel further across
 * than the wave puts it. */
enum { COUNT_TOP_BIT = 0x80 };

static uint16_t top_band_nudge(uint8_t frames) { return (frames & COUNT_TOP_BIT) ? 1 : 0; }

/* A scroll's low byte moved by one, its high byte left alone, as the
 * original steps it. */
static uint16_t stepped_low_byte(uint16_t scroll, int by) { return (uint16_t)((scroll & 0xFF00) | (uint8_t)(scroll + by)); }

void menu_turn_gate_swirl(GameState *g)
{
    uint8_t phase = (uint8_t)(menu_gate_screen_state(g, PHASE) + 1), frames = (uint8_t)(menu_gate_screen_state(g, FRAMES) + 1);
    if (frames % DRIFT_EVERY == 0) {
        set_menu_scroll_x(g, SWIRL_LAYER, stepped_low_byte(menu_scroll_x(g, SWIRL_LAYER), 1));
        set_menu_scroll_y(g, SWIRL_LAYER, stepped_low_byte(menu_scroll_y(g, SWIRL_LAYER), -1));
        phase = (uint8_t)(phase - 2);
    }
    set_menu_gate_screen_state(g, PHASE, phase);
    set_menu_gate_screen_state(g, FRAMES, frames);
    uint16_t across = menu_scroll_x(g, SWIRL_LAYER);
    for (unsigned band = 0; band < BANDS; band++) {
        uint16_t wave = menu_gate_wave(g, phase % PHASE_STEPS + BAND_STEP * band);
        set_menu_gate_swirl(g, band, (uint16_t)(wave + across + (band == 0 ? top_band_nudge(frames) : 0)));
    }
}

/* ---- Starting it ------------------------------------------------------------------------------ */

/* BG3's tilemap moves to its own place for the screen. */
enum { GATE_SCREEN_TILEMAP = 0x6800 };

/* The screen's state: two bytes the screen counts from zero, then its setup. */
enum { GATE_SCREEN_SETUP_BYTES = MENU_GATE_SCREEN_STATE_COUNT - GATE_SCREEN_COUNTS };

static void start_counting(GameState *g)
{
    for (unsigned i = 0; i < GATE_SCREEN_COUNTS; i++)
        set_menu_gate_screen_state(g, i, 0);
}

static void set_up_gate_screen(GameState *g)
{
    for (unsigned i = 0; i < GATE_SCREEN_SETUP_BYTES; i++)
        set_menu_gate_screen_state(g, GATE_SCREEN_COUNTS + i, menu_gate_screen_setup(g, i));
}

/* ---- The screen started, and its task --------------------------------------------------------- */

/* The swirl's graphics go where the menus unpack graphics before they send
 * them to VRAM. */
enum { SWIRL_GRAPHICS_UNPACKED_TO = 0x7EB000 };

// $FF:F677
/* The swirl's graphics unpacked, then the swirl drawn. */
void menu_draw_gate_screen(GameState *g)
{
    overworld_unpack(g, asset_address(ASSET_MENU_GATE_SWIRL_GRAPHICS), SWIRL_GRAPHICS_UNPACKED_TO);
    menu_draw_gate_swirl(g);
}

// $C2:CE9D
/* BG3's tilemap moved, the screen's counts from zero, its frame task
 * started, and its state set up. */
void menu_start_gate_screen(GameState *g, Platform *pf)
{
    const VidBg bg3 = { .map_base = GATE_SCREEN_TILEMAP, .map_w = MAP_TILES, .map_h = MAP_TILES };
    vid_set_bg_map(pf, PF_AT(0xC2CEA2), VID_BG3, &bg3);
    start_counting(g);
    menu_start_task(g, MENU_DESCRIPTION_TASK, MENU_GATE_SCREEN_TASK_START);
    set_up_gate_screen(g);
}

/* The swirl's per-line scroll is the menus' fifth line effect. */
enum { SWIRL_LINE_EFFECT = 0x10, FRAMES_A_TURN = 2 };

// $C2:CECE (with $C2:CEC2, its wait)
/* The screen's task: the swirl turned, its line effect kept on, and two
 * frames waited, round and round. */
static void turn_the_swirl(GameState *g)
{
    TIMED_AS(g, 0xFFF716, menu_turn_gate_swirl(g));
    set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) | SWIRL_LINE_EFFECT));
}

/* Each turn, while the swirl can turn as it stands. */
void menu_gate_screen_task(GameState *g, Platform *pf)
{
    (void)pf;
    while (menu_gate_swirl_can_turn(g)) {
        turn_the_swirl(g);
        menu_task_waits(g, FRAMES_A_TURN);
    }
    program_breaks_a_contract(g, 0xC2CECE, "a gate's swirl the port doesn't turn");
}

/* The swirl's bands are counted from the menus' zero word, which is 0
 * whenever the menus run. */
bool menu_gate_swirl_can_turn(const GameState *g) { return menu_zero(g) == 0; }
