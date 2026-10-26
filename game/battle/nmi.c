/* Battle: the vblank and line handlers.
 *
 * The vblank handler starts each frame's display work: it blanks the screen
 * for the uploads, runs the frame work for the battle's mode and, unless the
 * battle is paused, the game's per-frame work and the frame count; then it
 * takes the Start button's pause and the debug frame advance. The line
 * handler, at the line the battle sets for it, blanks the rest of the frame
 * and, on alternate frames, applies the alternating-frame setting or runs the
 * tile animation uploads.
 *
 * What each handler does to the game is C on the game's state. The entries
 * save and restore the interrupted code's registers around it, and with them
 * the scratch at the bottom of the direct page the frame work reuses and the
 * multiplier's or divider's operands the interrupted code may be relying on:
 * that is the interrupted code's context, not the battle's, so it lives in the
 * entries. The handlers run on the battle's page; the code they call leaves
 * the data bank at $7E, which they rely on, and a call that doesn't hands the
 * rest back to the original. */
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame.h"
#include "battle/program.h"
#include "battle/screen_data.h"
#include "charge.h"
#include "core/tile_animation.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "platform/input.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "vars.h"
#include "wram.h"

/* The handler reads the controller a half at a time, as the original does:
 * Start from its high byte, R and L from its low. */
static bool start_held(Platform *pf, PfOrigin o) { return (in_read_high(pf, o, 0) & (IN_BUTTON_START >> 8)) != 0; }
static bool low_held(Platform *pf, PfOrigin o, uint16_t button) { return (in_read_low(pf, o, 0) & button) != 0; }

/* A demo battle can't be paused, and the pause is taken only in the fight. */
enum { MODE_FIGHT = 1 };

/* ---- The frame -------------------------------------------------------------------------- */

/* The per-line effects take their values from word pairs; each frame the
 * first pair of each group of four is latched into the second, so the lines
 * after a split see this frame's values. */
static void latch_split_lines(GameState *g)
{
    static const uint8_t latched[4] = { 0, 1, 4, 5 };
    for (unsigned i = 0; i < 4; i++)
        set_battle_split_line_values(g, latched[i] + 2u, battle_split_line_values(g, latched[i]));
}

/* The frame count, and the timed battle message's countdown: when it runs out
 * the message closes (one opened by id is forgotten, so it can show again),
 * and the line handler applies the alternating-frame setting anew. */
static void count_frame(GameState *g)
{
    set_battle_frame_count(g, (uint16_t)(battle_frame_count(g) + 1));
    if (!battle_message_timed(g))
        return;
    uint8_t left = (uint8_t)(battle_message_frames(g) - 1);
    set_battle_message_frames(g, left);
    if (left != 0)
        return;
    if (battle_message_by_id(g))
        set_battle_message_shown(g, 0xFF);
    set_battle_message_by_id(g, 0);
    set_battle_vram_copy_mode(g, 0);
    set_battle_message_timed(g, 0);
    battle_lay_the_lines_out_again(g);
}

static bool pause_allowed(const GameState *g)
{
    return battle_mode(g) == MODE_FIGHT && !(battle_setup_flags_copy(g) & BATTLE_SETUP_DEMO) && !battle_pause_blocked(g);
}

/* Start pauses and unpauses the fight, once per press. Where the pause isn't
 * allowed (outside the fight, or forbidden) the fight runs on. */
static void take_pause(GameState *g, Platform *pf)
{
    if (battle_mode(g) == MODE_FIGHT && !pause_allowed(g))
        set_battle_paused(g, 0);
    else if (pause_allowed(g) && start_held(pf, PF_AT(0xD1E81E))) {
        if (!battle_pause_held(g)) {
            set_battle_paused(g, (uint8_t)((battle_paused(g) + 1) & 0x01));
            set_battle_pause_held(g, 1);
        }
        return;
    }
    set_battle_pause_held(g, 0);
}

/* The debug frame advance, where the ROM turns it on: R ends the pause, and
 * while Start holds it, every fourth frame runs if L is held. */
