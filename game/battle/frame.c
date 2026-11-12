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
 * battle/draw_objects.c).
 *
 * Each step is a routine of its own, charged at its rate and refused where
 * the port doesn't take its state. */
#include "battle/frame.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame_data.h"
#include "battle/message.h"
#include "battle/screen_script.h"
#include "core/input.h"
#include "core/palette.h"
#include "menu/menu.h"
#include "platform/timing.h"
#include "battle/program.h"
#include "battle/screen.h"
#include "battle/screen_data.h"
#include "battle/sound.h"
#include "field/field.h"
#include "charge.h"
#include "platform/input.h"
#include "platform/video.h"
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
enum {
    COLOR_PHASES = 0x3E,
    CURSOR_COLOR = 128 + 16 * 1 + 14,
    OTHER_CURSOR_COLOR = 128 + 16 * 5 + 14,
    CURSOR_UPPER_PALETTE = 0x08,
};

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
enum {
    ROTATED_SPRITE_COLORS = 128 + 16 * 4 + 1,
    SPRITE_COLORS_ROTATED = 12,
    PALETTE_COLORS = 16,
    BACKGROUND_PALETTES = 8,
};

/* The original works the rotation in the effect loops' cells: the color it
 * last kept, the moves it counted down, and where the last run started. */

static void rotate_the_colors(GameState *g)
{
    if (!battle_color_rotation_on(g))
        return;
    uint8_t left = (uint8_t)(battle_color_rotation_count(g) - 1);
    set_battle_color_rotation_count(g, left ? left : battle_color_rotation_period(g));
    if (left)
        return;
    rotate_colors_up(g, ROTATED_SPRITE_COLORS, SPRITE_COLORS_ROTATED);
    charge_accesses(g, 2);
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
        charge_accesses(g, 2);
        return;
    }
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
 * effect_paths.c, battle/object_motion.c, battle/draw_objects.c). */
enum {
    CYCLE_PARTY_COLORS = 0xCFE7D7, STEP_EFFECT_SLOTS = 0xCFEFAF, STEP_SCRIPTED_MOVES = 0xCFEC78,
    UPDATE_ANCHORS = 0xCFECC2, RECORD_TRAILS = 0xCFED18, RAISE_NUMBERS = 0xCFE781, PLACE_FORMATION = 0xCFF9D3,
    DRAW_OBJECTS = 0xCFE83A,
};

/* How the frame update ends: on with the frame, gone no further, or a demo
 * battle's end, which leaves for the title. */
typedef enum { UPDATE_DONE, UPDATE_STOPPED, UPDATE_DEMO_ENDS } UpdateEnds;

// $CF:E58A
/* Refused where the port doesn't take the object work's state: the
 * effects' paths of a kind the game doesn't give; the draw pass, nobody on
 * the battlefield. */
