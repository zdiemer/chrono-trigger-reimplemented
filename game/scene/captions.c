/* Scenes: the captions over the shared endings.
 *
 * Modes 10 and 11 of the shared endings show the staff's names as captions
 * over the zooming picture, one after another: each fades in, stays up a
 * while, fades out over its last 32 frames, and a gap goes by before the
 * next. The setup sets the times (endings.c); the captions' task
 * ($7E:9B4F) counts them down every frame and draws the caption that is up.
 *
 * A caption is text in the program: letters, spaces, the end of a line and a
 * new place to start from, written as sprites. A letter is one 16x16 sprite
 * if it is wide, or two 8x8 ones stacked if it is narrow, and the pen moves
 * on by its width and a pixel. Every caption starts where the last line of
 * text that set a place began (scene_caption_line_x/_y), and the pen is left
 * where the scenes' sprite lists take their offset. */
#include "scene/scene.h"

#include "core/palette.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* ---- A caption's text ------------------------------------------------------------------------- */

/* The text's codes: the end, a new place to start the lines from (its X and Y
 * follow), the end of a line, and a space; anything else is a letter. */
enum { TEXT_END = 0x40, TEXT_PLACE = 0x00, TEXT_NEW_LINE = 0x2F, TEXT_SPACE = 0x20 };

/* The program's tables of the letters, by code (scene_caption_letters): its
 * width in pixels, and 32 bytes on, its first tile. */
enum { TILES_AFTER_WIDTHS = 0x20 };

static uint8_t letter_width(const GameState *g, uint8_t code) { return scene_caption_letters(g, code); }
static uint8_t letter_tile(const GameState *g, uint8_t code) { return scene_caption_letters(g, (unsigned)code + TILES_AFTER_WIDTHS); }

/* A line is 16 pixels; a space 8. A letter 9 pixels wide or wider is drawn
 * as a large sprite; a narrower one as two small ones, the lower half's tiles
 * a row of tiles on from the upper's. */
enum { LINE_HEIGHT = 16, SPACE_WIDTH = 8, WIDE_LETTER = 9, HALF_LETTER = 8, NEXT_TILE_ROW = 0x10 };

static uint8_t text_byte(const GameState *g, uint16_t at) { return wram_read8(g, (uint32_t)SCENE_LOWER_WORK_RAM << 16 | at); }

/* Widths past 136 count as narrow, as the original's signed compare takes
 * them; no letter is that wide. */
static bool is_wide(uint8_t width) { return width >= WIDE_LETTER && width < WIDE_LETTER + 0x80; }

typedef struct {
    uint8_t x, y;
} Pen;

/* A letter drawn at the pen, which moves on past it. */
static void draw_letter(GameState *g, Pen *pen, uint8_t code)
{
    uint8_t tile = letter_tile(g, code), width = letter_width(g, code);
    uint8_t attributes = scene_caption_letter_attributes(g);
    SceneSprite upper = { pen->x, pen->y, tile, attributes };
    if (is_wide(width)) {
        scene_add_sprite_at_cursor(g, upper, true);
    } else {
        scene_add_sprite_at_cursor(g, upper, false);
        scene_add_sprite_at_cursor(g, (SceneSprite){ pen->x, (uint8_t)(pen->y + HALF_LETTER), (uint8_t)(tile + NEXT_TILE_ROW), attributes },
                                   false);
    }
    pen->x = (uint8_t)(pen->x + width + 1);
}

/* The sprites a caption's letters take, and where its text ends; false if the
 * text doesn't end within `most` bytes. */
static bool measure_caption(const GameState *g, uint16_t text, unsigned most, unsigned *sprites)
{
    *sprites = 0;
    for (unsigned read = 0; read < most;) {
        uint8_t code = text_byte(g, (uint16_t)(text + read++));
        if (code == TEXT_END)
            return true;
        if (code == TEXT_PLACE)
            read += 2;
        else if (code != TEXT_NEW_LINE && code != TEXT_SPACE)
            *sprites += is_wide(letter_width(g, code)) ? 1 : 2;
    }
    return false;
}

