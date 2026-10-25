/* The description window: the line of text at the foot of a screen that
 * says what its cursor is on -- an item, a tech, a tech kind's caption, a
 * setting -- typed out a few glyphs a frame.
 *
 * A screen asks for the description of what its cursor points at
 * (menu_describe_item, menu_describe): only something other than what the
 * window shows is typed, so a cursor that stays put doesn't start it over.
 * Then the window's row on the text layer is cleared and the menus' third
 * task types the text: each frame the typesetter sets the frame's glyphs
 * into the line's tiles (game/menu/typeset.c); the tiles go to VRAM, and
 * are laid on the row as far as the pen has got; and the row goes to VRAM
 * too. A text can break onto a second line, two tile rows down, its glyphs
 * in tiles further on. The typing ends with the text, and the task with it.
 *
 * A line's glyphs are set in a strip of tiles: across the line sixteen tiles
 * of its top half, then in VRAM their bottom halves, then the next sixteen
 * and theirs, so the row lays tile t's top half and below it t + 16's.
 *
 * The settings screen's description is an option's text and then, a tile
 * after it, its current value, which is typed again alone as the player
 * changes the setting (menu_describe_setting); the value's line is kept from
 * the option's typing to the value's, as the original keeps it in its first
 * cells. */
#include "menu/description.h"

#include "charge.h"
#include "core/program.h"
#include "menu/bridge.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/typeset.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

/* Where the typing's task starts: a description, the settings' option and
 * then its value, and its value alone. */
enum { TYPE_A_DESCRIPTION = MENU_TYPE_A_DESCRIPTION, TYPE_A_SETTING = MENU_TYPE_A_SETTING, TYPE_A_VALUE = MENU_TYPE_A_VALUE };

/* The text layer's tilemap: two bytes a tile, 32 tiles a row. A line of
 * text is two tile rows, its glyphs' top halves and their bottom halves. */
enum {
    TILE_BYTES = 2, TILE_ROW_BYTES = 0x40, ROW_START = 0xFFC0, LINE_BYTES = 2 * TILE_ROW_BYTES,
    TEXT_LAYER_AT = (uint16_t)RAM_MENU_TEXT_TILEMAP,
};

/* A strip of a line's tiles: sixteen top halves, then their bottom halves. */
enum { STRIP = 0x10 };

/* Pixels a tile; a line's tiles are laid from a tile in. */
enum { TILE_PIXELS = 8, A_TILE_IN = TILE_PIXELS };

/* Where a line's glyphs are set: the tiles in work RAM, where they go in
 * VRAM, and the tilemap word of its first; the next line's tiles are $40
 * on, $200 VRAM words further. */
typedef struct {
    uint16_t first_tile, vram, at;
} LineTiles;
static const LineTiles FIRST_LINE = { 0x3101, 0x7800, 0x5E00 };
static const LineTiles VALUE_LINE = { 0x3941, 0x7A00, 0x6200 };
enum { NEXT_LINE_FIRST_TILE = 0x41, FIRST_TILE_BANK = 0xC0, NEXT_LINE_VRAM = 0x200, LINE_TILE_BYTES = 0x400 };
enum { TEXT_BANK = 0x7E, WORK_RAM = 0x7E0000 };

/* How fast the items' and the settings' descriptions type. */
enum { A_GLYPH_A_FRAME = 1 };

/* The items' description's row, and the settings'. */
enum { ITEM_LINE = 0x4E84, ITEM_ROW_VRAM = 0x6840, SETTING_LINE = 0x4E0A, SETTING_ROW_VRAM = 0x6900 };

/* What a screen marks stale is typed again whatever it shows. */
enum { STALE = 0x80 };

/* ---- The row ------------------------------------------------------------------------------------- */

static void lay_tile(GameState *g, uint16_t at, uint16_t word) { set_menu_text_tilemap(g, (uint16_t)(at - TEXT_LAYER_AT) / TILE_BYTES, word); }

/* The row the description's line is on, from its first tile row. */
static uint16_t row_start(const GameState *g) { return (uint16_t)(menu_description_line(g) & ROW_START); }

