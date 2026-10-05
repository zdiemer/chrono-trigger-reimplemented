/* A shop.
 *
 * A shop is a screen of four windows: the buying and selling tabs with the
 * party's gold under them, a list beside them (the shop's goods with their
 * prices, or the party's items it would buy, with what they bring), a panel
 * of how many of the item on the cursor the party has and has on, and along
 * the bottom the party's members, each with the attack or defence it has
 * and would have with the item, brighter where the item is better and
 * darker where it is worse; a member that could use it strikes a pose.
 *
 * On the tabs a second cursor holds on the tab while the first moves; up or
 * down goes to the members to equip them from the shop (still the
 * original's), and going back leaves. Choosing a tab opens its list, whose
 * rows a page at a time turn with the shoulders and slide a row at a time
 * past their ends; the row's item is described as the cursor meets it.
 * Choosing a row opens the quantity window on one of it, if the party can
 * take any: room for it among its ninety-nine of an item, gold to pay for
 * it, or (to sell) room in its gold under the most it can hold. Up and down
 * change the quantity by ten and left and right by one, its price with it;
 * choosing makes the deal.
 *
 * The prices: a shop asks an item's price scaled by a word of the story
 * (in 256ths, rounded up), and pays half its price, rounded up. Gold is a
 * 24-bit count, at most 9,999,999.
 *
 * The shop's working cells are the original's: the lists' places and the
 * goods' counts in their tables, the quantity, the item and its count in the
 * menus' item cells (menu_item_operand), which the inventory's routines and
 * the description take them from. */
#include "menu/shop.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/cursor.h"
#include "menu/digits.h"
#include "menu/display.h"
#include "menu/held_cursors.h"
#include "menu/equipment.h"
#include "menu/items.h"
#include "menu/lists.h"
#include "menu/members.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/sprites.h"
#include "menu/text.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "vars.h"

/* Which of its steps the shop's task runs next frame. */
void menu_shop_go_to(GameState *g, ShopStep step) { set_menu_screen_step(g, (uint8_t)step); }
static void go_to_step(GameState *g, ShopStep step) { menu_shop_go_to(g, step); }

/* ---- Gold ----------------------------------------------------------------------------------------- */

enum { MOST_GOLD = 9999999, GOLD_OVERFLOW_HIGH = 0x98, A_COUNT_OF_ITEMS = 99 };

static uint32_t gold(const GameState *g) { return (uint32_t)party_gold_low(g) | (uint32_t)party_gold_high(g) << 16; }

static void set_gold(GameState *g, uint32_t v)
{
    set_party_gold_low(g, (uint16_t)v);
    set_party_gold_high(g, (uint8_t)(v >> 16));
}

/* Gold past the most the party holds is the most. A sum or difference that
 * leaves the 24 bits keeps its low word under the top byte of the most,
 * which the cap then takes or leaves, as the original's has it. */
static uint32_t within_the_most(uint32_t sum)
{
    if (sum > 0xFFFFFF)
        sum = (uint32_t)GOLD_OVERFLOW_HIGH << 16 | (sum & 0xFFFF);
    return sum >= MOST_GOLD ? MOST_GOLD : sum;
}

/* What the quantity comes to, 24 bits. */
static uint32_t total(const GameState *g) { return menu_shop_total(g) & 0xFFFFFF; }

// $C2:FDBB
void menu_shop_pay(GameState *g) { set_gold(g, within_the_most(gold(g) - total(g))); }

// $C2:FD97
static void receive(GameState *g) { set_gold(g, within_the_most(gold(g) + total(g))); }

/* ---- The shop's arithmetic, as its cells are left -------------------------------------------- */

/* The quotient of a 24-bit count by a price. A price of nothing goes into
 * any count without end, its remainder the whole count. The original divides
 * a bit at a time in cells, which it leaves. */
typedef struct {
    uint32_t quotient;
    uint16_t remainder;
} Quotient;

static Quotient divide(uint32_t n, uint16_t d)
{
    if (d == 0)
        return (Quotient){ 0xFFFFFF, (uint16_t)n };
    return (Quotient){ n / d, (uint16_t)(n % d) };
}

/* The division leaves the quotient's three bytes over the bits it shifted
 * out of the spare byte below them, and the remainder; its dividend and
 * divisor are its caller's cells. */
static void leave_division(GameState *g, uint32_t n, uint16_t d, Quotient q)
{
    uint8_t spare = (uint8_t)menu_shop_quotient(g);
    set_menu_scratch_shop_divided(g, (uint16_t)n);
    set_menu_scratch_shop_divided_high(g, (uint8_t)(n >> 16));
    set_menu_scratch_shop_divisor(g, d);
    set_menu_shop_quotient(g, (q.quotient & 0xFFFFFF) | (uint32_t)(uint8_t)(spare << 1) << 24);
    set_menu_shop_remainder(g, d == 0 ? n : q.remainder);
}

/* How many of the item on the cursor the party can take: the quotient, at
 * most the limit already set (menu_item_operand_count); none refuses. */
static bool take_at_most(GameState *g, uint32_t can)
{
    if (can == 0)
        return false;
    if (can < 0x100 && can < menu_item_operand_count(g))
        set_menu_item_operand_count(g, (uint8_t)can);
    return true;
}

/* The quantity window opens on one, its total one's price. */
static void start_on_one(GameState *g, uint16_t price)
{
    set_menu_item_operand_quantity(g, 1);
    set_menu_shop_total(g, price);
    set_menu_shop_unit_price(g, price);
}

// $C2:FCB2
/* To buy: how many the party's gold pays for. */
static bool affordable(GameState *g)
{
    uint16_t price = menu_shop_prices(g, menu_item_operand(g));
    start_on_one(g, price);
    Quotient q = divide(gold(g), price);
    ORIGINAL_EFFECT(leave_division(g, gold(g), price, q));
    return take_at_most(g, q.quotient);
}

/* What the shop pays for an item: half its price, rounded up. */
static uint16_t sale_price(const GameState *g, uint8_t item)
{
    uint16_t price = menu_item_prices(g, item);
    return (uint16_t)((price >> 1) + (price & 1));
}

// $C2:FCE1
/* To sell: how many bring no more gold than the party has room for. */
static bool sellable(GameState *g)
{
    uint16_t price = sale_price(g, menu_item_operand(g));
    start_on_one(g, price);
    uint32_t room = MOST_GOLD - gold(g);
    Quotient q = divide(room, price);
    ORIGINAL_EFFECT(leave_division(g, room, price, q));
    return take_at_most(g, q.quotient);
}

/* ---- The goods and their prices ------------------------------------------------------------------ */

static uint8_t shop_number(const GameState *g) { return (uint8_t)menu_screen_argument(g); }

/* The shop's list copied from the ROM, its count, and the places past it
 * cleared. */
static void take_the_goods(GameState *g)
{
    for (unsigned i = 0; i < MENU_SHOP_LIST_BYTES; i++)
        set_menu_shop_goods(g, i, menu_shop_good(g, shop_number(g), i));
    ORIGINAL_EFFECT(charge_block_move(g, MENU_SHOP_LIST_BYTES));
    unsigned offered = 0;
    while (menu_shop_goods(g, offered) != 0)
        offered++;
    set_menu_shop_goods_offered(g, (uint8_t)offered);
    for (unsigned i = offered; i < MENU_SHOP_GOODS_COUNT; i++)
        set_menu_shop_goods(g, i, 0);
}

/* A price by the story's scale, in 256ths rounded up and at least 1; one
 * that leaves 24 bits is 65,000. */
