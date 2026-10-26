/* Battle: setting a battle up, before its first frame.
 *
 * A battle is set up in two steps, from the battle's first step (battle
 * step 0, battle/main_loop.c) and after its graphics have started loading:
 *
 *   - the battle's state (battle_setup_state): everything a fight starts
 *     from put back (battle/setup_reset.c), the screen's tables, the
 *     battlefield grid and the tilemaps (battle/setup_screen.c), the names
 *     turned into tiles (battle/setup_names.c), the party (its slots, colors,
 *     tech lists, remembered cursors and objects: battle/setup_party.c), the
 *     battle's colors, and the enemies (battle/setup_enemies.c);
 *   - the objects' graphics (battle_setup_graphics): their sprites, the small
 *     tiles, the cursors, and where each object's graphics go in VRAM
 *     (battle/setup_screen.c); then it waits for the load steps, which the
 *     frame's interrupt runs, to finish (the wait is its own entry,
 *     battle/wait.c).
 *
 * All of it is C on the game's state, and this file is its machine side: the
 * entries and the guards. */
#include "battle/setup.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "charge.h"
#include "port/machine.h"

/* ---- The names --------------------------------------------------------------------------------- */

/* The original reaches its converter ($C1:01A9) through $CC:F063 and the
 * dispatch at $C1:0018; the conversion is timed as the converter's. */
typedef struct {
    uint8_t text[NAME_TILES];
    NameTiles tiles;
    bool enemy;
} LastName;

static void convert_names(GameState *g, NameTable table, uint8_t count, LastName *last)
{
    for (uint8_t n = 0; n < count; n++) {
        setup_name_text(g, table, n, last->text);
        TIMED_AS(g, 0xC101A9, last->tiles = battle_name_tiles(last->text));
        last->enemy = false;
        setup_keep_name_tiles(g, table, n, &last->tiles);
    }
}

static void convert_enemy_names(GameState *g, LastName *last)
{
    for (uint8_t kind = 0; kind < SETUP_ENEMY_KINDS; kind++) {
        if (!setup_enemy_kind_present(g, kind))
            continue;
        setup_enemy_name_text(g, kind, last->text);
        TIMED_AS(g, 0xC101A9, last->tiles = battle_name_tiles(last->text));
        last->enemy = true;
        setup_keep_enemy_name_tiles(g, kind, &last->tiles);
    }
    setup_enemy_names_converted(g);
}

/* ---- The entries ---------------------------------------------------------------------------- */

// $CC:E2E4
/* The battle's state. Nothing the original leaves in the registers is read
 * after it (the registry's dont_care). */
void battle_setup_state(GameState *g)
{
    setup_reset_for_new_fight(g);
    setup_sprite_tables(g);
    setup_base_grid(g);
    setup_tilemaps(g);
    setup_copy_character_names(g);
    LastName last;
    convert_names(g, CHARACTER_NAMES, SETUP_CHARACTERS, &last);
    setup_party_slots(g);
    convert_names(g, PARTY_NAMES, BATTLE_PARTY_SIZE, &last);
    setup_colors(g);
    setup_party_colors(g);
    setup_tech_lists(g);
    setup_remembered_cursors(g);
    setup_party_objects(g);
    setup_enemies(g);
    convert_enemy_names(g, &last);
    ORIGINAL_EFFECT(setup_leave_name_buffer(g, last.text, last.enemy, &last.tiles));
}

void battle_setup_state_entry(Snes *s)
{
    battle_setup_state(state_of(s));
    m_rtl(s);
}

/* It goes on to wait for the graphics to load, which reads nothing the setup
 * leaves in the registers. */
enum { WAIT_FOR_GRAPHICS = 0xCCF05B };

// $CC:ED87
void battle_setup_graphics(GameState *g)
{
    setup_sprite_entries(g);
    setup_size_bits(g);
    setup_enemy_sprite_attributes(g);
    setup_tiles(g);
    setup_cursor_sprites_and_colors(g);
    setup_uploads(g);
}

void battle_setup_graphics_entry(Snes *s)
{
    battle_setup_graphics(state_of(s));
    m_stop_at(s, WAIT_FOR_GRAPHICS, DC_A | DC_X | DC_Y | DC_FLAGS);
}

/* ---- The guards ------------------------------------------------------------------------------- */

/* The setup runs on the battle's page with its data bank, on a party, enemies
 * and remembered cursors the game makes (setup_party.c, setup_enemies.c). */
bool battle_setup_inputs_fit(const GameState *g)
{
    return setup_party_is_real(g) && setup_enemies_are_real(g) && setup_remembered_cursors_real(g);
}

bool battle_setup_inputs_are_real(Snes *s)
{
    if (!battle_on_page(s))
        return false;
    vars_peek(true);
    bool real = battle_setup_inputs_fit(state_of(s));
    vars_peek(false);
    return real;
}

/* ... and the graphics on combatants whose parts fit their slots. */
bool battle_setup_parts_fit(Snes *s)
{
    if (!battle_on_page(s))
        return false;
    vars_peek(true);
    bool fit = setup_objects_parts_fit(state_of(s));
    vars_peek(false);
    return fit;
}
