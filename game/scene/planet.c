/* Scenes: the planet seen from space.
 *
 * The scene program's modes 7 and 8 share a setup ($7E:6248) that shows the
 * planet hanging in space, with stars about it, each mode with a script of
 * its own that the setup picks by the mode. Mode 7 is the Day of Lavos's
 * end, when the party has lost: "But... the future refused to change." over
 * the planet as its colours drain away. Its script starts the two frame
 * tasks here:
 *
 * - the planet's colours step: every so many frames each colour of the
 *   scene's palette moves a step toward the colours the script chose, a step
 *   of each of its red, green and blue, for as many steps as the script
 *   said;
 * - the stars twinkle: two dozen stars, each waiting a while, then playing
 *   its twinkle a frame every four ticks, then put anew somewhere else on the
 *   screen to wait again. The setup puts every star out first. Once the
 *   script hides them, a star put anew goes below the screen, out of sight.
 *
 * The shared endings' scripts start the colours' step too, with colours of
 * their own and a slower period: the period is an operand of the step's own
 * code, which a routine of theirs changes (scene_planet_fade_period). A
 * twinkle's frame is drawn as the program's sprite for it, its attributes
 * and its place from the star's, with the frame's tile. */
#include "scene/scene.h"

#include "charge.h"
#include "core/program.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"
#include "core/palette.h"
#include "platform/video.h"

/* ---- The planet's colours ------------------------------------------------------------------- */

/* A colour's three parts, five bits each, and a step of each. */
typedef struct {
    uint16_t bits, step;
} ColourPart;
static const ColourPart RED = { 0x001F, 0x0001 }, GREEN = { 0x03E0, 0x0020 }, BLUE = { 0x7C00, 0x0400 };

/* One part of `colour` a step toward the same part of `target`. */
static uint16_t part_toward(uint16_t colour, uint16_t target, ColourPart part)
{
    uint16_t now = colour & part.bits, wanted = target & part.bits;
    if (now < wanted)
        return (uint16_t)(now + part.step);
    if (now > wanted)
        return (uint16_t)(now - part.step);
    return now;
}

static uint16_t colour_toward(uint16_t colour, uint16_t target)
{
    return part_toward(colour, target, RED) | part_toward(colour, target, GREEN) | part_toward(colour, target, BLUE);
}

/* The colours are in the program's bank, a word each. */
static uint32_t colour_at(uint16_t colours, unsigned i)
{
    return (uint32_t)SCENE_LOWER_WORK_RAM << 16 | (uint16_t)(colours + 2 * i);
}

static bool step_is_due(const GameState *g) { return ((uint8_t)scene_frame_counter(g) & scene_planet_fade_period(g)) == 0; }

/* The original keeps each colour's parts and its count of the colours left
 * in its working cells, which it writes and reads back 24 bytes a colour;
 * the port keeps them in locals and charges their time. */
enum { WORKING_CELL_BYTES_A_COLOUR = 24 };

/* Every colour a step toward its target. Each colour is read before it is
 * written, and its target with it, so colours that lie over their targets
 * step as the original's do. */
static void step_the_colours(GameState *g)
{
    uint16_t toward = scene_planet_fade_toward(g), palette = scene_planet_fade_palette(g);
    uint16_t colours = scene_planet_fade_colours(g);
    for (unsigned i = 0; i < colours; i++) {
        uint32_t at = colour_at(palette, i);
        wram_write16(g, at, colour_toward(wram_read16(g, at), wram_read16(g, colour_at(toward, i))));
    }
    ORIGINAL_EFFECT(charge_as(g, 0x7E6439, (uint32_t)WORKING_CELL_BYTES_A_COLOUR * colours));
}

/* For the original's code and shadow mode: what its step leaves in its
 * working cells, its last colour's parts and nothing left to count. */
static void leave_the_step_s_cells(GameState *g)
{
    unsigned last = scene_planet_fade_colours(g) - 1u;
    uint16_t target = wram_read16(g, colour_at(scene_planet_fade_toward(g), last));
    uint16_t stepped = wram_read16(g, colour_at(scene_planet_fade_palette(g), last));
    set_scene_planet_fade_target_part(g, target & BLUE.bits);
    set_scene_planet_fade_stepped_parts(g, stepped & (RED.bits | GREEN.bits));
    set_scene_planet_fade_colours_left(g, 0);
}

/* For the frame list: whether this frame's step was taken, which decides what
 * the task leaves in the registers and can't be told from the colours after
 * (scaffolding). */
static bool colours_stepped;

// $7E:6439
static TaskEnd step_the_planet_s_colours_task(GameState *g, Platform *pf)
{
    (void)pf;
    ORIGINAL_EFFECT(colours_stepped = false);
    if (!step_is_due(g) || scene_planet_fade_steps(g) == 0)
        return task_goes_on();
    set_scene_planet_fade_steps(g, (uint8_t)(scene_planet_fade_steps(g) - 1));
    step_the_colours(g);
    ORIGINAL_EFFECT((leave_the_step_s_cells(g), colours_stepped = true));
    return task_goes_on();
}

/* A step taken leaves its last colour in A and Y past the colours stepped;
 * otherwise A's low byte is the frame counter's place in the period (0 where
 * the steps are all taken), its high byte as the list called it. */
