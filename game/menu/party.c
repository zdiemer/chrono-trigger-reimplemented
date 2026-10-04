/* The party as the menus keep it: the record of who is in it and the
 * services that change it and what its members wear (menu/party.h says how
 * the pieces fit). The services' calling convention, the dispatcher the
 * original's code reaches them through, is menu/party_bridge.c. */
#include "menu/party.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/items.h"
#include "menu/menu_data.h"
#include "menu/saves.h"
#include "menu/sprites.h"
#include "menu/techs.h"
#include "original_effects.h"
#include "ram.h"
#include "vars.h"

/* ---- The record ------------------------------------------------------------------------------ */

/* Its storage: the active party's three places, then the reserve's six. */
static uint8_t at_place(const GameState *g, RecordPlace place)
{
    return place < PARTY_ACTIVE_PLACES ? party_members(g, place) : party_waiting(g, place - PARTY_ACTIVE_PLACES);
}

static void set_at_place(GameState *g, RecordPlace place, uint8_t v)
{
    if (place < PARTY_ACTIVE_PLACES)
        set_party_members(g, place, v);
    else
        set_party_waiting(g, place - PARTY_ACTIVE_PLACES, v);
}

static bool is_empty(uint8_t at) { return (at & PARTY_PLACE_EMPTIED) != 0; }

static void empty_the_place(GameState *g, RecordPlace place)
{
    set_at_place(g, place, (uint8_t)(at_place(g, place) | PARTY_PLACE_EMPTIED));
}

bool party_record_shows_characters(const GameState *g)
{
    for (RecordPlace place = 0; place < PARTY_ACTIVE_PLACES; place++) {
        uint8_t member = at_place(g, place);
        if (!is_empty(member) && member >= CHARACTER_RECORDS_COUNT)
            return false;
    }
    return true;
}

// $C2:8C75, $C2:8CCF
RecordPlace party_place_of(const GameState *g, uint8_t character, unsigned places)
{
    for (RecordPlace place = 0; place < places; place++)
        if (at_place(g, place) == character)
            return place;
    return NOT_IN_THE_RECORD;
}

/* The first empty place from `from` up to `to`, or NOT_IN_THE_RECORD. */
static RecordPlace first_empty(const GameState *g, RecordPlace from, RecordPlace to)
{
    for (RecordPlace place = from; place < to; place++)
        if (is_empty(at_place(g, place)))
            return place;
    return NOT_IN_THE_RECORD;
}

/* A character takes an empty place, and has joined the party. */
static void take_place(GameState *g, RecordPlace place, uint8_t character)
{
    set_party_characters_joined(g, (uint8_t)(party_characters_joined(g) | menu_character_bit(g, character)));
    set_at_place(g, place, character);
}

// $C2:8CBE
/* The reserve takes `whom` at its front: each member moves a place back,
 * until one moves into an empty place, which closes; with none, the last
 * leaves the record. */
static ReserveTaking reserve_takes(GameState *g, uint8_t whom)
{
    uint8_t moving = whom;
    for (RecordPlace place = PARTY_ACTIVE_PLACES; place < PARTY_RECORD_PLACES; place++) {
        uint8_t there = at_place(g, place);
        set_at_place(g, place, moving);
        if (is_empty(there))
            return (ReserveTaking){ place, there };
        moving = there;
    }
    return (ReserveTaking){ PARTY_RECORD_PLACES, moving };
}

// $C2:8CE5
PartyRecruit party_recruit(GameState *g, uint8_t character)
{
    RecordPlace place = party_place_of(g, character, PARTY_RECORD_PLACES);
    if (place != NOT_IN_THE_RECORD)
        return (PartyRecruit){ PARTY_ALREADY_RECRUITED, place };
    place = first_empty(g, PARTY_ACTIVE_PLACES, PARTY_RECORD_PLACES);
    if (place == NOT_IN_THE_RECORD)
        return (PartyRecruit){ PARTY_NO_ROOM, NOT_IN_THE_RECORD };
    take_place(g, place, character);
    return (PartyRecruit){ PARTY_RECRUITED, place };
}

// $C2:8CF7
RecordPlace party_dismiss(GameState *g, uint8_t character)
{
    RecordPlace place = party_place_of(g, character, PARTY_RECORD_PLACES);
    if (place != NOT_IN_THE_RECORD)
        empty_the_place(g, place);
    return place;
}