static void clear_the_row(GameState *g)
{
    uint16_t at = row_start(g), bytes = menu_description_row_bytes(g);
    for (uint16_t i = 0; i < bytes; i += TILE_BYTES)
        lay_tile(g, (uint16_t)(at + i), 0);
    ORIGINAL_EFFECT(charge_block_move(g, (uint16_t)(bytes - TILE_BYTES)));
}

/* The text's tiles, in work RAM's bank as the screen set it. */
static void set_the_text_in(GameState *g, uint16_t at) { set_menu_text_tiles(g, (menu_text_tiles(g) & 0xFF0000u) | at); }

/* For the original's code and shadow mode: it clears the counts beside the
 * glyphs sent and the line's laying, writing each as a word. */
static void leave_counts_cleared_as_words(GameState *g, bool glyphs_too)
{
    set_menu_description_laid_to_pad(g, 0);
    if (glyphs_too)
        set_menu_description_glyphs_sent_pad(g, 0);
}

static void begin_line(GameState *g, LineTiles t)
{
    set_menu_graphic_first_tile(g, t.first_tile);
    set_menu_description_laid_to(g, A_TILE_IN);
    set_menu_description_tiles_vram(g, t.vram);
    set_menu_description_tiles_at(g, t.at);
    set_the_text_in(g, t.at);
}

/* The text breaks onto the next line: two tile rows down, its tiles the
 * next $40 on and in VRAM after the first's. The original moves only the
 * low bytes of where the line starts and of its first tile. */
static void begin_the_next_line(GameState *g)
{
    set_menu_description_line_from(g, (uint8_t)((menu_description_line_from(g) & ~(LINE_BYTES / 4 - 1)) + LINE_BYTES));
    uint16_t first = menu_graphic_first_tile(g);
    set_menu_graphic_first_tile(g, (uint16_t)((first & 0xFF00) | (uint8_t)((first & FIRST_TILE_BANK) + NEXT_LINE_FIRST_TILE)));
    set_menu_description_glyphs_sent(g, 0);
    set_menu_description_tiles_vram(g, (uint16_t)(menu_description_tiles_vram(g) + NEXT_LINE_VRAM));
    set_menu_description_laid_to(g, A_TILE_IN);
}

/* ---- Sending a frame's typing -------------------------------------------------------------------- */

/* The original builds each upload's record in work RAM for the queue,
 * which copies a byte past it as it stands (menu_built_upload), and nothing
 * reads. */
static void send(GameState *g, MenuUpload u)
{
    ORIGINAL_EFFECT(u.spare = menu_built_upload_spare(g));
    TIMED_AS(g, 0xC2838E, menu_queue_upload(g, u));
}

/* The line's tiles: all of them, the glyphs set so far among them. */
static MenuUpload line_tiles(const GameState *g)
{
    return (MenuUpload){ menu_description_tiles_vram(g), WORK_RAM | menu_description_tiles_at(g), LINE_TILE_BYTES, 0 };
}

static MenuUpload the_row(const GameState *g)
{
    return (MenuUpload){ menu_description_row_vram(g), WORK_RAM | row_start(g), menu_description_row_bytes(g), 0 };
}

/* For the original's code and shadow mode: how far it counted the line's
 * tiles in its first cells. */
static void leave_tiles_counted(GameState *g, uint8_t reached, uint8_t laid)
{
    set_menu_scratch_typed_tiles(g, reached);
    set_menu_scratch_laid_tiles(g, laid);
}

/* The tiles the pen has reached into since they were last laid, laid on
 * the row: each tile's top half and below it its bottom half, along the
 * strips. */
static void lay_the_new_tiles(GameState *g)
{
    uint8_t pen = menu_text_reading_pen(g);
    uint8_t reached = (uint8_t)((pen + TILE_PIXELS - 1) / TILE_PIXELS);
    uint8_t laid = (uint8_t)(menu_description_laid_to(g) / TILE_PIXELS);
    uint16_t at = (uint16_t)(menu_description_line(g) + menu_description_line_from(g) + laid * TILE_BYTES);
    uint16_t word = (uint16_t)(menu_graphic_first_tile(g) + laid - 1 + STRIP);
    for (unsigned t = laid; t < reached; t++) {
        uint16_t top = (uint16_t)(word & ~STRIP);
        lay_tile(g, at, top);
        lay_tile(g, (uint16_t)(at + TILE_ROW_BYTES), (uint16_t)(top | STRIP));
        word = (uint16_t)((top | STRIP) + 1);
        at = (uint16_t)(at + TILE_BYTES);
    }
    set_menu_description_laid_to(g, pen);
    ORIGINAL_EFFECT(leave_tiles_counted(g, reached, laid));
}

