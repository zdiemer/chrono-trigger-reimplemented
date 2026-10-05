/* The party's members as the menus' screens list them.
 *
 * A screen that works on one member at a time (the equipment screen, and the
 * main menu's panel for it) lists the members the player can reach down its
 * left side, three rows at a time. A row is the member's face, name, level
 * and figures, drawn by a window command list from the member's record, which
 * the menus first copy where the status windows draw from (a member's
 * loading); a member whose HP is down to a tenth of its greatest has it drawn
 * marked. The row the cursor is on keeps its colours and the others are
 * dimmed. With more members than three the list scrolls, and a scroll bar
 * beside it shows how far (menu/equipment.c slides it).
 *
 * Beside the list, the chosen member's card: the time played, the member's
 * figures and stats (worked out and drawn by the original's routines, which
 * the card's bridge calls out to), the face as a large portrait, and a label
 * of the member's element. */
#include "menu/members.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/bridge.h"
#include "menu/equipment.h"
#include "menu/figures.h"
#include "menu/menu_data.h"
#include "menu/text.h"
#include "menu/tiles.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* ---- A member's record --------------------------------------------------------------------------- */

/* The menus list the party's characters by place (menu_members), the active
 * members first; a place past the last holds a byte with its top bit set. A
 * character's record is $50 bytes, and its name's six after the status
 * record's copy. */
enum { NO_MEMBER = 0x80, CHARACTER_BITS = 0x07, CHARACTERS = CHARACTER_RECORDS_COUNT };
enum { RECORD_BYTES = 0x50, NAME_BYTES = 6, WAITING = 0xFF, ACTIVE = 0 };

/* Where a character's record is in work RAM, as the menus note it for a
 * screen that writes a change back (menu_member_record_at holds the
 * address). */
static uint16_t record_place(uint8_t character)
{
    return (uint16_t)(RAM_ABS(RAM_CHARACTER_RECORDS) + character * RAM_CHARACTER_RECORDS_STRIDE);
}

uint8_t menu_member_worked_on(const GameState *g) { return (uint8_t)menu_screen_argument(g); }

void menu_work_on_member(GameState *g, uint8_t member)
{
    set_menu_screen_argument(g, (uint16_t)((menu_screen_argument(g) & 0xFF00) | member));
}

void menu_work_on_member_only(GameState *g, uint8_t member) { set_menu_screen_argument(g, member); }

/* The character whose record the menus loaded last, from where they noted
 * it. */
uint8_t menu_member_character(const GameState *g)
{
    return (uint8_t)((uint16_t)(menu_member_record_at(g) - RAM_ABS(RAM_CHARACTER_RECORDS)) / RAM_CHARACTER_RECORDS_STRIDE);
}

bool menu_member_record_is_a_characters(const GameState *g)
{
    uint16_t at = (uint16_t)(menu_member_record_at(g) - RAM_ABS(RAM_CHARACTER_RECORDS));
    return at % RAM_CHARACTER_RECORDS_STRIDE == 0 && at / RAM_CHARACTER_RECORDS_STRIDE < CHARACTER_RECORDS_COUNT;
}

/* The original finds the record and the name with the multiplier, which it
 * leaves holding the name's product. */
static void leave_multiplier(GameState *g, uint8_t character, uint8_t named)
{
    hw_set_multiplicand(g, character);
    (void)hw_multiply_by(g, RECORD_BYTES);
    hw_set_multiplicand(g, named);
    (void)hw_multiply_by(g, NAME_BYTES);
}

// $C2:8820
bool menu_load_member(GameState *g, uint8_t member)
{
    set_menu_member_waiting(g, member >= menu_active_members(g) ? WAITING : ACTIVE);
    return menu_load_character(g, menu_members(g, member));
}

// $C2:8816, $C2:8839
bool menu_load_character(GameState *g, uint8_t character)
{
    if (character & NO_MEMBER) {
        ORIGINAL_EFFECT(hw_set_multiplicand(g, character));
        return false;
    }
    set_menu_member_record_at(g, record_place(character));
    for (unsigned i = 0; i < RECORD_BYTES; i++)
        set_menu_status_record_bytes_byte(g, i, character_records_record_byte(g, character, i));
    uint8_t named = menu_status_record_character(g) & CHARACTER_BITS;
    for (unsigned i = 0; i < NAME_BYTES; i++)
        set_menu_status_record_tail_byte(g, i, character_names_byte(g, named, i));
    ORIGINAL_EFFECT(leave_multiplier(g, character, named));
    return true;
}

