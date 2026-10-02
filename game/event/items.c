/* Scripts and what the party carries: its items and its gold, and the menus a
 * script opens.
 *
 * A script gives the party an item or takes one away, gives or takes gold,
 * and asks whether the party holds an item or has enough gold, as a
 * condition (on after it if so, otherwise its jump). The party's inventory
 * and gold are kept by the battle's code, which the rest of the game asks
 * too (battle/inventory.h); each request is timed as the routine of the
 * battle's that answers it.
 *
 * A script can open one of the menus over the field (field/menus.c says
 * which a number opens) and wait until the player has closed it. */
#include "battle/inventory.h"
#include "charge.h"
#include "event/leaves.h"
#include "event/script.h"
#include "event/variables.h"
#include "field/field.h"

/* ---- Items ------------------------------------------------------------------------------- */

// $C0:37B1, $C0:37C7
/* The party gets one more of an item (none, where it holds 99). */
static EventFlow give_item(GameState *g, uint8_t item)
{
    TIMED_AS(g, 0xC1D005, battle_inventory_add(g, item));
    return next_command();
}
EVENT_OPERATION(give_item, item)

// $C0:37E3
/* The party has one fewer of an item. */
static EventFlow take_item(GameState *g, uint8_t item)
{
    TIMED_AS(g, 0xC1D056, battle_inventory_take(g, item));
    return next_command();
}
EVENT_OPERATION(take_item, item)

// $C0:3780
/* Whether the party holds an item. */
static EventFlow if_party_holds(GameState *g, uint8_t item)
{
    bool held = false;
    TIMED_AS(g, 0xC1CFE9, held = battle_inventory_holds(g, item));
    return unless_jump(held);
}
EVENT_OPERATION(if_party_holds, item)

/* ---- Gold -------------------------------------------------------------------------------- */

// $C0:385B
/* The party gets gold (up to the most it can hold). */
static EventFlow give_gold(GameState *g, uint16_t amount)
{
    TIMED_AS(g, 0xC1D0A2, battle_give_gold(g, amount));
    return next_command();
}
EVENT_OPERATION(give_gold, amount)

// $C0:3874
/* The party pays gold, where it has that much. */
static EventFlow take_gold(GameState *g, uint16_t amount)
{
    TIMED_AS(g, 0xC1D0ED, battle_take_gold(g, amount));
    return next_command();
}
EVENT_OPERATION(take_gold, amount)

// $C0:3827
/* Whether the party has at least so much gold. */
static EventFlow if_party_has_gold(GameState *g, uint16_t amount)
{
    bool has = false;
    TIMED_AS(g, 0xC1D086, has = battle_gold_covers(g, amount));
    return unless_jump(has);
}
EVENT_OPERATION(if_party_has_gold, amount)

/* ---- The menus a script opens ---------------------------------------------------------------- */

// $C0:3757
/* The field's main loop opens the menu at its next turn, and the script waits
 * on the command until the menu has been shown and closed. */
static EventFlow open_menu(GameState *g, uint8_t menu)
{
    if (field_requested(g, FIELD_REQUEST_MENUS))
        return stay();
    bool shown = (field_effect_status(g) & FIELD_SCRIPT_MENU_SHOWN) != 0;
    set_field_effect_status(g, (uint8_t)(field_effect_status(g) & ~FIELD_SCRIPT_MENU_SHOWN));
    if (shown)
        return next_command();
    set_field_script_menu(g, menu);
    field_request(g, FIELD_REQUEST_MENUS);
    return stay();
}
EVENT_OPERATION(open_menu, menu)

EVENT_OPCODES(event_items_opcodes,
    { 0xCA, OPERATION(give_item), .item = OPERAND(1), .length = 2 },
    { 0xC7, OPERATION(give_item), .item = VARIABLE_BYTE(1), .length = 2 },
    { 0xCB, OPERATION(take_item), .item = OPERAND(1), .length = 2 },
    { 0xC9, OPERATION(if_party_holds), .item = OPERAND(1), .length = 3, .jump = 2 },
    { 0xCD, OPERATION(give_gold), .amount = OPERAND_WORD(1), .length = 3 },
    { 0xCE, OPERATION(take_gold), .amount = OPERAND_WORD(1), .length = 3 },
    { 0xCC, OPERATION(if_party_has_gold), .amount = OPERAND_WORD(1), .length = 4, .jump = 3 },
    { 0xC8, OPERATION(open_menu), .menu = OPERAND(1), .length = 2 });