enum { HIGHEST_SCALED_PRICE = 65000 };

static uint16_t scaled(uint16_t price, uint16_t scale)
{
    uint32_t product = (uint32_t)price * scale;
    if (product >> 24)
        return HIGHEST_SCALED_PRICE;
    uint16_t rounded = (uint16_t)((product >> 8) + ((product & 0xFF) != 0));
    return rounded ? rounded : 1;
}

/* The original multiplies with the multiplier a byte at a time into cells,
 * which it leaves with the multiplier's last operands. */
static void leave_scaling(GameState *g, unsigned priced, uint16_t price, uint16_t scale)
{
    enum { SCALE_BYTES = 2 };
    set_menu_shop_work(g, (uint16_t)priced);
    set_menu_scratch_shop_multiplicand_bytes(g, SCALE_BYTES);
    set_menu_scratch_shop_multiplicand(g, scale);
    if (priced == 0)
        return;
    set_menu_scratch_shop_multiplier(g, price);
    uint32_t product = (uint32_t)price * scale;
    if (!(product >> 24) && (product & 0xFF))
        product = (product & 0xFF0000FFu) | ((((product >> 8) + 1) & 0xFFFF) << 8);
    set_menu_shop_product(g, product);
    hw_set_multiplicand(g, (uint8_t)(price >> 8));
    (void)hw_multiply_by(g, (uint8_t)(scale >> 8));
}

// $C2:DB9F
/* What the shop asks for each of its goods. */
static void price_the_goods(GameState *g)
{
    uint16_t scale = shop_price_scale(g), price = 0;
    unsigned priced = 0;
    for (uint8_t item; (item = menu_shop_goods(g, priced)) != 0; priced++) {
        price = menu_item_prices(g, item);
        set_menu_shop_prices(g, item, scaled(price, scale));
    }
    ORIGINAL_EFFECT(leave_scaling(g, priced, price, scale));
}

// $C2:DCDA
/* How many of each of the goods the party holds: the count at the first
 * place of the inventory that holds some. */
static uint8_t held(const GameState *g, uint8_t item)
{
    for (unsigned place = 0; place < MENU_INVENTORY_PLACES; place++)
        if (menu_inventory_item_at(g, place) == item && menu_inventory_count_at(g, place) != 0)
            return menu_inventory_count_at(g, place);
    return 0;
}

static void count_the_goods_held(GameState *g)
{
    for (unsigned i = 0; menu_shop_goods(g, i) != 0; i++)
        set_menu_shop_goods_owned(g, i, held(g, menu_shop_goods(g, i)));
}

/* ---- The members along the bottom ---------------------------------------------------------------- */

/* The party's characters as the menus list them, the empty places left
 * out; the cursor goes round the members it can reach. */
enum { NO_MEMBER = 0x80, END_OF_MEMBERS = 0xFF, LISTED_PLACES = 9 };

// $C2:DBE8
static void list_the_members(GameState *g)
{
    unsigned n = 0;
    for (unsigned place = 0; place < LISTED_PLACES; place++)
        if (!(menu_members(g, place) & NO_MEMBER))
            set_menu_shop_members(g, n++, menu_members(g, place));
    set_menu_shop_members(g, n, END_OF_MEMBERS);
    set_menu_shop_member_count(g, (uint8_t)n);
    uint8_t last = (uint8_t)(SHOP_FIRST_MEMBER + menu_members_reachable(g) - 1);
    set_menu_cursor_places_left(g, SHOP_FIRST_MEMBER, last);
    set_menu_cursor_places_right(g, last, SHOP_FIRST_MEMBER);
    ORIGINAL_EFFECT(set_menu_scratch_shop_last_reachable(g, (uint8_t)((menu_members_reachable(g) - 1) << 1)));
}

static bool a_member(const GameState *g, unsigned m) { return !(menu_shop_members(g, m) & NO_MEMBER); }

/* A character's sprite record: the record of its own number. The original
 * works out its place with the multiplier and notes it. */
enum { RECORD_BYTES = 0x40, RECORDS = 8 };

static unsigned record_of(GameState *g, uint8_t character)
{
    ORIGINAL_EFFECT(hw_set_multiplicand(g, character));
    ORIGINAL_EFFECT(set_menu_record_offset(g, hw_multiply_by(g, RECORD_BYTES)));
    return character;
}

/* A sprite record's poses here: standing, and cheering for an item it
 * could use. */
enum { NO_SPRITE = 0xFF, STANDING = 0x00, CHEERING = 0x0B };


/* The members' strips along the window (shop.h): an icon of the kind of
 * weapon each uses over its figures. */
enum { MEMBER_STRIP_AT = SHOP_MEMBER_STRIPS_AT, MEMBER_STRIP_BYTES = SHOP_MEMBER_STRIP_BYTES };
enum { ICON_TILES = 0x20, ICON_ATTRIBUTE = 0x20, FIGURES_ICON = 0x2E };
enum { TILE_BYTES = MENU_TILE_BYTES, ROW_BYTES = MENU_TILEMAP_ROW_BYTES, MEMBER_SPRITE_Y = 0xC0 };

static unsigned tile_index(uint16_t at) { return menu_front_tile_of(at); }

static void put_tile(GameState *g, uint16_t at, uint16_t tile) { set_menu_front_tilemap(g, tile_index(at), tile); }

static void put_tile_number(GameState *g, uint16_t at, uint8_t tile)
{
    unsigned i = tile_index(at);
    set_menu_front_tilemap(g, i, (uint16_t)((menu_front_tilemap(g, i) & 0xFF00) | tile));
}

static void put_tile_attribute(GameState *g, uint16_t at, uint8_t attribute)
{
    unsigned i = tile_index(at);
    set_menu_front_tilemap(g, i, (uint16_t)((menu_front_tilemap(g, i) & 0x00FF) | attribute << 8));
}

// $C2:DC1E
/* The members' sprites standing along the window, each over its icon. */
static void stand_the_members(GameState *g)
{
    TIMED_AS(g, 0xC2F588, menu_empty_the_records(g));
    enum { BLOCKS_A_FRAME = 7 };
    set_menu_record_block_limit(g, BLOCKS_A_FRAME);
    unsigned m = 0;
    for (; a_member(g, m); m++) {
        (void)menu_load_character(g, menu_shop_members(g, m));
        uint8_t character = menu_status_record_character(g);
        unsigned record = record_of(g, character);
        set_menu_record_sprite(g, record, character);
        set_menu_record_x_low(g, record, menu_shop_member_x(g, m));
        set_menu_record_y(g, record, MEMBER_SPRITE_Y);
        uint16_t at = (uint16_t)(MEMBER_STRIP_AT + m * MEMBER_STRIP_BYTES);
        put_tile_number(g, at, (uint8_t)(character | ICON_TILES));
        put_tile_attribute(g, at, ICON_ATTRIBUTE);
        put_tile_number(g, (uint16_t)(at + ROW_BYTES), FIGURES_ICON);
    }
    set_menu_screen_argument(g, (uint16_t)((menu_screen_argument(g) & 0xFF00) | m)); /* the members placed */
    ORIGINAL_EFFECT(set_menu_scratch_shop_members_placed(g, (uint16_t)m));
}

/* Every character's sprite standing again. */
enum { LAST_CHARACTER = 6 };

// $C2:D605
static void stand_everyone(GameState *g)
{
    for (unsigned c = LAST_CHARACTER + 1; c-- > 0;)
        set_menu_record_animation(g, record_of(g, (uint8_t)c), STANDING);
    ORIGINAL_EFFECT(set_menu_scratch_shop_characters_left(g, 0xFF));
}

