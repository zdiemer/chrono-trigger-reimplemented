"""The game memory the movie tools read, with how each address was established.

Addresses are SNES bus addresses, as everywhere else in the repo. Names in
parentheses are the ported routines in game/ that use the same address (see
docs/PORTING.md); those are the primary source. The rest were found by running
the machine under ct_drive and diffing work RAM between known states (the way
tools/trace.py diff does by hand): press a button, see which byte moved.

Nothing here is ROM data: these are addresses in the console's work RAM.
"""

# ---- field and map ---------------------------------------------------------------------------

MAP_ID = 0x7E0100
"""Current location id (2 bytes). Changes exactly when the screen changes to
another map; used to recognize doors, gates and map transitions. Found by
sampling work RAM across a replay of an existing movie and keeping the bytes
that changed only at map changes."""

DIALOG_OPEN = 0x7E0127
"""Non-zero while a text window is open and waiting (values seen: $16, $28, $40
for different window kinds). Found by diffing work RAM between states with a
window open (battle dialogue, an NPC line, a Yes/No prompt) and states without
one: this was the only byte that was constant in all the open states and
different in all the closed ones. The movie tools press A only when it is set,
which keeps stray presses out of battle menus."""

OBJECT_X = 0x7E1800
OBJECT_Y = 0x7E1880
"""Field object positions, two bytes each (`OBJECT_X + 2 * i`), in 1/16 pixel:
object 0 is the map's script, the party leader is usually object 1, NPCs follow.
$1800/$1880 are the tables the ported field and battle code indexes ($1D0C/$1D23
in battle_place_menu_cursors are the battle copies). Off-screen objects read
$FF00 (4080.0 in pixels). Which object the pad moves is found at run time
(campaign.Run.find_player) because it is not always object 1."""

WORLD_X = 0x7E0283
WORLD_Y = 0x7E0285
"""Overworld position in pixels (2 bytes each). Found by holding LEFT/RIGHT and
UP/DOWN from a saved state and keeping the words that moved with the direction
and stayed put on the other axis. The overworld moves in 16-pixel steps, so
world-map searches use whole-tile moves (bfs.ow_bfs)."""

OVERWORLD_TRAVEL = 0x7E027E
"""How the party is going about the world map (overworld_travel,
game/overworld/overworld.h): 0 on foot, 2 in the Epoch, 3 on a dactyl, 4
walking to a vehicle to board it, 5 getting off one. The vehicles'
controller (`$C2:309E`) sets it; a route presses confirm next to a vehicle
and watches this turn away from 0 to know boarding started."""

OVERWORLD_DACTYL_STATE = 0x7E029E
"""The dactyl's state on the 65,000,000 B.C. world map (overworld_dactyl's
`state` field, offset 4 of the 5-byte record at $7E:029A): bits 0-1 how many
of the party are aboard, bit 6 it is out (flying or boarded), bit 7 it is on
this map at all (game/overworld/overworld.h, VEHICLE_ABOARD/VEHICLE_OUT/
VEHICLE_HERE). A save earlier in the story than recruiting the dactyls has
bit 7 clear and nothing to board; ctsave07 slot 3 is at Tyrano Lair, already
past that point, standing beside one. Bit 5, set while the landing the route
presses confirm into plays out, was found the same way (single-stepping a
confirm press while flying) since the dactyl's own flight, `$C2:4CDA`-
`$C2:5627`, is still the original's and has no accessor yet."""

EVENT_MEMORY = 0x7F0000
"""Story flags: the event engine's memory, addressed as $7F:0000 + offset by the
script commands ported in game/event/story.c and variables.c ($53-$5A read and write it, $65
and $66 set and clear bits). Two flags the ending routes use:

    $7F:0055 bit 7   the Millennial Fair telepod exhibit is open ("They're still
                     setting up" until then); set by talking to the fountain NPC
                     in Leene Square's lower square
    $7F:0078         the Dream Project development room: one bit per developer
                     talked to, $FF when Keizo Kokubo has opened the Gates
    $7F:0077         the Dream Team room: one bit per member talked to, $1F when
                     all five have spoken

Those were found by scanning the decompressed script in work RAM ($7F:2000, see
game/event/engine.c) for the set-bit command and reading back the byte."""

TELEPOD_EXHIBIT_OPEN = (0x7F0055, 0x80)
DEVELOPERS_TALKED = 0x7F0078
DREAM_TEAM_TALKED = 0x7F0077
"""The three flags above, as (address, bit) or an address whose bits are counted."""