uint8_t menu_member_on_the_cursor(const GameState *g) { return (uint8_t)(menu_member_row(g) + menu_member_scroll(g)); }

/* ---- The list's rows ----------------------------------------------------------------------------- */

/* What a row's window list reads besides the record: the character whose
 * face and name it draws, and whether the HP is drawn in the low HP's
 * colour. */
enum { FACE_BIT = 0x20, LOW_HP_COLOUR = 4 };

// $C2:A1EF
void menu_note_member_face_and_hp(GameState *g)
{
    uint8_t character = menu_status_record_character(g) & CHARACTER_BITS;
    bool low = menu_low_hp_marks(g, character) >= menu_status_record_hp(g);
    set_menu_member_marked(g, low ? LOW_HP_COLOUR : 0);
    set_menu_member_marked_high(g, 0);
    set_menu_member_face(g, (uint16_t)(character | FACE_BIT));
}

/* The window lists of a member's row, of a row with no member, and of the
 * chosen member's card. */
enum { MEMBER_ROW = 0xBE0E, EMPTY_ROW = 0xBE15, MEMBER_CARD = 0xBE1C };

// $C2:A216
void menu_draw_member_row(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC2A1EF, menu_note_member_face_and_hp(g));
    menu_draw_windows(g, pf, MEMBER_ROW);
}

/* The three rows the list shows, from its first member, each six tile rows
 * below the last; the rows go on from where each row's list left the drawing
 * origin, and stop past the reachable members. */
enum { FIRST_ROW = MENU_FRONT_LAYER_AT, ROW_SPACING = 0x180, ROWS_END = 0x3280 };

// $C2:A1B2
void menu_draw_member_rows(GameState *g, Platform *pf)
{
    uint16_t member = menu_member_scroll(g);
    uint16_t at = FIRST_ROW;
    set_menu_draw_origin(g, at);
    for (;;) {
        bool loaded;
        TIMED_AS(g, 0xC28820, loaded = menu_load_member(g, (uint8_t)member));
        if (loaded)
            TIMED_AS(g, 0xC2A216, menu_draw_member_row(g, pf));
        else
            TIMED_AS(g, 0xC2A1E9, menu_draw_windows(g, pf, EMPTY_ROW));
        member++;
        if (member >= menu_members_reachable(g))
            break;
        at = (uint16_t)(menu_draw_origin(g) + ROW_SPACING);
        set_menu_draw_origin(g, at);
        if (at >= ROWS_END)
            break;
    }
    set_menu_screen_argument(g, member);
}

/* The rows' colours: a row is six tile rows of twelve tiles, from its second
 * column and second tile row, on the list's two layers, whose tilemap buffers
 * are $1000 bytes apart. The cursor's row is in palette row 0, the others in
 * the dimmed palette row 4. */
enum { FIRST_ROW_TILES = 0x2E84, BACK_LAYER = 0x1000, ROW_TILE_ROWS = 6, ROW_TILES = 12 };
enum { CHOSEN_COLOUR = 0x00, DIMMED_COLOUR = 0x10 };
enum { UPLOAD_BG2_ROWS = 0xFBFF };

static uint8_t colour_of(bool dimmed) { return dimmed ? DIMMED_COLOUR : CHOSEN_COLOUR; }

// $C2:9EB0
void menu_colour_member_row(GameState *g, uint16_t at, bool dimmed)
{
    menu_recolour_block(g, at, ROW_TILE_ROWS, ROW_TILES, colour_of(dimmed));
    menu_recolour_block(g, (uint16_t)(at + BACK_LAYER), ROW_TILE_ROWS, ROW_TILES, colour_of(dimmed));
}

/* What the original's colouring leaves in the menus' first cells: its count
 * of rows, the cursor's row, the place past the last row, and the last row's
 * tiles and colour as one word. Nothing reads them after it. */
static uint16_t row_colour_word(bool dimmed) { return (uint16_t)(colour_of(dimmed) << 8 | ROW_TILES); }

static void leave_rows_coloured(GameState *g, uint8_t chosen)
{
    set_menu_scratch_member_rows_done(g, MENU_MEMBER_ROWS);
    set_menu_scratch_member_row_chosen(g, chosen);
    set_menu_scratch_member_row_place(g, (uint16_t)(FIRST_ROW_TILES + MENU_MEMBER_ROWS * ROW_SPACING));
    set_menu_scratch_member_row_colour(g, row_colour_word(chosen != MENU_MEMBER_ROWS - 1));
}

/* What the original's colouring of a row on its own leaves in the menus'
 * cells: where the row is, and its tiles and colour as one word. */
