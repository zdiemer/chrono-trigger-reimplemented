/* Battle: setting the display up, and the window's per-line tables.
 *
 * While a battle is set up, the vblank's frame work is the display's setup:
 * it sends the palette, points the seven per-line effects at their tables
 * (the layers' maps and scroll, the screens, color math and the BG mode,
 * each changing at the line where the battlefield meets the window), loads a
 * step of the battle's graphics a frame, and scrolls the layers steadily. The
 * window's own per-line table, its edges line by line, is double-buffered:
 * the frame's effects build one while the other shows, and the vblank points
 * the window at the one for this frame.
 *
 * At the battle's end the game installs a vblank handler of its own for the
 * fade out: the palette, the brightness it is fading to, the layers at their
 * base scroll, and the map's own scroll and palette animation.
 *
 * Everything goes through the video service, which knows how the SNES
 * programmed each channel by the site the call stands for. */
#include "battle/frame.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame_data.h"
#include "charge.h"
#include "core/palette.h"
#include "field/line_effects.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/native.h"
#include "ram.h"
#include "vars.h"

/* The window's per-line effect is slot 7, beside the seven of the setup. */
enum { WINDOW_SLOT = 7 };

/* ---- The per-line effects ----------------------------------------------------------------- */

enum { LINE_TABLE_BANK = 0x7E };

static VidLineEffect line_effect(VidLineTarget target, uint32_t table)
{
    return (VidLineEffect){ .target = target, .table = PF_GAME(table), .indirect = true,
                            .indirect_bank = LINE_TABLE_BANK };
}

/* The seven effects, each in its slot, reading its table of the seven. */
static void point_the_line_effects(Platform *pf)
{
    static const struct {
        VidLineTarget target;
        uint32_t site;
    } effects[] = {
        { VID_LINE_BG_MAPS, 0xCD09F7 },     { VID_LINE_SCROLL_BG1, 0xCD0A11 }, { VID_LINE_SCROLL_BG2, 0xCD0A2B },
        { VID_LINE_SCROLL_BG3, 0xCD0A45 },  { VID_LINE_SCREENS, 0xCD0A5F },    { VID_LINE_COLOR_MATH, 0xCD0A79 },
        { VID_LINE_MODE, 0xCD0A93 },
    };
    for (uint8_t slot = 0; slot < sizeof effects / sizeof effects[0]; slot++) {
        const VidLineEffect e = line_effect(effects[slot].target,
                                            RAM_BATTLE_LINE_TABLES + slot * RAM_BATTLE_LINE_TABLES_STRIDE);
        vid_set_line_slot(pf, PF_AT(effects[slot].site), slot, &e);
    }
}

// $CD:0AEE
/* The window's table for this frame: the first on even frames, the second on
 * odd ones. */
void battle_show_window_table(GameState *g, Platform *pf)
{
    unsigned table = battle_frame_count(g) & 1;
    const VidLineEffect e = line_effect(VID_LINE_WINDOWS, RAM_BATTLE_WINDOW_LINE_TABLES
                                                              + table * RAM_BATTLE_WINDOW_LINE_TABLES_STRIDE);
    vid_set_line_slot(pf, PF_AT(0xCD0AF0), WINDOW_SLOT, &e);
}

/* ---- The line effects' arrangements --------------------------------------------------------- */

/* Each of the nine per-line tables (the seven effects' and the window's two)
 * is a short list of runs of lines, which says where the battlefield's lines
 * end and the battle window's begin and what each run takes. */
enum { LINE_TABLES = BATTLE_LINE_TABLES_COUNT, WINDOW_LINE_TABLES = BATTLE_WINDOW_LINE_TABLES_COUNT };
enum { BG2_SCROLL_TABLE = 2 };

// $CE:EE6E
void battle_arrange_line_effects(GameState *g, uint8_t arrangement)
{
    bool ripples = battle_background_flags(g) & BATTLE_BACKGROUND_HAS_GRADIENT;
    for (unsigned table = 0; table < LINE_TABLES + WINDOW_LINE_TABLES; table++) {
        unsigned list = table == BG2_SCROLL_TABLE && ripples ? RIPPLING_BG2_SCROLL_LIST : table;
        for (unsigned i = 0; i < LINE_ARRANGEMENT_LIST_BYTES; i++) {
            uint8_t v = line_arrangement_byte(g, arrangement, list, i);
            if (table < LINE_TABLES)
                set_battle_line_tables_byte(g, table, i, v);
            else
                set_battle_window_line_tables_byte(g, table - LINE_TABLES, i, v);
        }
    }
}