static TaskLeftovers step_the_planet_s_colours_leaves(const GameState *g, TaskEnd end)
{
    (void)end;
    if (!colours_stepped)
        return (TaskLeftovers){ .a_set = true, .a = (uint8_t)scene_frame_counter(g) & scene_planet_fade_period(g),
                                .a_8bit = true, .a_high_as_called = true };
    uint16_t colours = scene_planet_fade_colours(g), palette = scene_planet_fade_palette(g);
    return (TaskLeftovers){ .a_set = true, .a = wram_read16(g, colour_at(palette, colours - 1u)), .y_set = true,
                            .y = (uint16_t)(palette + 2 * colours) };
}

/* The colours set stepping toward a set of others, which the step's task then
 * takes them toward a step at a time: a scene's script sets them with its
 * stores, and the endings' routines so ($7E:98F6). The steps left are a word,
 * whose high byte nothing reads. */
void scene_step_the_colours_toward(GameState *g, SceneColourStep step)
{
    set_scene_planet_fade_toward(g, step.toward);
    set_scene_planet_fade_palette(g, step.palette);
    set_scene_planet_fade_steps(g, step.steps);
    ORIGINAL_EFFECT(set_scene_planet_fade_steps_high(g, 0));
    set_scene_planet_fade_colours(g, step.colours);
}

/* ---- The stars ------------------------------------------------------------------------------ */

enum { TWINKLES = 24 };

/* A twinkle is ten frames, each shown four ticks; a star waits up to 64
 * frames before it starts, and is put somewhere across the middle of the
 * screen (128 pixels from 64) and down it (128 lines from 48), or below it
 * once the stars are hidden. */
enum { TWINKLE_FRAMES = 10, TICKS_A_FRAME = 4, LONGEST_WAIT = 64 };
enum { PLACES_ACROSS = 0x80, FIRST_ACROSS = 0x40, PLACES_DOWN = 0x80, FIRST_DOWN = 0x30, BELOW_THE_SCREEN = 0xE0 };

/* The twinkle's frames are tiles from $80 on. */
enum { FIRST_TWINKLE_TILE = 0x80 };

static bool stars_hidden(const GameState *g) { return scene_twinkles_hidden(g) != 0; }

// $7E:65AE
/* The star put anew: a new place, a new wait, its twinkle from the start. */
static void put_the_star_anew(GameState *g, unsigned star)
{
    set_scene_twinkles_x(g, star, (uint8_t)(FIRST_ACROSS + scene_next_random_byte(g) % PLACES_ACROSS));
    uint8_t y = BELOW_THE_SCREEN;
    if (!stars_hidden(g))
        y = (uint8_t)(FIRST_DOWN + scene_next_random_byte(g) % PLACES_DOWN);
    set_scene_twinkles_y(g, star, y);
    set_scene_twinkles_wait(g, star, (uint8_t)(1 + scene_next_random_byte(g) % LONGEST_WAIT));
    set_scene_twinkles_frames_left(g, star, TWINKLE_FRAMES);
    set_scene_twinkles_ticks(g, star, TICKS_A_FRAME);
}

/* For the frame list: what the last star to twinkle or be put anew this frame
 * did, which decides what the task leaves in Y and can't be told from the
 * stars after (scaffolding). */
typedef enum { NO_STAR_TWINKLED, STAR_TWINKLED, STAR_PUT_ANEW } StarFrame;
static StarFrame last_star_frame;

/* The program's sprite for a twinkle, with the frame's tile. */
static SceneSprite twinkle_sprite(const GameState *g, uint8_t tile)
{
    return (SceneSprite){ .x = scene_twinkle_sprite_x(g), .y = scene_twinkle_sprite_y(g), .tile = tile,
                          .attributes = scene_twinkle_sprite_attributes(g) };
}

/* The star's twinkle on screen: the program's sprite for it, with the frame's
 * tile, at the star's place (through the scene's sprites' offset, as the
 * program draws it). The tile is kept in the program's sprite, whose bytes
 * the captions' tables of letters share. */
static void draw_the_twinkle(GameState *g, unsigned star)
{
    uint8_t tile = (uint8_t)(FIRST_TWINKLE_TILE | (uint8_t)(TWINKLE_FRAMES - scene_twinkles_frames_left(g, star)));
    scene_move_sprite_offset(g, scene_twinkles_x(g, star), scene_twinkles_y(g, star));
    set_scene_twinkle_sprite_tile(g, tile);
    TIMED_AS(g, 0xC30DBC, scene_draw_sprite_moved(g, twinkle_sprite(g, tile)));
}

/* A frame of one star: still waiting; or a tick of its twinkle, put anew
 * once its last frame is over, and drawn otherwise. */
static void twinkle_a_star(GameState *g, unsigned star)
{
    uint8_t wait = scene_twinkles_wait(g, star);
    if (wait) {
        set_scene_twinkles_wait(g, star, (uint8_t)(wait - 1));
        return;
    }
    uint8_t ticks = (uint8_t)(scene_twinkles_ticks(g, star) - 1);
    set_scene_twinkles_ticks(g, star, ticks);
    if (ticks == 0) {
        set_scene_twinkles_ticks(g, star, TICKS_A_FRAME);
        uint8_t frames_left = (uint8_t)(scene_twinkles_frames_left(g, star) - 1);
        set_scene_twinkles_frames_left(g, star, frames_left);
        if (frames_left == 0) {
            put_the_star_anew(g, star);
            ORIGINAL_EFFECT(last_star_frame = STAR_PUT_ANEW);
            return;
        }
    }
    draw_the_twinkle(g, star);
    ORIGINAL_EFFECT(last_star_frame = STAR_TWINKLED);
}