/* The active party's last place. */
enum { THIRD_PLACE = PARTY_ACTIVE_PLACES - 1 };

// $C2:8C79
/* The character leaves wherever it was first, so a member of the active
 * party (or of the reserve) moves rather than appearing twice. Only one
 * taking an empty place counts as joining (party_characters_joined). */
PartyBringingIn party_bring_in(GameState *g, uint8_t character)
{
    (void)party_dismiss(g, character);
    RecordPlace place = first_empty(g, 0, PARTY_ACTIVE_PLACES);
    if (place != NOT_IN_THE_RECORD) {
        take_place(g, place, character);
        return (PartyBringingIn){ true, place, { 0, 0 } };
    }
    uint8_t third = at_place(g, THIRD_PLACE);
    set_at_place(g, THIRD_PLACE, character);
    return (PartyBringingIn){ false, THIRD_PLACE, reserve_takes(g, third) };
}

// $C2:8CA2
PartySending party_send_to_the_reserve(GameState *g, uint8_t character)
{
    RecordPlace place = party_place_of(g, character, PARTY_ACTIVE_PLACES);
    if (place == NOT_IN_THE_RECORD)
        return (PartySending){ false, { 0, 0 } };
    for (; place < THIRD_PLACE; place++)
        set_at_place(g, place, at_place(g, (RecordPlace)(place + 1)));
    empty_the_place(g, THIRD_PLACE);
    return (PartySending){ true, reserve_takes(g, character) };
}

/* ---- HP and MP ------------------------------------------------------------------------------ */

/* The earrings that raise a character's greatest HP, and by how much: a
 * quarter, a half. HP goes to 999 at most. */
enum { SILVER_EARRING = 0xA0, GOLD_EARRING = 0xA1, MOST_HP = 999 };

uint16_t party_greatest_hp(uint16_t max_hp, uint8_t accessory)
{
    uint16_t raised = accessory == GOLD_EARRING ? (uint16_t)(max_hp / 2)
                      : accessory == SILVER_EARRING ? (uint16_t)(max_hp / 4)
                                                    : 0;
    uint16_t hp = (uint16_t)(max_hp + raised);
    return hp >= MOST_HP ? MOST_HP : hp;
}

/* The character's equipment, by slot (its category). */
static uint8_t worn(const GameState *g, uint8_t character, unsigned slot)
{
    return character_records_record_byte(g, character, menu_category_slot_place(g, (uint8_t)slot));
}

// $C2:8D1C
static void fill_hp_of(GameState *g, uint8_t character)
{
    set_character_records_hp(g, character, party_greatest_hp(character_records_max_hp(g, character), worn(g, character, MENU_ACCESSORIES)));
}

// $C2:8D05
void party_fill_hp(GameState *g)
{
    for (uint8_t character = 0; character < CHARACTER_RECORDS_COUNT; character++)
        fill_hp_of(g, character);
}

// $C2:8D45
/* MP is 99 at most: the original fills its low byte, and the high byte
 * stays as it is. */
void party_fill_mp(GameState *g)
{
    for (uint8_t character = 0; character < CHARACTER_RECORDS_COUNT; character++)
        set_character_records_mp(g, character,
                                 (uint16_t)((character_records_mp(g, character) & 0xFF00) | (uint8_t)character_records_max_mp(g, character)));
}

/* ---- Equipment ------------------------------------------------------------------------------ */

/* Ayla fights with her fists: stripping her leaves her weapon slot alone. */
enum { AYLA = 5 };

/* What a slot holds when nothing is in it, by slot. */
static const uint8_t slots_nothing[MENU_EQUIPMENT_SLOTS] = { MENU_NO_WEAPON, MENU_NO_ARMOUR, MENU_NO_HELMET, MENU_NO_ACCESSORY };

static void put_in_slot(GameState *g, uint8_t character, unsigned slot, uint8_t item)
{
    set_character_records_record_byte(g, character, menu_category_slot_place(g, (uint8_t)slot), item);
}

/* For the original's code and shadow mode: the item and how many the
 * equipping keeps, and the record's place found with the multiplier; the
 * place, where it goes on to a slot; and the cell it counts the slots in
 * (the looked-up category's, a word). */
enum { RECORD_BYTES = 0x50 };

static void leave_equipping(GameState *g, uint8_t character, uint8_t item, uint8_t how_many)
{
    set_menu_item_operand_quantity(g, how_many);
    set_menu_service_item(g, item);
    hw_set_multiplicand(g, character);
    (void)hw_multiply_by(g, RECORD_BYTES);
}

