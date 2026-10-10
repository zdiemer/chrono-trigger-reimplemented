/* Battle: the frame work and the frame update.
 *
 * The frame work is what the vblank handler runs for the battle each frame
 * (battle/nmi.c): while the battle is set up, the display's setup
 * (battle/display.c); in the fight, the frame work proper. That sends what
 * the frame built (battle/uploads.c), sets the window, slides the battle
 * window in or out, and then either, while the fight is paused, only dims the
 * screen and reads the buttons, or runs the frame: the layers' scroll and the
 * screen shake (battle/scroll.c), the window's arrangement, the HP gradient,
 * the sprites hidden and redrawn, the animation scripts' frame and palette
 * animation.
 *
 * The frame update is the part that moves and draws the battlefield: the
 * colors that blink, cycle and rotate, the frame counts, a demo battle's
 * exit, the party running away while L and R are held, and the effects,
 * paths and objects (battle/effect_paths.c, battle/object_motion.c,
 * battle/draw_objects.c), which are still reached as the original's calls.
 *
 * Most of the frame work's steps are other routines, many still the
 * original's, so the frame work and the frame update are bridges: they run
 * the ported steps as C and call the rest where the original does, on the
 * battle's page and data bank. */
#include "battle/frame.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame_data.h"
#include "battle/screen.h"
#include "battle/screen_data.h"
#include "original_effects.h"
#include "platform/input.h"
#include "platform/video.h"
#include "port/native.h"
#include "vars.h"

/* ---- The sprites ------------------------------------------------------------------------ */

/* The battle builds its sprites afresh each frame: every sprite but the
 * menu's four cursors is put below the screen, all of them large, and the
 * objects then draw themselves into the table (battle/draw_objects.c). */
enum { CURSOR_SPRITES = 4, BELOW_THE_SCREEN = 0xF0, FOUR_LARGE_SPRITES = 0xAA, CURSORS_HIGH_BYTE = 0 };

// $CE:F066
void battle_hide_sprites(GameState *g)
{
    for (unsigned sprite = CURSOR_SPRITES; sprite < OAM_BUFFER_COUNT; sprite++)
        set_oam_buffer_y(g, sprite, BELOW_THE_SCREEN);
    for (unsigned at = CURSORS_HIGH_BYTE + 1; at < OAM_HIGH_TABLE_BYTES; at++)
        set_oam_high_table_byte(g, at, FOUR_LARGE_SPRITES);
}

/* The cursors take their palette from the battle's cursor setting each frame. */
enum { SPRITE_PALETTE_BITS = 0x0E };

static void color_the_cursors(GameState *g)
{
    for (unsigned sprite = 0; sprite < CURSOR_SPRITES; sprite++)
        set_oam_buffer_attributes(g, sprite, (uint8_t)((oam_buffer_attributes(g, sprite) & ~SPRITE_PALETTE_BITS)
                                                       | battle_cursor_priority(g)));
}

/* ---- The window ------------------------------------------------------------------------- */

/* The window's settings go to the video each frame from their shadows. */
static void set_the_windows(GameState *g, Platform *pf)
{
    static const struct {
        uint32_t fields, site;
    } shadows[5] = {
        { VID_WINDOWS_BG1_BG2, 0xCD08BB }, { VID_WINDOWS_BG3_BG4, 0xCD08C2 }, { VID_WINDOWS_OBJ_BACK, 0xCD08C9 },
        { VID_WINDOWS_BG_LOGIC, 0xCD08D0 }, { VID_WINDOWS_OBJ_LOGIC, 0xCD08D7 },
    };
    VidWindows w = { 0 };
    for (unsigned i = 0; i < 5; i++) {
        vid_windows_decode(&w, shadows[i].fields, battle_window_registers(g, i));
        vid_set_windows(pf, PF_AT(shadows[i].site), &w, shadows[i].fields);
    }
}

/* The lines below the window's split take the battle's fixed color, the sixth
 * of the split's values. */
enum { FIXED_COLOR_VALUE = 5 };

static void color_below_the_split(GameState *g)
{
    uint16_t value = battle_split_line_values(g, FIXED_COLOR_VALUE);
    set_battle_split_line_values(g, FIXED_COLOR_VALUE, (uint16_t)((value & 0xFF00) | battle_fixed_color(g)));
}

