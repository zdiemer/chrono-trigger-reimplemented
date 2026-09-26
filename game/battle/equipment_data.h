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

enum { EQUIPMENT_BANK = 0xCC0000u };

static inline uint8_t equipment_bank_byte(const GameState *g, uint16_t place) { return rom_read8(g, EQUIPMENT_BANK | place); }

/* The item ranges, each from its first. */
enum { FIRST_ARMOR = 0x5A, FIRST_HELMET = 0x7B, FIRST_ACCESSORY = 0x94, FIRST_OTHER_ITEM = 0xBC, ITEMS_RECORDED = 0xF2 };

/* $FD:B033's ranges: the records of the range an item is in, from the first
 * item's, `bytes` long each; an item past the recorded ones has none
 * (`bytes` 0). */
typedef struct {
    uint8_t first, bytes;
    uint16_t place;
} ItemRecords;

static inline ItemRecords item_records_of(uint8_t item)
{
    if (item < FIRST_ARMOR)
        return (ItemRecords){ 0, 5, 0x0262 };
    if (item < FIRST_HELMET)
        return (ItemRecords){ FIRST_ARMOR, 3, 0x047E };
    if (item < FIRST_ACCESSORY)
        return (ItemRecords){ FIRST_HELMET, 3, 0x04E1 };
    if (item < FIRST_OTHER_ITEM)
        return (ItemRecords){ FIRST_ACCESSORY, 4, 0x052C };
    if (item < ITEMS_RECORDED)
        return (ItemRecords){ FIRST_OTHER_ITEM, 4, 0x05CC };
    return (ItemRecords){ 0, 0, 0 };
}

/* The weapons', armors' and helmets' second records, six bytes each, by the
 * piece's number from its range's first. */
typedef enum { WEAPON_BOOSTS, ARMOR_BOOSTS, HELMET_BOOSTS } EquipmentKind;
enum { BOOST_RECORD_BYTES = 6, BOOST_BYTE = 4, AFFINITY_BYTE = 5 };

static inline uint16_t boost_records_place(EquipmentKind kind)
{
    static const uint16_t first[] = { 0x06A4, 0x08C0, 0x0986 };
    return first[kind];
}

/* $CC:29D7: the stat boosts, two bytes each: which stats (a bit each), and
 * by how much. Where a record names none its byte is 0. */
typedef struct {
    uint8_t stats, amount;
} StatBoost;

enum { STAT_BOOSTS = 0x29D7, NO_BOOST = 0 };

static inline uint16_t stat_boost_place(uint8_t boost) { return (uint16_t)(boost * 2 + STAT_BOOSTS); }

static inline StatBoost stat_boost(const GameState *g, uint8_t boost)
{
    uint16_t at = stat_boost_place(boost);
    return (StatBoost){ equipment_bank_byte(g, at), equipment_bank_byte(g, (uint16_t)(at + 1)) };
}

/* A table of the ROM's the stat setup names beside the boosts, which nothing
 * the setup does reads ($CC:2A03). */
enum { STAT_SETUP_SPARE_TABLE = 0x2A03 };

/* The other items' battle records ($CC:0ABC, three bytes an item from $BC):
 * the first byte's top bit says the battle uses the item, and the rest go in
 * its place in the item list with its menu detail ($CC:1BB5, a byte an
 * item). */
enum { USED_IN_BATTLE = 0x80 };

typedef struct {
    bool used_in_battle;
    uint8_t flags, menu_detail;
} ItemInBattle;

static inline ItemInBattle item_in_battle(const GameState *g, uint8_t item)
{
    uint8_t number = (uint8_t)(item - FIRST_OTHER_ITEM);
    uint8_t first = equipment_bank_byte(g, (uint16_t)(0x0ABC + (uint8_t)(number * 3)));
    return (ItemInBattle){ (first & USED_IN_BATTLE) != 0, (uint8_t)(first & ~USED_IN_BATTLE),
                           equipment_bank_byte(g, (uint16_t)(0x1BB5 + number)) };
}

#endif
