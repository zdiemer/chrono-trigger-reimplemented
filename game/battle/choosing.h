/* Battle: the player choosing what a party member does.
 *
 * When a member's turn comes its menu opens (window.h: the command windows at
 * the battle window's left, the menu window above). Each frame the menu takes
 * the buttons the player pressed (menu_input.c): Attack, Tech or Item on the
 * command page, a row of the tech list or the item list on their pages, and
 * then a target among those the action can reach; the choice made, the action
 * is queued and the menu goes to the next member waiting to choose. The rest
 * of this part of the game is what that needs:
 *
 *   menu_lists.c     the tech list and the item list laid out as tiles, and
 *                    which of the tech list's rows the cursor stops on;
 *   name_tiles.c     a name as the two rows of tiles the windows draw it in;
 *   target_choice.c  whom the action can be aimed at, by its target kind: a
 *                    member, an enemy, a side, everyone, or those an area
 *                    around the chosen one takes in; and the player moving
 *                    the cursor among them;
 *   area_search.c    who stands in an area of the battlefield, which the
 *                    targeting of a tech or an item that hits an area asks
 *                    too (aim.c; the battle's step 7).
 *
 * The menu's state is the battle's cells (window.h names most of them); the
 * menu's in-band "nobody" is window.h's. */
#ifndef CT_GAME_BATTLE_CHOOSING_H
#define CT_GAME_BATTLE_CHOOSING_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/window.h"
#include "core/input.h"
#include "state.h"

/* ---- Names as tiles (name_tiles.c) ------------------------------------------------------------ */

/* A name of up to sixteen characters, ended early by a zero, as two rows of
 * tiles: the characters' own tiles, and above them the marks some
 * characters draw with (the font's two-part characters); a blank past the
 * name's end and above a character without a mark. */
enum { NAME_TILES = 16, NAME_END = 0 };
typedef struct {
    uint8_t glyphs[NAME_TILES], marks[NAME_TILES];
} NameTiles;

NameTiles battle_name_tiles(const uint8_t text[NAME_TILES]);

/* The original converts a name in battle_name_buffer and leaves the buffer
 * so; what it leaves there, for shadow mode (called in ORIGINAL_EFFECT):
 * the text it was given, `text`, and its tiles. */
void battle_leave_name_buffer(GameState *g, const uint8_t text[NAME_TILES], const NameTiles *tiles);

/* ---- The lists (menu_lists.c) ------------------------------------------------------------------ */

/* A member's tech list is twenty entries; an item list's record is five
 * bytes (battle_item_list). The menu writes "nobody" as $FF where it empties
 * a slot or a mark (window.h reads any byte with its top bit as nobody). */
enum { TECHS_A_MEMBER = 20, ITEM_LIST_RECORD_BYTES = 5, MENU_NONE = 0xFF };

/* The menu member's tech list from its scroll, four rows, as the tech page
 * copies them into the menu window ($C1:0A88); and which of its twenty
 * entries the cursor stops on ($C1:0AD3). */
void battle_lay_out_tech_list(GameState *g);
void battle_mark_tech_rows(GameState *g);

/* Whether a tech list's entry names a tech, rather than a section's heading
 * or a blank row. */
bool battle_tech_entry_is_a_tech(uint8_t entry);

/* Three rows of the item list from `top`, laid out in the battle window
 * ($C1:095D): each item the party holds with its name and how many, plain
 * where the member choosing may use it. */
void battle_lay_out_item_list(GameState *g, uint8_t top);

/* ---- The area search (area_search.c) ------------------------------------------------------------ */

/* The area search lists at most eleven combatants (battle_area_found); a
 * twelfth place keeps the list's end. */
enum { AREA_FOUND_PLACES = 11 };

/* The kinds of area (area_search.c has the rules' numbers for them). */
typedef enum {
    AREA_BEAM,           /* a band from the centre toward a second combatant and on past it */
    AREA_CIRCLE,         /* within a distance of the centre, on the battlefield's grid */
    AREA_PARTY_TRIANGLE, /* the enemies inside the triangle the three party members make */
    AREA_ROW,            /* level with the centre, 32 pixels above or below */
} AreaKind;

/* What an area search is asked: the side it looks over (the party's or the
 * enemies'), the combatant the area is placed on, the one a beam reaches
 * toward, how big it is (a beam's half width in eighths of a grid cell, a
 * circle's distance squared in cells), and for a beam, whether it stops at
 * the second combatant. */
typedef struct {
    bool among_party;
    uint8_t centre, toward, size;
    bool flag;
} AreaAsked;

/* Those in an area of a kind, listed in battle_area_found (the targets
 * cleared with it); and, by the area rules' number for its shape
 * (battle_area_shape), the battle's step 7, $C1:1FDD. */