/* The window's top border starts with the edges of its next line. */
static void square_the_window_top(GameState *g)
{
    set_battle_window_top_lines_w1(g, 0, battle_window_top_lines_w1(g, 1));
    set_battle_window_top_lines_w2(g, 0, battle_window_top_lines_w2(g, 1));
}

/* The window has two arrangements (battle_alternate_request), which the line
 * handler applies. On even frames, while the window isn't sliding, the
 * arrangement follows the one the battle wants, and the player's button flips
 * it, once a press; not in a demo battle. */
enum { FLIP_BUTTON = 0x01, HELD_BUTTONS = 1 };

static void flip_the_arrangement(GameState *g)
{
    uint8_t arrangement = (uint8_t)((battle_alternate_request(g) + 1) & 1);
    set_battle_alternate_request(g, arrangement);
    set_battle_alternate_frame(g, arrangement);
}

static void choose_the_arrangement(GameState *g)
{
    if (battle_alternate_frame(g) != battle_alternate_request(g))
        flip_the_arrangement(g);
    bool pressed = !(battle_setup_flags_copy(g) & BATTLE_SETUP_DEMO) && (input_mapped(g, HELD_BUTTONS) & FLIP_BUTTON);
    if (!pressed) {
        set_battle_alternate_toggle_held(g, 0);
    } else if (!battle_alternate_toggle_held(g)) {
        flip_the_arrangement(g);
        set_battle_alternate_toggle_held(g, 1);
    }
}

/* ---- The battle window's slide ------------------------------------------------------------ */

/* The battle window slides in or out of view a line at a time, by moving the
 * split between it and the battlefield in all the per-line effects' tables.
 * Sliding in starts with the battlefield's arrangement put back; once it is
 * in, the window's arrangement follows and the fight may be paused. Once it
 * is out, the battlefield's arrangement follows. The slide's state is a
 * byte: 0 still, $FF about to slide in, positive sliding in, other negatives
 * sliding out. */
enum { SLIDE_STILL = 0, SLIDE_STARTING_IN = 0xFF, SLIDE_OUT = 0x80, SLIDE_IN_STARTED = 1 };
enum { BATTLEFIELD_ARRANGEMENT = 0, WINDOW_ARRANGEMENT = 2 };

static bool sliding_out(uint8_t slide) { return (slide & SLIDE_OUT) != 0; }

/* Where a slide ends: the arrangement changes, and a slide in allows the
 * pause again. */
static void arrange_for(GameState *g, uint8_t arrangement)
{
    set_battle_alternate_base(g, arrangement);
    set_battle_alternate_apply(g, (uint8_t)(battle_alternate_apply(g) + 1));
}

static void end_the_slide(GameState *g)
{
    if (!sliding_out(battle_window_slide(g)))
        set_battle_pause_blocked(g, 0);
    set_battle_window_slide(g, SLIDE_STILL);
}

/* A line of the slide, in the per-line tables (battle/display.c). The frame
 * it counts down is the slide's. */
static void slide_a_line(Snes *s, bool out)
{
    GameState *g = state_of(s);
    NATIVE_TIME_AS(s, out ? 0xCD0577 : 0xCD053C, battle_slide_the_split(g, out));
    uint8_t left = (uint8_t)(battle_window_slide_frames(g) - 1);
    set_battle_window_slide_frames(g, left);
    if (left == 0)
        arrange_for(g, out ? BATTLEFIELD_ARRANGEMENT : WINDOW_ARRANGEMENT);
    if (left == 0 && out)
        end_the_slide(g);
}

// $CD:05B2
/* The slide's frame; a slide in ends the frame after its last line. */
static bool slide_the_window(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t slide = battle_window_slide(g);
    if (slide == SLIDE_STILL)
        return true;
    if (slide == SLIDE_STARTING_IN) {
        set_battle_alternate_base(g, BATTLEFIELD_ARRANGEMENT);
        NATIVE_TIME_AS(s, 0xCEEE6E, battle_arrange_line_effects(g, battle_alternate_latched(g)));
        battle_leave_arrangement_registers(s);
        set_battle_window_slide(g, SLIDE_IN_STARTED);
        return true;
    }
    if (!sliding_out(slide) && battle_window_slide_frames(g) == 0) {
        end_the_slide(g);
        return true;
    }
    slide_a_line(s, sliding_out(slide));
    return true;
}