/* A table's runs are three bytes each: how many lines the run lasts, and
 * where its values are. The window slides in and out a line a frame by
 * moving where the runs meet: the battlefield's run a line shorter (in) or
 * longer (out), the run after it two lines the other way and the one after
 * that one line. The BG3 scroll's and the color math's second runs also start
 * a line's values (two words) earlier or later in what they read. */
enum { RUN_BYTES = 3, RUN_VALUES_AT = 1, LINE_VALUE_BYTES = 4, BG3_SCROLL_TABLE = 3, COLOR_MATH_TABLE = 5 };

static uint8_t line_table_byte(const GameState *g, unsigned table, unsigned at)
{
    return table < LINE_TABLES ? battle_line_tables_byte(g, table, at) : battle_window_line_tables_byte(g, table - LINE_TABLES, at);
}

static void set_line_table_byte(GameState *g, unsigned table, unsigned at, uint8_t v)
{
    if (table < LINE_TABLES)
        set_battle_line_tables_byte(g, table, at, v);
    else
        set_battle_window_line_tables_byte(g, table - LINE_TABLES, at, v);
}

static void lengthen_run(GameState *g, unsigned table, unsigned run, int lines)
{
    unsigned at = run * RUN_BYTES;
    set_line_table_byte(g, table, at, (uint8_t)(line_table_byte(g, table, at) + lines));
}

static void move_run_values(GameState *g, unsigned table, unsigned run, int bytes)
{
    unsigned at = run * RUN_BYTES + RUN_VALUES_AT;
    uint16_t values = (uint16_t)(line_table_byte(g, table, at) | line_table_byte(g, table, at + 1) << 8);
    values = (uint16_t)(values + bytes);
    set_line_table_byte(g, table, at, (uint8_t)values);
    set_line_table_byte(g, table, at + 1, (uint8_t)(values >> 8));
}

// $CD:053C (in), $CD:0577 (out)
void battle_slide_the_split(GameState *g, bool out)
{
    int step = out ? 1 : -1;
    for (unsigned table = 0; table < LINE_TABLES + WINDOW_LINE_TABLES; table++) {
        lengthen_run(g, table, 1, step);
        lengthen_run(g, table, 2, -2 * step);
        lengthen_run(g, table, 3, step);
    }
    move_run_values(g, COLOR_MATH_TABLE, 2, LINE_VALUE_BYTES * step);
    move_run_values(g, BG3_SCROLL_TABLE, 2, LINE_VALUE_BYTES * step);
}

void battle_slide_split_in(Snes *s)
{
    battle_slide_the_split(state_of(s), false);
    s->cpu.a = 0;
    m_rts(s);
}

void battle_slide_split_out(Snes *s)
{
    battle_slide_the_split(state_of(s), true);
    s->cpu.a = 0;
    m_rts(s);
}

/* ---- The background's ripple ------------------------------------------------------------- */

/* BG2's scroll on a rippling background's lines (battle_background_ripple)
 * takes 64 sways of the wave, a step further along it every fourth frame,
 * round its 32 steps. */
enum { RIPPLE_LINES = BATTLE_BACKGROUND_RIPPLE_COUNT, FRAMES_A_RIPPLE_STEP = 4, RIPPLE_STEPS = 32, SWAYS_A_STEP = 2 };

// $CE:E18E
void battle_ripple_background(GameState *g)
{
    if (battle_palette_rows_moved(g)) {
        for (unsigned line = 0; line < RIPPLE_LINES; line++)
            set_battle_background_ripple(g, line, 0);
        return;
    }
    uint8_t frames = battle_ripple_frames(g);
    set_battle_ripple_frames(g, (uint8_t)(frames + 1));
    unsigned step = frames / FRAMES_A_RIPPLE_STEP % RIPPLE_STEPS;
    for (unsigned line = 0; line < RIPPLE_LINES; line++)
        set_battle_background_ripple(g, line, background_ripple_sway(g, step * SWAYS_A_STEP + line));
}

/* ---- The battle's graphics ----------------------------------------------------------------- */

/* The loads, three steps of them: the ROM's graphics for the message and
 * window layers; the window's tilemap and the tilemaps setup built, and more
 * of the window layer's tiles; and the party's window frames and the tiles
 * setup decoded. Each is a transfer in words to a place in VRAM. */
typedef struct {
    uint32_t source;
    uint16_t bytes, vram;
    uint32_t site;
} VramLoad;

/* Of the second tilemap setup built, 64 bytes go up from its 192nd. */
enum { SECOND_TILEMAP_SENT_FROM = 192 };