static bool can_use(GameState *g, uint8_t item)
{
    bool can = false;
    TIMED_AS(g, 0xC293A8, can = menu_loaded_member_can_use(g, item));
    return can;
}

/* The slot in a character's record an item of the category goes in. */
static uint8_t slot_place(const GameState *g, uint8_t category) { return menu_category_slot_place(g, category); }

/* The original finds each member's record with the multiplier, a record
 * $50 bytes, and leaves the slot and a byte of the party's in its cells. */
enum { CHARACTER_RECORD_BYTES = 0x50 };

static void leave_worn_counted(GameState *g, uint8_t slot)
{
    unsigned last = 0;
    while (last + 1 < MENU_MEMBER_WEARS_ITEM_COUNT && a_member(g, last + 1))
        last++;
    hw_set_multiplicand(g, CHARACTER_RECORD_BYTES);
    if (a_member(g, 0))
        (void)hw_multiply_by(g, menu_shop_members(g, last));
    set_menu_scratch_shop_slot_doubled(g, slot);
}

// $C2:8900
/* How many members have the item on, and which. */
static void count_the_item_worn(GameState *g, uint8_t item, uint8_t category)
{
    uint8_t slot = slot_place(g, category);
    uint8_t worn = 0;
    for (unsigned m = 0; m < MENU_MEMBER_WEARS_ITEM_COUNT && a_member(g, m); m++) {
        bool wears = character_records_record_byte(g, menu_shop_members(g, m), slot) == item;
        set_menu_member_wears_item(g, m, wears);
        worn = (uint8_t)(worn + wears);
    }
    set_menu_item_worn_count(g, worn);
    ORIGINAL_EFFECT(leave_worn_counted(g, slot));
    ORIGINAL_EFFECT(set_menu_scratch_shop_slot_high(g, 0));
    ORIGINAL_EFFECT(set_menu_scratch_shop_spare_count(g, party_characters_joined(g)));
}

/* The two figures the members' window shows: the record's attack and
 * defence, and where the figures' working out leaves them with the item. */
enum { ATTACK_IN_THE_RECORD = 0x3D, DEFENCE_IN_THE_RECORD = 0x3E, ATTACK_WORKED_OUT = 7, DEFENCE_WORKED_OUT = 8 };

/* A figure drawn with the item, its colour saying better (or worse, or the
 * same) than the member's own: two number tiles, the tens' and the last
 * two digits' (one digit's below ten). The original takes the digits from
 * the divider, which it leaves. */
enum { SAME = 0x21, WORSE = 0x3D, BETTER = 0x29, TENS_TILES = 0x6E, ONE_DIGIT_TILES = 100 };
enum { FIGURE_TILE_AT = 6, LAST_DIGITS_AT = 8 };

static void draw_figure(GameState *g, uint16_t at, uint8_t with, uint8_t before)
{
    uint8_t colour = with == before ? SAME : with < before ? WORSE : BETTER;
    put_tile_attribute(g, (uint16_t)(at + FIGURE_TILE_AT), colour);
    put_tile_attribute(g, (uint16_t)(at + LAST_DIGITS_AT), colour);
    put_tile_number(g, (uint16_t)(at + FIGURE_TILE_AT), (uint8_t)(TENS_TILES + with / 10));
    put_tile_number(g, (uint16_t)(at + LAST_DIGITS_AT), (uint8_t)(with % 100 + (with < 10 ? ONE_DIGIT_TILES : 0)));
    ORIGINAL_EFFECT(set_menu_scratch_shop_figure_with(g, with));
    ORIGINAL_EFFECT(set_menu_scratch_shop_figure_before(g, before));
    ORIGINAL_EFFECT(hw_set_dividend(g, with));
    ORIGINAL_EFFECT(hw_divide_by(g, 100));
}

/* A member who has the item on is marked over its icon. */
enum { WORN_MARK = 0x38, MARKS_AT = 0x2E84, STRIP_FIGURES_AT = 0x3400, SECOND_FIGURE_ROW = 0x40 };

// $C2:DE21
static void show_member_figures(GameState *g, unsigned m, uint8_t strip)
{
    if (menu_member_wears_item(g, m))
        put_tile_number(g, (uint16_t)(MARKS_AT + strip), WORN_MARK);
    uint16_t at = (uint16_t)(STRIP_FIGURES_AT + strip);
    draw_figure(g, at, menu_figures_worked_out(g, ATTACK_WORKED_OUT), menu_status_record_bytes_byte(g, ATTACK_IN_THE_RECORD));
    draw_figure(g, (uint16_t)(at + SECOND_FIGURE_ROW), menu_figures_worked_out(g, DEFENCE_WORKED_OUT),
                menu_status_record_bytes_byte(g, DEFENCE_IN_THE_RECORD));
}

/* ---- The panel ---------------------------------------------------------------------------------- */

/* The members' marks cleared, the counts' places blanked; then how many the
 * party has and has on, where there is an item. */
enum { MARKS_TILES = 28, MARK_BLANK = 0x2000, OWNED_AT = 0x3192, OWNED_TAIL_AT = 0x3194, WORN_AT = 0x3254 };
enum { OWNED_FORMAT = 0x11, WORN_FORMAT = 0x01 };

// $C2:DD7C
/* For the original's code and shadow mode: the panel leaves the item's
 * category where the figures' working out counts its slots, a word over the
 * first stat's boost. */
static void leave_category_for_the_figures(GameState *g)
{
    set_menu_figures_slot(g, (uint8_t)menu_item_category(g));
    set_menu_figures_boosts(g, 0, (uint8_t)(menu_item_category(g) >> 8));
}

void menu_shop_start_panel(GameState *g)
{
    for (unsigned i = 0; i < MARKS_TILES; i++)
        put_tile(g, (uint16_t)(MARKS_AT + i * TILE_BYTES), MARK_BLANK);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * MARKS_TILES));
    put_tile(g, OWNED_AT, 0);
    put_tile(g, OWNED_TAIL_AT, 0);
    put_tile(g, WORN_AT, 0);
    uint8_t item = menu_item_operand(g);
    MenuItemPlace p = menu_item_place(g, item);
    count_the_item_worn(g, item, p.category);
    ORIGINAL_EFFECT(leave_category_for_the_figures(g));
    ORIGINAL_EFFECT(set_menu_shop_work(g, slot_place(g, p.category)));
    if (item != 0) {
        (void)menu_draw_number_value(g, menu_item_operand_count(g), OWNED_FORMAT, OWNED_AT);
        (void)menu_draw_number_value(g, menu_item_worn_count(g), WORN_FORMAT, WORN_AT);
    }
}

/* A member of the panel: loaded, its figures with the item taken as its own
 * first; standing, or cheering with the item put in its slot where it could
 * use it (whose figures the original then works out). False past the last
 * member. */
/* The panel's drawing goes on at the member's place along the window, and
 * the member is the one the screen works on. */
static void at_panel_member(GameState *g, unsigned m)
{
    set_menu_screen_argument(g, (uint16_t)m);
    set_menu_draw_origin(g, (uint16_t)(m * MEMBER_STRIP_BYTES));
}