// $7E:64CA
/* A caption drawn from its text: returns where the text after it starts. */
static uint16_t draw_caption(GameState *g, uint16_t text)
{
    Pen pen = { scene_caption_line_x(g), scene_caption_line_y(g) };
    for (uint8_t code; (code = text_byte(g, text++)) != TEXT_END;) {
        switch (code) {
        case TEXT_PLACE:
            pen.x = text_byte(g, text++);
            pen.y = text_byte(g, text++);
            set_scene_caption_line_x(g, pen.x);
            set_scene_caption_line_y(g, pen.y);
            break;
        case TEXT_NEW_LINE:
            pen.x = scene_caption_line_x(g);
            pen.y = (uint8_t)(pen.y + LINE_HEIGHT);
            break;
        case TEXT_SPACE:
            pen.x = (uint8_t)(pen.x + SPACE_WIDTH);
            break;
        default:
            draw_letter(g, &pen, code);
        }
    }
    scene_move_sprite_offset(g, pen.x, pen.y);
    return text;
}

/* ---- The captions' turns ---------------------------------------------------------------------- */

/* The captions end where the staff roll's text starts. */
enum { CAPTIONS_END = 0xAB1A };

/* The letters' colors fade in and out in the first palette animation slot:
 * the four from color 240, a step of 8 every frame. */
enum { CAPTION_FADE_SLOT = 0, FADE_OUT_FRAMES = 32 };
static const PaletteFade caption_fades_in = { .first_color = 240, .color_count = 4, .step = 8, .period = 1, .from_level = 0, .to_level = 0xFF };
static const PaletteFade caption_fades_out = { .first_color = 240, .color_count = 4, .step = 8, .period = 1, .from_level = 0xFF, .to_level = 0 };

/* The screen is up once the scene's brightness has come all the way in. */
enum { FULL_BRIGHTNESS = 0x0F };
static bool screen_up(const GameState *g) { return (scene_brightness(g) & FULL_BRIGHTNESS) == FULL_BRIGHTNESS; }

static bool caption_is_up(const GameState *g) { return scene_ending_caption_phase(g) & 1; }

static void next_phase(GameState *g, uint16_t frames)
{
    set_scene_ending_caption_phase(g, (uint16_t)(scene_ending_caption_phase(g) + 1));
    set_scene_ending_caption_countdown(g, frames);
}

typedef enum {
    CAPTIONS_ALL_SHOWN,     /* none left to show */
    CAPTION_WAITS,          /* drawn, waiting for the screen to come up */
    CAPTION_STAYS,          /* drawn, counting down */
    CAPTION_FADES_OUT,      /* drawn, and its last frames start */
    CAPTION_GOES,           /* drawn for the last time; the gap starts */
    CAPTIONS_OVER,          /* the last caption has gone: the task ends */
    GAP_GOES_ON,            /* between captions */
    CAPTION_FADES_IN,       /* the gap is over: the next caption comes */
} CaptionTurn;

// $7E:9B4F
/* A frame of the captions. */
static CaptionTurn turn_captions(GameState *g)
{
    if (!scene_ending_captions_left(g))
        return CAPTIONS_ALL_SHOWN;
    uint16_t left = scene_ending_caption_countdown(g);
    if (!caption_is_up(g)) {
        set_scene_ending_caption_countdown(g, --left);
        if (left)
            return GAP_GOES_ON;
        next_phase(g, scene_ending_caption_shown(g));
        palette_start_fade_to_black(g, CAPTION_FADE_SLOT, caption_fades_in);
        return CAPTION_FADES_IN;
    }

    uint16_t after = draw_caption(g, scene_ending_caption_text(g));
    if (!screen_up(g))
        return CAPTION_WAITS;
    set_scene_ending_caption_countdown(g, --left);
    if (left == FADE_OUT_FRAMES) {
        palette_start_fade_to_black(g, CAPTION_FADE_SLOT, caption_fades_out);
        return CAPTION_FADES_OUT;
    }
    if (left)
        return CAPTION_STAYS;
    set_scene_ending_captions_left(g, (uint16_t)(scene_ending_captions_left(g) - 1));
    next_phase(g, scene_ending_caption_gap(g));
    if (after >= CAPTIONS_END)
        return CAPTIONS_OVER;
    set_scene_ending_caption_text(g, after);
    return CAPTION_GOES;
}

/* ---- Entries ---------------------------------------------------------------------------------- */

/* The drawing's code, but for the operands the program writes (the line's
 * place and the letters' attributes). */
static bool drawing_present(Snes *s)
{
    return m_code_matches(s, 0x7E64C5, 0x11, 0xD914FAA270D2DEECull) && m_code_matches(s, 0x7E64D7, 0x03, 0x0303201AC7C42ED2ull) &&
           m_code_matches(s, 0x7E64DB, 0x4A, 0x2A8890C378457217ull) && m_code_matches(s, 0x7E6526, 0x72, 0x8A6337B453CD5D8Cull);
}