static void take_frame_advance(GameState *g, Platform *pf)
{
    if (!battle_debug_frame_advance(g))
        return;
    if (low_held(pf, PF_AT(0xD1E84B), IN_BUTTON_R)) {
        set_battle_paused(g, 0);
        set_battle_pause_held(g, 0);
        return;
    }
    if (!battle_pause_held(g))
        return;
    uint8_t count = (uint8_t)(battle_frame_advance_count(g) + 1);
    set_battle_frame_advance_count(g, count);
    bool runs = (count & 0x03) == 0 && low_held(pf, PF_AT(0xD1E86A), IN_BUTTON_L);
    set_battle_paused(g, (count & 0x03) ? (count & 0x03) : !runs);
}

/* ---- The interrupted code's context --------------------------------------------------------- */

// $D1:FE71
/* The multiplier's or divider's operands, set again after the handler's own
 * callees used it: the divider's while the interrupted code has it busy,
 * otherwise the multiplier's. */
static void restore_math_operands(GameState *g)
{
    if (battle_divider_busy(g)) {
        hw_set_dividend(g, (uint16_t)(battle_divider_operands(g, 0) | battle_divider_operands(g, 1) << 8));
        hw_divide_by(g, battle_divider_operands(g, 2));
    } else {
        hw_set_multiplicand(g, battle_multiplier_operands(g, 0));
        (void)hw_multiply_by(g, battle_multiplier_operands(g, 1));
    }
}

static uint8_t last_math_operand(const GameState *g)
{
    return battle_divider_busy(g) ? battle_divider_operands(g, 2) : battle_multiplier_operands(g, 1);
}

/* The direct page's bottom bytes, which the frame work uses as scratch, are
 * moved aside around it. */
enum { SCRATCH_BYTES = 43, SCRATCH_ASIDE = 0xCC65 };

static bool came_back(const Snes *s) { return battle_widths_m8_x16(s) && s->cpu.db == BATTLE_DATA_BANK; }

/* ---- The vblank handler ----------------------------------------------------------------- */

/* The frame work for the battle's mode reads A's high byte and Y (the block
 * move's end); the game's per-frame work reads nothing. */
enum { FRAME_WORK = 0xCD0C95, GAME_FRAME = BATTLE_SCROLL_MAP_LAYERS_ENTRY };

/* The handler's calls on the battle's other routines: the scratch moved aside
 * and back, the frame work for the battle's mode (battle/frame.c), and the
 * game's per-frame work, the map's layers scrolled. */
typedef struct {
    void (*set_the_scratch_aside)(void *ctx);
    bool (*frame_work)(void *ctx);
    bool (*map_layers)(void *ctx);
    void (*put_the_scratch_back)(void *ctx);
} VblankCalls;

// $D1:E77C
/* The battle's vblank handler. Unless the last one is still running: the
 * line values latched, the screen blanked for the uploads, the frame work, and
 * unless paused the game's per-frame work and the frame count; then pause and
 * frame advance, and the battle's waits for the frame are released. False
 * where a call went no further. */
static bool take_the_vblank(GameState *g, Platform *pf, const VblankCalls *calls, void *ctx)
{
    vid_enable_line_effects(pf, PF_AT(0xD1E78A), 0);
    tm_ack_vblank(pf, PF_AT(0xD1E78E));
    tm_enable_interrupts(pf, PF_AT(0xD1E794), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD);
    if (battle_interrupt_busy(g))
        return true;
    set_battle_interrupt_busy(g, (uint8_t)(battle_interrupt_busy(g) + 1));
    calls->set_the_scratch_aside(ctx);
    latch_split_lines(g);
    vid_enable_line_effects(pf, PF_AT(0xD1E7CC), 0);
    tm_reset_beam_latch(pf, PF_AT(0xD1E7CF));
    vid_set_display(pf, PF_AT(0xD1E7D4), &(VidDisplay){ .blank = true });
    vid_start_vram_uploads(pf, PF_AT(0xD1E7D7), VID_VRAM_WORD_PORT);
    if (!calls->frame_work(ctx))
        return false;
    if (!battle_paused(g)) {
        if (!calls->map_layers(ctx))
            return false;
        count_frame(g);
    }
    take_pause(g, pf);
    ORIGINAL_EFFECT(restore_math_operands(g));
    take_frame_advance(g, pf);
    calls->put_the_scratch_back(ctx);
    set_battle_interrupt_busy(g, 0);
    set_battle_nmi_pending(g, 0);
    return true;
}

