/* A member's figures as the menus work them out: what its stats come to with
 * what it wears, and its attack, defence and greatest HP; and the party's
 * items set aside where the story holds them.
 *
 * A character's record keeps its seven stats as they are (power, stamina,
 * speed, magic, hit, evade, magic defence) and the figures they come to,
 * which the screens show and the battle reads. Each piece of equipment can
 * raise some stats by an amount (its boost); a weapon has an attack and
 * armour and a helmet a defence; an earring raises the greatest HP. The
 * figures are the stats with every boost added, each held to its most; the
 * attack is worked out from a stat and the weapon, by character (each fights
 * in its own way, Ayla with her fists by her level); the defence is stamina
 * and the armour's and helmet's defence. The menus work them out on the copy
 * of the record the windows draw from (menu_status_record), loaded first,
 * and a screen that changes what a member wears writes them back to the
 * member's own record (menu_note_member_figures). The shop works them out
 * for an item a member could buy, without writing them back.
 *
 * Where the story holds the party to what it has (menu_story_items), the
 * screens that list the party's items set them aside first, so the lists
 * show none, and put them back after. */
#include "menu/figures.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/bridge.h"
#include "menu/items.h"
#include "menu/members.h"
#include "menu/menu_data.h"
#include "menu/party.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

#include "battle/equipment_data.h"

/* ---- The figures -------------------------------------------------------------------------- */

/* The stats, by their places in the record and among the figures; then
 * the figures' attack and defence. */
enum { POWER = 0, STAMINA = 1, HIT = 4, EVADE = 5, ATTACK = 7, DEFENCE = 8 };

/* What the equipment adds, as the working out goes: each stat's boosts, the
 * weapon's attack and the armour's and helmet's defence. */
typedef struct {
    uint8_t boosts[MENU_STATS];
    uint8_t attack;
    uint8_t defence;
} Equipment;

/* The equipment's slots, the order the working out takes them in. */
static const MenuItemCategory slots_last_first[MENU_EQUIPMENT_SLOTS] = { MENU_ACCESSORIES, MENU_HELMETS, MENU_ARMOUR, MENU_WEAPONS };

static uint8_t worn(const GameState *g, MenuItemCategory slot)
{
    return menu_status_record_bytes_byte(g, menu_category_slot_place(g, (uint8_t)slot));
}

/* An item's boost: some stats, a bit each from the top in the caps' order
 * (the last bit no stat), raised by its amount. A byte each, wrapping as
 * the original's do. */
enum { NO_STAT_BIT = 0x01, FIRST_STAT_BIT = 0x80 };

static void add_boost(const GameState *g, Equipment *e, uint8_t item)
{
    uint8_t boost = menu_item_amounts(g, item);
    if (boost == NO_BOOST)
        return;
    StatBoost b = stat_boost(g, boost);
    uint8_t stats = (uint8_t)(b.stats & ~NO_STAT_BIT);
    for (unsigned stat = 0; stats != 0; stat++, stats = (uint8_t)(stats << 1))
        if (stats & FIRST_STAT_BIT) {
            uint8_t place = menu_stat_place(g, stat);
            e->boosts[place] = (uint8_t)(e->boosts[place] + b.amount);
        }
}

/* An earring raises the greatest HP; HP over it comes down to it. */
static uint16_t greatest_hp_with(GameState *g, uint8_t accessory)
{
    uint16_t greatest = party_greatest_hp(menu_status_record_max_hp(g), accessory);
    if (greatest < menu_status_record_hp(g))
        set_menu_status_record_hp(g, greatest);
    return greatest;
}

// $C2:9261 (with $C2:9276-$C2:92F3, a slot each)
static Equipment add_up_the_equipment(GameState *g, uint16_t *greatest_hp)
{
    Equipment e = { { 0 }, 0, 0 };
    for (unsigned i = 0; i < MENU_EQUIPMENT_SLOTS; i++) {
        MenuItemCategory slot = slots_last_first[i];
        uint8_t item = worn(g, slot);
        if (slot == MENU_WEAPONS)
            e.attack = menu_item_powers(g, item);
        else if (slot == MENU_ACCESSORIES)
            *greatest_hp = greatest_hp_with(g, item);
        else
            e.defence = (uint8_t)(e.defence + menu_item_powers(g, item));
        add_boost(g, &e, item);
    }
    return e;
}

/* How each character fights, which its attack comes from: a weapon wielded
 * with power, a gun or a bow aimed with hit, or Ayla's fists, which grow
 * with her level. */
