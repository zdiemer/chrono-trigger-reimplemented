/* The party exchange: menu screen 5 from the main menu, and screen 7 at the
 * End of Time's gate.
 *
 * The screen lists the party in rows, each a member's status window with the
 * member's sprite standing in it: the active party's three down the left, and
 * the reserve's down the right, four rows at a time, scrolling a row with a
 * six-frame slide as the cursor pushes past the first or the last shown. The
 * player chooses a member, then another, and the two change places in the
 * party (menu/party.h): two of the active party change order, or a member of
 * the reserve takes an active member's place. A member the story keeps in the
 * active party (story_party_kept) can't be chosen and stands dimmed. While
 * the cursor is on a member that member walks, and the member chosen first
 * holds a pose with a second cursor kept on it (menu/held_cursors.h).
 *
 * From the main menu the screen reorders the active party alone, drawn in the
 * middle (menu_exchange_layout, which the menus' opening leaves set); the
 * gate's screen lays it out as its event asks, the whole party unless it
 * says otherwise, over the gate's swirl (menu/gate_screen.h) and with a song
 * of its own (menu/sound.c). Its panel -- the rows, drawn under the main
 * menu while its cursor is on the option, or under the gate's label -- is the
 * same rows in the same layout.
 *
 * The screen's state is the rows (menu_exchange_rows): each the place in the
 * party's record of the member it shows, marked where that member stays or
 * where the row is empty, the active party's three and then the reserve's
 * six; how many members the player may move in each; the row under the
 * cursor; and the two chosen. The cursor's places are the active party's
 * three rows and the reserve's four shown; the reserve's top row
 * (menu_reserve_top) says which of its rows those four are.
 *
 * The bridges on the original's machine are menu/exchange_bridge.c. */
#include "menu/exchange.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/equipment.h"
#include "menu/held_cursors.h"
#include "menu/main_menu.h"
#include "menu/members.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/opening.h"
#include "menu/party.h"
#include "menu/sprites.h"
#include "menu/tiles.h"
#include "menu/typeset.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "vars.h"

/* ---- The rows ---------------------------------------------------------------------------------- */

/* A row holds a place of the party's record, marked where its member stays
 * in the party, or no one (exchange.h). */
enum { NO_ONE = EXCHANGE_ROW_EMPTY, STAYS = EXCHANGE_ROW_STAYS, CANT_BE_CHOSEN = NO_ONE | STAYS };

static uint8_t row(const GameState *g, unsigned r) { return menu_exchange_rows(g, r); }
static bool row_is_empty(uint8_t r) { return (r & NO_ONE) != 0; }
static bool row_is_free(uint8_t r) { return !(r & CANT_BE_CHOSEN); }
static RecordPlace place_in_the_party(uint8_t r) { return (RecordPlace)(r & ~STAYS); }

/* The cursor's places: the active party's rows, then the four of the
 * reserve's that show, from its top row on. */
enum { LAST_RESERVE_PLACE = EXCHANGE_FIRST_RESERVE_PLACE + EXCHANGE_RESERVE_SHOWN - 1 };

static bool on_the_reserve(uint8_t place) { return place >= EXCHANGE_FIRST_RESERVE_PLACE; }

static unsigned row_at(const GameState *g, uint8_t place)
{
    return on_the_reserve(place) ? (uint8_t)(place + menu_reserve_top(g)) : place;
}

/* Nobody chosen; and the row under the cursor where nobody may be chosen at
 * all. A chosen member is its row, which never has the top bit set. */
enum { NOBODY_CHOSEN = 0xFF, NOBODY_FREE = STAYS };

static bool someone_chosen(uint8_t chosen) { return !(chosen & NO_ONE); }

/* The layout: both columns, or the active party alone in the middle; and,
 * for the panel, the main menu's window or the gate's. */
enum { BOTH_COLUMNS = 0, MAIN_MENU_PANEL = 0x02 };

static bool active_party_alone(const GameState *g) { return menu_exchange_layout(g) != BOTH_COLUMNS; }

/* The story keeps some characters in the active party, a bit a character. */
static bool stays(const GameState *g, uint8_t character) { return (story_party_kept(g) & menu_character_bit(g, character)) != 0; }

/* For the original's code and shadow mode: the rows are filled with a block
 * move of the first word over the rest, and its loop leaves its place in
 * the menus' first cell. */
enum { ROWS_FILLED = EXCHANGE_ROWS - 2 };

static void leave_listed(GameState *g, uint8_t place)
{
    charge_block_move(g, ROWS_FILLED);
    set_menu_scratch_exchange_row(g, place);
}

// $C2:CD2B
/* The rows listed from the party: its active members in order, each marked
 * where it stays (those aren't counted as free), then the reserve's, unless
 * the active party is shown alone (the reserve's count is then left as it
 * was). */