bool menu_shop_panel_member(GameState *g, unsigned m, bool *usable)
{
    at_panel_member(g, m);
    if (!a_member(g, m))
        return false;
    (void)menu_load_character(g, menu_shop_members(g, m));
    set_menu_figures_worked_out(g, ATTACK_WORKED_OUT, menu_status_record_bytes_byte(g, ATTACK_IN_THE_RECORD));
    set_menu_figures_worked_out(g, DEFENCE_WORKED_OUT, menu_status_record_bytes_byte(g, DEFENCE_IN_THE_RECORD));
    unsigned record = record_of(g, menu_status_record_character(g));
    set_menu_record_animation(g, record, STANDING);
    uint8_t item = menu_item_operand(g);
    *usable = can_use(g, item);
    if (*usable) {
        set_menu_record_animation(g, record, CHEERING);
        set_menu_status_record_bytes_byte(g, slot_place(g, (uint8_t)menu_item_category(g)), item);
    }
    return true;
}

void menu_shop_panel_member_shown(GameState *g, unsigned m) { show_member_figures(g, m, (uint8_t)(m * MEMBER_STRIP_BYTES)); }

/* For the original's code and shadow mode: the panel's loop leaves its
 * count of members as the member worked on and where it got to along the
 * window as the drawing origin; both are set again before anything reads
 * them. */
void menu_shop_leave_panel_done(GameState *g, unsigned members) { at_panel_member(g, members); }

/* ---- Opening ------------------------------------------------------------------------------------ */

enum { SHOP_WINDOWS = 0xC213 };

/* The cursor's places kept, which the equipping from the shop puts its own
 * over. */
static void keep_the_cursor_places(GameState *g)
{
    menu_keep_cursor_places(g);
}

// $C2:DACA
/* The shop's windows, the cursor on the buying tab, the members listed, the
 * lists at their tops, the shop's goods; the description to show nothing. */
void menu_shop_open(GameState *g, Platform *pf)
{
    set_menu_shop_member_cursor(g, SHOP_FIRST_MEMBER);
    set_menu_cursor_index(g, SHOP_BUY_TAB);
    TIMED_AS(g, 0xC2984A, menu_clear_sprites(g));
    menu_draw_windows(g, pf, SHOP_WINDOWS);
    list_the_members(g);
    set_menu_shop_buy_row(g, 0);
    set_menu_shop_buy_top(g, 0);
    set_menu_shop_sell_row(g, 0);
    set_menu_shop_sell_top(g, 0);
    keep_the_cursor_places(g);
    take_the_goods(g);
    set_menu_description_item(g, 0);
    set_menu_description_stale(g, 0);
}

/* The shop's colours: the panel's highlight's four twice into palette row
 * 2, the equipment's four twice into row 7, the rows' colours dimmed into
 * rows 4 to 6; the members' window's line bands at rest; the number tiles. */
typedef struct {
    uint16_t from;
    uint8_t first, count;
} ShopColours;

static const ShopColours SHOP_COLOURS[] = {
    { MENU_ROM_PANEL_HIGHLIGHT_COLOURS, 0x20, 4 },
    { MENU_ROM_PANEL_HIGHLIGHT_COLOURS, 0x24, 4 },
    { MENU_ROM_EQUIPMENT_COLOURS, 0x70, 4 },
    { MENU_ROM_EQUIPMENT_COLOURS, 0x74, 4 },
};
enum { SHOP_COLOUR_RUNS = sizeof SHOP_COLOURS / sizeof SHOP_COLOURS[0], FIRST_DIMMED = 0x40 };
enum { BANDS_AT_REST = 11, BAND_AT_REST = 0x61FF };

void menu_shop_open_colours(GameState *g)
{
    for (unsigned run = 0; run < SHOP_COLOUR_RUNS; run++) {
        const ShopColours *c = &SHOP_COLOURS[run];
        for (unsigned i = 0; i < c->count; i++)
            set_menu_palette(g, c->first + i, menu_rom_colour(g, c->from, i));
        ORIGINAL_EFFECT(charge_block_move(g, 2u * c->count));
    }
    TIMED_AS(g, 0xC2A0E7, menu_equip_dim_colours(g, FIRST_DIMMED));
    for (unsigned band = 0; band < BANDS_AT_REST; band++)
        set_menu_row_line_values(g, band, BAND_AT_REST);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * BANDS_AT_REST));
    TIMED_AS(g, 0xFFF548, menu_build_number_tiles(g));
}

/* The members standing, the records' task started, the panel's item
 * nothing. */
void menu_shop_open_members(GameState *g)
{
    stand_the_members(g);
    TIMED_AS(g, 0xC2F643, menu_start_the_records(g));
    set_menu_item_operand(g, 0);
}

enum { UPLOAD_LINE_TABLES = 0xFC3E, UPLOAD_FRONT_LAYER_WHOLE = 0xFBCE, UPLOAD_BACK_LAYER_WHOLE = 0xFBF8 };
enum { SHOP_LINE_EFFECTS = 0x2F };

/* The colour math, the goods' prices, and the shop's layers queued. */
void menu_shop_open_last(GameState *g, Platform *pf)
{
    set_menu_item_operand_count(g, 0);
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    TIMED_AS(g, 0xC286DD, menu_build_math_table(g, pf));
    price_the_goods(g);
    menu_queue_fixed_upload(g, UPLOAD_LINE_TABLES);
    menu_queue_fixed_upload(g, UPLOAD_FRONT_LAYER_WHOLE);
    menu_queue_fixed_upload(g, UPLOAD_BACK_LAYER_WHOLE);
    menu_queue_fixed_upload(g, MENU_UPLOAD_SHOP_SPRITES);
    set_menu_line_effects(g, SHOP_LINE_EFFECTS);
}

// $C2:D52E
/* The second cursor holds on the buying tab and blinks slowly, its own
 * sprite (menu/held_cursors.h); on to the tabs. */
enum { HELD_ON_THE_TABS = 0x0B };
enum { SLOW_BLINK = 0x00F8, HELD_CURSOR_SPRITE = 0xB022 };

void menu_shop_hold_a_cursor_on_the_tabs(GameState *g)
{
    set_menu_held_cursor(g, HELD_ON_THE_TABS);
    TIMED_AS(g, 0xC28A98, menu_hold_a_cursor(g));
    set_menu_held_cursor_blink(g, SLOW_BLINK);
    set_menu_held_cursor_sprite(g, HELD_CURSOR_SPRITE);
    go_to_step(g, SHOP_ON_THE_TABS);
}

/* ---- The tabs ----------------------------------------------------------------------------------- */

enum { RECORD_BLOCKS_ON_THE_TABS = 1 };

// $C2:D546
void menu_shop_on_the_tabs_frame(GameState *g) { set_menu_record_block_limit(g, RECORD_BLOCKS_ON_THE_TABS); }

ShopTabChoice menu_shop_choose_tab(const GameState *g)
{
    if (menu_asked(g, MENU_CHOOSE))
        return TAB_CHOSEN;
    if (menu_asked(g, MENU_GO_BACK))
        return SHOP_LEFT;
    if (menu_pushed(g, MENU_PUSHED_UP) || menu_pushed(g, MENU_PUSHED_DOWN))
        return TO_THE_MEMBERS;
    return TAB_POINTED_AT;
}

/* The held cursor's task: stopped while the cursor is off the tabs, and
 * going on from where it waited once it is back. */
enum { TASK_STOPPED = 0, TASK_WAITING = 2 };

void menu_shop_stop_holding(GameState *g) { set_menu_tasks_state(g, MENU_HELD_CURSORS_TASK, TASK_STOPPED); }

/* A tab chosen: the held cursor stops and the panel forgets its place. */
enum { NO_PLACE_SHOWN = 0xFF };

void menu_shop_leave_the_tabs(GameState *g)
{
    menu_shop_stop_holding(g);
    set_menu_shop_place_shown(g, NO_PLACE_SHOWN);
}

