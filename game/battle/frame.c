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
#include "battle/message.h"
#include "battle/screen_script.h"
#include "core/input.h"
#include "core/palette.h"
#include "platform/timing.h"
#include "battle/program.h"
#include "battle/screen.h"
#include "battle/screen_data.h"
#include "battle/sound.h"
#include "field/field.h"
#include "charge.h"
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
enum { SLIDE_IN_STARTED = 1, WINDOW_LINES = 0x17 };
enum { BATTLEFIELD_ARRANGEMENT = 0, WINDOW_ARRANGEMENT = 2 };

static bool sliding_out(uint8_t slide) { return (slide & BATTLE_SLIDE_OUT) != 0; }

void battle_lay_the_lines_out_again(GameState *g)
{
    set_battle_alternate_apply(g, (uint8_t)(battle_alternate_apply(g) + 1));
}

void battle_arrange_the_lines(GameState *g, uint8_t arrangement)
{
    set_battle_alternate_base(g, arrangement);
    battle_lay_the_lines_out_again(g);
}

/* A slide starts from the frame after this (BATTLE_SLIDE_STARTING_IN, or
 * BATTLE_SLIDE_OUT). */
void battle_start_the_slide(GameState *g, uint8_t slide)
{
    set_battle_window_slide_frames(g, WINDOW_LINES);
    set_battle_window_slide(g, slide);
}

static void end_the_slide(GameState *g)
{
    if (!sliding_out(battle_window_slide(g)))
        set_battle_pause_blocked(g, 0);
    set_battle_window_slide(g, BATTLE_SLIDE_STILL);
}

/* A line of the slide, in the per-line tables (battle/display.c). The frame
 * it counts down is the slide's. */
static void slide_a_line(GameState *g, bool out)
{
    TIMED_AS(g, out ? 0xCD0577 : 0xCD053C, battle_slide_the_split(g, out));
    uint8_t left = (uint8_t)(battle_window_slide_frames(g) - 1);
    set_battle_window_slide_frames(g, left);
    if (left == 0)
        battle_arrange_the_lines(g, out ? BATTLEFIELD_ARRANGEMENT : WINDOW_ARRANGEMENT);
    if (left == 0 && out)
        end_the_slide(g);
}

// $CD:05B2
/* The slide's frame; a slide in ends the frame after its last line. True
 * where a slide in started, its lines laid out the battlefield's way. */