static void list_the_party(GameState *g)
{
    for (unsigned r = 0; r < EXCHANGE_ROWS; r++)
        set_menu_exchange_rows(g, r, NO_ONE);
    uint8_t listed = 0, free = 0;
    for (RecordPlace place = 0; place < PARTY_ACTIVE_PLACES; place++) {
        uint8_t who = party_at(g, place);
        if (party_place_is_empty(who))
            continue;
        bool kept = stays(g, who);
        set_menu_exchange_rows(g, listed++, (uint8_t)(place | (kept ? STAYS : 0)));
        if (!kept)
            free++;
    }
    set_menu_exchange_active_free(g, free);
    if (active_party_alone(g)) {
        ORIGINAL_EFFECT(leave_listed(g, PARTY_ACTIVE_PLACES));
        return;
    }
    listed = 0;
    for (RecordPlace place = PARTY_ACTIVE_PLACES; place < PARTY_RECORD_PLACES; place++)
        if (!party_place_is_empty(party_at(g, place)))
            set_menu_exchange_rows(g, EXCHANGE_ACTIVE_ROWS + listed++, place);
    set_menu_exchange_reserve_count(g, listed);
    ORIGINAL_EFFECT(leave_listed(g, 0));
}

/* ---- The members' rows and sprites ------------------------------------------------------------- */

/* Where a member's row and sprite go (menu_data.h's exchange places): the
 * active party's three, the reserve's four shown, the reserve's row above
 * and below them, and the active party's three in the middle. */
enum { ACTIVE_PLACES_AT = 0, RESERVE_PLACES_AT = 3, ROW_ABOVE = 7, ROW_BELOW = 8, MIDDLE_PLACES_AT = 9 };

static unsigned first_active_place(const GameState *g) { return active_party_alone(g) ? MIDDLE_PLACES_AT : ACTIVE_PLACES_AT; }

/* For the original's code and shadow mode: a place as an offset into the
 * places' tables, two bytes a place, as its loops leave it. */
static uint16_t place_offset(unsigned place) { return (uint16_t)(2 * place); }

/* A character's sprite stands at a place. */
static void stand_at(GameState *g, uint8_t character, unsigned place)
{
    MenuSpritePlace at = menu_exchange_sprite_place(g, place);
    ORIGINAL_EFFECT(menu_leave_record_found(g, character));
    set_menu_record_y(g, character, at.y);
    set_menu_record_x_low(g, character, at.x);
}

// $C2:CD9B
/* The member at `member` in the menus' list of the party drawn at a place:
 * its status window, and its sprite standing in it. */
static void show_member_at(GameState *g, Platform *pf, uint8_t member, unsigned place)
{
    bool loaded;
    TIMED_AS(g, 0xC28820, loaded = menu_load_member(g, member));
    if (!loaded)
        return;
    set_menu_draw_origin(g, menu_exchange_row_at(g, place));
    TIMED_AS(g, 0xC2A216, menu_draw_member_row(g, pf));
    uint8_t character = menu_status_record_character(g);
    stand_at(g, character, place);
    set_menu_record_sprite(g, character, character);
}

/* The member a row of the reserve shows, in the menus' list of the party:
 * the reserve's members follow the active party's there. */
static uint8_t member_of_reserve_row(const GameState *g, unsigned reserve_row)
{
    return (uint8_t)(menu_active_members(g) + reserve_row);
}

// $C2:CCDB
/* Every row drawn with its member: the reserve's four shown, up to its
 * first empty row, then the active party's. */
static void draw_the_rows(GameState *g, Platform *pf)
{
    uint8_t member = member_of_reserve_row(g, menu_reserve_top(g));
    unsigned place = RESERVE_PLACES_AT;
    for (unsigned r = 0; r < EXCHANGE_RESERVE_SHOWN && row_is_free(row(g, EXCHANGE_ACTIVE_ROWS + r)); r++, place++)
        show_member_at(g, pf, member++, place);
    place = first_active_place(g);
    member = 0;
    for (unsigned r = 0; r < EXCHANGE_ACTIVE_ROWS; r++, place++)
        if (!row_is_empty(row(g, r)))
            show_member_at(g, pf, member++, place);
    menu_work_on_member(g, member);
    ORIGINAL_EFFECT(set_menu_exchange_place_offset(g, place_offset(place)));
    ORIGINAL_EFFECT(set_menu_exchange_rows_counted(g, EXCHANGE_ACTIVE_ROWS));
}

// $C2:CE49
/* A row's member standing at a place, its sprite shown. */
static void stand_the_member(GameState *g, uint8_t r, unsigned place)
{
    uint8_t character = party_at(g, place_in_the_party(r));
    set_menu_record_sprite(g, character, character);
    stand_at(g, character, place);
}

/* For the original's code and shadow mode: where its loops left their row
 * and their place. */
static void leave_stood(GameState *g, unsigned r, unsigned place)
{
    set_menu_scratch_exchange_row(g, (uint8_t)r);
    set_menu_scratch_exchange_place_offset(g, place_offset(place));
}

// $C2:CDE3 (with $C2:CE2A)
/* Every member's sprite standing in its row: the records emptied, then the
 * active party's members at their places, and the reserve's four shown at
 * theirs (an empty row's place left empty). */
static void stand_the_members(GameState *g)
{
    menu_empty_the_character_records(g);
    unsigned place = first_active_place(g);
    for (unsigned r = 0; r < EXCHANGE_ACTIVE_ROWS; r++)
        if (!row_is_empty(row(g, r)))
            stand_the_member(g, row(g, r), place++);
    if (active_party_alone(g)) {
        ORIGINAL_EFFECT(leave_stood(g, EXCHANGE_ACTIVE_ROWS, place));
        return;
    }
    unsigned r = row_at(g, EXCHANGE_FIRST_RESERVE_PLACE);
    for (place = RESERVE_PLACES_AT; place < RESERVE_PLACES_AT + EXCHANGE_RESERVE_SHOWN; r++, place++)
        if (!row_is_empty(row(g, r)))
            stand_the_member(g, row(g, r), place);
    ORIGINAL_EFFECT(leave_stood(g, r, place));
}