// $7E:65DD
static TaskEnd twinkle_the_stars_task(GameState *g, Platform *pf)
{
    (void)pf;
    ORIGINAL_EFFECT(last_star_frame = NO_STAR_TWINKLED);
    scene_draw_lists_at(g, 0, 0);
    for (unsigned star = 0; star < TWINKLES; star++)
        twinkle_a_star(g, star);
    return task_goes_on();
}

// $7E:6598
/* Every star put out, as the scene sets up. */
static void put_out_the_stars(GameState *g)
{
    for (unsigned star = 0; star < TWINKLES; star++)
        put_the_star_anew(g, star);
}

/* The loop's end: the star records' end in A, 8-bit. Y is the next free
 * sprite entry after a twinkle drawn, the hiding's word after a star put
 * anew (both read it last), and as the list called it otherwise. */
enum { TWINKLES_END = 0x0CC0 };

static TaskLeftovers twinkle_the_stars_leaves(const GameState *g, TaskEnd end)
{
    (void)end;
    TaskLeftovers l = { .a_set = true, .a = TWINKLES_END, .a_8bit = true };
    if (last_star_frame == STAR_TWINKLED)
        l = (TaskLeftovers){ .a_set = true, .a = TWINKLES_END, .a_8bit = true, .y_set = true, .y = scene_sprite_index(g) };
    else if (last_star_frame == STAR_PUT_ANEW)
        l = (TaskLeftovers){ .a_set = true, .a = TWINKLES_END, .a_8bit = true, .y_set = true, .y = scene_twinkles_hidden(g) };
    return l;
}

/* ---- Where the script fades the planet to -------------------------------------------------------- */

/* The scene's two sets of colours the planet fades toward, unpacked by the
 * setup: the last it ends on, and the one mode 7's fade passes through
 * first. Each fade steps the scene's colours from the working palette's 32nd,
 * 192 of them, 32 steps, the screen's own fade's period a step every eight
 * frames. */
enum { LAST_COLOURS = 0xB574, COLOURS_ON_THE_WAY = 0xB774, FROM_THE_32ND = 0x40 };
enum { FADED_COLOURS = 0xC0, FADE_STEPS = 0x20, SCREEN_FADE_FRAMES = 8, FADED_PALETTE = 0x2240 };
/* The slow fade's step comes round once in 32 frames. */
enum { SLOW_FADE_PERIOD = 0x1F };

static void fade_toward(GameState *g, uint16_t colours)
{
    set_scene_planet_fade_toward(g, (uint16_t)(colours + FROM_THE_32ND));
    set_scene_planet_fade_steps(g, FADE_STEPS);
    ORIGINAL_EFFECT(set_scene_planet_fade_steps_high(g, 0));   /* the original sets the steps as a word */
}

// $7E:63FE, $7E:6403
/* Fade the planet toward a set of colours, the screen's fade slowed to a
 * step every eight frames. */
static void fade_the_planet_toward(GameState *g, uint16_t colours)
{
    fade_toward(g, colours);
    set_scene_fade_period(g, SCREEN_FADE_FRAMES);
    set_scene_fade_countdown(g, SCREEN_FADE_FRAMES);
    set_scene_planet_fade_palette(g, FADED_PALETTE);
    set_scene_planet_fade_colours(g, FADED_COLOURS);
}

static void fade_the_planet_through(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    fade_the_planet_toward(g, COLOURS_ON_THE_WAY);
}

static void fade_the_planet_out(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    fade_the_planet_toward(g, LAST_COLOURS);
}

// $7E:6422
/* On from the colours the planet has faded through, to the last, a step
 * every 32 frames. */
static void fade_the_planet_out_slowly(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    fade_toward(g, LAST_COLOURS);
    set_scene_planet_fade_period(g, SLOW_FADE_PERIOD);
}

/* The routines' code. */
bool scene_fade_the_planet_through_present(Snes *s) { return m_code_matches(s, 0x7E63FE, 0x24, 0x69FB288B5034BD32ull); }
bool scene_fade_the_planet_out_present(Snes *s) { return m_code_matches(s, 0x7E6403, 0x1F, 0x1D0B762344A2A669ull); }
bool scene_fade_the_planet_out_slowly_present(Snes *s) { return m_code_matches(s, 0x7E6422, 0x17, 0x49E214FE941AF74Full); }

enum { SCENE_ROW_FADE_THE_PLANET_THROUGH, SCENE_ROW_FADE_THE_PLANET_OUT, SCENE_ROW_FADE_THE_PLANET_OUT_SLOWLY };

const SceneScriptRoutine scene_planet_routines[] = {
    [SCENE_ROW_FADE_THE_PLANET_THROUGH] = { SCENE_ROUTINE_FADE_THE_PLANET_THROUGH, fade_the_planet_through,
                                             scene_fade_the_planet_through_present, false },
    [SCENE_ROW_FADE_THE_PLANET_OUT] = { SCENE_ROUTINE_FADE_THE_PLANET_OUT, fade_the_planet_out,
                                        scene_fade_the_planet_out_present, false },
    [SCENE_ROW_FADE_THE_PLANET_OUT_SLOWLY] = { SCENE_ROUTINE_FADE_THE_PLANET_OUT_SLOWLY, fade_the_planet_out_slowly,
                                                scene_fade_the_planet_out_slowly_present, false },
};
const unsigned scene_planet_routine_count = sizeof scene_planet_routines / sizeof scene_planet_routines[0];