/* ---- The HP gradient's phase ------------------------------------------------------------- */

/* The HP gradient's rows turn a phase at a time, thirteen phases round, in
 * step with palette animation slot 0: on the frames that slot isn't counting
 * a delay. */
enum { GRADIENT_LAST_PHASE = 13, GRADIENT_PACE_SLOT = 0 };

// $CD:0ADE
void battle_turn_the_gradient(GameState *g)
{
    if (palette_animations_delay(g, GRADIENT_PACE_SLOT) != 0)
        return;
    int8_t phase = (int8_t)(battle_gradient_phase(g) - 1);
    set_battle_gradient_phase(g, phase < 0 ? GRADIENT_LAST_PHASE : (uint8_t)phase);
}

/* ---- The frame update --------------------------------------------------------------------- */

/* Low HP digits blink, a phase every twelve frames. */
enum { BLINK_FRAMES = 12 };

static void count_the_blink(GameState *g)
{
    uint8_t left = (uint8_t)(battle_blink_timer(g) - 1);
    set_battle_blink_timer(g, left ? left : BLINK_FRAMES);
    if (!left)
        set_battle_blink(g, battle_blink(g) ^ 1);
}

/* While combatant 3 flickers, combatant 9 wears its palette. */
enum { FLICKERING = 3, WEARS_ITS_PALETTE = 9 };

static void mirror_the_flicker(GameState *g)
{
    if (battle_effect_mirror_slot3(g))
        set_battle_object_palette(g, WEARS_ITS_PALETTE, battle_object_palette(g, FLICKERING));
}

/* The cursors' palette has a color that cycles through 62 phases: entry 14
 * of sprite palette 1, or of sprite palette 5 when the cursors use that (the
 * palette bit flipped while the combatants' palettes are). */
enum { COLOR_PHASES = 0x3E, CURSOR_COLOR = 128 + 16 * 1 + 14, OTHER_CURSOR_COLOR = 128 + 16 * 5 + 14,
       CURSOR_UPPER_PALETTE = 0x08 };

static void cycle_the_cursor_color(GameState *g)
{
    uint8_t phase = (uint8_t)(battle_color_phase(g) + 1);
    if (phase == COLOR_PHASES)
        phase = 0;
    set_battle_color_phase(g, phase);
    uint8_t palette = battle_cursor_priority(g);
    if (battle_sprite_palettes_flipped(g))
        palette ^= CURSOR_UPPER_PALETTE;
    unsigned color = (palette & CURSOR_UPPER_PALETTE) ? OTHER_CURSOR_COLOR : CURSOR_COLOR;
    set_palette_working(g, color, battle_animated_color(g, (uint8_t)(phase * 2)));
}

/* Colors that rotate: a run of colors each moves up one, and the top one comes
 * round to the bottom. The original stores the top one a word below where the
 * last move read, as a 16-bit index: a run from the palette's very first color
 * puts it a bank on instead, past the palette (the word 0x8000 colors on),
 * and color 0 keeps its own. The game can see that, so it is kept. */
enum { A_BANK_ON = 0x8000 };

static void rotate_colors_up(GameState *g, unsigned first, unsigned count)
{
    unsigned top = first + count - 1;
    uint16_t kept = palette_working(g, top);
    for (unsigned color = top; color > first; color--)
        set_palette_working(g, color, palette_working(g, color - 1));
    if (first == 0)
        set_palette_working_unchecked(g, A_BANK_ON, kept);
    else
        set_palette_working(g, first, kept);
}

/* Every so many frames, while the rotation is on: sprite palette 4's colors
 * 1-12; and with the background's cycle on, the background palette it names
 * from the color it names up, and colors 1-15 of every palette after it, as
 * far as the last background palette (at least one). */
enum { ROTATED_SPRITE_COLORS = 128 + 16 * 4 + 1, SPRITE_COLORS_ROTATED = 12, PALETTE_COLORS = 16,
       BACKGROUND_PALETTES = 8 };

/* The original works the rotation in the effect loops' cells: the color it
 * last kept, the moves it counted down, and where the last run started. */