/* What the members are doing: standing, walking (the one under the cursor)
 * or holding the pose of the one chosen first. */
enum { STANDING = 0, WALKING = 1, CHOSEN_POSE = 0x0B };

// $C2:C997
static void animate_the_member_at(GameState *g, RecordPlace place, uint8_t animation)
{
    uint8_t character = party_at(g, place);
    if (!party_place_is_empty(character))
        set_menu_record_animation(g, character, animation);
}

/* For the original's code and shadow mode: its loop over the party's
 * places counts down past the first. */
enum { COUNTED_PAST_THE_FIRST = 0xFF };

// $C2:C973
static void animate_the_members(GameState *g)
{
    for (RecordPlace place = 0; place < PARTY_RECORD_PLACES; place++)
        animate_the_member_at(g, place, STANDING);
    uint8_t on = menu_exchange_on_cursor(g);
    if (row_is_free(on))
        animate_the_member_at(g, on, WALKING);
    uint8_t first = menu_exchange_first(g);
    if (someone_chosen(first))
        animate_the_member_at(g, first, CHOSEN_POSE);
    ORIGINAL_EFFECT(set_menu_scratch_exchange_row(g, COUNTED_PAST_THE_FIRST));
}

/* The seven characters' records, each standing. */
static void stand_everyone_still(GameState *g)
{
    for (uint8_t character = 0; character < CHARACTER_RECORDS_COUNT; character++)
        set_menu_record_animation(g, character, STANDING);
}

/* ---- The dimming of who stays ------------------------------------------------------------------ */

/* An active row whose member stays, or that is empty, dimmed: its window
 * (six tile rows on both layers in the dimmed palette row) and the sprite
 * colours of the character at its place of the party. */
enum { ROW_TILE_ROWS = 6, DIMMED = 0x10, BACK_LAYER = 0x1000, WINDOWS_APART = ROW_TILE_ROWS * MENU_TILEMAP_ROW_BYTES };
typedef struct {
    uint16_t at;
    uint8_t tiles;
} DimmedWindows;
static const DimmedWindows ACTIVE_WINDOWS = { 0x2E84, 14 }, MIDDLE_WINDOWS = { 0x2E90, 16 };

/* An emptied place keeps the byte of whoever left it with its top bit set,
 * which the original takes for a character all the same: its colours' place
 * lies past the palette, in the field's tables the menus lay their own over
 * (docs/TODO.md). */
static void dim_the_colours_at(GameState *g, uint8_t character)
{
    if (party_place_is_empty(character))
        ORIGINAL_EFFECT(menu_leave_colours_dimmed_past_the_palette(g, character));
    else
        menu_dim_character_colours(g, character);
}

/* For the original's code and shadow mode: its loop's count, the row's
 * tiles and attribute, and the windows' place past the third. */
static void leave_dimmed(GameState *g, DimmedWindows windows)
{
    set_menu_scratch_exchange_rows_looked_at(g, EXCHANGE_ACTIVE_ROWS);
    set_menu_scratch_exchange_dimmed_tiles(g, (uint16_t)(windows.tiles | DIMMED << 8));
    set_menu_scratch_exchange_dimmed_at(g, (uint16_t)(windows.at + EXCHANGE_ACTIVE_ROWS * WINDOWS_APART));
}

// $C2:CC4F (with $C2:CCB3)
/* The original dims the sprite of the character at the row's own place of
 * the party (the third row's at the third place), which is that row's
 * member, the active party having no gaps. In the middle the cursor's
 * places move to the middle too. */
enum { MIDDLE_CURSOR_X = 0x38 };

static void dim_who_stays(GameState *g)
{
    DimmedWindows windows = active_party_alone(g) ? MIDDLE_WINDOWS : ACTIVE_WINDOWS;
    if (active_party_alone(g))
        for (unsigned place = 0; place < EXCHANGE_ACTIVE_ROWS; place++)
            set_menu_cursor_places_x(g, place, MIDDLE_CURSOR_X);
    uint16_t at = windows.at;
    for (unsigned r = 0; r < EXCHANGE_ACTIVE_ROWS; r++, at = (uint16_t)(at + WINDOWS_APART)) {
        if (row_is_free(row(g, r)))
            continue;
        dim_the_colours_at(g, party_at(g, (RecordPlace)r));
        TIMED_AS(g, 0xC2EC93, menu_recolour_block(g, at, ROW_TILE_ROWS, windows.tiles, DIMMED));
        TIMED_AS(g, 0xC2EC93, menu_recolour_block(g, (uint16_t)(at + BACK_LAYER), ROW_TILE_ROWS, windows.tiles, DIMMED));
    }
    ORIGINAL_EFFECT(leave_dimmed(g, windows));
}

/* ---- The panel --------------------------------------------------------------------------------- */

/* The cursor's sprite held back for the panel's three frames. */
enum { PANEL_FRAMES = 3 };