#if ORIGINAL_EFFECTS
/* The fades leave A as their last load: the colours' count, or the slow
 * fade's period, whose high byte is the steps' word's. */
static uint16_t leaves_the_count(uint16_t called_with)
{
    (void)called_with;
    return FADED_COLOURS;
}
static uint16_t leaves_the_period(uint16_t called_with)
{
    (void)called_with;
    return SLOW_FADE_PERIOD;
}

const SceneRoutineLeaves scene_planet_routine_leaves[] = {
    [SCENE_ROW_FADE_THE_PLANET_THROUGH] = { leaves_the_count, NULL },
    [SCENE_ROW_FADE_THE_PLANET_OUT] = { leaves_the_count, NULL },
    [SCENE_ROW_FADE_THE_PLANET_OUT_SLOWLY] = { leaves_the_period, NULL },
};
#endif

void scene_fade_the_planet_through(Snes *s)
{
    scene_script_routine_alone(s, &scene_planet_routines[SCENE_ROW_FADE_THE_PLANET_THROUGH]);
}
void scene_fade_the_planet_out(Snes *s) { scene_script_routine_alone(s, &scene_planet_routines[SCENE_ROW_FADE_THE_PLANET_OUT]); }
void scene_fade_the_planet_out_slowly(Snes *s)
{
    scene_script_routine_alone(s, &scene_planet_routines[SCENE_ROW_FADE_THE_PLANET_OUT_SLOWLY]);
}

/* ---- The end of the scene ------------------------------------------------------------------------ */

/* The scene's caption, the program's text "But... the future refused to
 * change.", drawn every frame by a task of the script's while it is up. */
enum { THE_FUTURE_REFUSED_TO_CHANGE = 0x6644, CAPTION_DRAWING = 0x7E64CA };
enum { THE_DRAWING_READS_X = DC_A | DC_Y | DC_N | DC_V | DC_Z | DC_C };

// $7E:64BF
void scene_show_the_planet_s_caption_entry(Snes *s)
{
    s->cpu.x = THE_FUTURE_REFUSED_TO_CHANGE;
    m_set_nz16(s, THE_FUTURE_REFUSED_TO_CHANGE);
    m_jump_to(s, CAPTION_DRAWING);
    if (!scene_draw_caption_present(s)) {
        m_stop_at(s, CAPTION_DRAWING, THE_DRAWING_READS_X);
        return;
    }
    NATIVE_TIME_AS(s, CAPTION_DRAWING, scene_draw_caption_entry(s));
}

TaskEnd scene_show_the_planet_s_caption(GameState *g, Platform *pf, unsigned slot)
{
    (void)pf, (void)slot;
    if (!scene_caption_fits(g, THE_FUTURE_REFUSED_TO_CHANGE))
        return scene_task_needs_the_machine(g, CAPTION_DRAWING, "a caption the port doesn't draw");
    TIMED_AS(g, CAPTION_DRAWING, scene_draw_caption(g, THE_FUTURE_REFUSED_TO_CHANGE));
    return task_goes_on();
}

/* After the caption, the script's last task waits for a button: then a
 * sound, the screen darkens (the program's darkening, a task of its own), and
 * the task hands its slot to the countdown to the scene's end, after which the
 * game starts again from its reset. */
enum { RESTART_SOUND = 0x81, RESTART_SOUND_PARAMETER = 0x40, COUNT_DOWN_TO_THE_RESTART = 0x63F5 };
enum { SCENE_SEND = 0x7E377E, THE_SEND_READS_Y = DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C };
/* The list's exit that hands the task's slot over reads the task that takes
 * it in X; the reset reads none of the registers but Y, which the countdown
 * leaves as it was (the liveness can't rule it out down the field's start). */
enum { HAND_THE_SLOT_OVER = 0x7E39B1, THE_HAND_OVER_READS_X = DC_A | DC_N | DC_V | DC_Z | DC_C,
       CONSOLE_RESET = 0x00FF00, THE_RESET_MAY_READ_Y = DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C };

static void ask_for_the_restart_sound(GameState *g)
{
    set_sound_command(g, RESTART_SOUND);
    set_sound_command_parameters(g, 0, RESTART_SOUND_PARAMETER);
    set_sound_command_parameters(g, 1, 0);
}

// $7E:63D5
/* The send (`send`, on the machine the program's own from the task's call)
 * waits for the driver to take the sound. */
static TaskEnd wait_for_a_button_with(GameState *g, Platform *pf, void (*send)(void *ctx, Platform *pf), void *ctx)
{
    if (scene_joypad(g) == 0)
        return task_goes_on();
    ask_for_the_restart_sound(g);
    send(ctx, pf);
    TIMED_AS(g, 0x7E395E, (void)scene_add_task(g, SCENE_FRAME_TASKS, SCENE_TASK_DARKEN));
    return task_hands_over(COUNT_DOWN_TO_THE_RESTART);
}

static void send_on_the_machine(void *ctx, Platform *pf)
{
    (void)ctx;
    au_driver_call(pf, PF_CALL(0x7E63E5, SCENE_SEND), THE_SEND_READS_Y);
}

