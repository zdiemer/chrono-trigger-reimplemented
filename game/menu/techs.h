/* The techs as the menus list them (game/menu/techs.c): which a member has
 * learned of each kind, what one costs the member in MP, whether it can be
 * used from the menus and what it restores there, and its row in a list. */
#ifndef CT_GAME_MENU_TECHS_H
#define CT_GAME_MENU_TECHS_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* The kinds of tech, as the techs screen's tabs show them: a character's
 * own, the double techs two members do together and the triple techs of
 * three. */
typedef enum { MENU_SINGLE_TECHS = 0, MENU_DOUBLE_TECHS = 1, MENU_TRIPLE_TECHS = 2, MENU_TECH_KINDS = 3 } MenuTechKind;

/* No tech: a list's place past its last, the techs screen with none on its
 * cursor. */
enum { MENU_NO_TECH = 0xFF };

/* The first double tech: the techs from it on are the doubles' and the
 * triples', whose parts the characters take. The five triple techs a rock
 * gives are the last five groups, from the eleventh triple tech. */
enum { MENU_FIRST_DOUBLE_TECH = 0x39, MENU_FIRST_ROCK_GROUP = 32, MENU_FIRST_ROCK_TECH = 10 };

/* As the menus open, note how many members the screens let the player choose
 * among and what of the party's techs they list (menu/techs.c); and which of
 * the rocks' triple techs the party could do (menu_rock_techs_ready), which
 * the menus' services work out again as they change the party. */
void menu_note_party_techs(GameState *g);
void menu_note_rock_techs(GameState *g);

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

/* Whether the menus' tables hold what the techs screen reads of `tech` in a
 * list of `kind`: a tech of the game's, and for a double or triple tech one
 * from the first double on, whose parts the members have. */
bool menu_tech_is_the_games(uint8_t tech, MenuTechKind kind);

/* The registered entries ($FF:F813, $FF:F958) and their guards. */
void menu_note_party_techs_entry(Snes *s);
void menu_note_rock_techs_entry(Snes *s);
bool menu_party_techs_are_notable(Snes *s);
bool menu_rock_techs_are_notable(Snes *s);

#endif