static void leave_record_found(GameState *g, uint8_t character)
{
    set_menu_service_record_at(g, (uint16_t)(RAM_ABS(RAM_CHARACTER_RECORDS) + character * RECORD_BYTES));
}

static void leave_slots_counted(GameState *g) { set_menu_item_category(g, MENU_EQUIPMENT_SLOTS); }

// $C2:8DA7
static void strip(GameState *g, uint8_t character)
{
    for (unsigned slot = character == AYLA ? MENU_ARMOUR : MENU_WEAPONS; slot < MENU_EQUIPMENT_SLOTS; slot++)
        put_in_slot(g, character, slot, slots_nothing[slot]);
    ORIGINAL_EFFECT(leave_slots_counted(g));
}

// $C2:8D8F, $C2:8DE3
void party_equip(GameState *g, uint8_t character, uint8_t item)
{
    enum { ONE = 1 };
    ORIGINAL_EFFECT(leave_equipping(g, character, item, ONE));
    if (item == MENU_NO_WEAPON) {
        ORIGINAL_EFFECT(leave_record_found(g, character));
        strip(g, character);
    } else {
        MenuItemPlace p;
        TIMED_AS(g, 0xC28881, p = menu_item_place(g, item));
        if (p.category >= MENU_EQUIPMENT_SLOTS)
            return;
        ORIGINAL_EFFECT(leave_record_found(g, character));
        uint8_t was = worn(g, character, p.category);
        ORIGINAL_EFFECT(set_menu_item_operand(g, was));
        TIMED_AS(g, 0xC28791, (void)menu_add_items(g, was, ONE));
        ORIGINAL_EFFECT(set_menu_item_operand(g, item));
        put_in_slot(g, character, p.category, item);
        TIMED_AS(g, 0xC287D5, (void)menu_take_items(g, item, ONE));
    }
    fill_hp_of(g, character);
}

/* ---- What the party wears, shown ------------------------------------------------------------ */

/* A weapon's colours (menu/sprites.h) go in the last three of its wearer's
 * sprite palette on the map. The active members' palettes are the sixth to
 * eighth sprite palettes, in the order of the members there are. */
enum { FIRST_MEMBER_COLOUR = 0xDD, PALETTE_COLOURS = 16 };

static void show_weapon_colours(GameState *g, uint8_t character, unsigned palette)
{
    MenuWeaponColours c = menu_weapon_colours(g, character);
    for (unsigned i = 0; i < MENU_WEAPON_COLOURS; i++)
        set_palette_base(g, FIRST_MEMBER_COLOUR + palette * PALETTE_COLOURS + i, c.colour[i]);
}

/* The rocks a character can wear, the first a rock's triple tech's place
 * among the triple techs learned (MENU_FIRST_ROCK_TECH on). */
enum { FIRST_ROCK = 0xAE, LEARNED = 0x80 };

static bool is_a_rock(uint8_t accessory) { return accessory >= FIRST_ROCK && accessory < FIRST_ROCK + MENU_ROCKS; }

// $C2:82E1
void party_rocks_teach(GameState *g)
{
    for (unsigned rock = 0; rock < MENU_ROCKS; rock++)
        set_party_triple_techs_learned(g, MENU_FIRST_ROCK_TECH + rock, 0);
    for (uint8_t character = 0; character < CHARACTER_RECORDS_COUNT; character++) {
        uint8_t accessory = worn(g, character, MENU_ACCESSORIES);
        if (is_a_rock(accessory) && menu_rock_techs_ready(g, accessory - FIRST_ROCK))
            set_party_triple_techs_learned(g, MENU_FIRST_ROCK_TECH + (accessory - FIRST_ROCK), LEARNED);
    }
}

// $C2:834D
void party_wear_it_shows(GameState *g)
{
    unsigned palette = 0;
    for (RecordPlace place = 0; place < PARTY_ACTIVE_PLACES; place++) {
        uint8_t member = at_place(g, place);
        if (!is_empty(member))
            show_weapon_colours(g, member, palette++);
    }
    TIMED_AS(g, 0xFFF958, menu_note_rock_techs(g));
    party_rocks_teach(g);
}

// $C2:8D63
void party_note_the_game_finished(Platform *pf) { saves_note_finished(pf, PF_AT(0xC28D65)); }