void battle_find_in(GameState *g, AreaKind kind, const AreaAsked *a);
void battle_find_area(GameState *g, uint8_t shape, const AreaAsked *a);

/* Where the list's first place is empty, the list closes up. */
void battle_close_up_finds(GameState *g);

/* The area search keeps what it was asked in its cells (battle_area_search),
 * as much of it as the kind looks at; and the same read back, for the
 * original's callers, who hand the search its area there. */
void battle_note_area_asked(GameState *g, AreaKind kind, const AreaAsked *a);
AreaAsked battle_area_asked(const GameState *g);

/* Whether a combatant can be aimed at: it is in the battle, shown, not
 * sitting out, and not knocked out unless `fallen_too`. */
bool battle_can_be_aimed_at(const GameState *g, unsigned slot, bool fallen_too);

/* ---- The menu's click ------------------------------------------------------------------------------- */

/* The cursor clicks as it moves, and as the player chooses, goes back, turns
 * the item list's page or moves among the targets. Playing it is the host's
 * (the SNES machine's, battle/menu_bridge.c): the logic says when, and for
 * which move. */
typedef enum {
    CLICK_COMMAND_UP, CLICK_COMMAND_DOWN, CLICK_COMMAND_CHOSEN, CLICK_MEMBER_BACK, CLICK_MEMBER_ON,
    CLICK_TECH_CHOSEN, CLICK_TECH_BACK, CLICK_TECH_UP, CLICK_TECH_DOWN,
    CLICK_ITEM_CHOSEN, CLICK_ITEM_BACK, CLICK_ITEM_UP, CLICK_ITEM_DOWN, CLICK_ITEM_PAGE_ON, CLICK_ITEM_PAGE_BACK,
    CLICK_TARGETS_CALLED_OFF, CLICK_TARGET_CONFIRMED, CLICK_TARGET_NEXT, CLICK_TARGET_PREVIOUS,
    CLICK_AREA_ENEMY_ON, CLICK_AREA_ENEMY_BACK,
} MenuClick;

typedef struct MenuHost MenuHost;
struct MenuHost {
    void (*click)(MenuHost *host, MenuClick click);
};

/* ---- Choosing targets (target_choice.c) ----------------------------------------------------------- */

/* The menu's target kinds (battle_target_kind's low seven bits): how an
 * action picks its targets as the player chooses. Where the kind takes all
 * it finds together, battle_targets_together has its top bit. */
enum { TARGET_KIND_ATTACK = 7, TARGET_KIND_ALLY = 0x80, TARGET_KINDS = 0x21, TARGETS_TOGETHER = 0x80 };

/* What a target kind picks (target_choice.c's table): one from a side, all
 * of a side, the member itself, a fallen member, a particular character, or
 * those in an area; and a kind's number, its low seven bits (a kind past the
 * table's reads as its last). */
typedef enum { PICK_ONE, PICK_ALL, PICK_SELF, PICK_FALLEN, PICK_CHARACTER, PICK_AREA } TargetPick;
uint8_t battle_target_kind_number(uint8_t kind);
TargetPick battle_target_pick(uint8_t kind);

/* Whether the choosing aimed at anyone: a combatant among the targets. */
bool battle_anyone_aimed_at(const GameState *g);

/* $C1:1F79: list whom the member's action can be aimed at, and move the
 * cursor among them where the kind lets the player (`pressed`, the buttons
 * the menu took, each move clicking on `host`); true where nobody can be
 * aimed at. */
bool battle_choose_targets(GameState *g, Buttons pressed, MenuHost *host);

/* The player's cursor to the next target found, or the one before. */
void battle_next_target(GameState *g);
void battle_previous_target(GameState *g);

/* ---- The menu (menu_input.c) ---------------------------------------------------------------------- */

/* The frame around the member choosing: four sprites, the first four of the
 * OAM buffer, where the menu's cursors go when it has none ($C1:1115). */
void battle_frame_member_choosing(GameState *g);

/* An item a member stole waits apart from the item list until the menu next
 * runs; then it joins the list ($CF:FAE2), one member's a frame. */
void battle_list_stolen_item(GameState *g);

/* $C1:1153: the menu's frame: the buttons the menu took, the clicks played on
 * `host`. */
void battle_menu_frame(GameState *g, Buttons pressed, MenuHost *host);
/* Its guard's state half: the menu as the game keeps it (battle/menu_input.c). */
bool battle_menu_frame_fits(const GameState *g);

/* Note the stat block of the member whose menu is open (battle_current_stat_block,
 * $CF:FD9E). */
void battle_note_menu_stat_block(GameState *g, uint8_t member);

#endif
