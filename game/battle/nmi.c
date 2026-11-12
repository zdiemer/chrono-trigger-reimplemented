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
 * The vblank handler moves the battle's low work cells, which the frame work
 * reuses as scratch, aside around its work and puts them back after, so the
 * work it interrupted finds them as it left them. */
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame.h"
#include "battle/program.h"
#include "battle/screen_data.h"
#include "charge.h"
#include "core/tile_animation.h"
#include "cpu_io.h"
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

/* The battle's low work cells, which the frame work uses as scratch, are
 * moved aside around it. */
enum { SCRATCH_BYTES = 43, SCRATCH_ASIDE = 0xCC65 };

/* ---- The vblank handler ----------------------------------------------------------------- */

/* The game's per-frame work, the map's layers scrolled. */
enum { GAME_FRAME = BATTLE_SCROLL_MAP_LAYERS_ENTRY };

enum { PAGE_CELLS = 0x7E0000 | BATTLE_PAGE, CELLS_ASIDE = 0x7E0000 | SCRATCH_ASIDE };

/* The battle's vblank handler: the scratch moved aside and back around the
 * frame work for the battle's mode (battle/frame.c) and, unless paused, the
 * game's per-frame work, the map's layers scrolled; then the pause and the
 * frame advance taken. Where the frame work goes no further, so does the
 * handler. */
void battle_vblank(GameState *g, Platform *pf)
{
    vid_enable_line_effects(pf, PF_AT(0xD1E78A), 0);
    tm_ack_vblank(pf, PF_AT(0xD1E78E));
    tm_enable_interrupts(pf, PF_AT(0xD1E794), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD);
    if (battle_interrupt_busy(g))
        return;
    set_battle_interrupt_busy(g, (uint8_t)(battle_interrupt_busy(g) + 1));
    wram_copy(g, PAGE_CELLS, CELLS_ASIDE, SCRATCH_BYTES);
    latch_split_lines(g);
    vid_enable_line_effects(pf, PF_AT(0xD1E7CC), 0);
    tm_reset_beam_latch(pf, PF_AT(0xD1E7CF));
    vid_set_display(pf, PF_AT(0xD1E7D4), &(VidDisplay){ .blank = true });
    vid_start_vram_uploads(pf, PF_AT(0xD1E7D7), VID_VRAM_WORD_PORT);
    if (!battle_frame_dispatched(g, pf))
        return;
    if (!battle_paused(g)) {
        TIMED_AS(g, GAME_FRAME, battle_scroll_the_map_layers(g));
        count_frame(g);
    }
    take_pause(g, pf);
    take_frame_advance(g, pf);
    wram_copy(g, CELLS_ASIDE, PAGE_CELLS, SCRATCH_BYTES);
    set_battle_interrupt_busy(g, 0);
    set_battle_nmi_pending(g, 0);
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
 * back. */
void battle_line(GameState *g, Platform *pf)
{
    begin_line_handler(g, pf);
    if (!(battle_frame_count(g) & 0x01))
        TIMED_AS(g, 0xD1FE4C, apply_the_alternate_frame(g));
    else if (!battle_paused(g))
        TIMED_AS(g, TILE_ANIMATION_UPLOADS, tile_animations_send(g, pf));
    set_battle_interrupt_busy(g, 0);
}
