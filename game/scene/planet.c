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
#include "platform/audio.h"
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
    charge_as(g, 0x7E6439, (uint32_t)WORKING_CELL_BYTES_A_COLOUR * colours);
}

/* Whether this frame's step of the colours was due, which decides the time
 * the task is charged. */
static bool colours_stepped;

// $7E:6439
static TaskEnd step_the_planet_s_colours_task(GameState *g, Platform *pf)
{
    (void)pf;
    colours_stepped = false;
    if (!step_is_due(g) || scene_planet_fade_steps(g) == 0)
        return task_goes_on();
    set_scene_planet_fade_steps(g, (uint8_t)(scene_planet_fade_steps(g) - 1));
    step_the_colours(g);
    charge_accesses(g, 16);
    colours_stepped = true;
    return task_goes_on();
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
    charge_accesses(g, 1);
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

/* How the frame's last star went -- twinkled, or put anew -- which decides
 * the time the twinkling is charged. */
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
            last_star_frame = STAR_PUT_ANEW;
            return;
        }
    }
    draw_the_twinkle(g, star);
    last_star_frame = STAR_TWINKLED;
}

// $7E:65DD
static TaskEnd twinkle_the_stars_task(GameState *g, Platform *pf)
{
    (void)pf;
    last_star_frame = NO_STAR_TWINKLED;
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
    charge_accesses(g, 1);   /* the original sets the steps as a word */
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

enum { SCENE_ROW_FADE_THE_PLANET_THROUGH, SCENE_ROW_FADE_THE_PLANET_OUT, SCENE_ROW_FADE_THE_PLANET_OUT_SLOWLY };

const SceneScriptRoutine scene_planet_routines[] = {
    [SCENE_ROW_FADE_THE_PLANET_THROUGH] = { SCENE_ROUTINE_FADE_THE_PLANET_THROUGH, fade_the_planet_through },
    [SCENE_ROW_FADE_THE_PLANET_OUT] = { SCENE_ROUTINE_FADE_THE_PLANET_OUT, fade_the_planet_out },
    [SCENE_ROW_FADE_THE_PLANET_OUT_SLOWLY] = { SCENE_ROUTINE_FADE_THE_PLANET_OUT_SLOWLY, fade_the_planet_out_slowly },
};
const unsigned scene_planet_routine_count = sizeof scene_planet_routines / sizeof scene_planet_routines[0];

/* ---- The end of the scene ------------------------------------------------------------------------ */

/* The scene's caption, the program's text "But... the future refused to
 * change.", drawn every frame by a task of the script's while it is up. */
enum { THE_FUTURE_REFUSED_TO_CHANGE = 0x6644, CAPTION_DRAWING = 0x7E64CA };

TaskEnd scene_show_the_planet_s_caption(GameState *g, Platform *pf, unsigned slot)
{
    (void)pf, (void)slot;
    if (!scene_caption_fits(g, THE_FUTURE_REFUSED_TO_CHANGE))
        return scene_task_goes_no_further(g, CAPTION_DRAWING, "a caption the port doesn't draw");
    TIMED_AS(g, CAPTION_DRAWING, scene_draw_caption(g, THE_FUTURE_REFUSED_TO_CHANGE));
    return task_goes_on();
}

/* After the caption, the script's last task waits for a button: then a
 * sound, the screen darkens (the program's darkening, a task of its own), and
 * the task hands its slot to the countdown to the scene's end, after which the
 * game starts again from its reset. */
enum { RESTART_SOUND = 0x81, RESTART_SOUND_PARAMETER = 0x40, COUNT_DOWN_TO_THE_RESTART = 0x63F5 };

static void ask_for_the_restart_sound(GameState *g)
{
    set_sound_command(g, RESTART_SOUND);
    set_sound_command_parameters(g, 0, RESTART_SOUND_PARAMETER);
    set_sound_command_parameters(g, 1, 0);
}

// $7E:63D5
/* The wait, where the frame's list has room for the darkening (otherwise the
 * scene goes no further): at a button, the restart's sound sent, the program
 * waiting for the driver to take it. */
TaskEnd scene_wait_for_a_button_to_restart(GameState *g, Platform *pf, unsigned slot)
{
    (void)slot;
    if (!scene_task_listed(g, SCENE_FRAME_TASKS, NO_TASK))
        return scene_task_goes_no_further(g, 0x7E63D5, "no room on the frame's list for the darkening");
    if (scene_joypad(g) == 0)
        return task_goes_on();
    ask_for_the_restart_sound(g);
    scene_sends_the_command(g, pf);
    TIMED_AS(g, 0x7E395E, (void)scene_add_task(g, SCENE_FRAME_TASKS, SCENE_TASK_DARKEN));
    return task_hands_over(COUNT_DOWN_TO_THE_RESTART);
}

// $7E:63F5
/* A frame of the wait before the restart: true once it is over. */
static bool count_down(GameState *g)
{
    uint16_t left = (uint16_t)(scene_end_wait(g) - 1);
    set_scene_end_wait(g, left);
    return left == 0;
}

/* The countdown, ending in the reset the program's main thread takes
 * (core/program.h). */
TaskEnd scene_count_down_to_the_restart(GameState *g, Platform *pf, unsigned slot)
{
    (void)pf, (void)slot;
    if (count_down(g))
        program_resets_the_console(g);
    return task_goes_on();
}

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
    set_scene_call_transfer_register(g, scene_program_own(), SCENE_INTO_LOW_BYTES);
    TIMED_AS(g, 0x7E38E7, scene_send_unpacked(g, pf));
}