static bool slide_the_window(GameState *g)
{
    uint8_t slide = battle_window_slide(g);
    if (slide == BATTLE_SLIDE_STILL)
        return false;
    if (slide == BATTLE_SLIDE_STARTING_IN) {
        set_battle_alternate_base(g, BATTLEFIELD_ARRANGEMENT);
        TIMED_AS(g, 0xCEEE6E, battle_arrange_line_effects(g, battle_alternate_latched(g)));
        set_battle_window_slide(g, SLIDE_IN_STARTED);
        return true;
    }
    if (!sliding_out(slide) && battle_window_slide_frames(g) == 0) {
        end_the_slide(g);
        return false;
    }
    slide_a_line(g, sliding_out(slide));
    return false;
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
 * back to its title ($C0:FF00). True when it is time to leave. */
enum { LEAVING_THE_BATTLE = 0x80, DEMO_LEAVES = 0xC0FF00 };

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

/* The frame update's object work, each a routine of its own (battle/
 * effect_paths.c, battle/object_motion.c, battle/draw_objects.c), and the
 * effect sprites' attributes kept across it, where the original keeps them on
 * the stack. */
typedef enum {
    UPDATE_PARTY_COLORS, UPDATE_EFFECT_SLOTS, UPDATE_SCRIPTED_MOVES, UPDATE_ANCHORS, UPDATE_TRAILS, UPDATE_NUMBERS,
    UPDATE_FORMATION, UPDATE_DRAW_OBJECTS, UPDATE_KEEP_THE_ATTRIBUTES, UPDATE_ATTRIBUTES_BACK, UPDATE_CALLS
} UpdateCall;

/* How the frame update ends: on with the frame, gone no further, or a demo
 * battle's end, which leaves for the title. */
typedef enum { UPDATE_DONE, UPDATE_STOPPED, UPDATE_DEMO_ENDS } UpdateEnds;

// $CF:E58A
static UpdateEnds update_the_frame(GameState *g, Platform *pf, bool (*call)(void *ctx, UpdateCall call), void *ctx)
{
    count_the_blink(g);
    mirror_the_flicker(g);
    if (!call(ctx, UPDATE_PARTY_COLORS))
        return UPDATE_STOPPED;
    cycle_the_cursor_color(g);
    set_battle_frames(g, battle_frames(g) + 1);
    rotate_the_colors(g);
    color_the_cursors(g);
    uint8_t kept = battle_effect_sprite_attributes(g);
    if (!call(ctx, UPDATE_KEEP_THE_ATTRIBUTES))
        return UPDATE_STOPPED;
    if (end_the_demo(g, pf))
        return UPDATE_DEMO_ENDS;
    hold_to_run(g, pf);
    tick(g);
    if (!call(ctx, UPDATE_EFFECT_SLOTS))
        return UPDATE_STOPPED;
    if (battle_effects_running(g)
        && (!call(ctx, UPDATE_SCRIPTED_MOVES) || !call(ctx, UPDATE_ANCHORS) || !call(ctx, UPDATE_TRAILS)
            || !call(ctx, UPDATE_NUMBERS)))
        return UPDATE_STOPPED;
    if (battle_formation(g) && !call(ctx, UPDATE_FORMATION))
        return UPDATE_STOPPED;
    if (!battle_sprites_frozen(g)) {
        bring_the_upload_forward(g);
        if (!call(ctx, UPDATE_DRAW_OBJECTS))
            return UPDATE_STOPPED;
    }
    set_battle_frame_pending(g, 0);
    if (!call(ctx, UPDATE_ATTRIBUTES_BACK))
        return UPDATE_STOPPED;
    set_battle_effect_sprite_attributes(g, kept);
    return UPDATE_DONE;
}

/* On the machine the object work is called from the original's calls; its
 * callees read nothing of the registers but A's high byte, which is 0 here
 * as the original's frame count leaves it. */
enum { OBJECT_WORK_IGNORES = DC_X | DC_Y | DC_FLAGS | DC_AL };

static const struct {
    uint32_t site, routine;
} object_work[UPDATE_KEEP_THE_ATTRIBUTES] = {
    [UPDATE_PARTY_COLORS] = { 0xCFE5A7, 0xCFE7D7 }, [UPDATE_EFFECT_SLOTS] = { 0xCFE748, 0xCFEFAF },
    [UPDATE_SCRIPTED_MOVES] = { 0xCFE750, 0xCFEC78 }, [UPDATE_ANCHORS] = { 0xCFE753, 0xCFECC2 },
    [UPDATE_TRAILS] = { 0xCFE756, 0xCFED18 }, [UPDATE_NUMBERS] = { 0xCFE759, 0xCFE781 },
    [UPDATE_FORMATION] = { 0xCFE761, 0xCFF9D3 }, [UPDATE_DRAW_OBJECTS] = { 0xCFE778, 0xCFE83A },
};

static bool update_on_the_machine(void *ctx, UpdateCall call)
{
    Snes *s = ctx;
    switch (call) {
    case UPDATE_KEEP_THE_ATTRIBUTES:
        s->cpu.a = 0;
        m_push8(s, battle_effect_sprite_attributes(state_of(s)));
        return true;
    case UPDATE_ATTRIBUTES_BACK: (void)m_pull8(s); return true;
    default: return battle_call(s, object_work[call].site, object_work[call].routine, OBJECT_WORK_IGNORES);
    }
}

/* ---- The frame work ----------------------------------------------------------------------- */

/* While paused, the screen is dimmed to half, unless the ROM's debug frame
 * advance is on. */
static uint8_t paused_brightness(const GameState *g)
{
    return battle_debug_frame_advance(g) ? battle_brightness(g) : battle_brightness(g) >> 1;
}

/* The frame's uploads: all of them in the fight, or while the frame work is
 * cut down, the palette and a queued transfer only. The vblank's other
 * uploads wait while the window slides or the fight is paused. */
static void send_the_frame(GameState *g, Platform *pf)
{
    if (battle_frame_work_reduced(g)) {
        TIMED_AS(g, 0xCD09CE, battle_send_palette(pf));
        TIMED_AS(g, 0xCD3EB4, battle_send_queued_transfer(g, pf));
        return;
    }
    if (battle_window_slide(g) == BATTLE_SLIDE_STILL)
        TIMED_AS(g, 0xCD0607, battle_send_block_and_message_tiles(g, pf));
    TIMED_AS(g, 0xCD3EB4, battle_send_queued_transfer(g, pf));
    if (!battle_paused(g))
        TIMED_AS(g, 0xCFE300, battle_send_vblank_uploads(g, pf));
}

/* The frame work's calls on the battle's other routines, each where the
 * original makes it: the window's slide (twice), the buttons read and their
 * repeat counted (paused, and at the frame's end), the layers' scroll and
 * the screen's shake (battle/scroll.c), the HP gradient's step
 * (battle/screen_gradient.c), the sprites hidden, the frame update (above),
 * the wait for the line the animation scripts start at, the screen scripts
 * (battle/screen_script.c), and the palettes paused with the battle and
 * animated. */
typedef enum {
    WORK_SLIDE_FIRST, WORK_SLIDE_SECOND, WORK_READ_PAUSED, WORK_REPEAT_PAUSED, WORK_SCROLL, WORK_SHAKE,
    WORK_GRADIENT, WORK_HIDE_SPRITES, WORK_UPDATE, WORK_WAIT_FOR_THE_LINE, WORK_SCREEN_SCRIPTS,
    WORK_PAUSE_PALETTES, WORK_ANIMATE_PALETTES, WORK_READ, WORK_REPEAT,
} WorkCall;

/* While effects run, the animation scripts' frame starts once the beam is at
 * line 64, unless a script said not to wait. */
enum { LINE_TO_WAIT_FOR = 0x40 };

static bool waits_for_the_line(const GameState *g)
{
    return !battle_effect_line_wait_off(g) && battle_effects_running(g) && !battle_sprites_frozen(g);
}

// $CD:088D
static bool work_the_frame(GameState *g, Platform *pf, bool (*call)(void *ctx, WorkCall call), void *ctx)
{
    send_the_frame(g, pf);
    set_the_windows(g, pf);
    color_below_the_split(g);
    square_the_window_top(g);
    TIMED_AS(g, 0xCD0AEE, battle_show_window_table(g, pf));
    if (!call(ctx, WORK_SLIDE_FIRST) || !call(ctx, WORK_SLIDE_SECOND))
        return false;
    vid_enable_line_effects(pf, PF_AT(0xCD08FE), battle_line_effects_on(g));
    if (battle_paused(g)) {
        VidDisplay dimmed = vid_display_decode(paused_brightness(g));
        vid_set_display(pf, PF_AT(0xCD0916), &dimmed);
        return call(ctx, WORK_READ_PAUSED) && call(ctx, WORK_REPEAT_PAUSED);
    }
    VidDisplay display = vid_display_decode(battle_brightness(g));
    vid_set_display(pf, PF_AT(0xCD0925), &display);
    if (!call(ctx, WORK_SCROLL) || !call(ctx, WORK_SHAKE))
        return false;
    if (!(battle_frame_count(g) & 1) && battle_window_slide(g) == BATTLE_SLIDE_STILL)
        choose_the_arrangement(g);
    if (battle_background_flags(g) & BATTLE_BACKGROUND_HAS_GRADIENT) {
        TIMED_AS(g, 0xCEE18E, battle_ripple_background(g));
        if (!((uint8_t)battle_frame_count(g) & battle_gradient_frame_mask(g)) && !call(ctx, WORK_GRADIENT))
            return false;
    }
    if (!battle_keep_sprites(g) && !battle_sprites_frozen(g) && !call(ctx, WORK_HIDE_SPRITES))
        return false;
    if (!call(ctx, WORK_UPDATE) || (waits_for_the_line(g) && !call(ctx, WORK_WAIT_FOR_THE_LINE))
        || !call(ctx, WORK_SCREEN_SCRIPTS))
        return false;
    set_battle_sprites_frozen(g, battle_sprites_frozen_setting(g));
    TIMED_AS(g, 0xCD0B1E, battle_keep_animations_off_the_cycle(g));
    if (!call(ctx, WORK_PAUSE_PALETTES) || !call(ctx, WORK_ANIMATE_PALETTES))
        return false;
    TIMED_AS(g, 0xCD0ADE, battle_turn_the_gradient(g));
    return call(ctx, WORK_READ) && call(ctx, WORK_REPEAT);
}

/* On the machine: the frame work runs with the data bank $7E, on the
 * battle's page. Its call-outs read nothing of the registers but what each
 * names: the button reading reads Y, the gradient, the sound's and the
 * battlefield's routines A's high byte, which is 0 on the battle's page. */
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_FLAGS, READS_Y = DC_A | DC_X | DC_FLAGS,
       READS_A_HIGH = DC_X | DC_Y | DC_FLAGS | DC_AL };