void scene_wait_for_a_button_to_restart_entry(Snes *s)
{
    TaskEnd end = wait_for_a_button_with(state_of(s), m_platform(s), send_on_the_machine, s);
    if (end.outcome == TASK_GOES_ON) {
        ORIGINAL_EFFECT(s->cpu.a = 0);   /* the controller's load */
        m_set_nz16(s, 0);
        m_rts(s);
        return;
    }
    s->cpu.x = end.next;
    m_stop_at_own(s, HAND_THE_SLOT_OVER, THE_HAND_OVER_READS_X);
}

static void send(void *ctx, Platform *pf) { scene_sends_the_command(ctx, pf); }

/* As C, with its guard's state half: room on the frame's list for the
 * darkening. */
TaskEnd scene_wait_for_a_button_to_restart(GameState *g, Platform *pf, unsigned slot)
{
    (void)slot;
    if (!scene_task_listed(g, SCENE_FRAME_TASKS, NO_TASK))
        return scene_task_needs_the_machine(g, 0x7E63D5, "no room on the frame's list for the darkening");
    return wait_for_a_button_with(g, pf, send, g);
}

// $7E:63F5
/* A frame of the wait before the restart: true once it is over. */
static bool count_down(GameState *g)
{
    uint16_t left = (uint16_t)(scene_end_wait(g) - 1);
    set_scene_end_wait(g, left);
    return left == 0;
}

void scene_count_down_to_the_restart_entry(Snes *s)
{
    GameState *g = state_of(s);
    bool over = count_down(g);
    m_set_nz16(s, scene_end_wait(g));
    if (!over) {
        m_rts(s);
        return;
    }
    m_stop_at(s, CONSOLE_RESET, THE_RESET_MAY_READ_Y);
}

/* As C, the reset the program's main thread takes (core/program.h). */
TaskEnd scene_count_down_to_the_restart(GameState *g, Platform *pf, unsigned slot)
{
    (void)pf, (void)slot;
    if (count_down(g))
        program_resets_the_console(g);
    return task_goes_on();
}

/* The tasks' code, on the scene's page; the wait's send's, and the list's
 * room for the darkening. */
bool scene_planet_s_caption_present(Snes *s) { return m_code_matches(s, 0x7E64BF, 0x06, 0x95BAD85F81FC3237ull); }

bool scene_wait_for_a_button_to_restart_present(Snes *s)
{
    if (!scene_on_scene_page(s) || !m_code_matches(s, 0x7E63D5, 0x20, 0x04BAEB7BD16D9B0Eull) || !scene_send_sound_command_present(s))
        return false;
    vars_peek(true);
    bool room = scene_task_listed(state_of(s), SCENE_FRAME_TASKS, NO_TASK);
    vars_peek(false);
    return room;
}

bool scene_count_down_to_the_restart_present(Snes *s) { return m_code_matches(s, 0x7E63F5, 0x09, 0xD8B581955D8D3E20ull); }

/* ---- The setup ------------------------------------------------------------------------------------ */

/* The scene's graphics, by their resources in the program's list: the
 * captions' letters, as 4-bit sprite tiles at $4000; the stars' sprites at
 * $4800; the planet's tiles at $5000 and its map at $7000. */
enum { LETTERS = 0x63, STAR_SPRITES = 0x5D, PLANET_TILES = 0xCF, PLANET_MAP = 0xB1 };
enum { LETTERS_VRAM = 0x4000, STAR_SPRITES_VRAM = 0x4800, PLANET_TILES_VRAM = 0x5000, PLANET_MAP_VRAM = 0x7000 };

/* Its colours, unpacked into the program's bank: the loaded palette's from the
 * 16th and from the 224th, and the two sets the planet fades toward. */
enum { COLOURS = 0x54, LAST_COLOURS_RESOURCE = 0x57, COLOURS_ON_THE_WAY_RESOURCE = 0x5A, LATE_COLOURS = 0x60 };
enum { COLOURS_AT = 0x2020, LATE_COLOURS_AT = 0x21C0 };

/* The scripts: mode 7's, the Day of Lavos's end, and mode 8's, an ending's. */
enum { MODE_BITS = 0x0F, AN_ENDING_S_PLANET = 8, LOST_SCRIPT = 0x633D, ENDING_SCRIPT = 0x63A6 };

/* Frames from the button to the scene's end. */
enum { END_WAIT = 0x80 };

static void unpack_at(GameState *g, uint16_t at) { set_decompress_dest(g, (decompress_dest(g) & 0xFF0000u) | at); }

/* The unpacked graphics sent a word at a time; and the transfer's mode kept
 * in its cell's low byte, the high byte cleared, which the stars' random draws
 * stir in next (scene/random.c). */
static void send_unpacked_words(GameState *g, Platform *pf)
{
    set_scene_call_transfer_control(g, scene_program_own(), SCENE_TRANSFER_WORDS);
    set_scene_call_transfer_register(g, scene_program_own(), VID_SNES_VRAM_DATA_LOW);
    TIMED_AS(g, 0x7E38E7, scene_send_unpacked(g, pf));
}

/* The setup's steps that are other routines: a resource unpacked by the
 * program's unpacker, the scripts' first step and the stars put out. The program makes them as C, the machine's entry
 * from the original's calls. */