// $C2:FAE9
/* The line's tiles and their laying only where it has new glyphs; the row
 * every frame. */
void menu_send_the_typing(GameState *g)
{
    uint8_t glyphs = menu_text_line_glyphs(g);
    if (glyphs != menu_description_glyphs_sent(g)) {
        set_menu_description_glyphs_sent(g, glyphs);
        send(g, line_tiles(g));
        lay_the_new_tiles(g);
    }
    send(g, the_row(g));
    ORIGINAL_EFFECT(menu_leave_built_upload(g, the_row(g)));
}

/* A frame of typing: as many glyphs as the pace allows set and sent, a
 * break onto the next line taken. Whether the text goes on. */
static bool type_a_frame(GameState *g)
{
    set_menu_text_budget(g, menu_description_pace(g));
    TIMED_AS(g, 0xC25823, menu_text_go_on(g));
    TIMED_AS(g, 0xC2FAE9, menu_send_the_typing(g));
    MenuTextStop stop = (MenuTextStop)menu_text_stop(g);
    if (stop == MENU_TEXT_ENDED)
        return false;
    if (stop == MENU_TEXT_NEW_LINE)
        begin_the_next_line(g);
    return true;
}

/* ---- Asking for a description -------------------------------------------------------------------- */

bool menu_description_shows_another(const GameState *g, uint8_t which)
{
    return (menu_description_stale(g) & STALE) || which != menu_description_item(g);
}

// $C2:FA49
/* The row cleared, the first line begun, and the typing's task started;
 * the typesetter starts on the text at once. */
void menu_start_typing(GameState *g)
{
    set_menu_text_layout(g, MENU_TEXT_FOUR_COLOUR);
    set_menu_text_tiles(g, (uint32_t)TEXT_BANK << 16);
    set_menu_description_glyphs_sent(g, 0);
    set_menu_description_line_from(g, 0);
    clear_the_row(g);
    begin_line(g, FIRST_LINE);
    ORIGINAL_EFFECT(leave_counts_cleared_as_words(g, false));
    menu_start_task(g, MENU_DESCRIPTION_TASK, TYPE_A_DESCRIPTION);
    TIMED_AS(g, 0xC257DF, menu_text_start(g));
}

void menu_describe(GameState *g, uint8_t which, uint32_t texts, uint16_t row_vram)
{
    set_menu_description_item(g, which);
    set_menu_description_stale(g, 0);
    set_menu_text_string(g, which);
    set_menu_description_row_vram(g, row_vram);
    set_menu_text_strings(g, texts);
    set_menu_description_row_bytes(g, LINE_BYTES);
    TIMED_AS(g, 0xC2FA49, menu_start_typing(g));
}

// $C2:F2F3
void menu_describe_item(GameState *g, uint8_t item)
{
    if (!menu_description_shows_another(g, item))
        return;
    set_menu_description_line(g, ITEM_LINE);
    set_menu_description_pace(g, A_GLYPH_A_FRAME);
    menu_describe(g, item, MENU_ITEM_DESCRIPTIONS, ITEM_ROW_VRAM);
}

// $C2:F943
/* Another option: the row cleared and the option typed, then its value. The
 * same option, once the typing is done: the value's place cleared on both
 * of its tile rows from where it starts to the line's end (at least a
 * tile), and the value typed there, where it has changed. */
