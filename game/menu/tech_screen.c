/* The techs screen (the menus' screen 3), and the main menu's panel for it.
 *
 * The screen shows the party's members down its left, three rows at a time,
 * and beside them a list of eight rows of the techs the member on the cursor
 * knows, of one kind: the member's own, or the double or triple techs it does
 * with others, a tab each across the top. Its steps (menu_screen_step):
 *
 * - the members: up and down choose whose techs are listed (the list of
 *   members slides past its ends where the party has more than three
 *   reachable), left and right the kind; the description window gives the
 *   kind's caption. Choosing goes into the list;
 * - the list: the cursor among the techs, the rows showing the tech's
 *   members and what it costs each in MP (in the short colour where the
 *   member has less), the description the tech's. A tech the member is still
 *   learning has a window of its own. The list pages with the shoulder
 *   buttons and slides a row past its ends. The member whose list it is
 *   stands to the list's right. Choosing a healing tech an active member can
 *   pay for uses it; anything else is refused;
 * - a healing tech used: a window of the active members it is used on (a
 *   cursor on each for one that heals them all), chosen again and again until
 *   going back, each use taking its cost from the user's MP while the
 *   restoring helps.
 *
 * The main menu shows the member rows and the tabs beside it while its
 * cursor is on the techs (the panel).
 *
 * This file is what the game does; tech_screen_bridge.c runs it on the
 * original's machine, calling out between the parts here to what is still
 * the original's: the scroll bars' frames and their task, the cursor's moves
 * within a list, the paging, the list's slide frames, the description's
 * setting, the restoring of HP and MP, the cursors on every member, the
 * sprites' colours and task, and the frames the screen waits. */
#include "menu/tech_screen.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/item_screen.h"
#include "menu/lists.h"
#include "menu/members.h"
#include "menu/menu_data.h"
#include "menu/sprites.h"
#include "menu/text.h"
#include "menu/tiles.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "vars.h"
#include "wram.h"

/* The frame's input, as the cursor's reading leaves it (menu/cursor.h). */
static bool chose(const GameState *g) { return menu_asked(g, MENU_CHOOSE); }
static bool went_back(const GameState *g) { return menu_asked(g, MENU_GO_BACK); }

/* The steps follow each other: each moves on to the next, or back to the
 * one before, from where it stands. */
static void move_to_the_step(GameState *g, TechsStep step) { set_menu_screen_step(g, (uint8_t)step); }
static void next_step(GameState *g) { move_to_the_step(g, (TechsStep)(menu_screen_step(g) + 1)); }
static void step_back(GameState *g) { move_to_the_step(g, (TechsStep)(menu_screen_step(g) - 1)); }

/* The cursor's places: the list's eight rows first, then the members' rows
 * from 11 (and in the window a tech is used from, its members' from 8). */
enum { LIST_ROWS = 8, FIRST_MEMBER_PLACE = 0x0B, FIRST_TARGET_PLACE = 0x08, MEMBER_ROWS = MENU_MEMBER_ROWS };

static uint8_t member_place(uint8_t row) { return (uint8_t)(row + FIRST_MEMBER_PLACE); }

/* The member the cursor is on (or was left on): its row plus how far the
 * list has slid, a byte. */
static uint8_t member_on_the_cursor(const GameState *g) { return (uint8_t)(menu_tech_member_row(g) + menu_tech_member_scroll(g)); }

/* Load a member (or a character) where the status windows read it from. */
static bool load_member(GameState *g, uint8_t member)
{
    bool loaded = false;
    menu_work_on_member(g, member);
    TIMED_AS(g, 0xC28820, loaded = menu_load_member(g, member));
    return loaded;
}

static MenuTechKind kind_listed(const GameState *g) { return (MenuTechKind)menu_tech_category(g); }

/* The member loaded last is a waiting one: only the active members use techs
 * from the menus. */
enum { WAITING_BIT = 0x80 };
static bool loaded_member_waits(const GameState *g) { return (menu_member_waiting(g) & WAITING_BIT) != 0; }

/* The loaded member's MP, as the screen compares it with a cost: its low
 * byte (a member has no more than 99). */
enum { MP_AT = 7, MAGIC_FIGURE = 3 };
static uint8_t loaded_mp(const GameState *g) { return menu_status_record_bytes_byte(g, MP_AT); }
static bool loaded_member_short_of(const GameState *g, uint8_t cost) { return loaded_mp(g) < cost; }
static uint8_t tech_cost_needed(const GameState *g) { return menu_tech_cost_needed(g); }

/* No tech's cost for a row: the original clears the cost a word wide. */
static void clear_the_cost_needed(GameState *g)
{
    set_menu_tech_cost_needed(g, 0);
    ORIGINAL_EFFECT(set_menu_tech_cost_beside(g, 0));
}

/* The screen's line effects at rest, and while its member list slides. */
enum { LINE_EFFECTS = 0x6D, SLIDING_LINE_EFFECTS = 0x6B };

/* The list shown is noted as which member and which kind; forgetting it has
 * it made again. */
enum { NOTHING_SHOWN = 0xFF };
static void forget_the_list(GameState *g) { set_menu_tech_list_shown(g, NOTHING_SHOWN); }

/* ---- The menus' sprite records ----------------------------------------------------------------- */

/* Each character's sprite is the record of its own number; the eighth
 * record (7) is the member whose list the screen shows, standing to the
 * list's right, and the ninth is the one the opening leaves empty. */
enum { LISTER = 7, LAST_RECORD = 8, RECORD_BYTES = 0x40, HIDDEN = 0xFF, SHOWN = 0, STILL = 0 };

/* The records' reset: the seven characters' each showing its own
 * character's sprite, still, kept hidden but animating until a row places
 * it (menu/sprites.h). */
static void reset_records(GameState *g) { TIMED_AS(g, 0xC2F566, menu_reset_character_records(g)); }

/* Where a character's record is, as the original works it out ($C2:F626,
 * with the multiplier) and notes it. */
static void note_record_offset(GameState *g, uint8_t character)
{
    hw_set_multiplicand(g, character);
    set_menu_record_offset(g, hw_multiply_by(g, RECORD_BYTES));
}

/* A record's place on the screen: its Y and its X (the X's high byte, which
 * says whether it is off the screen's left, stays). */
static void place_record(GameState *g, unsigned record, MenuSpritePlace at)
{
    set_menu_record_y(g, record, at.y);
    set_menu_record_x_low(g, record, at.x);
}

/* A member row's sprite stands beside its row, shown. */
enum { MEMBER_SPRITE_X = 0x1C };

// $C2:BAFC
static void place_member_sprite(GameState *g, uint8_t character, uint16_t row)
{
    ORIGINAL_EFFECT(note_record_offset(g, character));
    set_menu_record_sprite(g, character, character);
    set_menu_record_hidden(g, character, SHOWN);
    place_record(g, character, (MenuSpritePlace){ menu_tech_row_sprite_y(g, row), MEMBER_SPRITE_X });
}

/* ---- The member rows --------------------------------------------------------------------------- */

/* A member row: the member's face, name, HP and MP (a window list from the
 * drawing origin), its MP in the short colour where it has less than the
 * tech on the cursor needs. An empty row past the last member. */
enum { MEMBER_ROW = 0xC01F, EMPTY_MEMBER_ROW = 0xC030, MP_SHORT = 0x04, MP_ENOUGH = 0 };
enum { FIRST_MEMBER_ROW_AT = 0x2ECA, MEMBER_ROW_BYTES = 0x180 };