/* What the setup unpacks, in its order, each from its own call of the
 * program's unpacker. */
typedef enum {
    UNPACK_LETTERS, UNPACK_STAR_SPRITES, UNPACK_PLANET_TILES, UNPACK_PLANET_MAP, UNPACK_COLOURS, UNPACK_LAST_COLOURS,
    UNPACK_COLOURS_ON_THE_WAY, UNPACK_LATE_COLOURS, PLANET_UNPACKS
} PlanetUnpack;

static const struct {
    uint32_t site;
    uint16_t resource;
} planet_unpacks[PLANET_UNPACKS] = {
    [UNPACK_LETTERS] = { 0x7E6259, LETTERS },
    [UNPACK_STAR_SPRITES] = { 0x7E626B, STAR_SPRITES },
    [UNPACK_PLANET_TILES] = { 0x7E627F, PLANET_TILES },
    [UNPACK_PLANET_MAP] = { 0x7E6295, PLANET_MAP },
    [UNPACK_COLOURS] = { 0x7E62AA, COLOURS },
    [UNPACK_LAST_COLOURS] = { 0x7E62B6, LAST_COLOURS_RESOURCE },
    [UNPACK_COLOURS_ON_THE_WAY] = { 0x7E62C2, COLOURS_ON_THE_WAY_RESOURCE },
    [UNPACK_LATE_COLOURS] = { 0x7E62D1, LATE_COLOURS },
};

typedef struct {
    void (*unpack)(void *ctx, PlanetUnpack which);
    bool (*first_step)(void *ctx);
    bool (*put_out_the_stars)(void *ctx);
} PlanetSetUpSteps;

static void load_the_graphics(GameState *g, Platform *pf, const PlanetSetUpSteps *steps, void *ctx)
{
    vid_start_vram_uploads(pf, PF_AT(0x7E624A), SCENE_VRAM_WHOLE_WORDS);
    set_decompress_dest(g, SCENE_UNPACK_BUFFER);
    vid_set_vram_address(pf, PF_AT(0x7E6253), LETTERS_VRAM);
    steps->unpack(ctx, UNPACK_LETTERS);
    scene_send_letters_widened(g, pf);
    vid_set_vram_address(pf, PF_AT(0x7E6265), STAR_SPRITES_VRAM);
    steps->unpack(ctx, UNPACK_STAR_SPRITES);
    send_unpacked_words(g, pf);
    steps->unpack(ctx, UNPACK_PLANET_TILES);
    vid_set_vram_address(pf, PF_AT(0x7E6285), PLANET_TILES_VRAM);
    TIMED_AS(g, 0x7E38E7, scene_send_unpacked(g, pf));
    vid_set_vram_address(pf, PF_AT(0x7E628F), PLANET_MAP_VRAM);
    steps->unpack(ctx, UNPACK_PLANET_MAP);
    TIMED_AS(g, 0x7E38E7, scene_send_unpacked(g, pf));
}

/* The original clears the call cells' second byte as it loads the colours,
 * which nothing the setup runs after it reads. */
static void clear_the_call_cells_second_byte(GameState *g)
{
    SceneProgramPage scene = scene_program_own();
    set_scene_call_first(g, scene, (uint16_t)(scene_call_first(g, scene) & 0x00FF));
}

static void load_the_colours(GameState *g, const PlanetSetUpSteps *steps, void *ctx)
{
    set_decompress_dest(g, COLOURS_AT);
    ORIGINAL_EFFECT(clear_the_call_cells_second_byte(g));
    steps->unpack(ctx, UNPACK_COLOURS);
    unpack_at(g, LAST_COLOURS);
    steps->unpack(ctx, UNPACK_LAST_COLOURS);
    unpack_at(g, COLOURS_ON_THE_WAY);
    steps->unpack(ctx, UNPACK_COLOURS_ON_THE_WAY);
    scene_set_caption_colours(g);
    unpack_at(g, LATE_COLOURS_AT);
    steps->unpack(ctx, UNPACK_LATE_COLOURS);
    TIMED_AS(g, 0x7E38AF, scene_use_loaded_palette(g));
}

/* The screen: Mode 3, the planet on BG1 (its map at $7000, its tiles at
 * $5000), set 72 pixels in and 52 down from the top left; the sprites on the
 * sub screen, added onto the planet; both screens masked by every window. */
static const VidBg planet_bg1 = { VID_TILEMAP(0x7000, 32, 32) };
enum { PLANET_ACROSS = -72, PLANET_DOWN = -52, EVERY_WINDOW = 0x1F };
static const VidColorMath sprites_added = { .sub_screen = true, .layers = VID_BG1_BIT };