/* The panel's windows: the main menu's, or the gate's, with a label set in
 * sixteen-colour tiles (its second wording where the story holds the party
 * to a rule, menu_story_items' first) and the gate's sprite on the screens'
 * eighth record; the label's tiles are sent and laid out in two strips, its
 * top half and its bottom. */
enum {
    MAIN_MENU_PANEL_WINDOWS = 0xBF48, GATE_PANEL_WINDOWS = 0xBF05, SCREEN_WINDOWS = 0xBF7B,
    GATE_LABEL = 4, GATE_LABEL_WITH_A_RULE = 5, GATE_LABEL_AT = 0x5E00, UPLOAD_GATE_LABEL = 0xCAD9,
    GATE_RECORD = 7, GATE_SPRITE = 0x48, GATE_SPRITE_SET = 1, GATE_SPRITE_Y = 0xCA, GATE_SPRITE_X = 0x20, GATE_SPRITE_COLOURS = 0x20,
};
static const struct {
    uint16_t at, columns, first_tile;
} GATE_LABEL_STRIPS[] = { { 0x334C, 8, 0x0100 }, { 0x33CC, 8, 0x0121 } };

/* The gate's colours: palette row 8's first twelve are row 12's. */
enum { GATE_COLOURS_FROM = 0xC0, GATE_COLOURS_TO = 0x80, GATE_COLOURS = 12 };

bool menu_exchange_panel_shows_the_gate(const GameState *g) { return !(menu_exchange_layout(g) & MAIN_MENU_PANEL); }

void menu_exchange_panel_start(GameState *g, Platform *pf)
{
    set_menu_tasks_countdown(g, MENU_CURSOR_TASK, PANEL_FRAMES);
    TIMED_AS(g, 0xC2F588, menu_empty_the_records(g));
    if (!menu_exchange_panel_shows_the_gate(g)) {
        menu_draw_windows(g, pf, MAIN_MENU_PANEL_WINDOWS);
        return;
    }
    menu_draw_windows(g, pf, GATE_PANEL_WINDOWS);
    ORIGINAL_EFFECT(set_menu_draw_origin(g, MENU_FRONT_LAYER_AT));
    uint8_t label = menu_story_items(g, 0) != 0 ? GATE_LABEL_WITH_A_RULE : GATE_LABEL;
    set_menu_load_destination(g, GATE_LABEL_AT);
    TIMED_AS(g, 0xC2F90C, (void)menu_set_label_laid_out(g, MENU_LABEL_STRINGS, label, GATE_LABEL_AT, MENU_TEXT_SIXTEEN_COLOUR_LABEL));
    for (unsigned i = 0; i < GATE_COLOURS; i++)
        set_menu_palette(g, GATE_COLOURS_TO + i, menu_palette(g, GATE_COLOURS_FROM + i));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * GATE_COLOURS));
}

void menu_exchange_put_the_gate_sprite_on_its_record(GameState *g)
{
    set_menu_record_sprite(g, GATE_RECORD, GATE_SPRITE);
    set_menu_record_sprite_set(g, GATE_RECORD, GATE_SPRITE_SET);
    set_menu_record_y(g, GATE_RECORD, GATE_SPRITE_Y);
    set_menu_record_x_low(g, GATE_RECORD, GATE_SPRITE_X);
}

void menu_exchange_gate_sprite_loaded(GameState *g)
{
    set_menu_record_colours(g, GATE_RECORD, GATE_SPRITE_COLOURS);
    for (unsigned i = 0; i < sizeof GATE_LABEL_STRIPS / sizeof GATE_LABEL_STRIPS[0]; i++)
        TIMED_AS(g, 0xC2FBB4, (void)menu_lay_out_strip(g, GATE_LABEL_STRIPS[i].at, GATE_LABEL_STRIPS[i].columns,
                                                       GATE_LABEL_STRIPS[i].first_tile));
    menu_queue_fixed_upload(g, UPLOAD_GATE_LABEL);
}

/* The panel's colours: palette row 6's first eight and row 2's first four
 * the panel's own, every character's sprite colours, and row 4 the first
 * colours dimmed. */
enum { PANEL_COLOURS = 0x60, HIGHLIGHT = 0x20, DIMMED_COLOURS_AT = 0x40 };

/* For the original's code and shadow mode: it copies row 0's first sixteen
 * into row 4 before the dimming writes over them. */
enum { ROW_COPIED = 2 * 16 };

void menu_exchange_panel_colours(GameState *g)
{
    for (unsigned i = 0; i < MENU_EXCHANGE_PANEL_COLOURS; i++)
        set_menu_palette(g, PANEL_COLOURS + i, menu_rom_colour(g, MENU_ROM_EXCHANGE_PANEL_COLOURS, i));
    for (unsigned i = 0; i < MENU_EXCHANGE_HIGHLIGHT_COLOURS; i++)
        set_menu_palette(g, HIGHLIGHT + i, menu_rom_colour(g, MENU_ROM_ITEMS_HIGHLIGHT_COLOURS, i));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * (MENU_EXCHANGE_PANEL_COLOURS + MENU_EXCHANGE_HIGHLIGHT_COLOURS) + ROW_COPIED));
    TIMED_AS(g, 0xC2F5A7, menu_load_character_colours(g));
    TIMED_AS(g, 0xC2A0E7, menu_equip_dim_colours(g, DIMMED_COLOURS_AT));
    list_the_party(g);
}