// $C2:BA2F
void menu_techs_draw_member_row(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC2A1EF, menu_note_member_face_and_hp(g));
    set_menu_member_marked_high(g, loaded_member_short_of(g, tech_cost_needed(g)) ? MP_SHORT : MP_ENOUGH);
    set_menu_text_attribute(g, 0);
    menu_draw_windows(g, pf, MEMBER_ROW);
}

static void start_drawing_rows(GameState *g)
{
    set_menu_fill_tile(g, 0);
    set_menu_text_attribute(g, 0);
}

// $C2:BA4F
/* The rows of the members the list shows from its first, as many as the
 * party can reach up to three, each with its sprite; then, where the party
 * has fewer, empty rows -- as many as the members drawn, as the original
 * counts them. The lister's record is hidden. */
void menu_techs_draw_member_rows(GameState *g, Platform *pf)
{
    reset_records(g);
    set_menu_record_hidden(g, LISTER, HIDDEN);
    uint8_t reachable = (uint8_t)menu_members_reachable(g);
    uint8_t shown = reachable > MEMBER_ROWS ? MEMBER_ROWS : reachable;
    uint8_t member = menu_tech_member_scroll(g);
    start_drawing_rows(g);
    uint16_t at = FIRST_MEMBER_ROW_AT;
    uint16_t row = 0;
    do {
        set_menu_draw_origin(g, at);
        if (!load_member(g, member))
            break;
        place_member_sprite(g, menu_status_record_character(g), row);
        clear_the_cost_needed(g);
        TIMED_AS(g, 0xC2BA2F, menu_techs_draw_member_row(g, pf));
        at = (uint16_t)(at + MEMBER_ROW_BYTES);
        member++;
        row++;
    } while (row < shown);
    menu_work_on_member(g, member);
    set_menu_tech_row_drawn(g, row);
    uint8_t empty = shown;
    if (MEMBER_ROWS - shown != 0)
        for (; empty != 0; empty--, at = (uint16_t)(at + MEMBER_ROW_BYTES)) {
            set_menu_draw_origin(g, at);
            menu_draw_windows(g, pf, EMPTY_MEMBER_ROW);
        }
    set_menu_tech_rows_to_draw(g, empty);
    set_menu_draw_origin(g, at);
    set_menu_scratch_list_rows(g, MEMBER_ROWS);
    set_menu_list_first_place(g, FIRST_MEMBER_PLACE);
}

/* The member list's scroll bar: twelve lines a member, its thumb at the
 * list's first member; its span (the frame's) in lines too. The original
 * multiplies with the multiplier, which it leaves with the span's. */
enum { LINES_A_MEMBER = 12, MEMBER_BAR_PLACE = 0x1278 };

static uint8_t in_lines(GameState *g, uint8_t members)
{
    ORIGINAL_EFFECT(hw_set_multiplicand(g, LINES_A_MEMBER));
    ORIGINAL_EFFECT((void)hw_multiply_by(g, members));
    return (uint8_t)(members * LINES_A_MEMBER);
}

void menu_techs_place_member_bar(GameState *g)
{
    set_menu_scroll_bar_thumb(g, in_lines(g, menu_tech_member_scroll(g)));
    set_menu_scroll_bar_place(g, MEMBER_BAR_PLACE);
    set_menu_scroll_bar_span(g, in_lines(g, menu_scroll_bar_span(g)));
}

/* ---- Opening the screen ------------------------------------------------------------------------ */

enum { SCREEN_WINDOWS = 0xBFDA, MEMBER_WINDOWS = 0xC042 };

// $C2:B35D, $C2:B979
void menu_techs_open(GameState *g, Platform *pf)
{
    next_step(g);
    menu_draw_windows(g, pf, SCREEN_WINDOWS);
    set_menu_cursor_index(g, member_place(menu_tech_member_row(g)));
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    forget_the_list(g);
    set_menu_description_stale(g, 0xFF);
    set_menu_line_effects(g, LINE_EFFECTS);
    menu_queue_fixed_upload(g, MENU_UPLOAD_LINE_TABLES);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
    menu_queue_fixed_upload(g, MENU_UPLOAD_TEXT_LAYER);
}

/* The member rows' bands of lines: each band an offset the front layer's
 * rows are shown from (the screen's offset-per-tile table), at rest where
 * they lie. */
enum { OFFSETS_THE_FRONT_LAYER = 0x2000 };
enum { MEMBER_LINES_AT_REST = OFFSETS_THE_FRONT_LAYER | 0x1FF, LINE_BANDS = MENU_TECH_MEMBER_LINES_COUNT };

static void show_member_rows_from(GameState *g, uint16_t line)
{
    for (unsigned band = 0; band < LINE_BANDS; band++) {
        set_menu_tech_member_lines(g, band, line);
        set_menu_tech_member_other_lines(g, band, line);
    }
}

static void put_member_lines_at_rest(GameState *g)
{
    show_member_rows_from(g, MEMBER_LINES_AT_REST);
    ORIGINAL_EFFECT(charge_block_move(g, 4 * LINE_BANDS));
}

/* The triple techs the rocks give (menu/techs.h) listed as the party has
 * learned them. */
void menu_techs_note_rock_techs(GameState *g)
{
    for (unsigned i = 0; i < MENU_ROCKS; i++)
        set_menu_tech_groups_listed(g, MENU_FIRST_ROCK_GROUP + i, party_triple_techs_learned(g, MENU_FIRST_ROCK_TECH + i));
    ORIGINAL_EFFECT(charge_block_move(g, MENU_ROCKS));
    put_member_lines_at_rest(g);
}

void menu_techs_show(GameState *g) { TIMED_AS(g, 0xC2E923, menu_clear_sprites_but_cursor(g)); }

// $C2:B365
void menu_techs_lay_out_members(GameState *g, Platform *pf)
{
    menu_draw_windows(g, pf, MEMBER_WINDOWS);
    next_step(g);
}

/* ---- The members ------------------------------------------------------------------------------- */

// $C2:B36D
TechsMemberChoice menu_techs_choose_member(const GameState *g)
{
    if (chose(g))
        return TECHS_MEMBER_CHOSEN;
    return went_back(g) ? TECHS_MEMBERS_LEFT : TECHS_MEMBER_POINTED_AT;
}

uint8_t menu_techs_note_list_shown(GameState *g)
{
    uint8_t before = menu_tech_list_shown(g);
    set_menu_tech_list_shown_before(g, before);
    return before;
}

bool menu_techs_list_changed(GameState *g, uint8_t before)
{
    uint8_t now = menu_tech_list_shown(g);
    if (now == before)
        return false;
    set_menu_tech_list_shown_before(g, now);
    return true;
}

// $C2:B520
/* Right moves to the next kind, left to the one before, round from the
 * triples to the members' own and back. (A byte past the kinds, as only a
 * random state holds, goes to the first or the last as the original's test
 * of its sign sends it.) */
enum { WRAPS_TO_THE_LAST = 0x80 };

void menu_techs_switch_kind(GameState *g)
{
    if (!menu_pushed(g, MENU_PUSHED_RIGHT) && !menu_pushed(g, MENU_PUSHED_LEFT))
        return;
    uint8_t kind = menu_tech_category(g);
    kind = menu_pushed(g, MENU_PUSHED_RIGHT) ? (uint8_t)(kind + 1) : (uint8_t)(kind - 1);
    if (kind >= MENU_TECH_KINDS)
        kind = ((uint8_t)(kind - MENU_TECH_KINDS) & WRAPS_TO_THE_LAST) ? MENU_TECH_KINDS - 1 : 0;
    set_menu_tech_category(g, kind);
}