static void leave_rotation_cells(GameState *g, uint16_t kept, uint16_t start)
{
    set_battle_effect_cells(g, 0, (uint8_t)kept);
    set_battle_effect_cells(g, 1, (uint8_t)(kept >> 8));
    set_battle_effect_cells(g, 2, 0);
    set_battle_effect_cells(g, 3, 0);
    set_battle_effect_cells(g, 4, (uint8_t)start);
    set_battle_effect_cells(g, 5, (uint8_t)(start >> 8));
}

static void rotate_the_colors(GameState *g)
{
    if (!battle_color_rotation_on(g))
        return;
    uint8_t left = (uint8_t)(battle_color_rotation_count(g) - 1);
    set_battle_color_rotation_count(g, left ? left : battle_color_rotation_period(g));
    if (left)
        return;
    rotate_colors_up(g, ROTATED_SPRITE_COLORS, SPRITE_COLORS_ROTATED);
    uint16_t kept = palette_working(g, ROTATED_SPRITE_COLORS);
    if (battle_background_cycle_on(g)) {
        uint8_t cycle = battle_background_cycle(g);
        unsigned palette = cycle >> 4, first = cycle & 0x0F;
        rotate_colors_up(g, palette * PALETTE_COLORS + first, PALETTE_COLORS - first);
        /* the palettes after it, by their byte offset in the palette, which
         * wraps from the last palette round to the first */
        unsigned next = (uint8_t)((palette + 1) * PALETTE_COLORS) / PALETTE_COLORS;
        do {
            rotate_colors_up(g, next * PALETTE_COLORS + 1, PALETTE_COLORS - 1);
            next++;
        } while (next < BACKGROUND_PALETTES);
        kept = palette_working(g, (next - 1) * PALETTE_COLORS + 1);
        ORIGINAL_EFFECT(leave_rotation_cells(g, kept, (uint16_t)((next * PALETTE_COLORS + PALETTE_COLORS - 1) * 2)));
        return;
    }
    ORIGINAL_EFFECT(set_battle_effect_cells(g, 0, (uint8_t)kept));
    ORIGINAL_EFFECT(set_battle_effect_cells(g, 1, (uint8_t)(kept >> 8)));
}

/* A demo battle ends on any button: the screen fades out, and the game goes
 * back to its title. True when it is time to leave. */
enum { LEAVING_THE_BATTLE = 0x80 };

static bool any_button_held(Platform *pf)
{
    return (in_read_low(pf, PF_AT(0xCFE6E0), 0) | in_read_high(pf, PF_AT(0xCFE6E4), 0)) != 0;
}

static bool end_the_demo(GameState *g, Platform *pf)
{
    if (!battle_is_demo(g))
        return false;
    if (any_button_held(pf))
        set_battle_demo_exiting(g, 1);
    if (!battle_demo_exiting(g))
        return false;
    uint8_t brightness = (uint8_t)(battle_brightness(g) - 1);
    set_battle_brightness(g, brightness);
    if (brightness != 0)
        return false;
    set_battle_leaving(g, LEAVING_THE_BATTLE);
    return true;
}

/* Holding L and R together for two seconds makes the party run, where the
 * battle allows it; the time counts on while they are held. */
enum { BUTTONS_L_R = IN_BUTTON_L | IN_BUTTON_R, RUN_AFTER_FRAMES = 120 };

static void hold_to_run(GameState *g, Platform *pf)
{
    if (battle_effects_running(g))
        return;
    if ((in_read_low(pf, PF_AT(0xCFE70B), 0) & BUTTONS_L_R) != BUTTONS_L_R) {
        set_battle_lr_state(g, 0);
        set_battle_lr_held(g, 0);
        return;
    }
    uint16_t held = (uint16_t)(battle_lr_held(g) + 1);
    set_battle_lr_held(g, held);
    if (held >= RUN_AFTER_FRAMES && !(battle_setup_flags(g) & BATTLE_SETUP_NO_RUNNING))
        set_battle_run_counter(g, (uint16_t)(battle_run_counter(g) + 1));
}