/* The shake puts its offsets on the layers with A's high byte as theirs,
 * which the scroll leaves 0; anything else is the original's. It leaves A's
 * high byte 0. */
static bool shake_on_the_machine(Snes *s)
{
    if (s->cpu.a & 0xFF00)
        return battle_call(s, 0xCD092C, 0xCD0B51, DC_X | DC_Y | DC_FLAGS | DC_AL);
    NATIVE_TIME_AS(s, 0xCD0B51, battle_shake_screen(state_of(s)));
    s->cpu.a = 0;
    return true;
}

/* The frame update, through the original's vector at $C1:000C. It runs here
 * with A's high byte 0, as every call the game makes has it; any other is the
 * original's. */
static bool update_the_battlefield_on_the_machine(Snes *s)
{
    if (s->cpu.a & 0xFF00)
        return battle_call_long(s, 0xCD0997, 0xC1000C, DC_X | DC_Y | DC_FLAGS | DC_AL);
    return battle_run_long(s, 0xCD0997, 0xCFE58A, battle_frame_update);
}

static bool work_on_the_machine(void *ctx, WorkCall call)
{
    Snes *s = ctx;
    switch (call) {
    case WORK_SLIDE_FIRST: return battle_run(s, 0xCD08F5, 0xCD05B2, battle_step_window_slide);
    case WORK_SLIDE_SECOND: return battle_run(s, 0xCD08F8, 0xCD05B2, battle_step_window_slide);
    case WORK_READ_PAUSED: return battle_call_long(s, 0xCD091A, 0xC28002, READS_Y);
    case WORK_REPEAT_PAUSED: return battle_call(s, 0xCD091E, 0xCD0340, READS_NOTHING);
    case WORK_SCROLL: return battle_scroll_the_layers(s);
    case WORK_SHAKE: return shake_on_the_machine(s);
    case WORK_GRADIENT:
        /* the step takes A's high byte along, which the ripple leaves clear */
        s->cpu.a = 0;
        return battle_call_long(s, 0xCD0985, 0xD1EA4B, READS_A_HIGH);
    case WORK_HIDE_SPRITES:
        NATIVE_TIME_AS(s, 0xCEF066, battle_hide_sprites(state_of(s)));
        s->cpu.a = BATTLE_PAGE;   /* as the original's clearing leaves it */
        return true;
    case WORK_UPDATE: return update_the_battlefield_on_the_machine(s);
    case WORK_WAIT_FOR_THE_LINE:
        return battle_wait_line(s, 0xCD09AC, 0xCD3B82, DC_X | DC_Y | DC_FLAGS | DC_AH | DC_DB, LINE_TO_WAIT_FOR);
    case WORK_SCREEN_SCRIPTS: return battle_run(s, 0xCD09AF, 0xCD1609, battle_run_screen_scripts);
    case WORK_PAUSE_PALETTES: return battle_call_long(s, 0xCD09BB, 0xD1F426, READS_NOTHING);
    case WORK_ANIMATE_PALETTES: return battle_call_long(s, 0xCD09BF, 0xFDFFF7, READS_NOTHING);
    case WORK_READ: return battle_call_long(s, 0xCD09C6, 0xC28002, READS_Y);
    default: return battle_call(s, 0xCD09CA, 0xCD0340, READS_NOTHING);
    }
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

/* ---- The frame wait's tail ---------------------------------------------------------------- */

/* The tail's calls: the script's sound and the pause's turn
 * (battle/sound.c), the battlefield put back (battle/battlefield_restore.c),
 * an effect's graphics loaded (battle/screen_graphics.c), and the message's
 * next letter (battle/message.c). */
typedef enum { TAIL_SCRIPT_SOUND, TAIL_PAUSE, TAIL_BATTLEFIELD_BACK, TAIL_EFFECT_GRAPHICS, TAIL_MESSAGE } TailCall;

// $CD:04AA
/* After the main loop's wait for the frame: unless paused, a sound a screen
 * script asked for; the pause's turn; then, unless a routine is
 * waiting on a queued transfer, the battlefield put back once the effects
 * stop, and an effect's graphics loaded where a script asked for them and
 * none are loading; and the next letter of a battle message. */
static bool finish_the_wait(GameState *g, bool (*call)(void *ctx, TailCall call), void *ctx)
{
    if (!battle_paused(g) && !call(ctx, TAIL_SCRIPT_SOUND))
        return false;
    if (battle_paused(g) && !call(ctx, TAIL_PAUSE))
        return false;
    if (!battle_vram_request_busy(g)) {
        if (battle_background_restore_request(g) && !battle_effects_running(g) && !call(ctx, TAIL_BATTLEFIELD_BACK))
            return false;
        if (battle_effect_graphics_wanted(g) && !battle_effect_graphics_loading(g)) {
            if (!call(ctx, TAIL_EFFECT_GRAPHICS))
                return false;
            set_battle_effect_graphics_wanted(g, 0);
        }
    }
    return call(ctx, TAIL_MESSAGE);
}

static bool tail_on_the_machine(void *ctx, TailCall call)
{
    Snes *s = ctx;
    switch (call) {
    case TAIL_SCRIPT_SOUND: return battle_run(s, 0xCD04AF, BATTLE_SCRIPT_SOUND_ENTRY, battle_play_the_script_sound_entry);
    case TAIL_PAUSE: return battle_run(s, 0xCD04B2, BATTLE_PAUSE_ENTRY, battle_sit_out_the_pause_entry);
    case TAIL_BATTLEFIELD_BACK: return battle_run(s, 0xCD04C4, 0xCD0453, battle_put_the_battlefield_back_entry);
    case TAIL_EFFECT_GRAPHICS:
        m_set_a8(s, battle_effect_graphics_effect(state_of(s)));
        return battle_call_long(s, 0xCD04D4, 0xD1ECF3, DC_X | DC_Y | DC_FLAGS);
    default: return battle_run(s, 0xCD04DB, BATTLE_TYPE_THE_MESSAGE_ENTRY, battle_type_the_message_entry);
    }
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

enum { FRAME_DISPATCH = 0xCD0C95 };

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
    s->cpu.db = BATTLE_DATA_BANK;
    if (!work_the_frame(state_of(s), m_platform(s), work_on_the_machine, s))
        return;
    m_rts(s);
}

void battle_step_window_slide(Snes *s)
{
    if (slide_the_window(state_of(s)))
        battle_leave_arrangement_registers(s);
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
    switch (update_the_frame(state_of(s), m_platform(s), update_on_the_machine, s)) {
    case UPDATE_DONE: m_rtl(s); return;
    case UPDATE_DEMO_ENDS: m_stop_at_own(s, DEMO_LEAVES, DC_A | DC_X | DC_FLAGS | DC_DB | DC_D); return;
    default: return;
    }
}

void battle_frame_wait_tail(Snes *s)
{
    if (!finish_the_wait(state_of(s), tail_on_the_machine, s))
        return;
    m_rtl(s);
}

// $CD:0C89 (through $C0:0002)
/* The map's layers that scroll by themselves, the field's, which the battle's
 * background is a map of: a frame's worth, the field's own work, timed as
 * its routine. */
void battle_scroll_the_map_layers(GameState *g) { TIMED_AS(g, 0xC02C41, field_scroll_layers_by_themselves(g)); }

void battle_scroll_map_layers(Snes *s)
{
    battle_scroll_the_map_layers(state_of(s));
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

static bool frame_work_fits(const GameState *g)
{
    return battle_vblank_uploads_in_range(g) && rotation_in_range(g) && battle_scroll_mode(g) < BATTLE_SCROLL_MODES;
}

static bool frame_work_state_ok(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = frame_work_fits(g) && battle_transfers_apart(g, s->cpu.s);
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

/* ---- As C ------------------------------------------------------------------------------------ */

/* The guards' state halves: the frame work's, with the transfers kept off
 * the whole of the stack's area (there is no machine's stack to say where
 * its top is); the dispatch's, with a mode its table has. */
static bool frame_work_takes(const GameState *g) { return frame_work_fits(g) && battle_transfers_keep_off_the_stack(g); }

bool battle_frame_dispatched(GameState *g, Platform *pf)
{
    uint8_t mode = battle_mode(g);
    if (mode >= FRAME_MODES || !frame_work_takes(g))
        return battle_needs_the_machine(g, FRAME_DISPATCH, "the battle's frame work in a state the port doesn't take");
    bool done = false;
    if (mode == 0)
        TIMED_AS(g, frame_work_of[mode].routine, done = battle_display_set_up(g, pf));
    else
        TIMED_AS(g, frame_work_of[mode].routine, done = battle_frame_worked(g, pf));
    return done;
}

typedef struct {
    GameState *game;
    Platform *pf;
} FrameAsC;

/* The object work, each with its guard's state half: the effects' paths of
 * a kind the game gives; the draw pass, someone on the battlefield; the rest
 * turn away only the page. */
static bool update_as_c(void *ctx, UpdateCall call)
{
    GameState *g = ((FrameAsC *)ctx)->game;
    bool done = true;
    switch (call) {
    case UPDATE_PARTY_COLORS: TIMED_AS(g, object_work[call].routine, battle_cycle_party_colors(g)); return true;
    case UPDATE_EFFECT_SLOTS:
        if (!battle_effect_paths_known(g))
            return battle_needs_the_machine(g, object_work[call].routine, "an effect's path of a kind the port doesn't know");
        TIMED_AS(g, object_work[call].routine, battle_step_effect_slots(g));
        return true;
    case UPDATE_SCRIPTED_MOVES: TIMED_AS(g, object_work[call].routine, battle_step_scripted_moves(g)); return true;
    case UPDATE_ANCHORS: TIMED_AS(g, object_work[call].routine, battle_update_anchors(g)); return true;
    case UPDATE_TRAILS: TIMED_AS(g, object_work[call].routine, battle_record_trails(g)); return true;
    case UPDATE_NUMBERS: TIMED_AS(g, object_work[call].routine, battle_raise_numbers(g)); return true;
    case UPDATE_FORMATION: TIMED_AS(g, object_work[call].routine, battle_place_formation(g)); return true;
    case UPDATE_DRAW_OBJECTS: TIMED_AS(g, object_work[call].routine, done = battle_objects_drawn(g)); return done;
    default: return true;
    }
}

/* The frame work's calls, each with its guard's state half where it has
 * one: the HP gradient's step, its phase in range; the frame update, a
 * rotation it takes; the input, a controller read and no reset held. */
enum { FRAMES_INPUT = 0xC28002, BUTTON_REPEAT = 0xCD0340, HP_GRADIENT = 0xD1EA4B, FRAME_UPDATE = 0xCFE58A,
       LINE_WAIT = 0xCD3B82, SCREEN_SCRIPTS = 0xCD1609, PALETTE_ANIMATION = 0xFDFFF7 };

static bool read_the_input(FrameAsC *c)
{
    bool read = false;
    TIMED_AS(c->game, FRAMES_INPUT, read = input_take_frame(c->game, c->pf));
    if (!read)
        return battle_needs_the_machine(c->game, FRAMES_INPUT, "the controller not read, or the reset buttons held");
    return true;
}

static bool work_as_c(void *ctx, WorkCall call)
{
    FrameAsC *c = ctx;
    GameState *g = c->game;
    bool done = false;
    switch (call) {
    case WORK_SLIDE_FIRST:
    case WORK_SLIDE_SECOND: TIMED_AS(g, 0xCD05B2, (void)slide_the_window(g)); return true;
    case WORK_READ_PAUSED:
    case WORK_READ: return read_the_input(c);
    case WORK_REPEAT_PAUSED:
    case WORK_REPEAT: TIMED_AS(g, BUTTON_REPEAT, battle_update_button_repeat(g)); return true;
    case WORK_SCROLL: battle_scroll_by_the_mode(g); return true;
    case WORK_SHAKE: TIMED_AS(g, 0xCD0B51, battle_shake_screen(g)); return true;
    case WORK_GRADIENT:
        if (!battle_hp_gradient_steps(g))
            return battle_needs_the_machine(g, HP_GRADIENT, "an HP gradient's phase out of range");
        TIMED_AS(g, HP_GRADIENT, battle_step_hp_gradient(g));
        return true;
    case WORK_HIDE_SPRITES: TIMED_AS(g, 0xCEF066, battle_hide_sprites(g)); return true;
    case WORK_UPDATE: {
        if (!rotation_in_range(g))
            return battle_needs_the_machine(g, FRAME_UPDATE, "a background colors' rotation the port doesn't make");
        UpdateEnds ends = UPDATE_STOPPED;
        TIMED_AS(g, FRAME_UPDATE, ends = update_the_frame(g, c->pf, update_as_c, c));
        if (ends == UPDATE_DEMO_ENDS)
            return battle_needs_the_machine(g, DEMO_LEAVES, "a demo battle's end");
        return ends == UPDATE_DONE;
    }
    case WORK_WAIT_FOR_THE_LINE:
        tm_wait_line(c->pf, PF_CALL_DC(0xCD09AC, LINE_WAIT, DC_X | DC_Y | DC_FLAGS | DC_AH | DC_DB), LINE_TO_WAIT_FOR);
        return true;
    case WORK_SCREEN_SCRIPTS: TIMED_AS(g, SCREEN_SCRIPTS, done = battle_screen_scripts_run(g, c->pf)); return done;
    case WORK_PAUSE_PALETTES: TIMED_AS(g, 0xD1F426, battle_pause_palette_animation(g)); return true;
    case WORK_ANIMATE_PALETTES: TIMED_AS(g, PALETTE_ANIMATION, done = palette_animated(g)); return done;
    default: return false;
    }
}

bool battle_frame_worked(GameState *g, Platform *pf)
{
    FrameAsC c = { g, pf };
    return work_the_frame(g, pf, work_as_c, &c);
}

/* The tail as C: the sounds and the message with their guards' state halves
 * (the sound's and the pause's turn away only a page). */
typedef struct {
    GameState *game;
    Platform *pf;
} TailAsC;

static bool tail_as_c(void *ctx, TailCall call)
{
    TailAsC *c = ctx;
    GameState *g = c->game;
    switch (call) {
    case TAIL_SCRIPT_SOUND: TIMED_AS(g, BATTLE_SCRIPT_SOUND_ENTRY, battle_play_the_script_sound(g, c->pf)); return true;
    case TAIL_PAUSE: TIMED_AS(g, BATTLE_PAUSE_ENTRY, battle_sit_out_the_pause(g, c->pf)); return true;
    case TAIL_BATTLEFIELD_BACK: {
        bool done = false;
        TIMED_AS(g, 0xCD0453, done = battle_put_the_battlefield_back(g, c->pf));
        return done;
    }
    case TAIL_EFFECT_GRAPHICS: return battle_needs_the_machine(g, 0xD1ECF3, "an effect's graphics loaded");
    default:
        if (!battle_message_typing_fits(g))
            return battle_needs_the_machine(g, BATTLE_TYPE_THE_MESSAGE_ENTRY, "a message the menus' text code doesn't go on from");
        TIMED_AS(g, BATTLE_TYPE_THE_MESSAGE_ENTRY, battle_type_the_message(g));
        return true;
    }
}

bool battle_frame_wait_finished(GameState *g, Platform *pf)
{
    TailAsC c = { g, pf };
    return finish_the_wait(g, tail_as_c, &c);
}