void menu_exchange_panel_draw_the_rows(GameState *g, Platform *pf) { draw_the_rows(g, pf); }

/* The records' frames go two blocks a frame; the panel's line effect is the
 * colour math's. */
enum { RECORD_BLOCKS_A_FRAME = 2, PANEL_LINE_EFFECTS = 0x01 };

void menu_exchange_panel_finish(GameState *g, Platform *pf)
{
    set_menu_record_block_limit(g, RECORD_BLOCKS_A_FRAME);
    TIMED_AS(g, 0xC2F643, menu_start_the_records(g));
    dim_who_stays(g);
    TIMED_AS(g, 0xC286DD, menu_build_math_table(g, pf));
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    set_menu_line_effects(g, PANEL_LINE_EFFECTS);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
    menu_queue_fixed_upload(g, MENU_UPLOAD_WINDOW_LINES);
}

// $C2:CAE6
void menu_exchange_panel_put_away(GameState *g)
{
    set_menu_line_effects(g, PANEL_LINE_EFFECTS);
    TIMED_AS(g, 0xC2F643, menu_start_the_records(g));
}

/* ---- Opening ----------------------------------------------------------------------------------- */

enum { TWO = 2 };

bool menu_exchange_has_two_to_swap(const GameState *g)
{
    return (uint8_t)(menu_exchange_active_free(g) + menu_exchange_reserve_count(g)) >= TWO;
}

// $C2:C627 (where there aren't two)
void menu_exchange_refuse(GameState *g)
{
    set_menu_screen(g, MENU_SCREEN_MAIN_MENU);
    set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1));
}

// $C2:C7AC
/* The layout the event asks for; the byte after it takes the same, which
 * nothing reads. */
void menu_exchange_lay_out_for_the_gate(GameState *g, uint8_t argument)
{
    set_menu_exchange_layout(g, argument);
    ORIGINAL_EFFECT(set_menu_exchange_layout_high(g, argument));
}

// $C2:C649
void menu_exchange_start_choosing(GameState *g, uint8_t from)
{
    uint8_t place = from;
    while (!row_is_free(row(g, place)))
        place++;
    set_menu_cursor_index(g, place);
    set_menu_exchange_first(g, NOBODY_CHOSEN);
    set_menu_exchange_second(g, NOBODY_CHOSEN);
    set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1));
}

/* The screen's line effect is the colour math's; its third layer stands
 * sixteen lines above its top; with the active party alone, the first
 * window runs from x 64 to 191, over the middle. */
enum { SCREEN_LINE_EFFECTS = 0x01, THIRD_LAYER = 2, THIRD_LAYER_Y = 0x01F0, MIDDLE_WINDOW_LEFT = 0x40, MIDDLE_WINDOW_RIGHT = 0xBF };
enum { MIDDLE_WINDOW_EDGES = MIDDLE_WINDOW_RIGHT << 8 | MIDDLE_WINDOW_LEFT };

/* The reserve's scroll bar, where it has more members than its four rows:
 * the thumb travels six lines a row the reserve can scroll, and its step a
 * row is the bar's steps over that. */
enum { LINES_A_ROW = 6, BAR_PLACE = 0x12EC, BAR_STEPS = 0xA400 };

static void show_the_scroll_bar(GameState *g)
{
    uint8_t rows_past = (uint8_t)(menu_exchange_reserve_count(g) - EXCHANGE_RESERVE_SHOWN);
    ORIGINAL_EFFECT(hw_set_dividend(g, BAR_STEPS));
    set_menu_scroll_bar_span(g, rows_past);
    if (menu_exchange_reserve_count(g) <= EXCHANGE_RESERVE_SHOWN)
        return;
    uint8_t span = (uint8_t)(rows_past * LINES_A_ROW);
    set_menu_scroll_bar_span(g, span);
    ORIGINAL_EFFECT(hw_divide_by(g, span));
    set_menu_scroll_bar_thumb(g, (uint8_t)(menu_reserve_top(g) * LINES_A_ROW));
    TIMED_AS(g, 0xC28B93, menu_start_the_scroll_bar(g));
    set_menu_scroll_bar_place(g, BAR_PLACE);
    set_menu_scroll_bar_step(g, (uint16_t)(BAR_STEPS / span));
}

// $C2:CAF3
void menu_exchange_draw_the_screen(GameState *g, Platform *pf)
{
    menu_draw_windows(g, pf, SCREEN_WINDOWS);
    TIMED_AS(g, 0xC2984A, menu_clear_sprites(g));
    set_menu_line_effects(g, SCREEN_LINE_EFFECTS);
    set_menu_scroll_y(g, THIRD_LAYER, THIRD_LAYER_Y);
    TIMED_AS(g, 0xC2F5A7, menu_load_character_colours(g));
    dim_who_stays(g);
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    if (active_party_alone(g))
        set_menu_window_shadows_window1_edges(g, MIDDLE_WINDOW_EDGES);
    show_the_scroll_bar(g);
    TIMED_AS(g, 0xC2A0E7, menu_equip_dim_colours(g, DIMMED_COLOURS_AT));
}