/* The frame counts: a byte every frame and another every fourth. */
static void tick(GameState *g)
{
    uint8_t ticks = (uint8_t)(battle_frame_tick(g) + 1);
    set_battle_frame_tick(g, ticks);
    if (!(ticks & 3))
        set_battle_frame_tick_4(g, (uint8_t)(battle_frame_tick_4(g) + 1));
}

/* A combatant's graphics built for the frame after next are sent next frame,
 * while effects run. */
enum { GRAPHICS_UPLOAD_NEXT_FRAME = 2 };

static void bring_the_upload_forward(GameState *g)
{
    if (battle_effects_running(g) && battle_graphics_upload_request(g) == GRAPHICS_UPLOAD_NEXT_FRAME)
        set_battle_graphics_upload_request(g, GRAPHICS_UPLOAD_NEXT_FRAME - 1);
}

/* The frame update's call-outs. Their callees read nothing of the registers
 * but A's high byte, which is 0 here as the original's frame count leaves
 * it. */
enum { OBJECT_WORK_IGNORES = DC_X | DC_Y | DC_FLAGS | DC_AL };

static bool object_work(Snes *s, uint32_t site, uint32_t routine)
{
    return battle_call(s, site, routine, OBJECT_WORK_IGNORES);
}

// $CF:E58A
/* The effect sprites' attributes are kept on the stack across the object
 * work, where the original keeps them, and put back after it. */
static bool update_the_frame(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    count_the_blink(g);
    mirror_the_flicker(g);
    if (!object_work(s, 0xCFE5A7, 0xCFE7D7))
        return false;
    cycle_the_cursor_color(g);
    set_battle_frames(g, battle_frames(g) + 1);
    s->cpu.a = 0;
    rotate_the_colors(g);
    color_the_cursors(g);
    m_push8(s, battle_effect_sprite_attributes(g));
    if (end_the_demo(g, pf)) {
        m_stop_at_own(s, 0xC0FF00, DC_A | DC_X | DC_FLAGS | DC_DB | DC_D);
        return false;
    }
    hold_to_run(g, pf);
    tick(g);
    if (!object_work(s, 0xCFE748, 0xCFEFAF))
        return false;
    if (battle_effects_running(g)
        && (!object_work(s, 0xCFE750, 0xCFEC78) || !object_work(s, 0xCFE753, 0xCFECC2)
            || !object_work(s, 0xCFE756, 0xCFED18) || !object_work(s, 0xCFE759, 0xCFE781)))
        return false;
    if (battle_formation(g) && !object_work(s, 0xCFE761, 0xCFF9D3))
        return false;
    if (!battle_sprites_frozen(g)) {
        bring_the_upload_forward(g);
        if (!object_work(s, 0xCFE778, 0xCFE83A))
            return false;
    }
    set_battle_frame_pending(g, 0);
    set_battle_effect_sprite_attributes(g, m_pull8(s));
    return true;
}

/* ---- The frame work ----------------------------------------------------------------------- */

/* The frame work's call-outs read nothing of the registers but what each
 * names: the button reading reads Y, the gradient, the sound's and the
 * battlefield's routines A's high byte, which is 0 on the battle's page. */
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_FLAGS, READS_Y = DC_A | DC_X | DC_FLAGS,
       READS_A_HIGH = DC_X | DC_Y | DC_FLAGS | DC_AL };

/* While paused, the screen is dimmed to half, unless the ROM's debug frame
 * advance is on. */
static uint8_t paused_brightness(const GameState *g)
{
    return battle_debug_frame_advance(g) ? battle_brightness(g) : battle_brightness(g) >> 1;
}

/* The buttons are read and their repeat counted, paused or not. */
static bool read_the_buttons(Snes *s, uint32_t read_site, uint32_t repeat_site)
{
    return battle_call_long(s, read_site, 0xC28002, READS_Y) && battle_call(s, repeat_site, 0xCD0340, READS_NOTHING);
}

/* The HP gradient, where the battle's background has one: the background's
 * ripple each frame, and the gradient's step on the frames its pace allows.
 * The step takes A's high byte along, which the ripple leaves clear. */
