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
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

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