void menu_exchange_show_the_screen(GameState *g)
{
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    menu_queue_fixed_upload(g, MENU_UPLOAD_WINDOW_LINES);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
}

/* The gate's swirl, its graphics unpacked (menu/gate_screen.h): the
 * graphics and its tilemap sent. */
enum { UPLOAD_SWIRL_GRAPHICS = 0xCE96 };

void menu_exchange_show_the_swirl(GameState *g)
{
    menu_queue_fixed_upload(g, UPLOAD_SWIRL_GRAPHICS);
    menu_queue_fixed_upload(g, MENU_UPLOAD_TEXT_LAYER_WHOLE);
}

/* The gate's screen shows its first three layers and the sprites. */
enum { GATE_LAYERS = 0x17 };

void menu_exchange_show_the_gate(GameState *g) { set_menu_window_shadows_main_screen(g, GATE_LAYERS); }

/* ---- Choosing ---------------------------------------------------------------------------------- */

ExchangeInput menu_exchange_input(const GameState *g)
{
    if (menu_asked(g, MENU_CHOOSE))
        return EXCHANGE_CHOSEN;
    if (menu_asked(g, MENU_GO_BACK))
        return EXCHANGE_LEFT;
    return EXCHANGE_MOVED;
}

bool menu_exchange_on_someone_free(const GameState *g) { return !(menu_exchange_on_cursor(g) & STAYS); }

/* A row on the tilemap: the active party's down the left from the third
 * tile row, six apart, the reserve's down the right; in the middle where
 * the active party is alone. Five tile rows of ten tiles a row. */
enum { LEFT_COLUMN = 3, RIGHT_COLUMN = 19, MIDDLE_COLUMN = 9, FIRST_TILE_ROW = 2, TILE_ROWS_A_ROW = 6 };
static const MenuWindowSize ROW_SIZE = { 10, 5 };

// $C2:C784
static MenuTilePlace tiles_of(GameState *g, uint8_t place)
{
    MenuTilePlace at = { LEFT_COLUMN, 0 };
    if (on_the_reserve(place)) {
        place = (uint8_t)(place - EXCHANGE_FIRST_RESERVE_PLACE);
        at.column = RIGHT_COLUMN;
    }
    at.row = (uint8_t)((place & 7) * TILE_ROWS_A_ROW + FIRST_TILE_ROW);
    if (active_party_alone(g))
        at.column = MIDDLE_COLUMN;
    ORIGINAL_EFFECT(set_menu_scratch_exchange_tiles(g, (uint16_t)(at.column | at.row << 8)));
    return at;
}

/* Where a reserve row chosen first is kept aside, in the tilemap below the
 * screen, for the swap to take it from where the reserve has scrolled it
 * out of view: its six tile rows. */
static const MenuTilePlace KEPT_ASIDE = { 2, 32 };
static const MenuWindowSize RESERVE_ROW_SIZE = { 10, 6 };

/* For the original's code and shadow mode: its multiply by six leaves the
 * row doubled in the menus' first cell. */
static void leave_row_doubled(GameState *g, uint8_t reserve_place)
{
    set_menu_scratch_exchange_row(g, (uint8_t)(2 * reserve_place));
}

// $C2:C674
void menu_exchange_choose_the_first(GameState *g)
{
    set_menu_held_reserve_top(g, menu_reserve_top(g));
    uint8_t place = menu_exchange_cursor_kept(g);
    set_menu_cursor_index(g, place);
    set_menu_held_cursor(g, place);
    if (on_the_reserve(place)) {
        uint8_t reserve_place = (uint8_t)(place - EXCHANGE_FIRST_RESERVE_PLACE);
        ORIGINAL_EFFECT(leave_row_doubled(g, reserve_place));
        set_menu_draw_origin(g, MENU_FRONT_LAYER_AT);
        MenuTilePlace from = { RIGHT_COLUMN, (uint8_t)(reserve_place * TILE_ROWS_A_ROW + FIRST_TILE_ROW) };
        menu_copy_tile_area(g, from, KEPT_ASIDE, RESERVE_ROW_SIZE);
    }
    set_menu_exchange_first(g, menu_exchange_on_cursor(g));
    set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1));
    TIMED_AS(g, 0xC28A31, menu_hold_the_cursor_on_a_member(g));
}

// $C2:C70C
/* The two change places in the party, and so their rows: the first's row,
 * where the reserve has since scrolled it, or from where it was kept aside
 * when that is out of view, swapped with the second's on the screen, and
 * the rows and sprites taken again from the party. */
void menu_exchange_choose_the_second(GameState *g)
{
    set_menu_exchange_second(g, menu_exchange_on_cursor(g));
    party_exchange(g, menu_exchange_first(g), menu_exchange_second(g));
    TIMED_AS(g, 0xC2816A, menu_list_party(g));
    uint8_t held = menu_held_cursor(g);
    MenuTilePlace first;
    if (on_the_reserve(held)) {
        held = (uint8_t)(held + menu_held_reserve_top(g) - menu_reserve_top(g));
        set_menu_held_cursor(g, held);
        bool in_view = (uint8_t)(held - EXCHANGE_FIRST_RESERVE_PLACE) < EXCHANGE_RESERVE_SHOWN;
        first = in_view ? tiles_of(g, held) : KEPT_ASIDE;
    } else {
        first = tiles_of(g, held);
    }
    MenuTilePlace second = tiles_of(g, menu_exchange_cursor_kept(g));
    set_menu_draw_origin(g, MENU_FRONT_LAYER_AT);
    menu_swap_tile_areas(g, first, second, ROW_SIZE);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    list_the_party(g);
    stand_the_members(g);
}

