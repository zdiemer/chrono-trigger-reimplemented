/* The techs as the menus list them (game/menu/techs.c): which a member has
 * learned of each kind, what one costs the member in MP, whether it can be
 * used from the menus and what it restores there, and its row in a list. */
#ifndef CT_GAME_MENU_TECHS_H
#define CT_GAME_MENU_TECHS_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

/* The kinds of tech, as the techs screen's tabs show them: a character's
 * own, the double techs two members do together and the triple techs of
 * three. */
typedef enum { MENU_SINGLE_TECHS = 0, MENU_DOUBLE_TECHS = 1, MENU_TRIPLE_TECHS = 2, MENU_TECH_KINDS = 3 } MenuTechKind;

/* No tech: a list's place past its last, the techs screen with none on its
 * cursor. */
enum { MENU_NO_TECH = 0xFF };

/* List the techs of `kind` the character has learned, the groups' techs in
 * their order (menu_tech_list, with each one's group, and how many there
 * are), as the techs screen shows them. */
void menu_list_techs(GameState *g, uint8_t character, MenuTechKind kind);

/* What a tech of `kind` costs the member loaded last (menu_load_member) in
 * MP: a single tech its own cost, a double or triple tech the cost of the
 * member's part in it; the member's accessory may halve it or quarter it
 * (rounding up). `part` is the part's place in the table of costs. The cost
 * is left for the member's row to compare with (menu_tech_cost_needed). */
typedef struct {
    uint8_t mp;
    uint16_t part;
} MenuTechCost;
MenuTechCost menu_tech_cost_for(GameState *g, uint8_t tech, MenuTechKind kind);

/* Whether a tech can be used from the menus (a healing tech), and whether
 * it is a member's next single tech, still being learned. */
bool menu_tech_heals(const GameState *g, uint8_t tech);
bool menu_tech_being_learned(const GameState *g, uint8_t tech);

/* What a healing tech does, as the restoring the menus share with the
 * tonics reads it (menu_use_effect, menu_heal_factor): whether it restores HP
 * and MP, for one member or every member, and its factor. */
typedef struct {
    uint8_t effect;
    uint8_t factor;
} MenuTechHealing;
MenuTechHealing menu_tech_healing(GameState *g, uint8_t tech);

/* A tech's row in a list at `at`: its name in the colour that says whether
 * it can be used here (`waiting`: the member isn't one of the active ones,
 * who alone use techs from the menus) or is still being learned; or blanks
 * for no tech. */
void menu_draw_tech_row(GameState *g, uint16_t at, uint8_t tech, bool waiting);

/* Whether the menus' tables hold what the techs screen reads of `tech`: a
 * tech of the game's, and for a double or triple tech the member's part. */
bool menu_tech_is_the_games(uint8_t tech);

#endif