static UpdateEnds update_the_frame(GameState *g, Platform *pf)
{
    count_the_blink(g);
    mirror_the_flicker(g);
    TIMED_AS(g, CYCLE_PARTY_COLORS, battle_cycle_party_colors(g));
    cycle_the_cursor_color(g);
    set_battle_frames(g, battle_frames(g) + 1);
    rotate_the_colors(g);
    color_the_cursors(g);
    uint8_t kept = battle_effect_sprite_attributes(g);
    if (end_the_demo(g, pf))
        return UPDATE_DEMO_ENDS;
    hold_to_run(g, pf);
    tick(g);
    if (!battle_effect_paths_known(g)) {
        (void)battle_contract_broken(g, STEP_EFFECT_SLOTS, "an effect's path of a kind the port doesn't know");
        return UPDATE_STOPPED;
    }
    TIMED_AS(g, STEP_EFFECT_SLOTS, battle_step_effect_slots(g));
    if (battle_effects_running(g)) {
        TIMED_AS(g, STEP_SCRIPTED_MOVES, battle_step_scripted_moves(g));
        TIMED_AS(g, UPDATE_ANCHORS, battle_update_anchors(g));
        TIMED_AS(g, RECORD_TRAILS, battle_record_trails(g));
        TIMED_AS(g, RAISE_NUMBERS, battle_raise_numbers(g));
    }
    if (battle_formation(g))
        TIMED_AS(g, PLACE_FORMATION, battle_place_formation(g));
    if (!battle_sprites_frozen(g)) {
        bring_the_upload_forward(g);
        bool drawn = true;
        TIMED_AS(g, DRAW_OBJECTS, drawn = battle_objects_drawn(g));
        if (!drawn)
            return UPDATE_STOPPED;
    }
    set_battle_frame_pending(g, 0);
    set_battle_effect_sprite_attributes(g, kept);
    return UPDATE_DONE;
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
 * original makes it and refused where the port doesn't take its state: the
 * window's slide (twice), the buttons read (a controller not read or a reset
 * held refused) and their repeat counted (paused, and at the frame's end),
 * the layers' scroll and the screen's shake (battle/scroll.c), the HP
 * gradient's step (battle/screen_gradient.c; its phase out of range
 * refused), the sprites hidden, the frame update (above; a rotation it
 * doesn't take refused), the wait for the line the animation scripts start
 * at, the screen scripts (battle/screen_script.c), and the palettes paused
 * with the battle and animated. */
enum {
    FRAMES_INPUT = 0xC28002,
    BUTTON_REPEAT = 0xCD0340,
    HP_GRADIENT = 0xD1EA4B,
    FRAME_UPDATE = 0xCFE58A,
    LINE_WAIT = 0xCD3B82,
    SCREEN_SCRIPTS = 0xCD1609,
    PALETTE_ANIMATION = 0xFDFFF7,
};

/* While effects run, the animation scripts' frame starts once the beam is at
 * line 64, unless a script said not to wait. */
enum { LINE_TO_WAIT_FOR = 0x40 };

static bool waits_for_the_line(const GameState *g)
{
    return !battle_effect_line_wait_off(g) && battle_effects_running(g) && !battle_sprites_frozen(g);
}

static void slide_the_window_on(GameState *g) { TIMED_AS(g, 0xCD05B2, (void)slide_the_window(g)); }

/* The buttons read, and their repeat counted. */
static bool read_the_input(GameState *g, Platform *pf)
{
    bool read = false;
    TIMED_AS(g, FRAMES_INPUT, read = input_take_frame(g, pf));
    if (!read)
        return battle_contract_broken(g, FRAMES_INPUT, "the controller not read, or the reset buttons held");
    TIMED_AS(g, BUTTON_REPEAT, battle_update_button_repeat(g));
    return true;
}

static bool step_the_hp_gradient(GameState *g)
{
    if (!battle_hp_gradient_steps(g))
        return battle_contract_broken(g, HP_GRADIENT, "an HP gradient's phase out of range");
    TIMED_AS(g, HP_GRADIENT, battle_step_hp_gradient(g));
    return true;
}

static bool rotation_in_range(const GameState *g);

static bool update_the_objects(GameState *g, Platform *pf)
{
    if (!rotation_in_range(g))
        return battle_contract_broken(g, FRAME_UPDATE, "a background colors' rotation the port doesn't make");
    UpdateEnds ends = UPDATE_STOPPED;
    TIMED_AS(g, FRAME_UPDATE, ends = update_the_frame(g, pf));
    if (ends == UPDATE_DEMO_ENDS)
        return battle_contract_broken(g, DEMO_LEAVES, "a demo battle's end");
    return ends == UPDATE_DONE;
}

// $CD:088D
bool battle_frame_worked(GameState *g, Platform *pf)
{
    send_the_frame(g, pf);
    set_the_windows(g, pf);
    color_below_the_split(g);
    square_the_window_top(g);
    TIMED_AS(g, 0xCD0AEE, battle_show_window_table(g, pf));
    slide_the_window_on(g);
    slide_the_window_on(g);
    vid_enable_line_effects(pf, PF_AT(0xCD08FE), battle_line_effects_on(g));
    if (battle_paused(g)) {
        VidDisplay dimmed = vid_display_decode(paused_brightness(g));
        vid_set_display(pf, PF_AT(0xCD0916), &dimmed);
        return read_the_input(g, pf);
    }
    VidDisplay display = vid_display_decode(battle_brightness(g));
    vid_set_display(pf, PF_AT(0xCD0925), &display);
    battle_scroll_by_the_mode(g);
    TIMED_AS(g, 0xCD0B51, battle_shake_screen(g));
    if (!(battle_frame_count(g) & 1) && battle_window_slide(g) == BATTLE_SLIDE_STILL)
        choose_the_arrangement(g);
    if (battle_background_flags(g) & BATTLE_BACKGROUND_HAS_GRADIENT) {
        TIMED_AS(g, 0xCEE18E, battle_ripple_background(g));
        if (!((uint8_t)battle_frame_count(g) & battle_gradient_frame_mask(g)) && !step_the_hp_gradient(g))
            return false;
    }
    if (!battle_keep_sprites(g) && !battle_sprites_frozen(g))
        TIMED_AS(g, 0xCEF066, battle_hide_sprites(g));
    if (!update_the_objects(g, pf))
        return false;
    if (waits_for_the_line(g))
        tm_wait_line(pf, PF_CALL(0xCD09AC, LINE_WAIT), LINE_TO_WAIT_FOR);
    bool done = false;
    TIMED_AS(g, SCREEN_SCRIPTS, done = battle_screen_scripts_run(g, pf));
    if (!done)
        return false;
    set_battle_sprites_frozen(g, battle_sprites_frozen_setting(g));
    TIMED_AS(g, 0xCD0B1E, battle_keep_animations_off_the_cycle(g));
    TIMED_AS(g, 0xD1F426, battle_pause_palette_animation(g));
    TIMED_AS(g, PALETTE_ANIMATION, done = palette_animated(g));
    if (!done)
        return false;
    TIMED_AS(g, 0xCD0ADE, battle_turn_the_gradient(g));
    return read_the_input(g, pf);
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

/* ---- The frame wait's tail ---------------------------------------------------------------- */

// $CD:04AA
/* After the main loop's wait for the frame: unless paused, a sound a screen
 * script asked for (battle/sound.c); the pause's turn; then, unless a routine
 * is waiting on a queued transfer, the battlefield put back once the effects
 * stop (battle/battlefield_restore.c), and an effect's graphics loaded where
 * a script asked for them and none are loading (battle/screen_graphics.c);
 * and the next letter of a battle message (battle/message.c), refused where
 * the menus' text code doesn't go on from it. */
bool battle_frame_wait_finished(GameState *g, Platform *pf)
{
    if (!battle_paused(g))
        TIMED_AS(g, BATTLE_SCRIPT_SOUND_ENTRY, battle_play_the_script_sound(g, pf));
    if (battle_paused(g))
        TIMED_AS(g, BATTLE_PAUSE_ENTRY, battle_sit_out_the_pause(g, pf));
    if (!battle_vram_request_busy(g)) {
        bool done = true;
        if (battle_background_restore_request(g) && !battle_effects_running(g)) {
            TIMED_AS(g, 0xCD0453, done = battle_put_the_battlefield_back(g, pf));
            if (!done)
                return false;
        }
        if (battle_effect_graphics_wanted(g) && !battle_effect_graphics_loading(g)) {
            TIMED_AS(g, 0xD1ECF3, done = battle_load_effect_graphics(g, pf, battle_effect_graphics_effect(g)));
            if (!done)
                return false;
            set_battle_effect_graphics_wanted(g, 0);
        }
    }
    if (!battle_message_typing_fits(g))
        return battle_contract_broken(g, BATTLE_TYPE_THE_MESSAGE_ENTRY, "a message the menus' text code doesn't go on from");
    TIMED_AS(g, BATTLE_TYPE_THE_MESSAGE_ENTRY, battle_type_the_message(g));
    return true;
}

/* ---- The frame work by the battle's mode ------------------------------------------------- */

/* The battle's modes, as the frame work runs them: the display's setup, then
 * the fight (and its end, which runs the same frame work). */
enum { FRAME_MODES = 3 };

enum { FRAME_DISPATCH = 0xCD0C95 };

// $CD:0C89 (through $C0:0002)
/* The map's layers that scroll by themselves, the field's, which the battle's
 * background is a map of: a frame's worth, the field's own work, timed as
 * its routine. */
void battle_scroll_the_map_layers(GameState *g) { TIMED_AS(g, 0xC02C41, field_scroll_layers_by_themselves(g)); }

/* What the frame work takes. The frame update rotates a background palette's
 * colors from one below its last, as the game names them: the original's
 * count of colors to move runs round from none to 65536 for its last color.
 * The dispatch takes a mode its table has; the frame work a scroll mode the
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

/* The frame dispatch's work, as measured: both request flags looked at
 * seventeen times, plus five bytes per queued transfer and two per block. */
static bool frame_work_takes(GameState *g)
{
    if (!frame_work_fits(g))
        return false;
    bool queued = battle_vram_request_pending(g) != 0;
    bool block = battle_block_upload_request(g) != 0;
    charge_accesses(g, 32 + (queued ? 85 : 0) + (block ? 34 : 0));
    return true;
}

static const struct { uint32_t routine; } frame_work_of[FRAME_MODES] = {
    { 0xCD09F1 },
    { 0xCD088D },
    { 0xCD088D },
};

bool battle_frame_dispatched(GameState *g, Platform *pf)
{
    uint8_t mode = battle_mode(g);
    if (mode >= FRAME_MODES || !frame_work_takes(g))
        return battle_contract_broken(g, FRAME_DISPATCH, "the battle's frame work in a state the port doesn't take");
    bool done = false;
    if (mode == 0)
        TIMED_AS(g, frame_work_of[mode].routine, done = battle_display_set_up(g, pf));
    else
        TIMED_AS(g, frame_work_of[mode].routine, done = battle_frame_worked(g, pf));
    return done;
}