/* The original's move within the list asks for a fast repeat when a button
 * is held; the members ask for none. */
bool menu_techs_push_slides_members(GameState *g)
{
    set_menu_fast_repeat(g, 0);
    return menu_list_scrolls(g) && menu_pushed(g, MENU_PUSHED_PAST_AN_EDGE);
}

/* ---- The list ---------------------------------------------------------------------------------- */

/* The list's rows: a tech's name each, two tile rows apart, from the list's
 * first; the row a slide brings in is drawn in the row past the last. */
enum { FIRST_LIST_ROW_AT = 0x3024, LIST_ROW_BYTES = 0x80, INCOMING_LIST_ROW_AT = 0x3424 };

// $C2:BD6B
void menu_techs_draw_list_rows(GameState *g, uint8_t from)
{
    bool waiting = loaded_member_waits(g);
    for (uint8_t row = 0; row < LIST_ROWS; row++) {
        uint8_t tech = menu_tech_list(g, (uint8_t)(from + row));
        set_menu_tech_pointed(g, tech);
        menu_draw_tech_row(g, (uint16_t)(FIRST_LIST_ROW_AT + row * LIST_ROW_BYTES), tech, waiting);
    }
    set_menu_draw_origin(g, FIRST_LIST_ROW_AT + LIST_ROWS * LIST_ROW_BYTES);
    ORIGINAL_EFFECT(set_menu_scratch_tech_list_place(g, (uint8_t)(from + LIST_ROWS)));
    ORIGINAL_EFFECT(set_menu_scratch_tech_rows_left(g, 0));
}

/* Which list it is: the member times sixteen, plus the kind. */
enum { MEMBERS_BY = 16 };

// $C2:BC6F
/* The member's list of the kind made and drawn, from its first tech, where
 * the member or the kind is another than the list was made for. */
void menu_techs_show_member_list(GameState *g)
{
    uint8_t member = member_on_the_cursor(g);
    uint8_t shown = (uint8_t)(member * MEMBERS_BY | menu_tech_category(g));
    bool another = shown != menu_tech_list_shown(g);
    set_menu_tech_list_shown(g, shown);
    if (!another)
        return;
    set_menu_tech_list_top(g, 0);
    set_menu_tech_list_row(g, 0);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    (void)load_member(g, member);
    menu_list_techs(g, menu_status_record_character(g), kind_listed(g));
    TIMED_AS(g, 0xC2BD6B, menu_techs_draw_list_rows(g, 0));
}

void menu_techs_point_at_member(GameState *g)
{
    set_menu_tech_member_row(g, (uint8_t)(menu_cursor_index(g) - FIRST_MEMBER_PLACE));
    TIMED_AS(g, 0xC2BC6F, menu_techs_show_member_list(g));
}

/* The kinds' tabs across the top, dimmed, and the chosen kind's in the
 * text's colours: ten tiles of two rows, two a tab, four tiles apart. */
enum { TABS_AT = 0x2EE4, TABS_TILES = 10, TAB_TILES = 2, TAB_BYTES = 8, DIMMED_TABS = 0x15, CHOSEN_TAB = 0x11 };

// $C2:B6AE
void menu_techs_mark_kind(GameState *g)
{
    TIMED_AS(g, 0xC2ECAC, menu_colour_row_pair(g, TABS_AT, TABS_TILES, DIMMED_TABS));
    uint8_t kind = menu_tech_category(g);
    uint16_t tab = (uint16_t)(TABS_AT + kind * TAB_BYTES);
    TIMED_AS(g, 0xC2ECAC, menu_colour_row_pair(g, tab, TAB_TILES, CHOSEN_TAB));
    ORIGINAL_EFFECT(set_menu_scratch_tech_tab_offset(g, (uint16_t)(kind * 2)));
    set_menu_draw_origin(g, tab);
}

/* ---- The member list's slide ------------------------------------------------------------------- */

/* A slide moves the view four lines a frame for twelve frames: the member
 * coming in is drawn above the rows for a slide up (its sprite on the row
 * above them, 3), below them for one down (4), and the rows are copied out
 * of view to sit beside it (menu_tech_slide_copy), where the view starts
 * (menu_tech_slide_line) and moves from. */
typedef struct {
    uint16_t sprite_row, copy_to, row_at, line;
    int16_t step;
} MemberSlide;

static const MemberSlide SLIDE_UP = { 3, 0x2605, 0x364A, OFFSETS_THE_FRONT_LAYER | 0x11F, -4 };
static const MemberSlide SLIDE_DOWN = { 4, 0x2005, 0x3ACA, OFFSETS_THE_FRONT_LAYER | 0x0EF, 4 };

static void set_up_member_slide(GameState *g, const MemberSlide *s)
{
    set_menu_tech_row_drawn(g, s->sprite_row);
    set_menu_tech_slide_copy(g, s->copy_to);
    set_menu_draw_origin(g, s->row_at);
    set_menu_scroll_step(g, (uint16_t)s->step);
    set_menu_tech_slide_line(g, s->line);
}

// $C2:B568
/* A push past the bottom slides a member on where the party has one past
 * the rows; past the top, one back where the list has slid. */
TechsMemberSlide menu_techs_slide_members(GameState *g)
{
    uint8_t scroll = menu_tech_member_scroll(g);
    if (menu_pushed(g, MENU_PUSHED_DOWN)) {
        if ((uint8_t)(scroll + MEMBER_ROWS) >= (uint8_t)menu_members_reachable(g))
            return NO_MEMBER_SLIDE;
        set_menu_tech_member_scroll(g, (uint8_t)(scroll + 1));
        set_up_member_slide(g, &SLIDE_DOWN);
        return MEMBERS_SLIDE_DOWN;
    }
    if ((int8_t)(scroll - 1) < 0)
        return NO_MEMBER_SLIDE;
    set_menu_tech_member_scroll(g, (uint8_t)(scroll - 1));
    set_up_member_slide(g, &SLIDE_UP);
    return MEMBERS_SLIDE_UP;
}

/* The rows sit on the front layer from column 5, row 2, ten tiles by
 * eighteen; a slide's copy back takes them from six rows off where they
 * were copied to. */
enum { MEMBER_LIST_LAYER = 0x2E00, SLIDE_FRAMES = 12, SLID_ROWS = 0x0600, LAST_ROW_PASSED = 0xFF };
static const MenuTilePlace MEMBER_ROWS_PLACE = { 5, 2 };
static const MenuWindowSize MEMBER_ROWS_SIZE = { 10, 18 };

static MenuTilePlace place_named(uint16_t place) { return (MenuTilePlace){ (uint8_t)place, (uint8_t)(place >> 8) }; }

// $C2:B5C8
void menu_techs_start_member_slide(GameState *g, Platform *pf)
{
    set_menu_line_effects(g, SLIDING_LINE_EFFECTS);
    (void)load_member(g, member_on_the_cursor(g));
    place_member_sprite(g, menu_status_record_character(g), menu_tech_row_drawn(g));
    clear_the_cost_needed(g);
    TIMED_AS(g, 0xC2BA2F, menu_techs_draw_member_row(g, pf));
    set_menu_draw_origin(g, MEMBER_LIST_LAYER);
    menu_copy_tile_area(g, MEMBER_ROWS_PLACE, place_named(menu_tech_slide_copy(g)), MEMBER_ROWS_SIZE);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER_LOWER_HALF);
    set_menu_scroll_moved(g, 0);
    set_menu_scroll_frames(g, SLIDE_FRAMES);
}