void menu_describe_setting(GameState *g, uint8_t option, uint8_t value_text)
{
    set_menu_text_layout(g, MENU_TEXT_FOUR_COLOUR);
    set_menu_text_tiles(g, (menu_text_tiles(g) & 0xFFFFu) | (uint32_t)TEXT_BANK << 16);
    set_menu_text_strings(g, MENU_SETTINGS_DESCRIPTIONS);
    set_menu_description_pace(g, A_GLYPH_A_FRAME);
    set_menu_description_row_vram(g, SETTING_ROW_VRAM);
    if (option != menu_description_item(g)) {
        set_menu_description_item(g, option);
        set_menu_text_string(g, option);
        for (uint16_t at = 0; at < LINE_BYTES; at += TILE_BYTES)
            lay_tile(g, (uint16_t)(TEXT_LAYER_AT + at), 0);
        ORIGINAL_EFFECT(charge_block_move(g, LINE_BYTES - TILE_BYTES));
        menu_start_task(g, MENU_DESCRIPTION_TASK, TYPE_A_SETTING);
    } else if (menu_tasks_state(g, MENU_DESCRIPTION_TASK) == 0 && value_text != menu_description_stale(g)) {
        uint16_t from = menu_description_line_from(g);
        do {
            lay_tile(g, (uint16_t)(SETTING_LINE + from), 0);
            lay_tile(g, (uint16_t)(SETTING_LINE + TILE_ROW_BYTES + from), 0);
            from = (uint16_t)(from + TILE_BYTES);
        } while (from < TILE_ROW_BYTES);
        menu_start_task(g, MENU_DESCRIPTION_TASK, TYPE_A_VALUE);
    }
}

/* The settings' option typed, its line's tiles laid as far as its text: the
 * value goes a tile on, at the option's line, the original keeps both. */
static void note_where_the_value_goes(GameState *g)
{
    uint8_t laid = (uint8_t)(menu_description_laid_to(g) / TILE_PIXELS);
    set_menu_description_line_from(g, (uint8_t)(laid * TILE_BYTES + TILE_BYTES));
    set_menu_description_value_line(g, menu_description_line(g));
    set_menu_description_value_tile(g, (uint8_t)((menu_graphic_first_tile(g) - 1) & (STRIP - 1)));
}

static void begin_the_setting(GameState *g)
{
    set_menu_description_row_bytes(g, LINE_BYTES);
    set_menu_description_line(g, SETTING_LINE);
    set_menu_description_glyphs_sent(g, 0);
    set_menu_description_line_from(g, 0);
    begin_line(g, FIRST_LINE);
    ORIGINAL_EFFECT(leave_counts_cleared_as_words(g, true));
    TIMED_AS(g, 0xC257DF, menu_text_start(g));
}

static void begin_the_value(GameState *g)
{
    uint8_t value_text = menu_settings_description(g);
    set_menu_text_string(g, value_text);
    set_menu_description_stale(g, value_text);
    set_menu_description_glyphs_sent(g, 0);
    set_menu_description_line(g, (uint16_t)(menu_description_value_line(g) + menu_description_value_tile(g) * TILE_BYTES));
    begin_line(g, VALUE_LINE);
    ORIGINAL_EFFECT(leave_counts_cleared_as_words(g, true));
    TIMED_AS(g, 0xC257DF, menu_text_start(g));
}

/* ---- The typing's tasks ------------------------------------------------------------------------- */

/* The task's wait for the next frame between the typing's frames: as C, or
 * on the machine (below); false where the task never came back. */
typedef bool (*TypingWait)(void *ctx);

// $C2:FAA8
/* The text typed to its end, a frame at a time. */
static bool type_the_text(GameState *g, TypingWait wait, void *ctx)
{
    while (type_a_frame(g))
        if (!wait(ctx))
            return false;
    return true;
}

/* The typing's three tasks, each from its own start: a description typed; a
 * setting's option typed, then its value after it; and a setting's value
 * typed again where it has changed. Each typing is the text typed to its end
 * (above), as C or on the machine, from the original's call of it at `site`;
 * false where it went no further. The task ends after. */
typedef enum { TYPE_THE_DESCRIPTION, TYPE_THE_SETTING, TYPE_THE_VALUE } TypingTask;
typedef bool (*Typing)(void *ctx, uint32_t site);