bool menu_shop_buying_chosen(const GameState *g) { return menu_cursor_index(g) == SHOP_BUY_TAB; }

/* ---- The lists ---------------------------------------------------------------------------------- */

/* A list's eight rows down the window, a row two tile rows; the row a slide
 * brings in drawn below them. A row is the item's name and its price, or
 * blanks where the list has run out. */
enum { LIST_ROWS_AT = 0x2F5C, LIST_ROW_BYTES = 0x80, INCOMING_ROW_AT = 0x335C };
enum { NAME_SETTINGS = 0x0B, PRICE_FORMAT = 0x42, BLANK_ROW_TILES = 0x10 };

// $C2:DD56
static void draw_row(GameState *g, uint16_t at, uint8_t item)
{
    if (item == 0) {
        (void)menu_draw_blanks(g, at, BLANK_ROW_TILES);
        return;
    }
    uint16_t pen = menu_draw_string(g, MENU_ITEM_TABLES_BANK | menu_item_name_places(g, item), at, NAME_SETTINGS);
    (void)menu_draw_number_value(g, menu_shop_row_price(g), PRICE_FORMAT, pen);
}

/* The places a list's rows show, from its top. */
static uint8_t list_top(const GameState *g, bool selling) { return selling ? menu_shop_sell_top(g) : menu_shop_buy_top(g); }
static void set_list_top(GameState *g, bool selling, uint8_t top)
{
    if (selling)
        set_menu_shop_sell_top(g, top);
    else
        set_menu_shop_buy_top(g, top);
}

static uint8_t item_listed(const GameState *g, bool selling, uint8_t place)
{
    return selling ? menu_shop_for_sale(g, place) : menu_shop_goods(g, place);
}

// $C2:DD20, $C2:DF51
/* A row of the list from the cursor's `row`: the item and its price noted,
 * then drawn. A row for sale notes the item as a word, its count cleared. */
static void draw_list_row(GameState *g, bool selling, uint8_t row, uint16_t at)
{
    uint8_t place = (uint8_t)(list_top(g, selling) + row);
    uint8_t item = item_listed(g, selling, place);
    set_menu_item_operand(g, item);
    if (selling) {
        set_menu_item_operand_count(g, 0);
        set_menu_shop_row_price(g, sale_price(g, item));
    } else
        set_menu_shop_row_price(g, menu_shop_prices(g, item));
    draw_row(g, at, item);
}

// $C2:DD02, $C2:DF31
void menu_shop_draw_rows(GameState *g, bool selling)
{
    for (unsigned row = 0; row < SHOP_LIST_ROWS; row++)
        draw_list_row(g, selling, (uint8_t)row, (uint16_t)(LIST_ROWS_AT + row * LIST_ROW_BYTES));
    ORIGINAL_EFFECT(set_menu_shop_work(g, SHOP_LIST_ROWS));
    set_menu_draw_origin(g, INCOMING_ROW_AT);
}

/* The scroll bar beside the list (menu/held_cursors.h). */
enum { LIST_BAR_PLACE = 0x2AF0 };

static void start_the_scroll_bar(GameState *g) { TIMED_AS(g, 0xC28B93, menu_start_the_scroll_bar(g)); }

/* A list opens on the row it was left on where the settings keep the
 * menus' cursors, else on its first. */
enum { BUYING_WINDOWS = 0xC2C1, SELLING_WINDOWS = 0xC2CB, DESCRIBE_AGAIN = 0xFF };

// $C2:DC7B
void menu_shop_open_buying(GameState *g, Platform *pf)
{
    set_menu_description_stale(g, DESCRIBE_AGAIN);
    set_menu_cursor_index(g, 0);
    set_menu_scroll_bar_place(g, LIST_BAR_PLACE);
    menu_draw_windows(g, pf, BUYING_WINDOWS);
    if (!menu_setting_keep_menu_cursors(g)) {
        set_menu_shop_buy_row(g, 0);
        set_menu_shop_buy_top(g, 0);
    }
    set_menu_cursor_index(g, menu_shop_buy_row(g));
    set_menu_scroll_bar_thumb(g, menu_shop_buy_top(g));
    menu_shop_draw_rows(g, false);
}