/* The records' sprites move with the rows; the thumb a line the way they
 * go (the original takes the way from whether the view's line carried as it
 * moved, which for the lines a slide starts from is the step's sign). */
static uint16_t records_origin_y(const GameState *g) { return (uint16_t)(menu_record_origin_y(g) | menu_record_origin_y_high(g) << 8); }

static void set_records_origin_y(GameState *g, uint16_t y)
{
    set_menu_record_origin_y(g, (uint8_t)y);
    set_menu_record_origin_y_high(g, (uint8_t)(y >> 8));
}

bool menu_techs_slide_members_a_frame(GameState *g)
{
    uint16_t step = menu_scroll_step(g);
    set_records_origin_y(g, (uint16_t)(records_origin_y(g) - step));
    set_menu_scroll_moved(g, (uint16_t)(menu_scroll_moved(g) + step));
    uint32_t line = (uint32_t)menu_tech_slide_line(g) + step;
    set_menu_tech_slide_line(g, (uint16_t)line);
    uint16_t thumb = menu_scroll_bar_thumb_word(g);
    set_menu_scroll_bar_thumb_word(g, (uint16_t)(line > 0xFFFF ? thumb - 1 : thumb + 1));
    show_member_rows_from(g, (uint16_t)line);
    menu_queue_fixed_upload(g, MENU_UPLOAD_LINE_TABLES);
    uint16_t frames = (uint16_t)(menu_scroll_frames(g) - 1);
    set_menu_scroll_frames(g, frames);
    return frames != 0;
}

/* The slide's end asks the input for a fast repeat on the next frame, so a
 * push held through it carries straight on. */
static void ask_for_a_fast_repeat(GameState *g) { set_menu_fast_repeat(g, (uint8_t)(menu_fast_repeat(g) - 1)); }

void menu_techs_finish_member_slide(GameState *g)
{
    MenuTilePlace slid = place_named((uint16_t)(menu_tech_slide_copy(g) ^ SLID_ROWS));
    menu_copy_tile_area(g, slid, MEMBER_ROWS_PLACE, MEMBER_ROWS_SIZE);
    put_member_lines_at_rest(g);
    menu_queue_fixed_upload(g, MENU_UPLOAD_LINE_TABLES);
    set_records_origin_y(g, 0);
    reset_records(g);
    uint8_t scroll = menu_tech_member_scroll(g);
    for (int row = MEMBER_ROWS - 1; row >= 0; row--) {
        (void)load_member(g, (uint8_t)(scroll + row));
        place_member_sprite(g, menu_status_record_character(g), (uint16_t)row);
    }
    menu_work_on_member(g, (uint8_t)(scroll - 1));
    set_menu_tech_row_drawn(g, LAST_ROW_PASSED);
    set_menu_line_effects(g, LINE_EFFECTS);
    ask_for_a_fast_repeat(g);
}

/* ---- Into the list ----------------------------------------------------------------------------- */

bool menu_techs_member_has_techs(const GameState *g) { return menu_techs_listed(g) != 0; }

/* Into the list: the cursor on the row it was left on, the description asked
 * for again (the original counts the request down from where it stands). */
void menu_techs_go_to_the_list(GameState *g)
{
    set_menu_cursor_index(g, menu_tech_list_row(g));
    next_step(g);
    set_menu_description_stale(g, (uint8_t)(menu_description_stale(g) - 1));
}

/* The member whose list it is, standing still to the list's right in the
 * eighth record, in the colours of the character's own record. */
enum { LISTER_BLOCKS = 2 };
static const MenuSpritePlace LISTER_PLACE = { 0x38, 0x1C };

// $C2:B9E1
void menu_techs_show_the_lister(GameState *g)
{
    set_menu_record_block_limit(g, LISTER_BLOCKS);
    (void)load_member(g, member_on_the_cursor(g));
    set_menu_record_animation(g, LISTER, STILL);
    set_menu_record_sprite_set(g, LISTER, 0);
    set_menu_record_hidden(g, LISTER, SHOWN);
    place_record(g, LISTER, LISTER_PLACE);
}

// $C2:BA09
/* Whether the record must load the character's sprite: it shows another. */
bool menu_techs_put_the_lister_on_its_record(GameState *g)
{
    uint8_t character = menu_status_record_character(g);
    ORIGINAL_EFFECT(note_record_offset(g, character));
    set_menu_record_colours(g, LISTER, menu_record_colours(g, character));
    if (menu_record_sprite(g, LISTER) == character)
        return false;
    set_menu_record_sprite(g, LISTER, character);
    return true;
}

/* The list's scroll bar: four lines a row (the multiplier's, $C2:8BA6). */
enum { LINES_A_ROW = 4, LIST_BAR_PLACE = 0x32E8 };

static uint8_t thumb_at_row(GameState *g, uint8_t row)
{
    ORIGINAL_EFFECT(hw_set_multiplicand(g, row));
    ORIGINAL_EFFECT((void)hw_multiply_by(g, LINES_A_ROW));
    return (uint8_t)(row * LINES_A_ROW);
}

// $C2:BB50
void menu_techs_frame_the_list(GameState *g)
{
    set_menu_scroll_bar_thumb(g, thumb_at_row(g, menu_tech_list_top(g)));
    set_menu_list_first_place(g, 0);
    set_menu_scratch_list_rows(g, LIST_ROWS);
}

/* ---- The tech on the cursor -------------------------------------------------------------------- */

/* Its members: the member whose list it is, then the group's others in the
 * characters' order. */
enum { NOBODY = 0xFF, FIRST_CHARACTER_BIT = 0x80 };

// $C2:BBD6
static void note_the_pointed_tech(GameState *g)
{
    set_menu_tech_members(g, 1, NOBODY);
    set_menu_tech_members(g, 2, NOBODY);
    reset_records(g);
    uint8_t at = (uint8_t)(menu_tech_list_row(g) + menu_tech_list_top(g));
    set_menu_tech_pointed(g, menu_tech_list(g, at));
    uint8_t others = menu_tech_group_members(g, menu_tech_list_groups(g, at));
    uint8_t character = menu_members(g, member_on_the_cursor(g));
    set_menu_tech_members(g, 0, character);
    others = (uint8_t)(others & ~menu_character_bit(g, character));
    uint8_t looked_at = 0;
    unsigned partner = 1;
    do {
        if (others & FIRST_CHARACTER_BIT)
            set_menu_tech_members(g, partner++, looked_at);
        others = (uint8_t)(others << 1);
        looked_at++;
    } while (others != 0);
    ORIGINAL_EFFECT(set_menu_scratch_tech_partner_bits(g, 0));
    ORIGINAL_EFFECT(set_menu_scratch_tech_partners_counted(g, looked_at));
}

// $C2:BB8C
/* The tech's members' rows from `row` on: each loaded, its sprite placed,
 * what the tech costs it worked out, and its row; an empty row where the
 * tech has no such member. */