typedef enum { WIELDS, AIMS, FISTS } Fighting;
static const Fighting fighting[8] = { WIELDS, AIMS, AIMS, WIELDS, WIELDS, FISTS, WIELDS, WIELDS };

/* The attack: a wielded weapon's five times its attack and twelve times the
 * power, in ninths; an aimed one's attack and the hit, doubled, in thirds;
 * Ayla's fists' power for her level and sixteen times her power, in
 * ninths. */
enum { WEAPON_SHARE = 5, POWER_SHARE = 12, WIELDED_BY = 9, AIMED_BY = 3, FIST_POWER_SHARE = 16 };

/* The attack's sum and what it is divided by. */
typedef struct {
    uint16_t sum;
    uint8_t by;
} AttackSum;

static AttackSum attack_sum(const GameState *g, Fighting how, uint8_t weapon_attack, const uint8_t *figures)
{
    uint8_t power = figures[POWER];
    switch (how) {
    case AIMS: return (AttackSum){ (uint16_t)((figures[HIT] + weapon_attack) * 2), AIMED_BY };
    case FISTS:
        return (AttackSum){ (uint16_t)(menu_fist_power(g, menu_status_record_level(g)) + power * FIST_POWER_SHARE), WIELDED_BY };
    case WIELDS: break;
    }
    return (AttackSum){ (uint16_t)(weapon_attack * WEAPON_SHARE + power * POWER_SHARE), WIELDED_BY };
}

/* A figure past 255 shows as 255. */
enum { MOST = 0xFF };

static uint8_t held_to_a_byte(unsigned v) { return v > MOST ? MOST : (uint8_t)v; }

/* For the original's code and shadow mode: the slot the working out counts
 * down to, past the weapon's; the menus' first cells, where it shifts each
 * boost's stats out (leaving 0 beside the last boost's amount) and then
 * keeps the weapon's part of the attack's sum (five times its attack, or an
 * aimed weapon's attack); the multiplier as the attack's sum leaves it; and
 * the divider. */
enum { PAST_THE_WEAPON = 0xFF };

static void leave_boosts_shifted(GameState *g)
{
    for (unsigned i = MENU_EQUIPMENT_SLOTS; i-- > 0;) {
        uint8_t boost = menu_item_amounts(g, worn(g, slots_last_first[i]));
        if (boost == NO_BOOST)
            continue;
        set_menu_scratch_figures_weapon_part(g, (uint16_t)(stat_boost(g, boost).amount << 8));
        return;
    }
}

static void leave_divided(GameState *g, Fighting how, uint8_t weapon_attack, const uint8_t *figures, AttackSum a)
{
    set_menu_figures_slot(g, PAST_THE_WEAPON);
    leave_boosts_shifted(g);
    if (how == WIELDS)
        set_menu_scratch_figures_weapon_part(g, (uint16_t)(weapon_attack * WEAPON_SHARE));
    else if (how == AIMS)
        set_menu_scratch_figures_weapon_part(g, weapon_attack);
    if (how != AIMS) {
        hw_set_multiplicand(g, how == WIELDS ? POWER_SHARE : FIST_POWER_SHARE);
        (void)hw_multiply_by(g, figures[POWER]);
    }
    hw_set_dividend(g, a.sum);
    hw_divide_by(g, a.by);
}

// $C2:916E
void menu_work_out_figures(GameState *g)
{
    uint16_t greatest_hp = 0;
    Equipment e = add_up_the_equipment(g, &greatest_hp);
    for (unsigned place = 0; place < MENU_STATS; place++)
        set_menu_figures_boosts(g, place, e.boosts[place]);
    set_menu_figures_weapon_attack(g, e.attack);
    set_menu_figures_armour_defence(g, e.defence);
    uint8_t figures[MENU_STATS];
    for (unsigned stat = MENU_STATS; stat-- > 0;) {
        uint8_t place = menu_stat_place(g, stat);
        uint8_t v = (uint8_t)(menu_status_record_stats_byte(g, place) + e.boosts[place]);
        figures[place] = v >= menu_stat_cap(g, stat) ? menu_stat_cap(g, stat) : v;
        set_menu_figures_worked_out(g, place, figures[place]);
    }
    set_menu_figures_greatest_hp(g, greatest_hp);
    Fighting how = fighting[menu_status_record_character(g) & 7];
    AttackSum a = attack_sum(g, how, e.attack, figures);
    set_menu_figures_worked_out(g, DEFENCE, held_to_a_byte((unsigned)figures[STAMINA] + e.defence));
    set_menu_figures_worked_out(g, ATTACK, held_to_a_byte(a.sum / a.by));
    ORIGINAL_EFFECT(leave_divided(g, how, e.attack, figures, a));
}

