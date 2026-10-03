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
 * and back one every fourth, which the screen's per-line effect applies. The
 * rest of the screen's task is still the original's. */
#include "menu/gate_screen.h"

#include "menu/bridge.h"
#include "menu/menu_data.h"
#include "original_effects.h"
#include "platform/video.h"
#include "port/machine.h"
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

static void leave_swirl_laid_out(GameState *g)
{
    set_menu_scratch_gate_row(g, 2 * MENU_GATE_ROWS);
    set_menu_scratch_gate_row_at(g, (uint16_t)(RAM_ABS(RAM_MENU_GATE_TILEMAP) + MENU_GATE_ROWS * ROW_BYTES));
    set_menu_scratch_gate_rows_left(g, 0);
}

// $FF:F677
/* Once the swirl's graphics are unpacked: its tilemap, its colours, and the
 * layer's scroll. */
void menu_draw_gate_swirl(GameState *g)
{
    set_menu_scroll_y(g, SWIRL_LAYER, SWIRL_START_Y);
    lay_out_swirl(g);
    load_swirl_colours(g);
    ORIGINAL_EFFECT(leave_swirl_laid_out(g));
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

// $FF:F716
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
    ORIGINAL_EFFECT(set_menu_scratch_gate_across(g, across));
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

/* ---- The registered entries ------------------------------------------------------------------- */

/* The unpacker ($C3:0002) takes where from and where to in its own cells,
 * and reads no register; the swirl's graphics go where the menus unpack
 * graphics before they send them to VRAM. */
enum { UNPACKER = 0xC30002, UNPACKER_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
enum { SWIRL_GRAPHICS_UNPACKED_TO = 0x7EB000 };

// $FF:F677
void menu_draw_gate_screen(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    set_decompress_source(g, MENU_GATE_PACKED_GRAPHICS);
    set_decompress_dest(g, SWIRL_GRAPHICS_UNPACKED_TO);
    menu_set_widths(s, false, false);
    if (!menu_call_long(s, 0xFFF690, UNPACKER, UNPACKER_IGNORES))
        return;
    menu_draw_gate_swirl(g);
    menu_restore_p(s);
    m_rtl(s);
}

// $FF:F716
/* Entered with either width of A, which it puts back; it comes back with the
 * last band's scroll in A, whose high byte the screen's task carries on. */
void menu_turn_gate_swirl_entry(Snes *s)
{
    GameState *g = state_of(s);
    menu_turn_gate_swirl(g);
    s->cpu.a = menu_gate_swirl(g, BANDS - 1);
    m_rtl(s);
}

/* The screen's frame task ($C2:CECE), added in the menus' task slot $10 by
 * $C2:8249, the original's. The setup is copied with a block move, which
 * leaves the data bank on work RAM's and Y past the state, which the caller
 * reads. */
enum { GATE_SCREEN_TASK = 0xCECE, GATE_SCREEN_TASK_SLOT = 0x0010 };

// $C2:CE9D
/* The original keeps its caller's P on the stack round the task's call, and
 * puts it back as it returns. */
void menu_start_gate_screen(Snes *s)
{
    GameState *g = state_of(s);
    m_push8(s, s->cpu.p);
    const VidBg bg3 = { .map_base = GATE_SCREEN_TILEMAP, .map_w = MAP_TILES, .map_h = MAP_TILES };
    vid_set_bg_map(m_platform(s), PF_AT(0xC2CEA2), VID_BG3, &bg3);
    start_counting(g);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    s->cpu.a = GATE_SCREEN_TASK;
    s->cpu.x = GATE_SCREEN_TASK_SLOT;
    m_jsr_dc(s, 0xC2CEB1, 0xC28249, DC_Y | DC_N | DC_V | DC_Z | DC_C | DC_DB);
    set_up_gate_screen(g);
    s->cpu.db = MENU_DATA_BANK;
    s->cpu.y = (uint16_t)(RAM_ABS(RAM_MENU_GATE_SCREEN_STATE) + MENU_GATE_SCREEN_STATE_COUNT);
    s->cpu.p = m_pull8(s);
    if (m_x8(s)) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
    m_rts(s);
}

/* The screen's routines run on the menus' page with the data bank on work
 * RAM's. */
bool menu_gate_screen_runs(Snes *s) { return menu_on_page_work_ram(s); }

/* The swirl's bands are counted from the menus' zero word, which is 0
 * whenever the menus run. */
bool menu_gate_swirl_turns(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_zero(state_of(s)) == 0;
    vars_peek(false);
    return ok;
}