/* Then (once the original has framed it) the goods' counts and the bar. */
void menu_shop_open_buying_last(GameState *g)
{
    count_the_goods_held(g);
    start_the_scroll_bar(g);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

// $C2:DE98
void menu_shop_open_selling(GameState *g, Platform *pf)
{
    set_menu_scroll_bar_place(g, LIST_BAR_PLACE);
    menu_draw_windows(g, pf, SELLING_WINDOWS);
    if (!menu_setting_keep_menu_cursors(g)) {
        set_menu_shop_sell_row(g, 0);
        set_menu_shop_sell_top(g, 0);
    }
    set_menu_cursor_index(g, menu_shop_sell_row(g));
}

/* Then (once the list is made and framed) its rows and the bar. */
void menu_shop_open_selling_last(GameState *g)
{
    set_menu_description_stale(g, DESCRIBE_AGAIN);
    menu_shop_draw_rows(g, true);
    start_the_scroll_bar(g);
}

// $C2:DECC
/* What the party can sell: its items in the inventory's order, but for key
 * items and what the shop won't take; each place's count is written as the
 * list goes, so the place past the last keeps the last count it saw. */
enum { FOR_SALE_CLEARED = 8, LAST_ROW = SHOP_LIST_ROWS - 1, NOT_FOR_SALE = 0x04, KEY_ITEMS = 0xF2 };

void menu_shop_list_for_sale(GameState *g)
{
    for (unsigned i = 0; i < FOR_SALE_CLEARED; i++)
        set_menu_shop_for_sale(g, i, 0);
    ORIGINAL_EFFECT(charge_block_move(g, FOR_SALE_CLEARED));
    set_menu_list_last_place(g, LAST_ROW);
    uint8_t listed = 0;
    for (unsigned place = 0; place < MENU_INVENTORY_PLACES; place++) {
        set_menu_shop_for_sale_counts(g, listed, menu_inventory_count_at(g, place));
        uint8_t item = menu_inventory_item_at(g, place);
        if (item == 0 || item >= KEY_ITEMS)
            continue;
        set_menu_shop_for_sale(g, listed, item);
        if (!(menu_item_kinds(g, item) & NOT_FOR_SALE))
            listed++;
    }
    set_menu_shop_for_sale_listed(g, listed);
    set_menu_shop_for_sale(g, listed, 0);
}

/* Once framed: the cursor and the list's top kept within it. */
void menu_shop_settle_for_sale(GameState *g)
{
    if (menu_list_last_place(g) < menu_cursor_index(g))
        set_menu_cursor_index(g, menu_list_last_place(g));
    if (menu_list_scroll_most(g) < menu_shop_sell_top(g))
        set_menu_shop_sell_top(g, menu_list_scroll_most(g));
    set_menu_paging_top(g, menu_shop_sell_top(g));
    set_menu_scroll_bar_thumb(g, menu_shop_sell_top(g));
}

// $C2:DD40
/* A list's frame shows eight rows from its first place. */
void menu_shop_frame_list(GameState *g)
{
    ORIGINAL_EFFECT(set_menu_scratch_shop_rows_shown(g, SHOP_LIST_ROWS));
    set_menu_list_first_place(g, 0);
}

/* What the frame's input does on a list. */
ShopListChoice menu_shop_choose_on_the_list(const GameState *g)
{
    if (menu_asked(g, MENU_CHOOSE))
        return SHOP_ROW_CHOSEN;
    if (menu_asked(g, MENU_GO_BACK))
        return SHOP_LIST_LEFT;
    return SHOP_ROW_POINTED_AT;
}

/* Pointing along: the top noted for the paging (the original's). */
uint8_t menu_shop_list_top(const GameState *g, bool selling) { return list_top(g, selling); }

/* A page turned: the list's top where the paging put it, the rows drawn
 * again. */
void menu_shop_turn_page(GameState *g, bool selling, uint8_t top)
{
    set_list_top(g, selling, top);
    menu_shop_draw_rows(g, selling);
}

static uint8_t listed_count(const GameState *g, bool selling)
{
    return selling ? menu_shop_for_sale_listed(g) : menu_shop_goods_offered(g);
}

// $C2:D82B
/* A push past the rows slides the list a row where there is a row that way
 * (the place before the cursor's short of the count's last but one). */
bool menu_shop_list_slide_brings_a_row(GameState *g, bool selling)
{
    if (!menu_list_scrolls(g) || !menu_pushed(g, MENU_PUSHED_PAST_AN_EDGE))
        return false;
    uint8_t last = (uint8_t)(listed_count(g, selling) - 2);
    ORIGINAL_EFFECT(set_menu_scratch_shop_last_place(g, last));
    return (uint8_t)(menu_cursor_index(g) + list_top(g, selling) - 1) < last;
}

/* The slide moves the list's nine rows (with the one coming in), and the
 * per-line table its block move brings their lines in from. */
enum { LIST_LINE_TABLE = 0x9083, LIST_LINE_TABLE_BYTES = 0x25, LIST_ROWS_SLID = 9 };

void menu_shop_set_up_list_slide(GameState *g)
{
    set_menu_block_move_dest(g, LIST_LINE_TABLE);
    set_menu_block_move_size(g, LIST_LINE_TABLE_BYTES);
    set_menu_slide_rows(g, LIST_ROWS_SLID);
}

/* The list's top a row on (or back), the bar with it, and the row coming
 * in drawn below the list, from the cursor's row. */
void menu_shop_bring_in_a_row(GameState *g, bool selling)
{
    uint8_t top = list_top(g, selling);
    set_list_top(g, selling, menu_pushed(g, MENU_PUSHED_DOWN) ? (uint8_t)(top + 1) : (uint8_t)(top - 1));
    set_menu_scroll_bar_thumb(g, list_top(g, selling));
    ORIGINAL_EFFECT(set_menu_shop_work(g, menu_cursor_index(g)));
    set_menu_draw_origin(g, INCOMING_ROW_AT);
    draw_list_row(g, selling, menu_cursor_index(g), INCOMING_ROW_AT);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

/* A held push repeating fast skips the slide's frames. */
enum { REPEATING_FAST = 0x80 };

bool menu_shop_list_slide_takes_frames(GameState *g)
{
    if (menu_fast_repeat(g) & REPEATING_FAST)
        return false;
    set_menu_scroll_moved(g, 0);
    return true;
}

/* A frame of the slide: the rows a step on. Whether frames are left. */
bool menu_shop_slide_list_a_frame(GameState *g)
{
    set_menu_scroll_moved(g, (uint16_t)(menu_scroll_moved(g) + menu_scroll_step(g)));
    uint16_t frames = (uint16_t)(menu_scroll_frames(g) - 1);
    set_menu_scroll_frames(g, frames);
    return frames != 0;
}

/* The rows moved a row along in the tilemap once the list has slid: the
 * window's sixteen tile rows two up, or down with the row that came in
 * (drawn below them) to the top. */
enum { LIST_WINDOW_AT = 0x2F1C, LIST_WINDOW_TILES = 16, LIST_TILE_ROWS = 16, ROWS_KEPT_SLIDING_UP = 14 };
enum { INCOMING_ROWS_AT = 0x3300, TOP_ROWS_AT = 0x2F00, TWO_TILE_ROWS = 2 * ROW_BYTES, WHOLE_ROWS_TILES = 64 };

static void copy_tiles(GameState *g, uint16_t from, uint16_t to, unsigned tiles)
{
    for (unsigned i = 0; i < tiles; i++)
        set_menu_front_tilemap(g, tile_index(to) + i, menu_front_tilemap(g, tile_index(from) + i));
}

/* The original copies the rows with a block move a tile row at a time,
 * walking its source and destination along in cells it leaves. */
static void leave_rows_moved(GameState *g, bool up)
{
    if (up) {
        uint16_t from = (uint16_t)(LIST_WINDOW_AT - ROW_BYTES + LIST_WINDOW_TILES * TILE_BYTES - 1);
        set_menu_scratch_shop_rows_from(g, from);
        set_menu_scratch_shop_rows_to(g, (uint16_t)(from + TWO_TILE_ROWS));
    } else {
        set_menu_scratch_shop_rows_from(g, (uint16_t)(LIST_WINDOW_AT + TWO_TILE_ROWS + LIST_TILE_ROWS * ROW_BYTES));
        set_menu_scratch_shop_rows_to(g, (uint16_t)(LIST_WINDOW_AT + LIST_TILE_ROWS * ROW_BYTES));
    }
    set_menu_scratch_shop_rows_left(g, 0);
}

// $C2:D995
static void move_the_rows(GameState *g, bool up)
{
    ORIGINAL_EFFECT(leave_rows_moved(g, up));
    if (!up) {
        for (unsigned r = 0; r < LIST_TILE_ROWS; r++) {
            uint16_t to = (uint16_t)(LIST_WINDOW_AT + r * ROW_BYTES);
            copy_tiles(g, (uint16_t)(to + TWO_TILE_ROWS), to, LIST_WINDOW_TILES);
        }
        ORIGINAL_EFFECT(charge_block_move(g, LIST_TILE_ROWS * LIST_WINDOW_TILES * TILE_BYTES));
        return;
    }
    for (unsigned r = ROWS_KEPT_SLIDING_UP; r-- > 0;) {
        uint16_t from = (uint16_t)(LIST_WINDOW_AT + r * ROW_BYTES);
        copy_tiles(g, from, (uint16_t)(from + TWO_TILE_ROWS), LIST_WINDOW_TILES);
    }
    copy_tiles(g, INCOMING_ROWS_AT, TOP_ROWS_AT, WHOLE_ROWS_TILES);
    ORIGINAL_EFFECT(charge_block_move(g, ROWS_KEPT_SLIDING_UP * LIST_WINDOW_TILES * TILE_BYTES + TWO_TILE_ROWS));
}

/* The slide over: the rows moved along, the upload queue emptied (the
 * frames sent it), and the block move set to bring the rows' new lines
 * in. */

void menu_shop_finish_list_slide(GameState *g)
{
    move_the_rows(g, menu_list_slid_up(g));
    set_menu_scroll_step(g, 0);
    set_menu_vram_queue_bytes(g, 0);
    menu_put_the_list_bands_back(g);
}

// $C2:D7F1, $C2:D8D4
/* The row the cursor is on taken: the list's row kept, the item there and
 * how many of it the party has noted for the panel and the description. */
void menu_shop_take_the_row(GameState *g, bool selling)
{
    uint8_t row = menu_cursor_index(g);
    if (selling)
        set_menu_shop_sell_row(g, row);
    else
        set_menu_shop_buy_row(g, row);
    uint8_t place = (uint8_t)(row + list_top(g, selling));
    set_menu_item_list_place(g, place);
    set_menu_item_operand_count(g, selling ? menu_shop_for_sale_counts(g, place) : menu_shop_goods_owned(g, place));
    set_menu_item_operand(g, item_listed(g, selling, place));
}

/* Whether the panel shows another place now, noting it. */
bool menu_shop_another_place(GameState *g)
{
    uint8_t place = (uint8_t)menu_item_list_place(g);
    bool another = place != menu_shop_place_shown(g);
    set_menu_shop_place_shown(g, place);
    return another;
}

void menu_shop_queue_the_list(GameState *g) { menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER); }

/* ---- Buying and selling ------------------------------------------------------------------------- */

// $C2:D598
/* To buy: room under the party's ninety-nine of the item (those it has on
 * counted), the quantity's limit; then gold for at least one, the limit the
 * fewer. */
uint8_t menu_shop_held_and_worn(const GameState *g) { return (uint8_t)(menu_item_operand_count(g) + menu_item_worn_count(g)); }

bool menu_shop_room_to_buy(GameState *g)
{
    set_menu_cursor_index(g, menu_shop_buy_row(g));
    uint8_t held = menu_shop_held_and_worn(g);
    ORIGINAL_EFFECT(set_menu_scratch_shop_held(g, held));
    if (held > A_COUNT_OF_ITEMS - 1)
        return false;
    set_menu_item_operand_count(g, (uint8_t)(A_COUNT_OF_ITEMS - held));
    return true;
}

bool menu_shop_gold_buys_some(GameState *g) { return affordable(g); }

// $C2:D652
/* To sell: an item the party has; then room in its gold for at least one's
 * price, the limit the fewer. */
bool menu_shop_some_to_sell(GameState *g)
{
    set_menu_cursor_index(g, menu_shop_sell_row(g));
    return menu_item_operand(g) != 0 && menu_item_operand_count(g) != 0;
}

bool menu_shop_room_for_the_gold(GameState *g) { return sellable(g); }

/* The quantity window opens on one: the list's cursor put aside, the item's
 * name, its windows (its attack or defence where it has one), the step. The
 * limit counted past the last the party can take, the bar hidden. */
enum { QUANTITY_WINDOWS = 0xC2DE, UPLOAD_QUANTITY = 0xFBEA, QUANTITY_NAME_AT = 0x3BDE };
/* The bands past the member rows show the layer 145 lines up (its vertical
 * scroll); the original's word also carries a bit above the scroll's ten,
 * which the scroll ignores and the table keeps. */
enum { QUANTITY_BANDS = 16, QUANTITY_SCROLL = 0x016F, SCROLL_SPARE_BIT = 0x2000 };
enum { QUANTITY_BAND = QUANTITY_SCROLL | SCROLL_SPARE_BIT };

// $C2:DF76
void menu_shop_open_quantity(GameState *g, Platform *pf, ShopStep step)
{
    set_menu_item_operand_count(g, (uint8_t)(menu_item_operand_count(g) + 1));
    set_menu_scroll_bar_hidden(g, (uint8_t)(menu_scroll_bar_hidden(g) - 1));
    go_to_step(g, step);
    uint8_t item = menu_item_operand(g);
    /* The item's attack or defence for the window. The items past the
     * figures' table have none to show, and the game reads past it all the
     * same. */
    set_menu_item_power(g, menu_item_powers_unchecked(g, item));
    set_menu_shop_list_cursor(g, menu_cursor_index(g));
    set_menu_cursor_index(g, SHOP_QUANTITY);
    for (unsigned band = 0; band < QUANTITY_BANDS; band++)
        set_menu_row_line_values_past(g, band, QUANTITY_BAND);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * QUANTITY_BANDS));
    (void)menu_draw_string(g, MENU_ITEM_TABLES_BANK | menu_item_name_places(g, item), QUANTITY_NAME_AT, NAME_SETTINGS);
    menu_draw_windows(g, pf, QUANTITY_WINDOWS);
    menu_queue_fixed_upload(g, UPLOAD_QUANTITY);
    MenuItemPlace p = menu_item_place(g, item);
    menu_draw_windows(g, pf, menu_shop_quantity_windows(g, p.category));
    menu_queue_fixed_upload(g, UPLOAD_LINE_TABLES);
}