// $C2:C6E7
void menu_exchange_let_go(GameState *g)
{
    set_menu_exchange_first(g, NOBODY_CHOSEN);
    set_menu_exchange_second(g, NOBODY_CHOSEN);
    TIMED_AS(g, 0xC28255, menu_stop_tasks(g, 1u << MENU_HELD_CURSORS_TASK));
    stand_everyone_still(g);
}

// $C2:C6E1
void menu_exchange_back_to_the_first(GameState *g)
{
    set_menu_cursor_index(g, menu_exchange_cursor_kept(g));
    set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) - 1));
    menu_exchange_let_go(g);
}

/* ---- The cursor's move ------------------------------------------------------------------------- */

/* A slide: the reserve's column of the tilemap copied below the screen with
 * the row coming in drawn above or below it, then copied back a tile row
 * further each frame, eight lines, while the members' sprites move with it
 * and the second cursor and the scroll bar's thumb follow. A slide's masking
 * line effect hides the rows past the reserve's window meanwhile. */
enum { SLIDE_FRAMES = 6, SLIDE_LINES = 8, RESERVE_LINES = 48, MASKED_LINE_EFFECT = 0x04 };
static const MenuTilePlace RESERVE_COLUMN = { RIGHT_COLUMN, FIRST_TILE_ROW };
static const MenuWindowSize RESERVE_COLUMN_SIZE = { 10, 24 };
static const MenuTilePlace SLID_BACK_FROM = { RIGHT_COLUMN, 38 }, SLID_ON_FROM = { RIGHT_COLUMN, 32 };

/* For the original's code and shadow mode: the slide's state in its cells,
 * where its loop keeps it from frame to frame. */
static void leave_slide(GameState *g, const ExchangeSlide *slide)
{
    set_menu_exchange_slide_from(g, (uint16_t)(slide->from.column | slide->from.row << 8));
    set_menu_scroll_step(g, (uint16_t)(int16_t)slide->step);
    set_menu_scroll_frames(g, slide->frames_left);
}

static void start_the_slide(GameState *g, Platform *pf, ExchangeSlide *slide, unsigned row_place)
{
    uint8_t reserve_row = (uint8_t)(menu_cursor_index(g) - EXCHANGE_FIRST_RESERVE_PLACE + menu_reserve_top(g));
    uint8_t member = member_of_reserve_row(g, reserve_row);
    show_member_at(g, pf, member, row_place);
    menu_work_on_member(g, (uint8_t)(member + 1));
    set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) | MASKED_LINE_EFFECT));
    set_menu_draw_origin(g, MENU_FRONT_LAYER_AT);
    menu_copy_tile_area(g, RESERVE_COLUMN, slide->from, RESERVE_COLUMN_SIZE);
    set_menu_scroll_moved(g, 0);
    slide->frames_left = SLIDE_FRAMES;
    ORIGINAL_EFFECT(leave_slide(g, slide));
    ORIGINAL_EFFECT(set_menu_scratch_exchange_row(g, reserve_row));
    ORIGINAL_EFFECT(set_menu_exchange_place_offset(g, place_offset(row_place)));
}

/* Pushed past the reserve's first row, it scrolls back a row, its row
 * above sliding in; past its last, on a row, its row below sliding in. The
 * cursor stays on the first or last row either way, and the second cursor
 * held on a member starts a row's lines off it, to slide back on. */
static bool scroll(GameState *g, Platform *pf, ExchangeSlide *slide)
{
    if (menu_pushed(g, MENU_PUSHED_UP)) {
        set_menu_cursor_index(g, EXCHANGE_FIRST_RESERVE_PLACE);
        if (menu_reserve_top(g) == 0)
            return false;
        set_menu_reserve_top(g, (uint8_t)(menu_reserve_top(g) - 1));
        set_menu_held_cursor_shift_low(g, (uint8_t)-RESERVE_LINES);
        *slide = (ExchangeSlide){ SLID_BACK_FROM, -SLIDE_LINES, 0 };
        start_the_slide(g, pf, slide, ROW_ABOVE);
        return true;
    }
    uint8_t last_top = (uint8_t)(menu_exchange_reserve_count(g) - (EXCHANGE_RESERVE_SHOWN - 1));
    ORIGINAL_EFFECT(set_menu_scratch_exchange_row(g, last_top));
    set_menu_cursor_index(g, LAST_RESERVE_PLACE);
    uint8_t top = (uint8_t)(menu_reserve_top(g) + 1);
    if (top >= last_top)
        return false;
    set_menu_reserve_top(g, top);
    set_menu_held_cursor_shift_low(g, RESERVE_LINES);
    *slide = (ExchangeSlide){ SLID_ON_FROM, SLIDE_LINES, 0 };
    start_the_slide(g, pf, slide, ROW_BELOW);
    return true;
}