static void set_up_the_screen(Platform *pf)
{
    vid_set_mode(pf, PF_AT(0x7E6303), &(VidMode){ .mode = 3 });
    vid_set_bg_map(pf, PF_AT(0x7E6308), VID_BG1, &planet_bg1);
    vid_set_bg_chars(pf, PF_AT(0x7E630D), VID_BG1, PLANET_TILES_VRAM, 0x0000);
    vid_set_scroll(pf, PF_AT(0x7E6312), VID_BG1, (uint16_t)PLANET_ACROSS, 0, VID_SCROLL_H);
    vid_set_scroll(pf, PF_AT(0x7E631C), VID_BG1, 0, (uint16_t)PLANET_DOWN, VID_SCROLL_V);
    vid_set_color_math(pf, PF_AT(0x7E6327), &sprites_added, VID_MATH_CONTROL | VID_MATH_LAYERS);
    vid_set_screens(pf, PF_AT(0x7E632D), VID_BG1_BIT, VID_OBJ_BIT, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_set_windows(pf, PF_AT(0x7E6333), &(VidWindows){ .win_main = EVERY_WINDOW, .win_sub = EVERY_WINDOW },
                    VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
}

// $7E:6248
/* The planet's scene set up, from the program's mode table (modes 7 and 8):
 * its graphics and colours loaded, the mode's script started, the stars put
 * out, the palette animations stopped and the screen laid out; and the
 * scene's end a few seconds after the button the script's last task waits
 * for. False where a step went no further. */
static bool set_up_the_planet_with(GameState *g, Platform *pf, const PlanetSetUpSteps *steps, void *ctx)
{
    load_the_graphics(g, pf, steps, ctx);
    load_the_colours(g, steps, ctx);
    bool an_ending = (scene_flags(g) & MODE_BITS) == AN_ENDING_S_PLANET;
    set_scene_channels_script(g, SCENE_CAMERA_CHANNEL, an_ending ? ENDING_SCRIPT : LOST_SCRIPT);
    if (!steps->first_step(ctx) || !steps->put_out_the_stars(ctx))
        return false;
    TIMED_AS(g, 0xFDFFEE, palette_stop_animations(g));
    set_up_the_screen(pf);
    set_scene_end_wait(g, END_WAIT);
    return true;
}

/* ---- On the machine ---- */

/* Putting the stars out, as its entry runs (scene_put_out_the_stars, which
 * refuses a carry the original would add in); it reads nothing else, and
 * leaves the overflow clear for the setup's callers. */
enum { PUT_OUT_THE_STARS = 0x7E6598, THE_STARS_READ_THE_CARRY = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z };

static void unpack_on_the_machine(void *ctx, PlanetUnpack which)
{
    scene_bridge_unpack(ctx, planet_unpacks[which].site, planet_unpacks[which].resource);
}

static bool first_step_on_the_machine(void *ctx)
{
    scene_bridge_first_script_step(ctx, 0x7E62F0, SCENE_Y_PAST_THE_WORKING_PALETTE);
    return true;
}

static bool put_out_the_stars_on_the_machine(void *ctx)
{
    return scene_run(ctx, 0x7E62FA, PUT_OUT_THE_STARS, THE_STARS_READ_THE_CARRY, scene_put_out_the_stars);
}

/* True unless a call-out stopped it. The palette animations' stop leaves Y
 * past their slots, as the original's loop does. */
bool scene_planet_set_up_scene(Snes *s)
{
    static const PlanetSetUpSteps on_the_machine = {
        unpack_on_the_machine, first_step_on_the_machine, put_out_the_stars_on_the_machine,
    };
    if (!set_up_the_planet_with(state_of(s), m_platform(s), &on_the_machine, s))
        return false;
    ORIGINAL_EFFECT(s->cpu.y = SCENE_Y_PAST_THE_ANIMATIONS_SLOTS);
    return true;
}

void scene_planet_set_up_entry(Snes *s)
{
    if (scene_planet_set_up_scene(s))
        m_rts(s);
}

/* ---- As C ---- */

/* scene/program.c's setups with no machine: the setup's and the stars'
 * guards are their code alone. The scripts' first step is the camera
 * channel's turn. */
typedef struct {
    GameState *game;
    Platform *pf;
} PlanetInC;

static void unpack_the_resource(void *ctx, PlanetUnpack which)
{
    PlanetInC *c = ctx;
    TIMED_AS(c->game, SCENE_UNPACK, scene_unpacks(c->game, planet_unpacks[which].resource));
}

static bool turn_the_camera_channel(void *ctx)
{
    PlanetInC *c = ctx;
    SceneTurn turn = SCENE_TURN_ENDED;
    TIMED_AS(c->game, SCENE_FIRST_STEP, turn = scene_channel_turn(c->game, c->pf, SCENE_CAMERA_CHANNEL));
    return turn == SCENE_TURN_ENDED;
}

static bool stars_put_out(void *ctx)
{
    PlanetInC *c = ctx;
    TIMED_AS(c->game, PUT_OUT_THE_STARS, put_out_the_stars(c->game));
    return true;
}

bool scene_planet_set_up(GameState *g, Platform *pf)
{
    static const PlanetSetUpSteps in_the_program = { unpack_the_resource, turn_the_camera_channel, stars_put_out };
    PlanetInC c = { g, pf };
    return set_up_the_planet_with(g, pf, &in_the_program, &c);
}

/* The setup's code, and the program's routines it runs as C: the unpacker's
 * destination set, the letters' widening, the transfer, the captions'
 * colours, the palette made current. */
bool scene_planet_set_up_present(Snes *s)
{
    return scene_on_scene_page(s) && m_bank_reaches_io(s) && m_code_matches(s, 0x7E6248, 0xF5, 0xC63E429D7ED112C2ull) &&
           m_code_matches(s, 0x7E89ED, 0x14, 0x22A581F747CC4E58ull) && m_code_matches(s, 0x7E66B0, 0x2D, 0x668182BFFD2B1A5Bull) &&
           scene_transfer_present(s) && m_code_matches(s, 0x7E8772, 0x1E, 0xACB5DF9A02DD40EAull) &&
           m_code_matches(s, 0x7E38AF, 0x14, 0xD31DAC893D4743DCull);
}

/* ---- The rows ------------------------------------------------------------------------------- */

enum { ROW_STEP_THE_COLOURS, ROW_TWINKLE };

const SceneTaskPort scene_planet_tasks[] = {
    [ROW_STEP_THE_COLOURS] = { SCENE_TASK_STEP_THE_PLANET_S_COLOURS, SCENE_FRAME_TASKS, step_the_planet_s_colours_task,
      scene_step_the_planet_s_colours_present, step_the_planet_s_colours_leaves, false },
    [ROW_TWINKLE] = { SCENE_TASK_TWINKLE_THE_STARS, SCENE_FRAME_TASKS, twinkle_the_stars_task,
      scene_twinkle_the_stars_present, twinkle_the_stars_leaves, false },
};
const unsigned scene_planet_task_count = sizeof scene_planet_tasks / sizeof scene_planet_tasks[0];

/* ---- Entries -------------------------------------------------------------------------------- */

void scene_step_the_planet_s_colours(Snes *s) { scene_task_alone(s, &scene_planet_tasks[ROW_STEP_THE_COLOURS]); }
void scene_twinkle_the_stars(Snes *s) { scene_task_alone(s, &scene_planet_tasks[ROW_TWINKLE]); }

/* Called by the setup with 8-bit A; it leaves the loop's end in X and A, the
 * hiding's word in Y, as the original's last loads do, and the overflow its
 * last step to the end clears. */
void scene_put_out_the_stars(Snes *s)
{
    GameState *g = state_of(s);
    put_out_the_stars(g);
    s->cpu.x = TWINKLES_END;
    s->cpu.a = TWINKLES_END;
    s->cpu.y = scene_twinkles_hidden(g);
    m_set_flag(s, CPU_FLAG_V, false);
    m_rts(s);
}

/* ---- The guards ----------------------------------------------------------------------------- */

/* The program's next random byte ($7E:3448), which putting a star anew takes. */
static bool random_byte_present(Snes *s) { return m_code_matches(s, 0x7E3448, 0x19, 0x092856CFEC289766ull); }

/* Putting a star anew adds with the carry its caller leaves, which puts the
 * first star a pass puts anew one pixel further right where it is set: no
 * showing enters with it set, and the port refuses it. */
static bool put_anew_present(Snes *s)
{
    return !m_flag(s, CPU_FLAG_C) && m_code_matches(s, 0x7E65AE, 0x2F, 0x2A8A796676B64834ull) && random_byte_present(s);
}

/* The step's code, from its start to its return, but for its period, an
 * operand the script changes; and where it keeps its working values, which
 * it writes as each colour steps. */
enum { STEP_CODE = 0x6439, STEP_CODE_END = 0x64BF, WORKING_CELLS = 0x03F0, WORKING_CELLS_END = 0x0400 };

static bool clear_of(uint32_t from, uint32_t bytes, uint16_t start, uint16_t end)
{
    return from + bytes <= start || from >= end;
}

/* The colours, at least one of each, inside the program's bank and clear of
 * the step's code, its working cells and the top of the stack, where its
 * call's return and the page and bank it sets go: the game's are the scene's
 * palette and colours the setup loaded, and where they lay over any of those
 * the original would step its own code or its working values. The list asks
 * before its call pushes the return, an entry's guard after. */
enum { PUSHED_BELOW = 4 };

static bool colours_step_apart(uint16_t at, uint32_t bytes, uint16_t stack)
{
    return bytes > 0 && at + bytes <= 0x10000 && clear_of(at, bytes, STEP_CODE, STEP_CODE_END) &&
           clear_of(at, bytes, WORKING_CELLS, WORKING_CELLS_END) &&
           clear_of(at, bytes, (uint16_t)(stack - PUSHED_BELOW + 1), (uint16_t)(stack + 1));
}

bool scene_step_the_planet_s_colours_present(Snes *s)
{
    if (!scene_on_scene_page(s) || !m_code_matches(s, 0x7E6439, 0x05, 0x003641B369165D01ull) ||
        !m_code_matches(s, 0x7E643F, 0x80, 0xC9E23A0C8E34C0ACull))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint32_t bytes = 2u * scene_planet_fade_colours(g);
    uint16_t stack = s->cpu.s;
    bool ok = colours_step_apart(scene_planet_fade_toward(g), bytes, stack) &&
              colours_step_apart(scene_planet_fade_palette(g), bytes, stack);
    vars_peek(false);
    return ok;
}

/* The twinkling's code, on the scene's page with the index registers 16-bit,
 * the star records reached through a data bank that also reaches the
 * sprites' tables; room for every star's sprite from the cursor. */
bool scene_twinkle_the_stars_present(Snes *s)
{
    if (!scene_on_scene_page_io_bank(s) || m_x8(s) || !m_code_matches(s, 0x7E65DD, 0x63, 0x317527917B4521B0ull) ||
        !put_anew_present(s))
        return false;
    vars_peek(true);
    bool fits = scene_sprites_fit(state_of(s), TWINKLES);
    vars_peek(false);
    return fits;
}

bool scene_put_out_the_stars_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && !m_x8(s) && m_code_matches(s, 0x7E6598, 0x16, 0x18149F6024C885CDull) &&
           put_anew_present(s);
}