/* As C. */
typedef struct {
    GameState *game;
    Platform *pf;
} HandlerAsC;

enum { PAGE_CELLS = 0x7E0000 | BATTLE_PAGE, CELLS_ASIDE = 0x7E0000 | SCRATCH_ASIDE };

static void scratch_aside(void *ctx) { wram_copy(((HandlerAsC *)ctx)->game, PAGE_CELLS, CELLS_ASIDE, SCRATCH_BYTES); }
static void scratch_back(void *ctx) { wram_copy(((HandlerAsC *)ctx)->game, CELLS_ASIDE, PAGE_CELLS, SCRATCH_BYTES); }

static bool frame_work(void *ctx)
{
    HandlerAsC *c = ctx;
    return battle_frame_dispatched(c->game, c->pf);
}

static bool map_layers(void *ctx)
{
    GameState *g = ((HandlerAsC *)ctx)->game;
    TIMED_AS(g, GAME_FRAME, battle_scroll_the_map_layers(g));
    return true;
}

void battle_vblank(GameState *g, Platform *pf)
{
    static const VblankCalls as_c = { scratch_aside, frame_work, map_layers, scratch_back };
    HandlerAsC c = { g, pf };
    (void)take_the_vblank(g, pf, &as_c, &c);
}

/* On the machine: the scratch moved with the original's block moves (which
 * leave Y at the move's end, as the frame work reads it), and the frame work
 * on the data bank $00 with A $0080, each call from the original's. */
static void scratch_aside_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    m_block_move(s, BATTLE_DATA_BANK, SCRATCH_ASIDE, BATTLE_DATA_BANK, BATTLE_PAGE, SCRATCH_BYTES);
    s->cpu.db = 0x00;
}

static void scratch_back_on_the_machine(void *ctx)
{
    m_block_move(ctx, BATTLE_DATA_BANK, BATTLE_PAGE, BATTLE_DATA_BANK, SCRATCH_ASIDE, SCRATCH_BYTES);
}

static bool frame_work_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    s->cpu.a = 0x0080;
    m_jsl_dc(s, 0xD1E7DA, FRAME_WORK, DC_X | DC_N | DC_Z | DC_C);
    return came_back(s);
}

static bool map_layers_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    m_jsl_dc(s, 0xD1E7E3, GAME_FRAME, DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C);
    return came_back(s);
}

void battle_nmi(Snes *s)
{
    static const VblankCalls on_the_machine = { scratch_aside_on_the_machine, frame_work_on_the_machine,
                                                map_layers_on_the_machine, scratch_back_on_the_machine };
    m_save_interrupted_registers_bank_first(s);
    s->cpu.d = BATTLE_PAGE;
    s->cpu.a = 0;
    m_sep(s, CPU_FLAG_M);
    if (take_the_vblank(state_of(s), m_platform(s), &on_the_machine, s))
        m_return_to_interrupted_bank_first(s);
}

/* It leaves A 8-bit, the last operand it set. */
void battle_restore_math_operands(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t last = last_math_operand(g);
    ORIGINAL_EFFECT(restore_math_operands(g));
    m_set_a8(s, last);
    m_set_nz8(s, last);
    m_rtl(s);
}

/* ---- The line handler ------------------------------------------------------------------- */

/* The alternating-frame setting: an arrangement of the line effects
 * (battle_arrange_line_effects), its base plus the request, offset by the
 * VRAM copy mode. */
enum { ARRANGE_LINE_EFFECTS = 0xCEEE6E, TILE_ANIMATION_UPLOADS = 0xFDFFFD };

/* Whether the alternating-frame setting must be applied: the request changed
 * (and is latched), or an application was asked for. */