static const VramLoad step_1[] = {
    { BATTLE_MESSAGE_LAYER_TILES, 0x1000, 0x5800, 0xCCE00C },
    { BATTLE_WINDOW_TILE_BLOCK_2, 0x0040, 0x0060, 0xCCE047 },
    { BATTLE_WINDOW_TILE_BLOCK_1, 0x0040, 0x0020, 0xCCE076 },
};
static const VramLoad step_2[] = {
    { (uint16_t)RAM_BATTLE_WINDOW_TILEMAP, 0x0780, 0x7820, 0xCCE0AF },
    { BATTLE_WINDOW_TILE_BLOCK_3, 0x0040, 0x0160, 0xCCE0EB },
    { BATTLE_WINDOW_TILE_BLOCK_3, 0x0040, 0x0120, 0xCCE11B },
    { RAM_BATTLE_LOAD_TILEMAP, 0x01C0, 0x7B00, 0xCCE14B },
    { RAM_BATTLE_LOAD_TILEMAP_2 + SECOND_TILEMAP_SENT_FROM, 0x0040, 0x7800, 0xCCE17A },
    { BATTLE_WINDOW_TILE_BLOCK_4, 0x0040, 0x0000, 0xCCE22E },
    { BATTLE_WINDOW_TILE_BLOCK_5, 0x0040, 0x0100, 0xCCE267 },
};
static const VramLoad step_3[] = {
    { (uint16_t)RAM_BATTLE_WINDOW_FRAMES_TILEMAP, 0x0180, 0x7A00, 0xCCE1B3 },
    { RAM_BATTLE_DECODED_TILES, 0x0C00, 0x1800, 0xCCE1EE },
};

static const struct {
    const VramLoad *loads;
    unsigned count;
} load_steps[] = {
    { step_1, sizeof step_1 / sizeof step_1[0] },
    { step_2, sizeof step_2 / sizeof step_2[0] },
    { step_3, sizeof step_3 / sizeof step_3[0] },
};
enum { LOAD_STEPS = sizeof load_steps / sizeof load_steps[0] };

// $CC:E000
/* The next step of the load, a step a frame, counted from 1; nothing before
 * the count starts, or after the last step. */
static void load_the_next_step(GameState *g, Platform *pf)
{
    uint8_t step = battle_graphics_load_step(g);
    if (step == 0 || step > LOAD_STEPS)
        return;
    for (unsigned i = 0; i < load_steps[step - 1].count; i++) {
        const VramLoad *l = &load_steps[step - 1].loads[i];
        vid_send_vram(pf, PF_AT(l->site), l->vram, PF_GAME(l->source), l->bytes, NULL);
    }
    set_battle_graphics_load_step(g, (uint8_t)(step + 1));
}

/* ---- The display's setup ----------------------------------------------------------------- */

/* Every per-line effect on, the window's included. */
enum { ALL_LINE_EFFECTS = 0xFF };

/* The call-outs read nothing of the registers. */
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_FLAGS };

/* The palettes' animation, through the original's jump to it. */
enum { PALETTE_ANIMATION = 0xFDFFF7 };

// $CD:09F1
/* The setup runs with the data bank $7E from the line effects on. */
static bool set_the_display_up(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    NATIVE_TIME_AS(s, 0xCD09CE, battle_send_palette(pf));
    point_the_line_effects(pf);
    s->cpu.db = BATTLE_DATA_BANK;
    if (battle_graphics_load_in_vblank(g))
        NATIVE_TIME_AS(s, 0xCCE000, load_the_next_step(g, pf));
    NATIVE_TIME_AS(s, 0xCD0AEE, battle_show_window_table(g, pf));
    vid_enable_line_effects(pf, PF_AT(0xCD0AC1), ALL_LINE_EFFECTS);
    VidDisplay display = vid_display_decode(battle_brightness(g));
    vid_set_display(pf, PF_AT(0xCD0AC8), &display);
    NATIVE_TIME_AS(s, 0xCD06C4, battle_scroll_layers(g, BATTLE_SCROLL_STEADY));
    s->cpu.a = 0;
    NATIVE_TIME_AS(s, 0xCD0B1E, battle_keep_animations_off_the_cycle(g));
    if (!battle_call_long(s, 0xCD0AD2, 0xD1F426, READS_NOTHING) ||
        !battle_call_long(s, 0xCD0AD6, PALETTE_ANIMATION, READS_NOTHING | DC_DB))
        return false;
    NATIVE_TIME_AS(s, 0xCD0ADE, battle_turn_the_gradient(g));
    return true;
}

/* ---- The battle's last vblank handler ---------------------------------------------------- */

/* The fading screen's brightness is what the fade left in the scratch cell
 * $45; the handler clears $47, which the fade waits on for the frame. */
static uint8_t fade_brightness(const GameState *g) { return battle_scratch(g, 0); }
static void release_the_fade(GameState *g) { set_battle_scratch(g, 2, 0); }