static void draw_tech_members(GameState *g, Platform *pf, uint8_t row, uint16_t at)
{
    ORIGINAL_EFFECT(set_menu_tech_row_partners(g, (uint16_t)(menu_tech_members(g, 1) | menu_tech_members(g, 2) << 8)));
    start_drawing_rows(g);
    for (; row < MEMBER_ROWS; row++, at = (uint16_t)(at + MEMBER_ROW_BYTES)) {
        set_menu_draw_origin(g, at);
        set_menu_tech_row_drawn(g, row);
        uint8_t character = menu_tech_members(g, row);
        if (character & FIRST_CHARACTER_BIT) {
            menu_draw_windows(g, pf, EMPTY_MEMBER_ROW);
            continue;
        }
        TIMED_AS(g, 0xC28820, (void)menu_load_character(g, character));
        place_member_sprite(g, menu_status_record_character(g), row);
        MenuTechCost c = menu_tech_cost_for(g, menu_tech_pointed(g), kind_listed(g));
        ORIGINAL_EFFECT(set_menu_tech_part_costs(g, row, (uint8_t)c.part));
        TIMED_AS(g, 0xC2BA2F, menu_techs_draw_member_row(g, pf));
    }
    set_menu_screen_argument(g, MEMBER_ROWS);
    set_menu_draw_origin(g, at);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

void menu_techs_show_the_list(GameState *g, Platform *pf)
{
    set_menu_scroll_bar_span(g, thumb_at_row(g, menu_scroll_bar_span(g)));
    set_menu_scroll_bar_place(g, LIST_BAR_PLACE);
    note_the_pointed_tech(g);
    set_menu_screen_argument(g, 0);
    draw_tech_members(g, pf, 0, FIRST_MEMBER_ROW_AT);
}

/* The tech's cost to the member, beside the list, in the short colour where
 * the member has less MP: two digits. */
enum { COST_AT = 0x2F9A, COST_FORMAT = 0x19 };

// $C2:BB1F
void menu_techs_show_the_pointed_tech(GameState *g, Platform *pf)
{
    note_the_pointed_tech(g);
    MenuTechCost c = menu_tech_cost_for(g, menu_tech_pointed(g), kind_listed(g));
    ORIGINAL_EFFECT(set_menu_tech_part_costs(g, 0, (uint8_t)c.part));
    set_menu_text_attribute(g, loaded_member_short_of(g, c.mp) ? MP_SHORT : MP_ENOUGH);
    TIMED_AS(g, 0xC2F114, (void)menu_draw_number_value(g, tech_cost_needed(g), COST_FORMAT, COST_AT));
    set_menu_screen_argument(g, 1);
    draw_tech_members(g, pf, 1, (uint16_t)(FIRST_MEMBER_ROW_AT + MEMBER_ROW_BYTES));
}

/* A single tech still being learned has a window saying so. */
enum { LEARNING_WINDOW = 0xC026 };

// $C2:BDEB
void menu_techs_show_learning(GameState *g, Platform *pf)
{
    if (kind_listed(g) == MENU_SINGLE_TECHS && menu_tech_being_learned(g, menu_tech_pointed(g)))
        menu_draw_windows(g, pf, LEARNING_WINDOW);
}

/* ---- Pointing along the list ------------------------------------------------------------------- */

// $C2:B3AE
void menu_techs_hold_line_effects(GameState *g) { set_menu_line_effects(g, LINE_EFFECTS); }

TechsListChoice menu_techs_choose_on_the_list(const GameState *g)
{
    if (chose(g))
        return TECH_CHOSEN;
    return went_back(g) ? TECHS_LEFT : TECH_POINTED_AT;
}

// $C2:B6D3
/* The member on the cursor loaded, and the list's first row, to page. */
uint8_t menu_techs_list_top_for_paging(GameState *g)
{
    (void)load_member(g, member_on_the_cursor(g));
    return menu_tech_list_top(g);
}

/* The paging put the thumb at the new first row; it is drawn four lines a
 * row, and the list's rows drawn again from there. */
void menu_techs_turn_page(GameState *g, uint8_t top)
{
    set_menu_scroll_bar_thumb(g, thumb_at_row(g, menu_scroll_bar_thumb(g)));
    set_menu_tech_list_top(g, top);
    ORIGINAL_EFFECT(set_menu_scratch_tech_list_place_high(g, 0));
    TIMED_AS(g, 0xC2BD6B, menu_techs_draw_list_rows(g, top));
}

// $C2:B72F
/* A push past the top row slides the list back a row where it has slid on;
 * past the bottom, on a row where it has techs past its rows. */
bool menu_techs_list_slide_brings_a_tech(const GameState *g)
{
    uint8_t top = menu_tech_list_top(g);
    if (menu_cursor_index(g) == 0)
        return top != 0;
    return top < menu_list_scroll_most(g);
}

/* The slide moves the list's nine rows (with the one coming in), and the
 * per-line table its block move brings their lines in from. */
enum { LIST_LINE_TABLE = 0x9303, LIST_LINE_TABLE_BYTES = 0x22, LIST_ROWS_SLID = 9 };

void menu_techs_set_up_list_slide(GameState *g)
{
    set_menu_block_move_dest(g, LIST_LINE_TABLE);
    set_menu_block_move_size(g, LIST_LINE_TABLE_BYTES);
    set_menu_slide_rows(g, LIST_ROWS_SLID);
}

/* The list's first row a row on (or back), the tech now on the cursor
 * drawn in the row past the last. */
void menu_techs_bring_in_a_tech(GameState *g)
{
    uint8_t top = menu_tech_list_top(g);
    top = menu_pushed(g, MENU_PUSHED_DOWN) ? (uint8_t)(top + 1) : (uint8_t)(top - 1);
    set_menu_tech_list_top(g, top);
    uint8_t tech = menu_tech_list(g, (uint8_t)(menu_cursor_index(g) + top));
    set_menu_tech_pointed(g, tech);
    set_menu_draw_origin(g, INCOMING_LIST_ROW_AT);
    menu_draw_tech_row(g, INCOMING_LIST_ROW_AT, tech, loaded_member_waits(g));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

/* A held push repeating fast skips the slide's frames. */
enum { REPEATING_FAST = 0x80 };

bool menu_techs_list_slide_takes_frames(GameState *g)
{
    if (menu_fast_repeat(g) & REPEATING_FAST)
        return false;
    set_menu_scroll_moved(g, 0);
    return true;
}

/* A frame: the rows a step on, the thumb a line the way they went. Whether
 * frames are left. */
bool menu_techs_slide_list_a_frame(GameState *g)
{
    uint16_t moved = (uint16_t)(menu_scroll_moved(g) + menu_scroll_step(g));
    set_menu_scroll_moved(g, moved);
    uint16_t thumb = menu_scroll_bar_thumb_word(g);
    set_menu_scroll_bar_thumb_word(g, (uint16_t)((int16_t)moved < 0 ? thumb - 1 : thumb + 1));
    uint16_t frames = (uint16_t)(menu_scroll_frames(g) - 1);
    set_menu_scroll_frames(g, frames);
    return frames != 0;
}

/* The list's names sit in a column of the front layer from tile 18: once
 * slid, each name's two tile rows move a row along. Down the list, the
 * sixteen tile rows from the second row's move up a row (the incoming row
 * with them). Up the list, the first seven rows' fourteen move down a row,
 * the bottom first, and the incoming row is copied to the first; the
 * original's copies down start a byte left of the names' tiles (taking the
 * attribute of the tile before and leaving the last tile's), which the port
 * keeps. */
enum { NAME_COLUMN_BYTES = 0x16, TILE_ROW_BYTES = 0x40, ROWS_UP = 16, ROWS_DOWN = 14 };
enum { SECOND_ROW_TOP = 0x3064, FIRST_ROW_TOP = 0x2FE4, SEVENTH_ROW_END = 0x3338, EIGHTH_ROW_END = 0x33B8 };
enum { INCOMING_ROWS = 0x33C0, FIRST_ROWS = 0x2FC0, INCOMING_ROWS_BYTES = 0x80 };
enum { WORK_RAM = 0x7E0000 };

static void leave_rows_copied_up(GameState *g)
{
    set_menu_window_place(g, (uint16_t)(SECOND_ROW_TOP + ROWS_UP * TILE_ROW_BYTES));
    set_menu_line_start(g, (uint16_t)(FIRST_ROW_TOP + ROWS_UP * TILE_ROW_BYTES));
    set_menu_text_work_window_rows(g, 0);
    set_menu_text_work_window_columns(g, NAME_COLUMN_BYTES - 1);
}

static void leave_rows_copied_down(GameState *g)
{
    set_menu_scratch_tech_rows_from(g, (uint16_t)(SEVENTH_ROW_END - ROWS_DOWN * TILE_ROW_BYTES));
    set_menu_scratch_tech_rows_to(g, (uint16_t)(EIGHTH_ROW_END - ROWS_DOWN * TILE_ROW_BYTES));
    set_menu_scratch_tech_rows_still(g, 0);
}

// $C2:B7CC
static void move_list_rows(GameState *g, bool up)
{
    if (!up) {
        for (unsigned r = 0; r < ROWS_UP; r++)
            wram_copy(g, WORK_RAM | (uint16_t)(SECOND_ROW_TOP + r * TILE_ROW_BYTES),
                      WORK_RAM | (uint16_t)(FIRST_ROW_TOP + r * TILE_ROW_BYTES), NAME_COLUMN_BYTES);
        ORIGINAL_EFFECT(charge_block_move(g, ROWS_UP * NAME_COLUMN_BYTES));
        ORIGINAL_EFFECT(leave_rows_copied_up(g));
        return;
    }
    for (unsigned r = 0; r < ROWS_DOWN; r++) {
        uint16_t from_end = (uint16_t)(SEVENTH_ROW_END - r * TILE_ROW_BYTES), to_end = (uint16_t)(EIGHTH_ROW_END - r * TILE_ROW_BYTES);
        wram_copy(g, WORK_RAM | (uint16_t)(from_end - (NAME_COLUMN_BYTES - 1)), WORK_RAM | (uint16_t)(to_end - (NAME_COLUMN_BYTES - 1)),
                  NAME_COLUMN_BYTES);
    }
    wram_copy(g, WORK_RAM | INCOMING_ROWS, WORK_RAM | FIRST_ROWS, INCOMING_ROWS_BYTES);
    ORIGINAL_EFFECT(charge_block_move(g, ROWS_DOWN * NAME_COLUMN_BYTES + INCOMING_ROWS_BYTES));
    ORIGINAL_EFFECT(leave_rows_copied_down(g));
}

/* Once slid: the rows moved in their buffer, the list at rest, the upload
 * queue emptied (the frames sent it), the thumb at the new first row, and
 * the block move set to bring the rows' new lines in. */
enum { LIST_LINES_FROM = 0x9710, LINE_EFFECTS_MOVED = 0x80 };

void menu_techs_finish_list_slide(GameState *g)
{
    move_list_rows(g, menu_list_slid_up(g));
    set_menu_scroll_step(g, 0);
    set_menu_vram_queue_bytes(g, 0);
    set_menu_scroll_bar_thumb(g, thumb_at_row(g, menu_tech_list_top(g)));
    set_menu_block_move_source(g, LIST_LINES_FROM);
    set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) | LINE_EFFECTS_MOVED));
}

