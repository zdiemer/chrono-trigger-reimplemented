/* The party's members as the menus' screens list them.
 *
 * A screen that works on one member at a time (the equipment screen, and the
 * main menu's panel for it) lists the members the player can reach down its
 * left side, three rows at a time. A row is the member's face, name, level
 * and figures, drawn by a window command list from the member's record, which
 * the menus first copy where the status windows draw from (a member's
 * loading); a member whose HP is down to a tenth of the HP its restoring stops
 * at has it drawn marked, against a mark each character's is worked out to as
 * the menus open and after the equipment screen changes what a member wears. The row the cursor is on keeps its colours and the others are
 * dimmed. With more members than three the list scrolls, and a scroll bar
 * beside it shows how far (menu/equipment.c slides it).
 *
 * Beside the list, the chosen member's card: the time played, the member's
 * figures and stats (worked out by menu/figures.c), the face as a large portrait, and a label
 * of the member's element. */
#include "menu/members.h"

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "menu/cursor.h"
#include "menu/equipment.h"
#include "menu/figures.h"
#include "menu/menu_data.h"
#include "menu/text.h"
#include "menu/tiles.h"
#include "menu/vram.h"
#include "menu/windows.h"
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

bool menu_load_member(GameState *g, uint8_t member)
{
    set_menu_member_waiting(g, member >= menu_active_members(g) ? WAITING : ACTIVE);
    return menu_load_character(g, menu_members(g, member));
}

// $C2:8816, $C2:8839
bool menu_load_character(GameState *g, uint8_t character)
{
    if (character & NO_MEMBER) {
        return false;
    }
    set_menu_member_record_at(g, record_place(character));
    for (unsigned i = 0; i < RECORD_BYTES; i++)
        set_menu_status_record_bytes_byte(g, i, character_records_record_byte(g, character, i));
    uint8_t named = menu_status_record_character(g) & CHARACTER_BITS;
    for (unsigned i = 0; i < NAME_BYTES; i++)
        set_menu_status_record_tail_byte(g, i, character_names_byte(g, named, i));
    return true;
}

uint8_t menu_member_on_the_cursor(const GameState *g) { return (uint8_t)(menu_member_row(g) + menu_member_scroll(g)); }

MemberListSlide menu_member_slide_for_push(const GameState *g, uint8_t scroll, bool one_past_the_rows)
{
    if (menu_pushed(g, MENU_PUSHED_DOWN))
        return one_past_the_rows ? MEMBERS_SLIDE_DOWN : NO_MEMBER_SLIDE;
    return (int8_t)(scroll - 1) < 0 ? NO_MEMBER_SLIDE : MEMBERS_SLIDE_UP;
}

/* ---- The low-HP marks ---------------------------------------------------------------------------- */

/* A member's HP shows in the low HP's colour once it is down to a tenth of
 * the HP its restoring stops at (menu_note_member_face_and_hp). */
enum { LOW_HP_FRACTION = 10 };

// $C2:813C
void menu_work_out_low_hp_marks(GameState *g)
{
    for (unsigned character = 0; character < CHARACTERS; character++)
        set_menu_low_hp_marks(g, character, (uint16_t)(character_records_hp_cap(g, character) / LOW_HP_FRACTION));
}

/* The original walks the records from the menus' zero word, which their
 * opening clears. */
bool menu_low_hp_marks_can_be_worked_out(const GameState *g) { return menu_zero(g) == 0; }

/* ---- The list's rows ----------------------------------------------------------------------------- */

/* What a row's window list reads besides the record: the character whose
 * face and name it draws, and whether the HP is drawn in the low HP's
 * colour. */
enum { FACE_BIT = 0x20, LOW_HP_COLOUR = 4 };

/* The HP a character's row is marked as low below. A record naming the
 * eighth character, which the game has no record or mark for (the status
 * record as a cursor past the last member leaves it can), is marked against
 * the word after the seven marks, as the original reads it. */
static uint16_t low_hp_mark(const GameState *g, uint8_t character)
{
    return character < CHARACTERS ? menu_low_hp_marks(g, character) : menu_low_hp_marks_unchecked(g, character);
}

void menu_note_member_face_and_hp(GameState *g)
{
    uint8_t character = menu_status_record_character(g) & CHARACTER_BITS;
    bool low = low_hp_mark(g, character) >= menu_status_record_hp(g);
    set_menu_member_marked(g, low ? LOW_HP_COLOUR : 0);
    set_menu_member_marked_high(g, 0);
    set_menu_member_face(g, (uint16_t)(character | FACE_BIT));
}