static void scroll_to_the_base(GameState *g, Platform *pf)
{
    static const uint32_t sites[3] = { 0xD1F4F8, 0xD1F514, 0xD1F530 };
    for (unsigned bg = 0; bg < 3; bg++)
        vid_set_scroll(pf, PF_AT(sites[bg]), (VidLayer)bg, battle_bg_scroll_x(g, bg), battle_bg_scroll_y(g, bg),
                       VID_SCROLL_H | VID_SCROLL_V);
}

/* ---- The entries ------------------------------------------------------------------------ */

void battle_setup_display(Snes *s)
{
    if (!set_the_display_up(s))
        return;
    m_rts(s);
}

void battle_load_graphics(Snes *s)
{
    load_the_next_step(state_of(s), m_platform(s));
    m_rtl(s);
}

void battle_leave_arrangement_registers(Snes *s)
{
    s->cpu.a = 0;
    s->cpu.y = (uint16_t)(RAM_BATTLE_WINDOW_LINE_TABLES + RAM_BATTLE_WINDOW_LINE_TABLES_STRIDE + LINE_ARRANGEMENT_LIST_BYTES);
}

/* The arrangement in A; the carry comes back as the arrangement's top bit,
 * which the original's indexing shifts out. */
void battle_load_line_arrangement(Snes *s)
{
    uint8_t arrangement = (uint8_t)s->cpu.a;
    battle_arrange_line_effects(state_of(s), arrangement);
    battle_leave_arrangement_registers(s);
    m_set_flag(s, CPU_FLAG_C, arrangement & 0x80);
    m_rtl(s);
}

void battle_step_ripple(Snes *s)
{
    battle_ripple_background(state_of(s));
    s->cpu.a = 0;
    m_rtl(s);
}

void battle_setup_window_hdma(Snes *s)
{
    battle_show_window_table(state_of(s), m_platform(s));
    m_rts(s);
}

/* The field's line effects rebuilt for the picture the battle hands back
 * ($C0:0005, field/line_effects.c), as C where its builders can build and the
 * handler runs as the field's routine is entered, else the original's call
 * from the handler's JSL; either leaves the effects to switch on in A's low
 * byte. */
static bool rebuild_the_fields_line_effects(Snes *s)
{
    GameState *g = state_of(s);
    vars_peek(true);
    bool builds = field_line_effects_can_be_built(g);
    vars_peek(false);
    if (!builds || !battle_binary_m8_x16(s))
        return battle_call_long(s, 0xD1F4E2, FIELD_REBUILD_THE_LINE_EFFECTS_ENTRY, 0);
    uint8_t effects;
    TIMED_AS(g, FIELD_REBUILD_THE_LINE_EFFECTS_ENTRY, effects = field_rebuild_line_effects(g, m_platform(s)));
    m_set_a8(s, effects);
    return true;
}

// $D1:F4C0
/* The handler saves the interrupted code's registers and runs on the battle's
 * page. The per-line effects' enable is the one write it makes through the
 * data bank, which is the interrupted code's: with a bank that doesn't reach
 * the registers -- which is how `battle-golem-twins-gameover` reaches this
 * handler on its way back to the title -- the store lands in memory, so the
 * port writes the memory byte instead. Returns with RTI. */
void battle_nmi_alternate(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    m_save_interrupted_registers_bank_first(s);
    s->cpu.d = BATTLE_PAGE;
    s->cpu.a = 0;
    m_sep(s, CPU_FLAG_M);
    vid_enable_line_effects(pf, PF_AT(0xD1F4CE), 0x00);
    tm_ack_vblank(pf, PF_AT(0xD1F4D2));
    tm_enable_interrupts(pf, PF_AT(0xD1F4D8), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD);
    release_the_fade(g);
    m_jsl_dc(s, 0xD1F4DE, 0xCD09CE, DC_A | DC_X | DC_N | DC_Z | DC_C);
    if (!battle_widths_m8_x16(s))
        return;
    if (!rebuild_the_fields_line_effects(s))
        return;
    if (m_io_bank(s->cpu.db))
        vid_enable_line_effects(pf, PF_AT(0xD1F4E6), (uint8_t)s->cpu.a);
    else
        m_write8(s, m_abs(s, M_REG_LINE_EFFECTS), (uint8_t)s->cpu.a);
    VidDisplay display = vid_display_decode(fade_brightness(g));
    vid_set_display(pf, PF_AT(0xD1F4ED), &display);
    s->cpu.db = BATTLE_DATA_BANK;
    scroll_to_the_base(g, pf);
    m_jsl_dc(s, 0xD1F549, 0xCD0C89, DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C);
    if (!battle_widths_m8_x16(s))
        return;
    if (!battle_run_long(s, 0xD1F54D, PALETTE_ANIMATION, palette_animate) || !battle_widths_m8_x16(s))
        return;
    m_return_to_interrupted_bank_first(s);
}