static void load_the_graphics(GameState *g, Platform *pf)
{
    vid_start_vram_uploads(pf, PF_AT(0x7E624A), SCENE_VRAM_WHOLE_WORDS);
    set_decompress_dest(g, SCENE_UNPACK_BUFFER);
    vid_set_vram_address(pf, PF_AT(0x7E6253), LETTERS_VRAM);
    scene_unpacks(g, LETTERS);
    scene_send_letters_widened(g, pf);
    vid_set_vram_address(pf, PF_AT(0x7E6265), STAR_SPRITES_VRAM);
    scene_unpacks(g, STAR_SPRITES);
    send_unpacked_words(g, pf);
    scene_unpacks(g, PLANET_TILES);
    vid_set_vram_address(pf, PF_AT(0x7E6285), PLANET_TILES_VRAM);
    TIMED_AS(g, 0x7E38E7, scene_send_unpacked(g, pf));
    vid_set_vram_address(pf, PF_AT(0x7E628F), PLANET_MAP_VRAM);
    scene_unpacks(g, PLANET_MAP);
    TIMED_AS(g, 0x7E38E7, scene_send_unpacked(g, pf));
}

/* The original clears the call cells' second byte as it loads the colours,
 * which nothing the setup runs after it reads. */

static void load_the_colours(GameState *g)
{
    set_decompress_dest(g, COLOURS_AT);
    charge_accesses(g, 4);
    scene_unpacks(g, COLOURS);
    unpack_at(g, LAST_COLOURS);
    scene_unpacks(g, LAST_COLOURS_RESOURCE);
    unpack_at(g, COLOURS_ON_THE_WAY);
    scene_unpacks(g, COLOURS_ON_THE_WAY_RESOURCE);
    scene_set_caption_colours(g);
    unpack_at(g, LATE_COLOURS_AT);
    scene_unpacks(g, LATE_COLOURS);
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
 * for. False where the script's first step went no further. */
enum { PUT_OUT_THE_STARS = 0x7E6598 };

bool scene_planet_set_up(GameState *g, Platform *pf)
{
    load_the_graphics(g, pf);
    load_the_colours(g);
    bool an_ending = (scene_flags(g) & MODE_BITS) == AN_ENDING_S_PLANET;
    set_scene_channels_script(g, SCENE_CAMERA_CHANNEL, an_ending ? ENDING_SCRIPT : LOST_SCRIPT);
    if (!scene_first_step(g, pf, SCENE_CAMERA_CHANNEL))
        return false;
    TIMED_AS(g, PUT_OUT_THE_STARS, put_out_the_stars(g));
    TIMED_AS(g, 0xFDFFEE, palette_stop_animations(g));
    set_up_the_screen(pf);
    set_scene_end_wait(g, END_WAIT);
    return true;
}

/* ---- The rows ------------------------------------------------------------------------------- */

enum { ROW_STEP_THE_COLOURS, ROW_TWINKLE };

const SceneTaskPort scene_planet_tasks[] = {
    [ROW_STEP_THE_COLOURS] = { SCENE_TASK_STEP_THE_PLANET_S_COLOURS, SCENE_FRAME_TASKS, step_the_planet_s_colours_task },
    [ROW_TWINKLE] = { SCENE_TASK_TWINKLE_THE_STARS, SCENE_FRAME_TASKS, twinkle_the_stars_task },
};
const unsigned scene_planet_task_count = sizeof scene_planet_tasks / sizeof scene_planet_tasks[0];