/* The cursor's row kept: whether the cursor moved this frame. */
bool menu_techs_keep_the_cursors_row(GameState *g)
{
    set_menu_tech_list_row(g, menu_cursor_index(g));
    return menu_cursor_index(g) != menu_cursor_frame_start(g);
}

/* ---- The description --------------------------------------------------------------------------- */

/* A tech's description is set a glyph a frame on the description's row;
 * a kind's caption ten glyphs a frame, further along it. Both are strings
 * of the techs' table (the captions after the techs, menu_data.h). */
enum { TECH_LINE = 0x5344, KIND_LINE = 0x5356, TECH_PACE = 1, CAPTION_PACE = 0x0A };
enum { DESCRIPTION_TILES = 0x6AA0, DESCRIPTION_SPARE = 0x0F75, DESCRIPTION_ROW_BYTES = 0x80, STALE = 0x80 };

bool menu_techs_description_shows_another(const GameState *g, uint8_t which)
{
    return (menu_description_stale(g) & STALE) || which != menu_description_item(g);
}

// $C2:BE44
static bool ask_for_description(GameState *g, uint16_t line, uint8_t pace, uint8_t which)
{
    set_menu_description_line(g, line);
    set_menu_description_pace(g, pace);
    if (!menu_techs_description_shows_another(g, which))
        return false;
    set_menu_description_item(g, which);
    set_menu_text_string(g, which);
    set_menu_description_stale(g, 0);
    ORIGINAL_EFFECT(set_menu_description_tiles(g, DESCRIPTION_TILES));
    ORIGINAL_EFFECT(set_menu_tech_description_spare(g, DESCRIPTION_SPARE));
    set_menu_text_strings(g, (uint32_t)MENU_TECH_DESCRIPTIONS_BANK << 16 | MENU_TECH_DESCRIPTIONS);
    set_menu_description_clear_bytes(g, DESCRIPTION_ROW_BYTES);
    return true;
}

// $C2:BE33
bool menu_techs_describe_tech(GameState *g) { return ask_for_description(g, TECH_LINE, TECH_PACE, menu_tech_pointed(g)); }

// $C2:BE16
bool menu_techs_describe_kind(GameState *g)
{
    set_menu_line_effects(g, LINE_EFFECTS);
    return ask_for_description(g, KIND_LINE, CAPTION_PACE, (uint8_t)(menu_tech_category(g) + MENU_TECH_KIND_CAPTIONS));
}

/* ---- Choosing a tech --------------------------------------------------------------------------- */

void menu_techs_back_to_the_members(GameState *g)
{
    step_back(g);
    reset_records(g);
    set_menu_record_hidden(g, LAST_RECORD, HIDDEN);
    set_menu_cursor_index(g, member_place(menu_tech_member_row(g)));
    set_menu_tech_pointed(g, 0);
}

/* The original finds the tech's effect record with the multiplier as it
 * looks at the tech, and keeps its offset once it heals. */
enum { TECH_RECORD_BYTES = MENU_TECH_EFFECT_BYTES };

static void leave_record_looked_up(GameState *g, uint8_t tech)
{
    hw_set_multiplicand(g, TECH_RECORD_BYTES);
    (void)hw_multiply_by(g, tech);
}

enum { CHOSEN_SPARE = 0xFF };

TechsUse menu_techs_use(GameState *g)
{
    set_menu_cursor_index(g, menu_tech_list_row(g));
    if (loaded_member_waits(g))
        return TECH_REFUSED_WAITING;
    uint8_t tech = menu_tech_pointed(g);
    ORIGINAL_EFFECT(leave_record_looked_up(g, tech));
    if (!menu_tech_heals(g, tech))
        return TECH_REFUSED_NOT_HEALING;
    ORIGINAL_EFFECT(set_menu_scratch_tech_record_offset(g, (uint16_t)(tech * TECH_RECORD_BYTES)));
    uint8_t user = member_on_the_cursor(g);
    set_menu_tech_user(g, user);
    (void)load_member(g, user);
    ORIGINAL_EFFECT(set_menu_tech_spare(g, CHOSEN_SPARE));
    MenuTechCost c = menu_tech_cost_for(g, tech, kind_listed(g));
    set_menu_tech_cost(g, c.mp);
    return loaded_member_short_of(g, c.mp) ? TECH_REFUSED_SHORT_OF_MP : TECH_USED;
}