static bool run_the_gradient(Snes *s)
{
    GameState *g = state_of(s);
    if (!(battle_background_flags(g) & BATTLE_BACKGROUND_HAS_GRADIENT))
        return true;
    NATIVE_TIME_AS(s, 0xCEE18E, battle_ripple_background(g));
    if ((uint8_t)battle_frame_count(g) & battle_gradient_frame_mask(g))
        return true;
    s->cpu.a = 0;
    return battle_call_long(s, 0xCD0985, 0xD1EA4B, READS_A_HIGH);
}

/* While effects run, the animation scripts' frame starts once the beam is at
 * line 64, unless a script said not to wait. */
enum { LINE_TO_WAIT_FOR = 0x40 };

static bool wait_for_the_line(Snes *s)
{
    GameState *g = state_of(s);
    if (battle_effect_line_wait_off(g) || !battle_effects_running(g) || battle_sprites_frozen(g))
        return true;
    return battle_wait_line(s, 0xCD09AC, 0xCD3B82, DC_X | DC_Y | DC_FLAGS | DC_AH | DC_DB, LINE_TO_WAIT_FOR);
}

/* The frame's uploads: all of them in the fight, or while the frame work is
 * cut down, the palette and a queued transfer only. The vblank's other
 * uploads wait while the window slides or the fight is paused. */
static void send_the_frame(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    if (battle_frame_work_reduced(g)) {
        NATIVE_TIME_AS(s, 0xCD09CE, battle_send_palette(pf));
        NATIVE_TIME_AS(s, 0xCD3EB4, battle_send_queued_transfer(g, pf));
        return;
    }
    if (battle_window_slide(g) == SLIDE_STILL)
        NATIVE_TIME_AS(s, 0xCD0607, battle_send_block_and_message_tiles(g, pf));
    NATIVE_TIME_AS(s, 0xCD3EB4, battle_send_queued_transfer(g, pf));
    if (!battle_paused(g))
        NATIVE_TIME_AS(s, 0xCFE300, battle_send_vblank_uploads(g, pf));
}

/* The shake puts its offsets on the layers with A's high byte as theirs,
 * which the scroll leaves 0; anything else is the original's. It leaves A's
 * high byte 0. */
static bool shake_the_screen(Snes *s)
{
    if (s->cpu.a & 0xFF00)
        return battle_call(s, 0xCD092C, 0xCD0B51, DC_X | DC_Y | DC_FLAGS | DC_AL);
    NATIVE_TIME_AS(s, 0xCD0B51, battle_shake_screen(state_of(s)));
    s->cpu.a = 0;
    return true;
}

/* The sprites are hidden before the objects draw, unless a script keeps them
 * or they are frozen; the original's clearing leaves A the page (0). */
static void hide_the_sprites(Snes *s)
{
    GameState *g = state_of(s);
    if (battle_keep_sprites(g) || battle_sprites_frozen(g))
        return;
    NATIVE_TIME_AS(s, 0xCEF066, battle_hide_sprites(g));
    s->cpu.a = BATTLE_PAGE;
}

/* The frame update, through the original's vector at $C1:000C. It runs here
 * with A's high byte 0, as every call the game makes has it; any other is the
 * original's. */
static bool update_the_battlefield(Snes *s)
{
    if (s->cpu.a & 0xFF00)
        return battle_call_long(s, 0xCD0997, 0xC1000C, DC_X | DC_Y | DC_FLAGS | DC_AL);
    return battle_run_long(s, 0xCD0997, 0xCFE58A, battle_frame_update);
}

/* An enemy's rule that cycles the background's colors (battle_enemy_rule,
 * with the cycle's first color in battle_background_cycle) keeps the battle's
 * own palette animations (slots 8-11) short of the cycle: one that would run
 * past its first color is cut to end there. A script lifts the limit by
 * setting the rule's top bit (the special effect that rotates the cycle
 * itself). */
enum { NO_ANIMATION = 0, OFF_THE_PALETTE = 0x80 };

// $CD:0B1E
void battle_keep_animations_off_the_cycle(GameState *g)
{
    uint8_t rule = battle_enemy_rule(g), cycle = battle_background_cycle(g);
    if (rule == 0 || (rule & BATTLE_RULE_LIFTED))
        return;
    for (unsigned slot = BATTLE_FIRST_OWN_ANIMATION; slot < BATTLE_FIRST_OWN_ANIMATION + BATTLE_OWN_ANIMATIONS; slot++) {
        uint8_t first = palette_animations_first_color(g, slot);
        if (palette_animations_kind(g, slot) == NO_ANIMATION || (first & OFF_THE_PALETTE))
            continue;
        if ((uint8_t)(first + palette_animations_color_count(g, slot)) > cycle)
            set_palette_animations_color_count(g, slot, (uint8_t)(cycle - first));
    }
}