void menu_leave_member_row_coloured(GameState *g, uint16_t at, bool dimmed)
{
    set_menu_scratch_member_row_place(g, at);
    set_menu_scratch_member_row_colour(g, row_colour_word(dimmed));
}

// $C2:9E76
void menu_colour_member_rows(GameState *g)
{
    uint8_t chosen = menu_member_row(g);
    for (uint8_t row = 0; row < MENU_MEMBER_ROWS; row++)
        TIMED_AS(g, 0xC29EB0, menu_colour_member_row(g, (uint16_t)(FIRST_ROW_TILES + row * ROW_SPACING), row != chosen));
    ORIGINAL_EFFECT(leave_rows_coloured(g, chosen));
    menu_queue_fixed_upload(g, UPLOAD_BG2_ROWS);
}

/* ---- The chosen member's card -------------------------------------------------------------------- */

/* The label of the member's element, six tiles by two beside the portrait,
 * and its sixteen colours in palette row 6. */
enum { LABEL_AT = 0x2EEC, LABEL_TILES = 6, LABEL_ROWS = 2, LABEL_COLOURS = 16, LABEL_FIRST_COLOUR = 0x60 };

// $C2:A273
static void show_element(GameState *g)
{
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    MenuElement element = menu_character_element(g, menu_status_record_character(g));
    uint16_t tile = menu_element_label_tile(g, element);
    for (unsigned row = 0; row < LABEL_ROWS; row++)
        for (unsigned i = 0; i < LABEL_TILES; i++, tile++)
            wram_write16(g, 0x7E0000u | (uint16_t)(LABEL_AT + row * MENU_TILEMAP_ROW_BYTES + i * MENU_TILE_BYTES), tile);
    for (unsigned i = 0; i < LABEL_COLOURS; i++)
        set_menu_palette(g, LABEL_FIRST_COLOUR + i, menu_element_label_colour(g, element, i));
}

/* The card: the time played at the screen's foot (its digits are the
 * clock's at $7E:0402), the member's figures and stats, the large portrait in
 * palette row 3 and the element's label. */
enum { PORTRAIT_AT = 0x2EDE, PORTRAIT_ATTRIBUTE = 0x0E };
enum { UPLOAD_BG1_CARD = 0xFBE3 };

// $C2:A22F
void menu_start_member_card(GameState *g)
{
    menu_draw_the_clock(g);
    uint8_t member = menu_member_on_the_cursor(g);
    menu_work_on_member(g, member);
    TIMED_AS(g, 0xC28820, (void)menu_load_member(g, member));
}

void menu_draw_member_card_window(GameState *g, Platform *pf) { menu_draw_windows(g, pf, MEMBER_CARD); }

void menu_finish_member_card(GameState *g)
{
    uint8_t character = menu_status_record_character(g);
    set_menu_portrait_character(g, character);
    set_menu_draw_origin(g, PORTRAIT_AT);
    TIMED_AS(g, 0xC2EC38, menu_draw_portrait(g, PORTRAIT_AT, character, PORTRAIT_ATTRIBUTE));
    TIMED_AS(g, 0xC2A273, show_element(g));
    menu_queue_fixed_upload(g, UPLOAD_BG1_CARD);
}

/* ---- What a screen can have drawn ---------------------------------------------------------------- */

/* A place of the list loads a character whose record names a character too
 * (the status record's first byte picks the name and the element), or is
 * past the last member. */
bool menu_member_can_be_loaded(const GameState *g, uint8_t member)
{
    if (member >= MENU_MEMBERS_COUNT)
        return false;
    uint8_t character = menu_members(g, member);
    if (character & NO_MEMBER)
        return true;
    return character < CHARACTERS && (character_records_record_byte(g, character, 0) & CHARACTER_BITS) < CHARACTERS;
}

/* The record's character, whole, picks the portrait and the element. */
static bool character_is_drawable(const GameState *g, uint8_t member)
{
    uint8_t character = menu_members(g, member);
    return (character & NO_MEMBER) || character_records_record_byte(g, character, 0) < CHARACTERS;
}

/* What the card draws the member as having on is items of the game's: the
 * record's, or (past the last member, where nothing is loaded) the status
 * record's as it stands. */
enum { EQUIPPED_FROM = 0x27, EQUIPPED_BYTES = 4 };