ShopQuantityChoice menu_shop_choose_quantity(const GameState *g)
{
    if (menu_asked(g, MENU_CHOOSE))
        return QUANTITY_TAKEN;
    if (menu_asked(g, MENU_GO_BACK))
        return QUANTITY_LEFT;
    return QUANTITY_CHANGED;
}

/* The cursor holds on the quantity against the pushes, which change it. */
void menu_shop_hold_the_quantity(GameState *g) { set_menu_cursor_held(g, 0xFF); }

/* The total moves with the quantity: `step` more or less of the price,
 * 24 bits. The original multiplies the price (negative to take it off) a
 * byte at a time, and leaves its operands and product. */
static void leave_step_priced(GameState *g, uint8_t step, uint32_t price)
{
    set_menu_scratch_shop_step(g, step);
    set_menu_scratch_shop_multiplicand(g, (uint16_t)price);
    set_menu_scratch_shop_multiplicand_high(g, (uint8_t)(price >> 16));
    set_menu_shop_product(g, (uint32_t)step * price);
    hw_set_multiplicand(g, step);
    (void)hw_multiply_by(g, (uint8_t)(price >> 16));
}

static void price_the_step(GameState *g, uint8_t step, bool less)
{
    uint32_t price = menu_shop_unit_price(g);
    if (less)
        price = (0x1000000 - price) & 0xFFFFFF;
    ORIGINAL_EFFECT(leave_step_priced(g, step, price));
    set_menu_shop_total(g, (total(g) + step * price) & 0xFFFFFF);
}

/* For the original's code and shadow mode: a push left or right steps the
 * quantity by one, which it notes in its step's cell first. */
static void leave_a_step_of_one(GameState *g, MenuCursorDirection way)
{
    if (way == MENU_CURSOR_WENT_RIGHT || way == MENU_CURSOR_WENT_LEFT)
        set_menu_scratch_shop_step(g, 1);
}

/* For the original's code and shadow mode: it keeps the quantity before the
 * push in the panel's place cell, which closing the window sets again
 * before anything reads it. */
static void leave_quantity_before(GameState *g, uint8_t quantity) { set_menu_shop_place_shown(g, quantity); }

/* The quantity's change by a push: right one more, left one fewer, up ten
 * more and down ten fewer, never below one nor up to the limit (ten more
 * than there is room for go to the most, ten fewer than one to one). */
enum { A_TEN = 10 };

static void step_the_quantity(GameState *g, MenuCursorDirection way)
{
    uint8_t quantity = menu_item_operand_quantity(g), limit = menu_item_operand_count(g);
    switch (way) {
    case MENU_CURSOR_WENT_RIGHT: {
        uint8_t more = (uint8_t)(quantity + 1);
        if (more >= limit)
            return;
        set_menu_item_operand_quantity(g, more);
        price_the_step(g, 1, false);
        return;
    }
    case MENU_CURSOR_WENT_LEFT: {
        uint8_t fewer = (uint8_t)(quantity - 1);
        if (fewer == 0 || fewer >= limit)
            return;
        set_menu_item_operand_quantity(g, fewer);
        price_the_step(g, 1, true);
        return;
    }
    case MENU_CURSOR_WENT_UP: {
        uint8_t more = (uint8_t)(quantity + A_TEN);
        uint8_t step = A_TEN;
        if (more >= limit) {
            step = (uint8_t)(limit - 1 - quantity);
            more = (uint8_t)(limit - 1);
        }
        set_menu_item_operand_quantity(g, more);
        price_the_step(g, step, false);
        return;
    }
    case MENU_CURSOR_WENT_DOWN: {
        uint8_t fewer = (uint8_t)(quantity - A_TEN);
        uint8_t step = A_TEN;
        if (fewer == 0 || fewer >= limit) {
            step = (uint8_t)(quantity - 1);
            fewer = 1;
        }
        set_menu_item_operand_quantity(g, fewer);
        price_the_step(g, step, true);
        return;
    }
    case MENU_CURSOR_NOT_PUSHED: return;
    }
}