void battle_keep_animations_off_the_cycle_entry(Snes *s)
{
    battle_keep_animations_off_the_cycle(state_of(s));
    m_rts(s);
}

/* Palette animation: the animated slots kept off the background cycle's
 * colors, paused with the battle, and run. */
static bool animate_the_palettes(Snes *s)
{
    NATIVE_TIME_AS(s, 0xCD0B1E, battle_keep_animations_off_the_cycle(state_of(s)));
    return battle_call_long(s, 0xCD09BB, 0xD1F426, READS_NOTHING) && battle_call_long(s, 0xCD09BF, 0xFDFFF7, READS_NOTHING);
}

// $CD:088D
/* The frame work runs with the data bank $7E, on the battle's page. */
static bool work_the_frame(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    s->cpu.db = BATTLE_DATA_BANK;
    send_the_frame(s);
    set_the_windows(g, pf);
    color_below_the_split(g);
    square_the_window_top(g);
    NATIVE_TIME_AS(s, 0xCD0AEE, battle_show_window_table(g, pf));
    if (!battle_run(s, 0xCD08F5, 0xCD05B2, battle_step_window_slide)
        || !battle_run(s, 0xCD08F8, 0xCD05B2, battle_step_window_slide))
        return false;
    vid_enable_line_effects(pf, PF_AT(0xCD08FE), battle_line_effects_on(g));
    if (battle_paused(g)) {
        VidDisplay dimmed = vid_display_decode(paused_brightness(g));
        vid_set_display(pf, PF_AT(0xCD0916), &dimmed);
        return read_the_buttons(s, 0xCD091A, 0xCD091E);
    }
    VidDisplay display = vid_display_decode(battle_brightness(g));
    vid_set_display(pf, PF_AT(0xCD0925), &display);
    if (!battle_scroll_the_layers(s) || !shake_the_screen(s))
        return false;
    if (!(battle_frame_count(g) & 1) && battle_window_slide(g) == SLIDE_STILL)
        choose_the_arrangement(g);
    if (!run_the_gradient(s))
        return false;
    hide_the_sprites(s);
    if (!update_the_battlefield(s) || !wait_for_the_line(s) || !battle_run(s, 0xCD09AF, 0xCD1609, battle_run_screen_scripts))
        return false;
    set_battle_sprites_frozen(g, battle_sprites_frozen_setting(g));
    if (!animate_the_palettes(s))
        return false;
    NATIVE_TIME_AS(s, 0xCD0ADE, battle_turn_the_gradient(g));
    return read_the_buttons(s, 0xCD09C6, 0xCD09CA);
}

/* ---- The frame wait's tail ---------------------------------------------------------------- */

// $CD:04AA
/* After the main loop's wait for the frame: unless paused, a sound an
 * animation script asked for; the sound's pause; then, unless a routine is
 * waiting on a queued transfer, the battlefield put back once the effects
 * stop, and an effect's graphics loaded where a script asked for them and
 * none are loading; and the menu's work for the frame. */