static bool equipment_is_the_games(const GameState *g, uint8_t member)
{
    uint8_t character = menu_members(g, member);
    for (unsigned i = 0; i < EQUIPPED_BYTES; i++) {
        uint8_t item = (character & NO_MEMBER) ? menu_status_record_bytes_byte(g, EQUIPPED_FROM + i)
                                               : character_records_record_byte(g, character, EQUIPPED_FROM + i);
        if (item >= MENU_ITEM_NAME_PLACES_COUNT)
            return false;
    }
    return true;
}

bool menu_member_card_can_be_drawn_for(const GameState *g, uint8_t member)
{
    return menu_member_can_be_loaded(g, member) && character_is_drawable(g, member) && equipment_is_the_games(g, member)
           && menu_palette_changes_have_room(g) && menu_time_digits_are_decimal(g, MENU_CLOCK_DIGITS)
           && menu_vram_queue_bytes(g) % MENU_UPLOAD_BYTES == 0;
}

bool menu_member_card_can_be_drawn(const GameState *g)
{
    return menu_member_card_can_be_drawn_for(g, menu_member_on_the_cursor(g));
}

/* Every row the list draws, at its spacing: the member's place loadable and
 * its row's list drawable there. A row's list that moved the drawing origin
 * would take the next row elsewhere; the game's don't. The rows read a zero
 * from the menus' zero word, as the original tests a load. */
bool menu_member_rows_can_be_drawn_from(const GameState *g, uint8_t first)
{
    if (menu_zero(g) != 0)
        return false;
    uint16_t member = first;
    for (uint16_t at = FIRST_ROW; at < ROWS_END; at = (uint16_t)(at + ROW_SPACING), member++) {
        if (!menu_member_can_be_loaded(g, (uint8_t)member)
            || !menu_window_list_drawable_from(g, (menu_members(g, member) & NO_MEMBER) ? EMPTY_ROW : MEMBER_ROW, at))
            return false;
        if (member + 1u >= menu_members_reachable(g))
            return true;
    }
    return true;
}

bool menu_member_rows_can_be_drawn(const GameState *g) { return menu_member_rows_can_be_drawn_from(g, menu_member_scroll(g)); }

/* ---- The entries --------------------------------------------------------------------------------- */

// $C2:8820
/* The member menu_screen_argument's low byte names: A is 0 when one was
 * loaded, or the list's byte for no member in its low byte. */
void menu_load_member_entry(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t member = (uint8_t)menu_screen_argument(g);
    if (menu_load_member(g, member))
        s->cpu.a = 0;
    else
        m_set_a8(s, menu_members(g, member));
    m_rts(s);
}

// $C2:A1EF
void menu_note_member_face(Snes *s)
{
    menu_note_member_face_and_hp(state_of(s));
    m_rts(s);
}

// $C2:A216
void menu_draw_member_row_entry(Snes *s)
{
    menu_draw_member_row(state_of(s), m_platform(s));
    m_rts(s);
}

// $C2:A1E9
void menu_draw_empty_member_row(Snes *s)
{
    menu_draw_windows(state_of(s), m_platform(s), EMPTY_ROW);
    m_rts(s);
}

// $C2:A1B2
void menu_draw_member_rows_entry(Snes *s)
{
    menu_draw_member_rows(state_of(s), m_platform(s));
    m_rts(s);
}

void menu_leave_member_rows_coloured(Snes *s) { menu_leave_queued_upload(s, UPLOAD_BG2_ROWS); }

// $C2:9E76
/* Its callers go on with the accumulator's high byte as the upload's
 * queueing leaves it. */
void menu_colour_member_rows_entry(Snes *s)
{
    menu_colour_member_rows(state_of(s));
    ORIGINAL_EFFECT(menu_leave_member_rows_coloured(s));
    m_rts(s);
}

// $C2:9EB0
/* A row coloured: where it starts in the menus' fifth cell, its tiles and
 * colour in Y (twelve tiles, in the cursor's colour or dimmed). */
void menu_colour_member_row_entry(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t at = menu_scratch_member_row_place(g);
    bool dimmed = (s->cpu.y >> 8) == DIMMED_COLOUR;
    menu_colour_member_row(g, at, dimmed);
    ORIGINAL_EFFECT(menu_leave_member_row_coloured(g, at, dimmed));
    m_rts(s);
}

// $C2:A273
void menu_show_member_element(Snes *s)
{
    show_element(state_of(s));
    m_rts(s);
}

/* The card works the member's figures out with what it has on (and writes
 * them back), then draws what the member has on beside them. */
enum { EQUIPPED_AT = 0x30A2 };

// $C2:A22F
/* The card of the member the cursor is on; false where a call-out came back
 * otherwise than it was called, and the port has stopped. */