/* The reserve's rows a slide can bring in: past its four shown. */
static bool can_scroll(const GameState *g) { return menu_exchange_reserve_count(g) > EXCHANGE_RESERVE_SHOWN; }

// $C2:C805 (its start, with $C2:C85B and $C2:C8BA's)
bool menu_exchange_scrolls(GameState *g, Platform *pf, ExchangeSlide *slide)
{
    set_menu_held_cursor_shift_low(g, 0);
    if (!can_scroll(g) || !menu_pushed(g, MENU_PUSHED_PAST_AN_EDGE))
        return false;
    return scroll(g, pf, slide);
}

// $C2:C949
/* The reserve's members' sprites move with the slide, standing; the
 * original goes from the last up. */
static void move_the_reserve(GameState *g, int8_t step)
{
    for (unsigned r = menu_exchange_reserve_count(g); r-- > 0;) {
        uint8_t character = party_at(g, place_in_the_party(row(g, EXCHANGE_ACTIVE_ROWS + r)));
        ORIGINAL_EFFECT(menu_leave_record_found(g, character));
        set_menu_record_y(g, character, (uint8_t)(menu_record_y(g, character) - step));
        set_menu_record_animation(g, character, STANDING);
    }
}

// $C2:C8F7
/* A frame of the slide: the column shown a tile row further on, the
 * second cursor's offset closing in on its row, the thumb a line on. */
bool menu_exchange_slide_a_frame(GameState *g, ExchangeSlide *slide)
{
    int8_t toward = slide->step < 0 ? -1 : 1;
    slide->from.row = (uint8_t)(slide->from.row + toward);
    set_menu_held_cursor_shift(g, (uint16_t)(menu_held_cursor_shift(g) - slide->step));
    set_menu_scroll_bar_thumb_word(g, (uint16_t)(menu_scroll_bar_thumb_word(g) + toward));
    move_the_reserve(g, slide->step);
    set_menu_draw_origin(g, MENU_FRONT_LAYER_AT);
    menu_copy_tile_area(g, slide->from, RESERVE_COLUMN, RESERVE_COLUMN_SIZE);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    slide->frames_left--;
    ORIGINAL_EFFECT(leave_slide(g, slide));
    return slide->frames_left != 0;
}

// $C2:C93B
void menu_exchange_end_the_slide(GameState *g)
{
    set_menu_held_cursor_shift(g, 0);
    ORIGINAL_EFFECT(set_menu_scroll_step(g, 0));
    stand_the_members(g);
}

// $C2:C9AE
/* The cursor stepped off a row it can't stand on: pushed across, to the
 * other column where its own has nobody free; otherwise on, the way it was
 * pushed (down where it went across), as the cursor's places link them. */

static void step_off(GameState *g)
{
    uint8_t place = menu_cursor_index(g);
    if (menu_pushed(g, MENU_PUSHED_LEFT) || menu_pushed(g, MENU_PUSHED_RIGHT)) {
        if (!on_the_reserve(place) && menu_exchange_active_free(g) == 0) {
            set_menu_cursor_index(g, (uint8_t)(place + EXCHANGE_FIRST_RESERVE_PLACE));
            return;
        }
        if (on_the_reserve(place) && menu_exchange_reserve_count(g) == 0) {
            set_menu_cursor_index(g, (uint8_t)(place - EXCHANGE_FIRST_RESERVE_PLACE));
            return;
        }
    }
    uint8_t next = menu_pushed(g, MENU_PUSHED_UP) ? menu_cursor_places_up(g, place) : menu_cursor_places_down(g, place);
    set_menu_cursor_index(g, next & EXCHANGE_LINKED_PLACE);
}

/* The cursor's places link every row to the next, so a search for one it
 * can stand on ends within as many steps as there are places. Where the
 * links go round rows nobody may be chosen on -- the main menu's exchange,
 * which shows the active party alone, holding a reserve count another
 * screen left, with the cursor pushed across into the reserve's empty
 * column -- the original searches for ever and the game hangs; the port
 * stops after MOST_STEPS with nobody under the cursor (docs/TODO.md). */
enum { MOST_STEPS = 64 };

// $C2:C805 (from $C2:C819)
/* The cursor settles on a row it can stand on (or, where there is nobody
 * the player may move, on the first, with nobody under it); its member
 * walks, and the cursor faces the other way on the reserve's side. */
void menu_exchange_settle_the_cursor(GameState *g)
{
    uint8_t on = NOBODY_FREE;
    for (unsigned steps = 0; steps < MOST_STEPS; steps++) {
        if ((menu_exchange_active_free(g) | menu_exchange_reserve_count(g)) == 0) {
            set_menu_cursor_index(g, 0);
            break;
        }
        uint8_t r = row(g, row_at(g, menu_cursor_index(g)));
        if (row_is_free(r)) {
            on = r;
            break;
        }
        step_off(g);
    }
    set_menu_exchange_on_cursor(g, on);
    animate_the_members(g);
    set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) & ~MASKED_LINE_EFFECT));
    uint8_t place = menu_cursor_index(g);
    set_menu_exchange_cursor_kept(g, place);
    set_menu_cursor_flip(g, on_the_reserve(place) ? 1 : 0);
    menu_queue_fixed_upload(g, MENU_UPLOAD_WINDOW_LINES);
}