/* Whether the frame's push is one way or another (none leaves the
 * quantity as it is, its window not drawn again). */
bool menu_shop_quantity_pushed(const GameState *g) { return !(menu_cursor_direction(g) & 0x80); }

// $C2:DA01
bool menu_shop_change_quantity(GameState *g, Platform *pf)
{
    enum { WORDS_OF_THE_PRICE = 3 };
    menu_shop_hold_the_quantity(g);
    ORIGINAL_EFFECT(set_menu_scratch_shop_multiplicand(g, menu_shop_unit_price(g)));
    ORIGINAL_EFFECT(set_menu_scratch_shop_multiplicand_high(g, 0));
    ORIGINAL_EFFECT(set_menu_scratch_shop_multiplicand_bytes(g, WORDS_OF_THE_PRICE));
    uint8_t way = menu_cursor_direction(g);
    if (way & MENU_CURSOR_NOT_PUSHED)
        return false;
    uint8_t before = menu_item_operand_quantity(g);
    ORIGINAL_EFFECT(leave_quantity_before(g, before));
    step_the_quantity(g, (MenuCursorDirection)way);
    ORIGINAL_EFFECT(leave_a_step_of_one(g, (MenuCursorDirection)way));
    menu_draw_windows(g, pf, QUANTITY_WINDOWS);
    menu_queue_fixed_upload(g, UPLOAD_QUANTITY);
    return menu_item_operand_quantity(g) != before;
}

// $C2:D618
void menu_shop_bought(GameState *g)
{
    TIMED_AS(g, 0xC28791, (void)menu_add_items(g, menu_item_operand(g), menu_item_operand_quantity(g)));
    menu_shop_pay(g);
    count_the_goods_held(g);
}

// $C2:D690
void menu_shop_sold(GameState *g)
{
    TIMED_AS(g, 0xC287D5, (void)menu_take_items(g, menu_item_operand(g), menu_item_operand_quantity(g)));
    receive(g);
}

/* The quantity window closed: the list's cursor back, the window's bands
 * cleared, the panel to be drawn again, the gold drawn. */
enum { GOLD_AT = 0x3046, GOLD_FORMAT = 0x63 };

// $C2:DFCF
static void close_the_quantity(GameState *g)
{
    set_menu_cursor_index(g, menu_shop_list_cursor(g));
    for (unsigned band = 0; band < QUANTITY_BANDS; band++)
        set_menu_row_line_values_past(g, band, 0);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * QUANTITY_BANDS));
    set_menu_shop_place_shown(g, NO_PLACE_SHOWN);
    ORIGINAL_EFFECT(set_menu_shop_place_shown_past(g, 0xFF));
    (void)menu_draw_number_value(g, gold(g), GOLD_FORMAT, GOLD_AT);
    menu_queue_fixed_upload(g, UPLOAD_LINE_TABLES);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

// $C2:D638
/* Back on the goods: the bar shown again. */
void menu_shop_back_to_buying(GameState *g)
{
    set_menu_scroll_bar_hidden(g, 0);
    go_to_step(g, SHOP_BUYING);
    close_the_quantity(g);
}

// $C2:D6AD
/* Back on the list for sale, the bar's blink started over and shown again;
 * the window closed, the list made again (it has changed) and its rows
 * drawn once the original has framed it. */
void menu_shop_restart_the_bars_blink(GameState *g) { set_menu_scroll_bar_blink(g, 0); }

void menu_shop_back_to_selling(GameState *g)
{
    go_to_step(g, SHOP_SELLING);
    set_menu_scroll_bar_hidden(g, 0);
    close_the_quantity(g);
}

void menu_shop_draw_for_sale(GameState *g) { menu_shop_draw_rows(g, true); }

/* ---- Back to the tabs --------------------------------------------------------------------------- */

// $C2:D5D9, $C2:D689
/* Going back from a list: the cursor on its tab, the panel to show nothing
 * (drawn as the original's panel), a tick; then every character standing,
 * the tabs' windows, the bar's task stopped and the held cursor going on. */
void menu_shop_back_from_the_list(GameState *g, bool selling)
{
    set_menu_cursor_index(g, selling ? SHOP_SELL_TAB : SHOP_BUY_TAB);
    set_menu_item_operand(g, 0);
}

enum { TABS_WINDOWS = 0xC2B0 };

// $C2:D5E6
void menu_shop_back_on_the_tabs(GameState *g, Platform *pf)
{
    stand_everyone(g);
    menu_draw_windows(g, pf, TABS_WINDOWS);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    set_menu_tasks_state(g, MENU_SCROLL_BAR_TASK, TASK_STOPPED);
    ORIGINAL_EFFECT(set_menu_scratch_tasks_to_stop(g, 0));
    set_menu_tasks_state(g, MENU_HELD_CURSORS_TASK, TASK_WAITING);
    go_to_step(g, SHOP_ON_THE_TABS);
}

/* ---- What the steps can run on (the guards') ----------------------------------------------------- */

/* The shop's goods end within its list, its members are characters with
 * records, and the members it lets the cursor reach are some of them. */
static bool goods_end_in_the_list(const GameState *g)
{
    for (unsigned i = 0; i < MENU_SHOP_LIST_BYTES; i++)
        if (menu_shop_good(g, (uint8_t)menu_screen_argument(g), i) == 0)
            return true;
    return false;
}

bool menu_shop_can_open(const GameState *g)
{
    uint8_t reachable = (uint8_t)menu_members_reachable(g);
    unsigned listed = 0;
    for (unsigned place = 0; place < LISTED_PLACES; place++)
        if (!(menu_members(g, place) & NO_MEMBER)) {
            if (menu_members(g, place) > LAST_CHARACTER)
                return false;
            listed++;
        }
    return goods_end_in_the_list(g) && listed <= MENU_MEMBER_WEARS_ITEM_COUNT && reachable >= 1 && reachable <= listed
           && shop_price_scale(g) < 0x200 && menu_window_list_drawable(g, SHOP_WINDOWS);
}

/* The members the shop shows are characters, ending within the list. */
bool menu_shop_members_are_shown(const GameState *g)
{
    for (unsigned m = 0; m < MENU_SHOP_MEMBERS_COUNT; m++) {
        uint8_t c = menu_shop_members(g, m);
        if (c & NO_MEMBER)
            return m <= MENU_MEMBER_WEARS_ITEM_COUNT;
        if (c > LAST_CHARACTER)
            return false;
    }
    return false;
}

/* A list's rows stay within its table: the goods' thirty-two (the last rows
 * past the goods are empty places of it), the places for sale a byte's. */
bool menu_shop_on_a_list(const GameState *g, bool selling)
{
    if (!menu_shop_members_are_shown(g))
        return false;
    if (selling)
        return menu_cursor_index(g) < SHOP_LIST_ROWS;
    return menu_shop_buy_top(g) + SHOP_LIST_ROWS <= MENU_SHOP_GOODS_COUNT && menu_cursor_index(g) < SHOP_LIST_ROWS
           && menu_shop_goods_offered(g) <= MENU_SHOP_GOODS_COUNT;
}

bool menu_shop_gold_within_the_most(const GameState *g) { return gold(g) <= MOST_GOLD; }
