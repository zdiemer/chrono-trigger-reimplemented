/* Battle: setting a battle up, before its first frame (battle/setup.c says
 * how the pieces fit together). */
#ifndef CT_GAME_BATTLE_SETUP_H
#define CT_GAME_BATTLE_SETUP_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/choosing.h"
#include "original_effects.h"
#include "snes/system.h"
#include "state.h"
#include "vars.h"

/* The registered entries (battle/setup.c). */
void battle_setup_state(Snes *s);
void battle_setup_graphics(Snes *s);
bool battle_setup_inputs_are_real(Snes *s);
bool battle_setup_parts_fit(Snes *s);

/* ---- What the pieces share -------------------------------------------------------------- */

enum { SETUP_NONE = 0xFF };

/* The player's settings (game_settings) the battle reads: the first byte's
 * tech and item descriptions and active time, the second's cursor memory,
 * and the third, the battle gauge setting, picks the window's layout. */
enum { SETTINGS_TIME_AND_INFO = 0, SETTINGS_MEMORY = 1, SETTINGS_GAUGE = 2 };
enum { SHOW_DESCRIPTIONS = 0x80, ACTIVE_TIME = 0x40 };
enum { REMEMBER_LISTS = 0x80, REMEMBER_COMMANDS = 0x40 };

/* How many more battle slots (and sprite rows) an object's extra parts take,
 * by its graphics kind: none, one, two, or five for every kind from 3 on. */
uint8_t setup_extra_parts(uint8_t kind);

/* The names the battle turns into tiles: the seven characters', the party's
 * and the enemy kinds' (battle/setup_names.c). A name's text is made into
 * two rows of tiles (name_tiles.c), and the rows are kept where the window
 * draws them from. The original converts each in its name buffer; what the
 * last conversion leaves there is scaffolding (setup_leave_name_buffer). */
enum { SETUP_CHARACTERS = 7, SETUP_ENEMY_KINDS = 3 };
typedef enum { CHARACTER_NAMES, PARTY_NAMES } NameTable;

void setup_copy_character_names(GameState *g);
void setup_name_text(GameState *g, NameTable table, uint8_t n, uint8_t text[NAME_TILES]);
void setup_keep_name_tiles(GameState *g, NameTable table, uint8_t n, const NameTiles *tiles);
bool setup_enemy_kind_present(const GameState *g, uint8_t kind);
void setup_enemy_name_text(GameState *g, uint8_t kind, uint8_t text[NAME_TILES]);
void setup_keep_enemy_name_tiles(GameState *g, uint8_t kind, const NameTiles *tiles);
void setup_enemy_names_converted(GameState *g);
void setup_leave_name_buffer(GameState *g, const uint8_t text[NAME_TILES], bool enemy, const NameTiles *tiles);

/* battle/setup_reset.c: the battle's state as every fight starts it. */
void setup_reset_for_new_fight(GameState *g);

/* battle/setup_screen.c: the screen's tables, the grid and the colors, and
 * the objects' graphics (the second entry). */
void setup_sprite_tables(GameState *g);
void setup_base_grid(GameState *g);
void setup_tilemaps(GameState *g);
void setup_colors(GameState *g);
void setup_sprite_entries(GameState *g);
void setup_size_bits(GameState *g);
void setup_enemy_sprite_attributes(GameState *g);
void setup_tiles(GameState *g);
void setup_cursor_sprites_and_colors(GameState *g);
void setup_uploads(GameState *g);
bool setup_objects_parts_fit(const GameState *g);

/* battle/setup_party.c: the party. */
void setup_party_slots(GameState *g);
void setup_party_colors(GameState *g);
void setup_tech_lists(GameState *g);
void setup_remembered_cursors(GameState *g);
void setup_party_objects(GameState *g);
bool setup_party_is_real(const GameState *g);
bool setup_remembered_cursors_real(const GameState *g);
void setup_place_combatant(GameState *g, unsigned slot);
/* The frame each combatant's object starts with, kept a byte on from its
 * marker's frame (setup_party.c). */
uint8_t setup_start_frame(const GameState *g, unsigned slot);

/* battle/setup_enemies.c: the enemies. */
void setup_enemies(GameState *g);
bool setup_enemies_are_real(const GameState *g);

/* ---- Scaffolding ------------------------------------------------------------------------- */

/* What the original leaves in the battle page's working cells ($80-$9D,
 * battle_work) as each piece ends, for shadow mode and the original's code
 * after it; `cell` is the original's direct-page address. Nothing in game/
 * reads them back. */
static inline void setup_leave(GameState *g, uint8_t cell, uint8_t v) { set_battle_work(g, cell - 0x80u, v); }
static inline void setup_leave_word(GameState *g, uint8_t cell, uint16_t v)
{
    setup_leave(g, cell, (uint8_t)v);
    setup_leave(g, (uint8_t)(cell + 1), (uint8_t)(v >> 8));
}

#endif