/* The longest caption the guards follow to its end. */
enum { LONGEST_CAPTION = 0x200 };

/* A caption the port draws: its text ends inside the program's bank, and its
 * letters fit the sprite table from the cursor. */
static bool caption_fits(const GameState *g, uint16_t text)
{
    unsigned sprites = 0;
    return (uint32_t)text + LONGEST_CAPTION <= 0x10000 && measure_caption(g, text, LONGEST_CAPTION, &sprites) &&
           scene_sprites_fit(g, sprites);
}

bool scene_draw_caption_present(Snes *s)
{
    if (!scene_on_scene_page(s) || !drawing_present(s))
        return false;
    vars_peek(true);
    bool fits = caption_fits(state_of(s), s->cpu.x);
    vars_peek(false);
    return fits;
}

/* Entered with X at the text; back, X is past it, Y the next free sprite
 * entry and A the end's code, as the original leaves them, and the data bank
 * 0: the original reads the text through the program's bank, pushed as a word
 * whose other byte it then takes as the bank it goes back with. */
void scene_draw_caption(Snes *s)
{
    GameState *g = state_of(s);
    s->cpu.x = draw_caption(g, s->cpu.x);
    s->cpu.db = 0x00;
    ORIGINAL_EFFECT(s->cpu.y = scene_sprite_index(g));
    ORIGINAL_EFFECT(s->cpu.a = TEXT_END);
    m_rts(s);
}

/* The task's code and the drawing's, a data bank on low RAM, the scene's
 * page; where a caption is up, it is one the port draws, and its text is in
 * the program, past the start of the bank, so the captions' end is a plain
 * compare. */
enum { PROGRAM_START = 0x3000 };

bool scene_turn_captions_present(Snes *s)
{
    if (!scene_on_scene_page(s) || !m_bank_reaches_low_ram(s) || !drawing_present(s) ||
        !m_code_matches(s, 0x7E9B4F, 0x69, 0x52724AD781AB6989ull))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint16_t text = scene_ending_caption_text(g);
    bool ok = !scene_ending_captions_left(g) || !caption_is_up(g) || (text >= PROGRAM_START && caption_fits(g, text));
    vars_peek(false);
    return ok;
}

/* What the task leaves in A: the last word the original loaded or worked out
 * on its way, whose high byte the frame's work may read after the last task;
 * with a caption drawn, Y is the next free sprite entry. */
enum { FADE_KIND_AS_STORED = 0x0050 };

static void leave_turn(Snes *s, CaptionTurn turn, uint16_t phase_before)
{
    const GameState *g = state_of(s);
    switch (turn) {
    case CAPTIONS_ALL_SHOWN: s->cpu.a = 0; return;
    case GAP_GOES_ON: s->cpu.a = (uint16_t)(phase_before >> 1); return;
    case CAPTION_FADES_IN: s->cpu.a = FADE_KIND_AS_STORED; return;
    case CAPTION_WAITS: s->cpu.a = scene_brightness(g) & FULL_BRIGHTNESS; break;
    case CAPTION_STAYS: s->cpu.a = scene_ending_caption_countdown(g); break;
    case CAPTION_FADES_OUT: s->cpu.a = FADE_KIND_AS_STORED; break;
    case CAPTION_GOES:
    case CAPTIONS_OVER: s->cpu.a = scene_ending_caption_gap(g); break;
    }
    s->cpu.y = scene_sprite_index(g);
}

/* The list's exit for a task that takes itself off. */
enum { REMOVE_THIS_TASK = 0x7E39A7 };

/* A frame task, entered with 16-bit A. */
void scene_turn_captions(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t phase_before = scene_ending_caption_phase(g);
    CaptionTurn turn = turn_captions(g);
    if (turn != CAPTIONS_ALL_SHOWN && turn != GAP_GOES_ON && turn != CAPTION_FADES_IN)
        s->cpu.db = 0x00;   /* as the caption's drawing goes back */
    ORIGINAL_EFFECT(leave_turn(s, turn, phase_before));
    if (turn == CAPTIONS_OVER) {
        m_stop_at(s, REMOVE_THIS_TASK, DC_X | DC_N | DC_V | DC_Z | DC_C);
        return;
    }
    m_rts(s);
}