/* An accessory that doubles evade, which the menus count as they write the
 * figures back. */
enum { EVADE_DOUBLER = 0xA8 };

void menu_work_out_figures_shown(GameState *g)
{
    menu_work_out_figures(g);
    if (worn(g, MENU_ACCESSORIES) == EVADE_DOUBLER)
        set_menu_figures_worked_out(g, EVADE, (uint8_t)(menu_figures_worked_out(g, EVADE) << 1));
}

// $C2:9137
void menu_note_member_figures(GameState *g)
{
    menu_work_out_figures_shown(g);
    uint8_t character = menu_member_character(g);
    set_character_records_hp(g, character, menu_status_record_hp(g));
    for (unsigned i = 0; i < MENU_FIGURES_WORKED_OUT_COUNT; i++) {
        set_character_records_figures_byte(g, character, i, menu_figures_worked_out(g, i));
        set_menu_status_record_figures_byte(g, i, menu_figures_worked_out(g, i));
    }
    set_character_records_hp_cap(g, character, menu_figures_greatest_hp(g));
    set_menu_status_record_hp_cap(g, menu_figures_greatest_hp(g));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * (MENU_FIGURES_WORKED_OUT_COUNT + 2)));
}

// $C2:99B2
void menu_note_everyones_figures(GameState *g)
{
    for (uint8_t character = 0; character < CHARACTER_RECORDS_COUNT; character++) {
        (void)menu_load_character(g, character);
        TIMED_AS(g, 0xC29137, menu_note_member_figures(g));
    }
    ORIGINAL_EFFECT(set_menu_scratch_characters_noted(g, CHARACTER_RECORDS_COUNT));
}

/* ---- The party's items set aside ------------------------------------------------------------ */

/* The inventory's items and counts, each a run of 256 places. */
enum { PLACES = 0x100, INVENTORY_BYTES = 2 * PLACES };

static bool story_holds_the_items(const GameState *g) { return menu_story_items(g, 1) != 0; }

// $C2:90FC
void menu_set_items_aside(GameState *g)
{
    if (!story_holds_the_items(g))
        return;
    wram_copy(g, RAM_INVENTORY_ITEMS, RAM_MENU_ITEMS_SET_ASIDE, INVENTORY_BYTES);
    wram_fill8(g, RAM_INVENTORY_ITEMS, PLACES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, INVENTORY_BYTES + PLACES));
}

// $C2:9121
void menu_put_items_back(GameState *g)
{
    if (!story_holds_the_items(g))
        return;
    wram_copy(g, RAM_MENU_ITEMS_SET_ASIDE, RAM_INVENTORY_ITEMS, INVENTORY_BYTES);
    ORIGINAL_EFFECT(charge_block_move(g, INVENTORY_BYTES));
}

/* ---- The entries ------------------------------------------------------------------------------ */

/* What the working out hands back in A: the divider's, as the attack's sum
 * and quotient leave it (the sum's high byte, the quotient's). Written
 * back, A is what the block moves of the figures leave it; and once every
 * character's are, the count of characters. */
static void leave_a_divided(Snes *s)
{
    GameState *g = state_of(s);
    s->cpu.a = (uint16_t)((hw_dividend(g) & 0xFF00) | hw_quotient(g) >> 8);
}

static void leave_a(Snes *s, uint16_t a) { s->cpu.a = a; }

enum { BLOCK_MOVED = 0xFFFF };

// $C2:916E
void menu_work_out_figures_entry(Snes *s)
{
    menu_work_out_figures(state_of(s));
    ORIGINAL_EFFECT(leave_a_divided(s));
    m_rts(s);
}

// $C2:9137
void menu_note_member_figures_entry(Snes *s)
{
    menu_note_member_figures(state_of(s));
    ORIGINAL_EFFECT(leave_a(s, BLOCK_MOVED));
    m_rts(s);
}

// $C2:99B2
void menu_note_everyones_figures_entry(Snes *s)
{
    menu_note_everyones_figures(state_of(s));
    ORIGINAL_EFFECT(leave_a(s, CHARACTER_RECORDS_COUNT));
    m_rts(s);
}

/* The setting aside and the putting back leave A as their block moves do,
 * or as the story's word reads where it holds nothing. */