static bool draw_member_card(Snes *s)
{
    GameState *g = state_of(s);
    menu_set_widths(s, true, m_x8(s));
    menu_start_member_card(g);
    TIMED_AS(g, 0xC29137, menu_note_member_figures(g));
    menu_draw_member_card_window(g, m_platform(s));
    TIMED_AS(g, 0xC2A6F0, menu_equip_draw_equipped(g, EQUIPPED_AT));
    menu_finish_member_card(g);
    return true;
}

void menu_draw_member_card(Snes *s)
{
    menu_save_p(s);
    if (!draw_member_card(s))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:A21F
/* The card again, and the member's row drawn at the top of the list's
 * place. The figures' routine works on the status record the row draws
 * from; where it leaves a record that names no character (on states the
 * game doesn't make), the original draws the row. */
enum { ROW_AFTER_THE_CARD = 0xC2A225, ROW_READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static bool record_names_a_character(const GameState *g);

void menu_redraw_member_card(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, false, false);
    if (!menu_run(s, 0xC2A222, 0xC2A22F, menu_draw_member_card))
        return;
    if (!record_names_a_character(g)) {
        m_stop_at(s, ROW_AFTER_THE_CARD, ROW_READS_NOTHING);
        return;
    }
    set_menu_draw_origin(g, FIRST_ROW);
    TIMED_AS(g, 0xC2A216, menu_draw_member_row(g, m_platform(s)));
    menu_restore_p(s);
    m_rts(s);
}

/* ---- The guards ---------------------------------------------------------------------------------- */

static bool on_the_menus_page(Snes *s) { return menu_on_page_work_ram(s); }

/* A row's face and mark read the character's low HP mark. */
static bool record_names_a_character(const GameState *g)
{
    return (menu_status_record_character(g) & CHARACTER_BITS) < CHARACTERS;
}

bool menu_member_is_loadable(Snes *s)
{
    if (!on_the_menus_page(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = menu_member_can_be_loaded(g, (uint8_t)menu_screen_argument(g));
    vars_peek(false);
    return ok;
}

bool menu_member_face_is_noteable(Snes *s)
{
    if (!on_the_menus_page(s))
        return false;
    vars_peek(true);
    bool ok = record_names_a_character(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_member_row_is_drawable(Snes *s)
{
    if (!on_the_menus_page(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = record_names_a_character(g) && menu_window_list_drawable(g, MEMBER_ROW);
    vars_peek(false);
    return ok;
}

bool menu_empty_member_row_is_drawable(Snes *s)
{
    if (!on_the_menus_page(s))
        return false;
    vars_peek(true);
    bool ok = menu_window_list_drawable(state_of(s), EMPTY_ROW);
    vars_peek(false);
    return ok;
}

bool menu_member_rows_are_drawable(Snes *s)
{
    if (!on_the_menus_page(s))
        return false;
    vars_peek(true);
    bool ok = menu_member_rows_can_be_drawn(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_member_rows_are_colourable(Snes *s)
{
    if (!on_the_menus_page(s))
        return false;
    vars_peek(true);
    bool ok = menu_vram_queue_bytes(state_of(s)) % MENU_UPLOAD_BYTES == 0;
    vars_peek(false);
    return ok;
}

/* A row the original is asked to colour on its own: in the cursor's colour
 * or dimmed, its twelve tiles in the tilemap buffers on both layers. */
enum { LOW_RAM_END = 0x2000, BANK_END = 0x10000 };

bool menu_member_row_is_colourable(Snes *s)
{
    if (!on_the_menus_page(s))
        return false;
    uint16_t word = s->cpu.y;
    if (word != row_colour_word(false) && word != row_colour_word(true))
        return false;
    vars_peek(true);
    uint16_t at = menu_scratch_member_row_place(state_of(s));
    vars_peek(false);
    uint32_t reach = (uint32_t)at + BACK_LAYER + (ROW_TILE_ROWS - 1) * MENU_TILEMAP_ROW_BYTES + ROW_TILES * MENU_TILE_BYTES;
    return at >= LOW_RAM_END && reach <= BANK_END;
}

bool menu_member_element_is_showable(Snes *s)
{
    if (!on_the_menus_page(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = menu_status_record_character(g) < CHARACTERS && menu_palette_changes_have_room(g);
    vars_peek(false);
    return ok;
}

bool menu_member_card_is_drawable(Snes *s)
{
    if (!on_the_menus_page(s))
        return false;
    vars_peek(true);
    bool ok = menu_member_card_can_be_drawn(state_of(s));
    vars_peek(false);
    return ok;
}