/* The menus' tasks a tech's use stops and starts again: the description's
 * (held a frame longer), the scroll bar's. */
enum { DESCRIPTION_TASK = 2, SCROLL_BAR_TASK = 4, TASK_STOPPED = 0, TASK_STARTING = 1 };

// $C2:B43A
/* The restoring's figures from the tech, its strength the user's magic,
 * the scroll bar's task stopped, and the cursor on the window's first
 * member, the second cursor with it. */
void menu_techs_choose_targets(GameState *g)
{
    MenuTechHealing h = menu_tech_healing(g, menu_tech_pointed(g));
    set_menu_use_effect(g, h.effect);
    set_menu_heal_factor(g, h.factor);
    set_menu_tasks_state(g, SCROLL_BAR_TASK, TASK_STOPPED);
    set_menu_heal_power(g, menu_status_record_figures_byte(g, MAGIC_FIGURE));
    next_step(g);
    set_menu_cursor_index(g, FIRST_TARGET_PLACE);
    set_menu_held_cursor(g, FIRST_TARGET_PLACE);
    set_menu_use_first_member(g, FIRST_TARGET_PLACE);
    set_menu_use_member(g, 0);
}

bool menu_techs_heal_every_member(const GameState *g) { return (menu_use_effect(g) & MENU_USE_FOR_EVERY_MEMBER) != 0; }

void menu_techs_hold_the_description(GameState *g)
{
    set_menu_tasks_countdown(g, DESCRIPTION_TASK, (uint8_t)(menu_tasks_countdown(g, DESCRIPTION_TASK) + 1));
}

/* ---- The window a tech is used from ------------------------------------------------------------ */

// $C2:BE79
/* The user's row at the top of the member rows, what the tech costs it
 * noted. */
void menu_techs_open_targets(GameState *g, Platform *pf)
{
    uint8_t user = member_on_the_cursor(g);
    set_menu_screen_argument(g, user);
    TIMED_AS(g, 0xC28820, (void)menu_load_member(g, user));
    (void)menu_tech_cost_for(g, menu_tech_pointed(g), kind_listed(g));
    start_drawing_rows(g);
    set_menu_draw_origin(g, FIRST_MEMBER_ROW_AT);
    TIMED_AS(g, 0xC2BA2F, menu_techs_draw_member_row(g, pf));
}

/* The window's rows: each active member's, a tile row a member apart, its
 * sprite beside it acting hurt where its HP is down to its mark (the mark of
 * the character whose number is the member's place, as the original reads
 * it). */
enum { TARGET_ROW = 0xC056, FIRST_TARGET_ROW_AT = 0x3662, TARGET_ROW_BYTES = 0x100, HURT = 0x12 };

// $C2:BEDC
static void draw_targets(GameState *g, Platform *pf)
{
    uint16_t at = FIRST_TARGET_ROW_AT;
    set_menu_screen_argument(g, 0);
    uint8_t member = 0;
    do {
        set_menu_draw_origin(g, at);
        menu_work_on_member(g, member);
        bool loaded = false;
        TIMED_AS(g, 0xC28820, loaded = menu_load_member(g, member));
        if (!loaded)
            return;
        TIMED_AS(g, 0xC2A1EF, menu_note_member_face_and_hp(g));
        menu_draw_windows(g, pf, TARGET_ROW);
        uint8_t character = menu_status_record_character(g);
        ORIGINAL_EFFECT(note_record_offset(g, character));
        place_record(g, character, menu_tech_target_sprite_place(g, member));
        at = (uint16_t)(at + TARGET_ROW_BYTES);
        bool hurt = menu_low_hp_marks(g, member) >= menu_status_record_hp(g);
        set_menu_record_hidden(g, character, SHOWN);
        set_menu_record_animation(g, character, hurt ? HURT : STILL);
        member++;
    } while (member != menu_active_members(g));
    menu_work_on_member(g, member);
    set_menu_draw_origin(g, at);
}

/* A frame later: the window's line table, its members, and its tilemaps
 * sent. */
enum { TARGET_LINES = OFFSETS_THE_FRONT_LAYER | 0x0C7, TARGET_BLOCKS = 3, UPLOAD_TARGET_ROWS = 0xBECE, UPLOAD_TARGET_WINDOW = 0xBED5 };

void menu_techs_show_targets(GameState *g, Platform *pf)
{
    for (unsigned band = 0; band < MENU_TECH_TARGET_LINES_COUNT; band++)
        set_menu_tech_target_lines(g, band, TARGET_LINES);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * MENU_TECH_TARGET_LINES_COUNT));
    set_menu_record_block_limit(g, TARGET_BLOCKS);
    draw_targets(g, pf);
    set_menu_use_helps(g, 0);
    menu_queue_fixed_upload(g, UPLOAD_TARGET_ROWS);
    menu_queue_fixed_upload(g, UPLOAD_TARGET_WINDOW);
    menu_queue_fixed_upload(g, MENU_UPLOAD_LINE_TABLES);
}

// $C2:B48E
TechsTargetChoice menu_techs_choose_target(const GameState *g)
{
    if (chose(g))
        return TARGET_CHOSEN;
    return went_back(g) ? TARGETS_LEFT : TARGET_POINTED_AT;
}

// $C2:B823
void menu_techs_check_the_users_mp(GameState *g)
{
    (void)load_member(g, menu_tech_user(g));
    if (loaded_member_short_of(g, menu_tech_cost(g)))
        set_menu_use_helps(g, 0);
}

bool menu_techs_use_helps(const GameState *g) { return menu_use_helps(g) != 0; }

/* The user's casting pose: an animation looping on the character's frame. */
enum { CASTING = 0x90 };

// $C2:B4DE
void menu_techs_cast(GameState *g)
{
    set_menu_record_animation(g, LISTER, CASTING);
    set_menu_record_loop_frame(g, LISTER, menu_tech_casting_frame(g, menu_record_sprite(g, LISTER)));
}

/* The cost taken from the user's MP, a byte as the original subtracts it;
 * the MP it had. */
uint8_t menu_techs_after_casting(GameState *g)
{
    set_menu_record_animation(g, LISTER, STILL);
    (void)load_member(g, menu_tech_user(g));
    uint8_t character = menu_member_character(g);
    uint8_t mp = character_records_record_byte(g, character, MP_AT);
    set_character_records_record_byte(g, character, MP_AT, (uint8_t)(mp - menu_tech_cost(g)));
    return mp;
}

// $C2:B49C
/* Back to the list: the cursor on its row, the records reset, the lister
 * loaded again, the scroll bar's task started again. */
void menu_techs_back_from_the_targets(GameState *g)
{
    set_menu_cursor_index(g, menu_tech_list_row(g));
    step_back(g);
    reset_records(g);
    (void)load_member(g, member_on_the_cursor(g));
    set_menu_record_block_limit(g, LISTER_BLOCKS);
    set_menu_tasks_state(g, SCROLL_BAR_TASK, (uint8_t)(menu_tasks_state(g, SCROLL_BAR_TASK) + TASK_STARTING));
}