SCRIPT_BUFFER = 0x7F2000
"""Where the current map's event script is decompressed. Its first byte is the
object count, then 16 two-byte function pointers per object. Used only for
diagnostics (which object sets which flag)."""

# ---- battle ----------------------------------------------------------------------------------

BATTLE_MENU_MEMBER = 0x7E95D5
"""The party member whose command menu is open; >= $80 means none. The ported
battle code calls it $95D5 all over game/battle/menu_window.c and window.c."""

BATTLE_MENU_PAGE = 0x7E95DB
"""Menu page: 0 the command list (Attack/Tech/Item), 1 the tech list, 2 the item
list (battle_draw_menu_window, game/battle/menu_window.c)."""

BATTLE_TARGETING = 0x7E9609
"""Non-zero while targets are being chosen; the cursors are then placed on the
objects in BATTLE_TARGETS (battle_place_menu_cursors)."""

BATTLE_TARGETS = 0x7EA62D
"""The chosen targets, one slot number per byte, $FF for unused entries. Entry 0
is the single target; a second entry means the action hits a group."""

BATTLE_COMMAND_CURSOR = 0x7E95DC
"""Command list cursor per member ($95DC + member): 0 Attack, 1 Tech, 2 Item
(clear_member_cells indexes it by member)."""

BATTLE_TECH_CURSOR = 0x7E95DF
BATTLE_TECH_SCROLL = 0x7E95EB
"""Tech list cursor row and scroll per member; the entry under the cursor is
list index `scroll + cursor` (draw_tech_page adds $95EB + member and $95DF +
member before reading the list)."""

BATTLE_ITEM_ROW = 0x7E95E5
BATTLE_ITEM_TOP = 0x7E95E6
"""Item list cursor row on screen and the list index of the top row; the
selected item is `top + row` (place_list_cursor draws the cursor from $95E5 and
the arrows from $95E6)."""

BATTLE_STATS = 0x7E5E30
BATTLE_STAT_STRIDE = 0x80
"""Per-combatant stats, `BATTLE_STATS + slot * $80`: HP, max HP, MP, max MP as
four 16-bit values. Party members are slots 0-2, enemies 3-10. $5E30 is the HP
the ported battle_draw_party_status and battle_stats code read."""

BATTLE_TECH_LIST = 0x7E9551
BATTLE_TECH_LIST_BASE = 0xCCF38C
"""The in-battle tech list: entry `i` of member `m` is at $9551 + ROM byte
$CC:F38C + m + i, and $FF marks a blank row. mark_tech_list and draw_tech_page
(game/battle/menu_window.c) use the same two tables."""

BATTLE_TECH_LIST_IDS = 0x7E94D0
"""Each member's tech list by tech id rather than by record number: entry `i`
of member `m` is at $94D0 + the same $CC:F38C base BATTLE_TECH_LIST uses, plus
`i`. $FC marks the double techs' heading, $FD the triple techs', $FE/$FF a
blank row; anything less is a real tech id (battle_tech_entry_is_a_tech,
game/battle/menu_lists.c). The tech-sweep policy (policies.py) walks this to
find every tech a member's list can show, singles first, then doubles and
triples, without needing to know the party's combination techs by name."""

# ---- inventory -------------------------------------------------------------------------------

INVENTORY_ITEMS = 0x7E2400
INVENTORY_COUNTS = 0x7E2500
INVENTORY_SIZE = 0xF2
"""Item ids and their counts, one byte each. Found by reading $7E:2400 in a
battle and matching the list against the item menu. The battle item list is the
inventory with the items that can't be used in battle skipped, so the index a
battle menu needs is computed from these two tables (policies.item_index)."""

PARTY_MEMBERS = 0x7E2980
"""The three battle slots as character ids (0 Crono, 1 Marle, 2 Lucca, 3 Robo,
4 Frog, 5 Ayla, 6 Magus), $80 for an empty slot. Used to pick a policy's tech
table for whoever is actually in the party."""

# ---- load screen -----------------------------------------------------------------------------

LOAD_SLOT_SHOWN = 0x7E0079
"""The save slot (0-3) whose summary the load screen is currently showing
(`menu_save_slot_shown`, re/ram.toml; game/menu/save_screen.c, menu_show_save_slot
$C2:E743). The cursor skips slots with no save, so a save with more than one
slot in use needs this read back to land on a particular one (campaign.Run.select_slot)."""
