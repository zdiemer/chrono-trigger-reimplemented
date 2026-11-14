/* Battle: the records of the party's equipment and items, as the ROM holds
 * them in bank CC, for setting the members' stats up and listing the items
 * the battle can use.
 *
 * Every item has a record, found by its number in five ranges: the weapons
 * (five bytes a record), the armors, the helmets (three each), the
 * accessories and the other items (four each); the setup copies a member's
 * equipment's records into its stat block. The weapons, armors and helmets
 * have a second record too, six bytes, whose fifth byte names a boost to the
 * member's stats and whose sixth adds to its elemental affinities. The other
 * items have a third, which says whether the battle uses one. */
#ifndef CT_GAME_BATTLE_EQUIPMENT_DATA_H
#define CT_GAME_BATTLE_EQUIPMENT_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "rom.h"
#include "state.h"

/* The item ranges, each from its first. */
enum {
    FIRST_ARMOR = 0x5A,
    FIRST_HELMET = 0x7B,
    FIRST_ACCESSORY = 0x94,
    FIRST_OTHER_ITEM = 0xBC,
    ITEMS_RECORDED = 0xF2,
};

/* $FD:B033's ranges: the records of the range an item is in, from the first
 * item's, `bytes` long each; an item past the recorded ones has none
 * (`bytes` 0). */
typedef struct {
    uint8_t first, bytes;
    uint16_t place;
} ItemRecords;

/* The weapons', armors' and helmets' second records, six bytes each, by the
 * piece's number from its range's first. */
typedef enum { WEAPON_BOOSTS, ARMOR_BOOSTS, HELMET_BOOSTS } EquipmentKind;
enum { BOOST_RECORD_BYTES = 6, BOOST_BYTE = 4, AFFINITY_BYTE = 5 };

/* The characters a weapon is made for, a bit each from the top by character
 * (the fourth byte of its second record, $CC:06A7, six bytes a weapon; the
 * place is taken in a word, as the original's multiply gives it). */
enum { WEAPON_SECOND_RECORD_BYTES = 6 };

/* $CC:29D7: the stat boosts, two bytes each: which stats (a bit each), and
 * by how much. Where a record names none its byte is 0. */
typedef struct {
    uint8_t stats, amount;
} StatBoost;

enum { NO_BOOST = 0 };

/* Where the boosts are in their bank, which the setup leaves in a cell; and
 * a table of the ROM's the stat setup names beside them, which nothing the
 * setup does reads ($CC:2A03). */
static inline uint16_t stat_boosts_place(void) { return (uint16_t)asset_address(ASSET_STAT_BOOSTS); }
static inline uint16_t stat_setup_spare_table_place(void) { return (uint16_t)asset_address(ASSET_STAT_SETUP_SPARE_TABLE); }

/* The other items' battle records ($CC:0ABC, three bytes an item from $BC):
 * the first byte's top bit says the battle uses the item, and the rest go in
 * its place in the item list with its menu detail ($CC:1BB5, a byte an
 * item). */
enum { USED_IN_BATTLE = 0x80 };

typedef struct {
    bool used_in_battle;
    uint8_t flags, menu_detail;
} ItemInBattle;

/* A byte of bank CC where a place the records or the setup's cells give
 * leads: the equipment's records link to one another, and to the boosts, by
 * place in their bank. */
static inline uint8_t equipment_bank_byte(const GameState *g, uint16_t place)
{
    return rom_read8_in_bank_of(g, ASSET_WEAPON_RECORDS, place);
}

static inline ItemRecords item_records_of(uint8_t item)
{
    if (item < FIRST_ARMOR)
        return (ItemRecords){ 0, 5, (uint16_t)asset_address(ASSET_WEAPON_RECORDS) };
    if (item < FIRST_HELMET)
        return (ItemRecords){ FIRST_ARMOR, 3, (uint16_t)asset_address(ASSET_ARMOR_RECORDS) };
    if (item < FIRST_ACCESSORY)
        return (ItemRecords){ FIRST_HELMET, 3, (uint16_t)asset_address(ASSET_HELMET_RECORDS) };
    if (item < FIRST_OTHER_ITEM)
        return (ItemRecords){ FIRST_ACCESSORY, 4, (uint16_t)asset_address(ASSET_ACCESSORY_RECORDS) };
    if (item < ITEMS_RECORDED)
        return (ItemRecords){ FIRST_OTHER_ITEM, 4, (uint16_t)asset_address(ASSET_OTHER_ITEM_RECORDS) };
    return (ItemRecords){ 0, 0, 0 };
}

static inline uint16_t boost_records_place(EquipmentKind kind)
{
    static const AssetId boosts[] = { ASSET_WEAPON_BOOST_RECORDS, ASSET_ARMOR_BOOST_RECORDS, ASSET_HELMET_BOOST_RECORDS };
    return (uint16_t)asset_address(boosts[kind]);
}

static inline StatBoost stat_boost(const GameState *g, uint8_t boost)
{
    uint32_t at = (uint32_t)(boost * 2);
    return (StatBoost){ asset_read8(g, ASSET_STAT_BOOSTS, at), asset_read8(g, ASSET_STAT_BOOSTS, at + 1) };
}

static inline ItemInBattle item_in_battle(const GameState *g, uint8_t item)
{
    uint8_t number = (uint8_t)(item - FIRST_OTHER_ITEM);
    uint8_t first = asset_read8(g, ASSET_ITEM_RECORD_FLAGS, (uint8_t)(number * 3));
    return (ItemInBattle){ (first & USED_IN_BATTLE) != 0, (uint8_t)(first & ~USED_IN_BATTLE),
                           asset_read8(g, ASSET_ITEM_AIM_MODE, number) };
}

#endif