// $C2:FA9F, $C2:F9B2, $C2:FA06
static bool typing_task_with(GameState *g, TypingTask task, Typing type, void *ctx)
{
    switch (task) {
    case TYPE_THE_DESCRIPTION: return type(ctx, 0xC2FA9F);
    case TYPE_THE_SETTING:
        begin_the_setting(g);
        if (!type(ctx, 0xC2F9E8))
            return false;
        note_where_the_value_goes(g);
        break;
    case TYPE_THE_VALUE: break;
    }
    begin_the_value(g);
    return type(ctx, 0xC2FA43);
}

/* As C, from inside the task's coroutine (menu/tasks.c), timed as the typing's
 * routine. */
static bool wait(void *ctx)
{
    menu_task_waits(ctx, 1);
    return true;
}

/* The guards' state halves, below: the typing laid where it fits, and each
 * task's start. */
static bool typing_goes_on(const GameState *g);
static bool typing_task_can_start(const GameState *g, TypingTask task);

/* Each typing and each task where its guard's state half takes it; otherwise
 * the program goes no further there. */
static bool type(void *ctx, uint32_t site)
{
    (void)site;
    GameState *g = ctx;
    if (!typing_goes_on(g)) {
        program_needs_the_machine(g, 0xC2FAA8, "a description the port doesn't type");
        return false;
    }
    bool typed = false;
    TIMED_AS(g, 0xC2FAA8, typed = type_the_text(g, wait, g));
    return typed;
}

static void typing_task(GameState *g, TypingTask task)
{
    static const uint32_t starts[] = { 0xC2FA9F, 0xC2F9B2, 0xC2FA06 };
    if (!typing_task_can_start(g, task)) {
        program_needs_the_machine(g, starts[task], "a typing task the port doesn't start");
        return;
    }
    (void)typing_task_with(g, task, type, g);
}

void menu_type_the_description_task(GameState *g, Platform *pf)
{
    (void)pf;
    typing_task(g, TYPE_THE_DESCRIPTION);
}

void menu_type_the_setting_task(GameState *g, Platform *pf)
{
    (void)pf;
    typing_task(g, TYPE_THE_SETTING);
}

void menu_type_the_value_task(GameState *g, Platform *pf)
{
    (void)pf;
    typing_task(g, TYPE_THE_VALUE);
}

/* ---- The entries --------------------------------------------------------------------------------- */

/* The end of a task, which the scheduler goes on from, and which reads none
 * of the registers. */
enum { END_OF_A_TASK = 0xC28216 };
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

// $C2:F2F3
void menu_describe_item_entry(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t item = (uint8_t)s->cpu.a;
    bool another = menu_description_shows_another(g, item);
    menu_describe_item(g, item);
    if (another)
        ORIGINAL_EFFECT(menu_leave_typing_started(s));
    m_rts(s);
}

void menu_leave_typing_started(Snes *s) { s->cpu.a = 0; }

// $C2:FA49
void menu_start_typing_entry(Snes *s)
{
    menu_start_typing(state_of(s));
    ORIGINAL_EFFECT(menu_leave_typing_started(s));
    m_rts(s);
}

// $C2:F943
void menu_describe_setting_entry(Snes *s)
{
    GameState *g = state_of(s);
    menu_describe_setting(g, menu_settings_described_option(g), menu_settings_description(g));
    m_rts(s);
}

// $C2:FAE9
void menu_send_the_typing_entry(Snes *s)
{
    menu_send_the_typing(state_of(s));
    m_rts(s);
}

static bool wait_on_the_machine(void *ctx) { return menu_wait_a_frame_from(ctx, 0xC2FAA5); }

// $C2:FAA8
/* It leaves an 8-bit accumulator. */
void menu_type_the_description(Snes *s)
{
    menu_set_widths(s, true, false);
    if (type_the_text(state_of(s), wait_on_the_machine, s))
        m_rts(s);
}

/* On the machine each typing is a sibling run as the original's JSR reaches
 * it, with 16-bit registers, and the task's end is the scheduler's. */
static bool type_on_the_machine(void *ctx, uint32_t site)
{
    Snes *s = ctx;
    menu_set_widths(s, false, false);
    return menu_run_any_widths(s, site, 0xC2FAA8, menu_type_the_description);
}

static void typing_task_on_the_machine(Snes *s, TypingTask task)
{
    if (typing_task_with(state_of(s), task, type_on_the_machine, s))
        m_stop_at(s, END_OF_A_TASK, READS_NOTHING);
}