static bool finish_the_wait(Snes *s)
{
    GameState *g = state_of(s);
    if (!battle_paused(g) && !battle_call(s, 0xCD04AF, 0xCD3B5F, READS_A_HIGH))
        return false;
    if (!battle_call(s, 0xCD04B2, 0xCD3E44, READS_A_HIGH))
        return false;
    if (!battle_vram_request_busy(g)) {
        if (battle_background_restore_request(g) && !battle_effects_running(g)
            && !battle_call(s, 0xCD04C4, 0xCD0453, READS_A_HIGH))
            return false;
        if (battle_effect_graphics_wanted(g) && !battle_effect_graphics_loading(g)) {
            m_set_a8(s, battle_effect_graphics_effect(g));
            if (!battle_call_long(s, 0xCD04D4, 0xD1ECF3, DC_X | DC_Y | DC_FLAGS))
                return false;
            set_battle_effect_graphics_wanted(g, 0);
        }
    }
    return battle_call(s, 0xCD04DB, 0xCD04DF, READS_NOTHING);
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The battle's modes, as the frame work runs them: the display's setup, then
 * the fight (and its end, which runs the same frame work). */
enum { FRAME_MODES = 3 };
static const struct {
    uint32_t routine;
    void (*entry)(Snes *);
} frame_work_of[FRAME_MODES] = {
    { 0xCD09F1, battle_setup_display },
    { 0xCD088D, battle_frame_work },
    { 0xCD088D, battle_frame_work },
};

// $CD:0C95
void battle_frame_dispatch(Snes *s)
{
    uint8_t mode = battle_mode(state_of(s));
    if (!battle_run(s, 0xCD0C99, frame_work_of[mode].routine, frame_work_of[mode].entry))
        return;
    m_rtl(s);
}

void battle_frame_work(Snes *s)
{
    if (!work_the_frame(s))
        return;
    m_rts(s);
}

void battle_step_window_slide(Snes *s)
{
    if (!slide_the_window(s))
        return;
    m_rts(s);
}

void battle_step_gradient_phase(Snes *s)
{
    battle_turn_the_gradient(state_of(s));
    m_rts(s);
}

/* It runs on the battle's page, which it leaves in A, with 16-bit index
 * registers. */
void battle_clear_oam_buffer(Snes *s)
{
    battle_hide_sprites(state_of(s));
    s->cpu.a = BATTLE_PAGE;
    m_rep(s, CPU_FLAG_X);
    m_rtl(s);
}

void battle_frame_update(Snes *s)
{
    if (!update_the_frame(s))
        return;
    m_rtl(s);
}

void battle_frame_wait_tail(Snes *s)
{
    if (!finish_the_wait(s))
        return;
    m_rtl(s);
}

// $CD:0C89
/* The map's layers that scroll by themselves (the field's, which the battle's
 * background is a map of): the field's routine keeps its own page and bank,
 * so the battle's data bank, page and flags are kept on the stack around it,
 * where the original keeps them. */
void battle_scroll_map_layers(Snes *s)
{
    m_push8(s, s->cpu.db);
    m_push16(s, s->cpu.d);
    m_push8(s, s->cpu.p);
    m_jsl_dc(s, 0xCD0C8C, 0xC00002, READS_NOTHING | DC_DB | DC_D);
    s->cpu.p = m_pull8(s);
    if (m_x8(s)) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
    s->cpu.d = m_pull16(s);
    s->cpu.db = m_pull8(s);
    m_rtl(s);
}

/* The guards. The frame update rotates a background palette's colors from
 * one below its last, as the game names them: the original's count of colors
 * to move runs round from none to 65536 for its last color. The dispatch
 * runs on the battle's page with A's high byte clear (its index takes it
 * along) and a mode its table has; the frame work with A's high byte clear too
 * (the block upload's table index takes it along), a scroll mode the
 * dispatch's table has, and what the uploads and the frame update read in
 * range. */
enum { LAST_COLOR = 0x0F };

static bool rotation_in_range(const GameState *g)
{
    return !(battle_color_rotation_on(g) && battle_background_cycle_on(g)
             && (battle_background_cycle(g) & 0x0F) == LAST_COLOR);
}

static bool frame_work_state_ok(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = battle_vblank_uploads_in_range(g) && battle_transfers_apart(g, s->cpu.s) && rotation_in_range(g)
              && battle_scroll_mode(g) < BATTLE_SCROLL_MODES;
    vars_peek(false);
    return ok;
}

bool battle_frame_work_ready(Snes *s) { return battle_page(s) && (s->cpu.a & 0xFF00) == 0 && frame_work_state_ok(s); }

bool battle_frame_update_ready(Snes *s)
{
    vars_peek(true);
    bool ok = rotation_in_range(state_of(s));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && ok;
}

bool battle_frame_dispatch_ready(Snes *s)
{
    vars_peek(true);
    bool a_mode = battle_mode(state_of(s)) < FRAME_MODES;
    vars_peek(false);
    return battle_frame_work_ready(s) && (s->cpu.a & 0xFF00) == 0 && a_mode;
}