void menu_techs_close_targets(GameState *g)
{
    for (unsigned band = 0; band < MENU_TECH_TARGET_LINES_COUNT; band++)
        set_menu_tech_target_lines(g, band, 0);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * MENU_TECH_TARGET_LINES_COUNT));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    menu_queue_fixed_upload(g, MENU_UPLOAD_LINE_TABLES);
}

/* ---- The members a restoring is used on -------------------------------------------------------- */

bool menu_restoring_every_member(const GameState *g) { return (menu_use_effect(g) & MENU_USE_FOR_EVERY_MEMBER) != 0; }

// $C2:B83E
void menu_restoring_take_the_cursor_round(GameState *g)
{
    if (!menu_restoring_every_member(g))
        menu_items_take_the_cursor_round(g);
}

// $C2:B870
bool menu_restoring_point_at_member(GameState *g) { return menu_items_point_at_a_member(g); }

/* It helps a member short of the most HP where it restores HP, or of the
 * most MP where it restores MP (the MP's low bytes compared): the bits of
 * menu_use_helps. One that heals every member looks at each active member
 * until one it helps. */
enum { HELPS_HP = 0x02, HELPS_MP = 0x01 };

void menu_restoring_look_for_help(GameState *g)
{
    uint8_t members = 1, member = menu_use_member(g);
    if (menu_restoring_every_member(g)) {
        members = menu_active_members(g);
        member = 0;
        menu_work_on_member(g, member);
    }
    uint8_t effect = menu_use_effect(g), helps = 0;
    do {
        TIMED_AS(g, 0xC28820, (void)menu_load_member(g, member));
        uint8_t character = menu_member_character(g);
        helps = 0;
        if ((effect & MENU_USE_RESTORES_HP) && character_records_hp(g, character) < character_records_hp_cap(g, character))
            helps |= HELPS_HP;
        if ((effect & MENU_USE_RESTORES_MP)
            && character_records_record_byte(g, character, MP_AT) < (uint8_t)character_records_max_mp(g, character))
            helps |= HELPS_MP;
        set_menu_use_helps(g, helps);
        if (helps)
            break;
        member++;
        menu_work_on_member(g, member);
    } while (--members != 0);
    ORIGINAL_EFFECT(set_menu_scratch_restoring_members_left(g, members));
}

/* ---- The main menu's panel --------------------------------------------------------------------- */

enum { PANEL_WINDOWS = 0xBFA0, PANEL_AWAY_WINDOWS = 0xC037, PANEL_LINE_EFFECTS = 0x01 };

// $C2:B8DA
/* The panel's windows, the cursor's member brought onto an active one. */
void menu_techs_panel_lay_out(GameState *g, Platform *pf)
{
    menu_draw_windows(g, pf, PANEL_WINDOWS);
    uint8_t row = menu_tech_member_row(g);
    set_menu_tech_member_row(g, row < menu_active_members(g) ? row : 0);
}

/* The tabs, and the panel's colours: the highlight's four twice into palette
 * row 2, the equipment's four twice into row 3's first half, then eight
 * into row 5's and eight into row 4's. */
typedef struct {
    uint16_t from;
    uint8_t first, count;
} PanelColours;

static const PanelColours PANEL_COLOURS[] = {
    { MENU_ROM_ITEMS_HIGHLIGHT_COLOURS, 0x20, 4 }, { MENU_ROM_ITEMS_HIGHLIGHT_COLOURS, 0x24, 4 },
    { MENU_ROM_EQUIPMENT_COLOURS, 0x30, 4 },       { MENU_ROM_EQUIPMENT_COLOURS, 0x34, 4 },
    { MENU_ROM_TECH_PANEL_COLOURS, 0x50, 8 },      { MENU_ROM_TECH_PANEL_LIST_COLOURS, 0x40, 8 },
};
enum { PANEL_COLOUR_RUNS = sizeof PANEL_COLOURS / sizeof PANEL_COLOURS[0] };

void menu_techs_panel_show(GameState *g, Platform *pf)
{
    menu_techs_mark_kind(g);
    for (unsigned run = 0; run < PANEL_COLOUR_RUNS; run++) {
        const PanelColours *c = &PANEL_COLOURS[run];
        for (unsigned i = 0; i < c->count; i++)
            set_menu_palette(g, c->first + i, menu_rom_colour(g, c->from, i));
        ORIGINAL_EFFECT(charge_block_move(g, 2u * c->count));
    }
    set_menu_line_effects(g, PANEL_LINE_EFFECTS);
    TIMED_AS(g, 0xC286DD, menu_build_math_table(g, pf));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
}

// $C2:B960
void menu_techs_panel_hold(GameState *g) { set_menu_line_effects(g, PANEL_LINE_EFFECTS); }

void menu_techs_panel_put_away(GameState *g, Platform *pf)
{
    menu_draw_windows(g, pf, PANEL_AWAY_WINDOWS);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

/* ---- What the steps can run on (the guards') --------------------------------------------------- */

/* The members the rows show: the list slid no further than the reachable
 * members, each a character with a record, and a sprite record of its own. */
bool menu_techs_members_are_drawable(const GameState *g)
{
    uint8_t reachable = (uint8_t)menu_members_reachable(g), scroll = menu_tech_member_scroll(g);
    uint8_t shown = reachable > MEMBER_ROWS ? MEMBER_ROWS : reachable;
    if (reachable == 0 || reachable > CHARACTER_RECORDS_COUNT || menu_tech_member_row(g) >= shown
        || menu_active_members(g) == 0 || menu_active_members(g) > MEMBER_ROWS || scroll + shown > reachable)
        return false;
    for (unsigned member = 0; member < reachable; member++)
        if (menu_members(g, member) >= CHARACTER_RECORDS_COUNT)
            return false;
    return true;
}

/* The member's list as the screen made it: within its places, every tech
 * one of the game's and its group one of the groups. */
bool menu_techs_member_is_listable(const GameState *g)
{
    uint8_t listed = menu_techs_listed(g);
    if (menu_tech_category(g) >= MENU_TECH_KINDS || !menu_techs_members_are_drawable(g)
        || member_on_the_cursor(g) >= (uint8_t)menu_members_reachable(g) || listed >= MENU_TECH_LIST_GROUPS_COUNT)
        return false;
    for (unsigned at = 0; at < listed; at++)
        if (!menu_tech_is_the_games(menu_tech_list(g, at), kind_listed(g)) || menu_tech_list_groups(g, at) >= MENU_TECH_GROUPS)
            return false;
    return true;
}

bool menu_techs_list_is_pointable(const GameState *g)
{
    uint8_t listed = menu_techs_listed(g), top = menu_tech_list_top(g);
    if (!menu_techs_member_is_listable(g) || listed > MENU_TECH_LIST_GROUPS_COUNT - 1 || menu_tech_list_row(g) >= LIST_ROWS)
        return false;
    if (top + LIST_ROWS > MENU_TECH_LIST_GROUPS_COUNT || (listed > LIST_ROWS ? top > listed - LIST_ROWS : top != 0))
        return false;
    for (unsigned at = 0; at < MENU_TECH_LIST_GROUPS_COUNT; at++) {
        uint8_t tech = menu_tech_list(g, at);
        if (at < listed ? !menu_tech_is_the_games(tech, kind_listed(g)) || menu_tech_list_groups(g, at) >= MENU_TECH_GROUPS
                        : tech != MENU_NO_TECH)
            return false;
    }
    return menu_tech_list_row(g) + top < (listed > 0 ? listed : 1);
}