static void leave_set_aside_or_not(Snes *s)
{
    GameState *g = state_of(s);
    s->cpu.a = story_holds_the_items(g) ? BLOCK_MOVED : menu_story_items(g, 1);
}

// $C2:90FC
void menu_set_items_aside_entry(Snes *s)
{
    menu_set_items_aside(state_of(s));
    ORIGINAL_EFFECT(leave_set_aside_or_not(s));
    m_rts(s);
}

// $C2:9121
void menu_put_items_back_entry(Snes *s)
{
    menu_put_items_back(state_of(s));
    ORIGINAL_EFFECT(leave_set_aside_or_not(s));
    m_rts(s);
}

/* The working out runs on the menus' page with the data bank on work RAM's,
 * for a loaded record whose weapon, armour and helmet have a power in the
 * menus' table (menu_item_powers) and whose equipment has data
 * (menu_item_amounts): the game's always have. */
static bool an_item_of_the_games(uint8_t item, MenuItemCategory slot)
{
    return item < MENU_ITEM_AMOUNTS_COUNT && (slot == MENU_ACCESSORIES || item < MENU_ITEM_POWERS_COUNT);
}

static bool equipment_is_the_games(const GameState *g)
{
    for (unsigned slot = 0; slot < MENU_EQUIPMENT_SLOTS; slot++)
        if (!an_item_of_the_games(worn(g, (MenuItemCategory)slot), (MenuItemCategory)slot))
            return false;
    return true;
}

/* ---- Trying an item on -------------------------------------------------------------------- */

void menu_try_on(GameState *g, uint8_t item)
{
    set_menu_item_operand(g, item);
    set_menu_status_record_bytes_byte(g, menu_equip_record_place(g), item);
}

static uint8_t figure_shown(const GameState *g, MenuFiguresWindow window, unsigned i)
{
    return window == FIGURES_IN_THE_SHOP ? menu_shop_figure_shown(g, i) : menu_equipment_figure_shown(g, i);
}

// $C2:A5E7, $C2:E18A
void menu_mark_the_figures_tried_on(GameState *g, MenuFiguresWindow window)
{
    bool shop = window == FIGURES_IN_THE_SHOP;
    unsigned shown = shop ? MENU_SHOP_FIGURES_SHOWN : MENU_EQUIPMENT_FIGURES_SHOWN;
    TIMED_AS(g, 0xC2916E, menu_work_out_figures_shown(g));
    for (unsigned i = 0; i < shown; i++) {
        uint8_t figure = figure_shown(g, window, i);
        uint8_t was = menu_status_record_figures_byte(g, figure), is = menu_figures_worked_out(g, figure);
        if (shop)
            set_menu_status_record_figures_byte(g, figure, is);
        set_menu_figure_marks(g, i, menu_figure_mark(was, is));
    }
}

bool menu_figures_workable_with(const GameState *g, uint8_t place, uint8_t item)
{
    for (unsigned slot = 0; slot < MENU_EQUIPMENT_SLOTS; slot++) {
        bool tried = menu_category_slot_place(g, (uint8_t)slot) == place;
        if (!an_item_of_the_games(tried ? item : worn(g, (MenuItemCategory)slot), (MenuItemCategory)slot))
            return false;
    }
    return true;
}

bool menu_figures_can_be_worked_out(const GameState *g) { return equipment_is_the_games(g); }

bool menu_figures_are_workable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = equipment_is_the_games(state_of(s));
    vars_peek(false);
    return ok;
}

/* ... and writing them back, for a member loaded from a character's
 * record. */
bool menu_member_figures_are_notable(Snes *s)
{
    if (!menu_figures_are_workable(s))
        return false;
    vars_peek(true);
    bool ok = menu_member_record_is_a_characters(state_of(s));
    vars_peek(false);
    return ok;
}

/* Every character's: the characters' records hold the game's equipment. */
bool menu_everyones_figures_can_be_noted(const GameState *g)
{
    bool ok = true;
    for (uint8_t character = 0; ok && character < CHARACTER_RECORDS_COUNT; character++)
        for (unsigned slot = 0; ok && slot < MENU_EQUIPMENT_SLOTS; slot++) {
            uint8_t item = character_records_record_byte(g, character, menu_category_slot_place(g, (uint8_t)slot));
            ok = an_item_of_the_games(item, (MenuItemCategory)slot);
        }
    return ok;
}

bool menu_everyones_figures_are_notable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_everyones_figures_can_be_noted(state_of(s));
    vars_peek(false);
    return ok;
}