/* The window lists of a member's row, of a row with no member, and of the
 * chosen member's card. */
enum { MEMBER_ROW = 0xBE0E, EMPTY_ROW = 0xBE15, MEMBER_CARD = 0xBE1C };

void menu_draw_member_row(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC2A1EF, menu_note_member_face_and_hp(g));
    menu_draw_windows(g, pf, MEMBER_ROW);
}

/* The three rows the list shows, from its first member, each six tile rows
 * below the last; the rows go on from where each row's list left the drawing
 * origin, and stop past the reachable members. */
enum { FIRST_ROW = MENU_FRONT_LAYER_AT, ROW_SPACING = 0x180, ROWS_END = 0x3280 };

// $C2:A1E9
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

void menu_colour_member_row(GameState *g, uint16_t at, bool dimmed)
{
    menu_recolour_block(g, at, ROW_TILE_ROWS, ROW_TILES, colour_of(dimmed));
    menu_recolour_block(g, (uint16_t)(at + BACK_LAYER), ROW_TILE_ROWS, ROW_TILES, colour_of(dimmed));
}

void menu_colour_member_rows(GameState *g)
{
    uint8_t chosen = menu_member_row(g);
    for (uint8_t row = 0; row < MENU_MEMBER_ROWS; row++)
        TIMED_AS(g, 0xC29EB0, menu_colour_member_row(g, (uint16_t)(FIRST_ROW_TILES + row * ROW_SPACING), row != chosen));
    menu_queue_fixed_upload(g, UPLOAD_BG2_ROWS);
}

/* ---- The chosen member's card -------------------------------------------------------------------- */

/* The label of the member's element, six tiles by two beside the portrait,
 * and its sixteen colours in palette row 6. */
enum { LABEL_AT = 0x2EEC, LABEL_TILES = 6, LABEL_ROWS = 2, LABEL_COLOURS = 16, LABEL_FIRST_COLOUR = 0x60 };

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
    return menu_character_can_be_loaded(g, menu_members(g, member));
}

/* Loading a character also loads its name. A record's name selector must
 * name one of the seven characters, even when no party list is involved. */
bool menu_character_can_be_loaded(const GameState *g, uint8_t character)
{
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

// $C2:A273

/* The card works the member's figures out with what it has on (and writes
 * them back), then draws what the member has on beside them. */
enum { EQUIPPED_AT = 0x30A2 };

// $C2:A22F
/* The card of the member the cursor is on. */
void menu_draw_member_card(GameState *g, Platform *pf)
{
    menu_start_member_card(g);
    TIMED_AS(g, 0xC29137, menu_note_member_figures(g));
    menu_draw_member_card_window(g, pf);
    TIMED_AS(g, 0xC2A6F0, menu_equip_draw_equipped(g, EQUIPPED_AT));
    menu_finish_member_card(g);
}

// $C2:A21F
/* The card again, and the member's row drawn at the top of the list's
 * place, from the status record the card's figures were worked out on. */
typedef struct {
    bool (*draw_card)(void *ctx);
} CardRedrawSteps;

static bool redraw_member_card_with(GameState *g, Platform *pf, const CardRedrawSteps *n, void *ctx)
{
    if (!n->draw_card(ctx))
        return false;
    set_menu_draw_origin(g, FIRST_ROW);
    TIMED_AS(g, 0xC2A216, menu_draw_member_row(g, pf));
    return true;
}

typedef struct {
    GameState *game;
    Platform *pf;
} CardInC;

static bool draw_the_card(void *ctx)
{
    CardInC *c = ctx;
    if (!menu_member_card_can_be_drawn(c->game)) {
        program_breaks_a_contract(c->game, 0xC2A22F, "a member card the port doesn't draw");
        return false;
    }
    TIMED_AS(c->game, 0xC2A22F, menu_draw_member_card(c->game, c->pf));
    return true;
}

bool menu_redraw_member_card(GameState *g, Platform *pf)
{
    static const CardRedrawSteps in_c = { draw_the_card };
    CardInC c = { g, pf };
    return redraw_member_card_with(g, pf, &in_c, &c);
}

/* A row the original is asked to colour on its own: in the cursor's colour
 * or dimmed, its twelve tiles in the tilemap buffers on both layers. */
enum { LOW_RAM_END = 0x2000, BANK_END = 0x10000 };