static bool alternate_wanted(GameState *g)
{
    uint8_t request = battle_alternate_request(g);
    if (request != battle_alternate_latched(g)) {
        set_battle_alternate_latched(g, request);
        return true;
    }
    return battle_alternate_apply(g) != 0;
}

// $D1:FE4C
/* On the line handler's even frames: apply the alternating-frame setting when
 * it is wanted, and clear the ask. */
static void apply_the_alternate_frame(GameState *g)
{
    if (alternate_wanted(g)) {
        uint8_t arrangement = (uint8_t)(battle_alternate_base(g) + battle_alternate_latched(g) + battle_vram_copy_mode(g));
        TIMED_AS(g, ARRANGE_LINE_EFFECTS, battle_arrange_line_effects(g, arrangement));
        set_battle_alternate_apply(g, 0);
    }
}

void battle_apply_alternate_frame(Snes *s)
{
    apply_the_alternate_frame(state_of(s));
    m_rts(s);
}

/* The line handler's start: the handler marked busy, the line interrupt
 * acknowledged (NMI and auto-joypad kept on) and the line effects off; the
 * line let be drawn and the rest of the frame blanked. */
static void begin_line_handler(GameState *g, Platform *pf)
{
    set_battle_interrupt_busy(g, (uint8_t)(battle_interrupt_busy(g) + 1));
    tm_ack_line_irq(pf, PF_AT(0xD1FEA8));
    tm_enable_interrupts(pf, PF_AT(0xD1FEAE), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
    vid_enable_line_effects(pf, PF_AT(0xD1FEB5), 0);
    tm_wait_line_drawn(pf, PF_LOOP(0xD1FEB8, 0xD1FEBF));
    vid_raster_display(pf, PF_AT(0xD1FEC1), VID_AT_LINE_HANDLER, &(VidDisplay){ .blank = true });
}

// $D1:FE97
/* The battle's line handler, at the line its picture ends after: the picture
 * ended (begin_line_handler); then on even frames the alternating-frame
 * setting and on odd ones, unless paused, the tile animation uploads (a
 * routine of all the modes', core/tile_animation.c); then the math operands
 * back. False where the uploads went no further. */
static bool take_the_line(GameState *g, Platform *pf, bool (*tile_animation_uploads)(void *ctx), void *ctx)
{
    begin_line_handler(g, pf);
    if (!(battle_frame_count(g) & 0x01))
        TIMED_AS(g, 0xD1FE4C, apply_the_alternate_frame(g));
    else if (!battle_paused(g) && !tile_animation_uploads(ctx))
        return false;
    ORIGINAL_EFFECT(restore_math_operands(g));
    set_battle_interrupt_busy(g, 0);
    return true;
}

/* As C, the uploads' guard turning away only the machine's stack. */
static bool tile_animation_uploads(void *ctx)
{
    HandlerAsC *c = ctx;
    TIMED_AS(c->game, TILE_ANIMATION_UPLOADS, tile_animations_send(c->game, c->pf));
    return true;
}

void battle_line(GameState *g, Platform *pf)
{
    HandlerAsC c = { g, pf };
    (void)take_the_line(g, pf, tile_animation_uploads, &c);
}

/* On the machine the uploads run under the flags of the pause's test,
 * pushed. The page is the battle's and the data bank the registers', as the
 * original's has them at its wait. */
static bool tile_animation_uploads_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    m_set_a8(s, 0);
    m_set_nz8(s, 0);
    m_push8(s, s->cpu.p);
    if (!battle_run_long(s, 0xD1FEDA, TILE_ANIMATION_UPLOADS, tile_animation_upload) || !battle_widths_m8_x16(s))
        return false;
    (void)m_pull8(s);   /* the pause test's flags off the stack; the return puts the interrupted code's back */
    return true;
}

void battle_irq(Snes *s)
{
    m_save_interrupted_registers_bank_first(s);
    s->cpu.d = BATTLE_PAGE;
    s->cpu.db = 0x00;
    m_sep(s, CPU_FLAG_M);
    if (take_the_line(state_of(s), m_platform(s), tile_animation_uploads_on_the_machine, s))
        m_return_to_interrupted_bank_first(s);
}