void menu_type_the_description_task_entry(Snes *s) { typing_task_on_the_machine(s, TYPE_THE_DESCRIPTION); }
void menu_type_the_setting_task_entry(Snes *s) { typing_task_on_the_machine(s, TYPE_THE_SETTING); }
void menu_type_the_value_task_entry(Snes *s) { typing_task_on_the_machine(s, TYPE_THE_VALUE); }

/* ---- The guards ---------------------------------------------------------------------------------- */

/* The description's row lies in the text layer's tilemap, its line on it,
 * a whole number of tiles; the typing's counts start from a line's (the
 * bytes beside them the setups clear). */
static bool row_fits(const GameState *g)
{
    uint16_t at = row_start(g), bytes = menu_description_row_bytes(g), line = menu_description_line(g);
    return bytes >= 2 * TILE_BYTES && bytes % TILE_BYTES == 0 && at >= TEXT_LAYER_AT
           && at + bytes <= TEXT_LAYER_AT + MENU_TEXT_TILEMAP_COUNT * TILE_BYTES && line % TILE_BYTES == 0;
}

static bool laying_fits(const GameState *g)
{
    uint16_t line = menu_description_line(g), from = menu_description_line_from(g);
    return row_fits(g) && from < 0x100 && from % TILE_BYTES == 0
           && line + from + TILE_ROW_BYTES + 2 * TILE_ROW_BYTES <= TEXT_LAYER_AT + MENU_TEXT_TILEMAP_COUNT * TILE_BYTES;
}

static bool guarded(Snes *s, bool (*ok)(const GameState *g))
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool fine = ok(state_of(s));
    vars_peek(false);
    return fine;
}

static bool item_row_fits(const GameState *g)
{
    (void)g;
    return true;
}

bool menu_item_is_describable(Snes *s) { return guarded(s, item_row_fits) && menu_text_is_startable(s); }

bool menu_typing_can_start(Snes *s) { return guarded(s, row_fits) && menu_text_is_startable(s); }

/* The value's line is cleared from where the option's text ended, a whole
 * number of tiles along it. */
static bool setting_fits(const GameState *g) { return menu_description_line_from(g) % TILE_BYTES == 0; }

bool menu_setting_is_describable(Snes *s) { return guarded(s, setting_fits); }

/* Laying the tiles: the pen reached past the tiles laid wherever the line
 * has new glyphs (the typesetter moves it on with each). */
static bool sending_fits(const GameState *g)
{
    if (!laying_fits(g) || menu_text_line_glyphs(g) == menu_description_glyphs_sent(g))
        return laying_fits(g);
    return (menu_text_reading_pen(g) + TILE_PIXELS - 1) / TILE_PIXELS > menu_description_laid_to(g) / TILE_PIXELS;
}

bool menu_typing_can_be_sent(Snes *s) { return guarded(s, sending_fits); }

static bool typing_goes_on(const GameState *g) { return laying_fits(g) && menu_text_can_carry_on(g); }

bool menu_typing_can_go_on(Snes *s) { return guarded(s, typing_goes_on); }

static bool value_fits(const GameState *g)
{
    return menu_description_line_from(g) < 0x100 && menu_description_line_from(g) % TILE_BYTES == 0
           && menu_description_line(g) % TILE_BYTES == 0;
}

static bool typing_task_can_start(const GameState *g, TypingTask task)
{
    switch (task) {
    case TYPE_THE_DESCRIPTION: return typing_goes_on(g);
    case TYPE_THE_SETTING: return menu_text_can_start(g);
    case TYPE_THE_VALUE: return value_fits(g) && menu_text_can_start(g);
    }
    return false;
}

static bool setting_typing_starts(const GameState *g) { return typing_task_can_start(g, TYPE_THE_SETTING); }
static bool value_typing_starts(const GameState *g) { return typing_task_can_start(g, TYPE_THE_VALUE); }

bool menu_setting_typing_can_start(Snes *s) { return guarded(s, setting_typing_starts); }

bool menu_value_typing_can_start(Snes *s) { return guarded(s, value_typing_starts); }
