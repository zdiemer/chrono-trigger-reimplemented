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
 * The one piece still the original's is the name converter; the conversions
 * are bridges around it. Everything else is C on the game's state, and this
 * file is its machine side: the entries, the bridges and the guards. */
#include "battle/setup.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "port/machine.h"

/* ---- The name converter --------------------------------------------------------------------- */

/* The original's converter ($C1:01A9), which the setup reaches through
 * $CC:F063 (which clears A to pick it from its dispatcher at $C1:0018). It
 * reads nothing of the registers, nor the data bank. */
enum { CONVERT_NAME = 0xCCF063, READS_NOTHING = DC_A | DC_X | DC_Y | DC_FLAGS | DC_DB };
enum { CONVERT_CHARACTER_NAME = 0xCCE5C2, CONVERT_PARTY_NAME = 0xCCE686, CONVERT_ENEMY_NAME = 0xCCED50 };

static bool convert_names(Snes *s, NameTable table, uint8_t count, uint32_t site)
{
    GameState *g = state_of(s);
    for (uint8_t n = 0; n < count; n++) {
        setup_name_to_convert(g, table, n);
        if (!battle_call(s, site, CONVERT_NAME, READS_NOTHING))
            return false;
        setup_keep_name_tiles(g, table, n);
    }
    setup_names_converted(g, count);
    return true;
}

static bool convert_enemy_names(Snes *s)
{
    GameState *g = state_of(s);
    for (uint8_t kind = 0; kind < SETUP_ENEMY_KINDS; kind++) {
        if (!setup_enemy_kind_present(g, kind))
            continue;
        setup_enemy_name_to_convert(g, kind);
        if (!battle_call(s, CONVERT_ENEMY_NAME, CONVERT_NAME, READS_NOTHING))
            return false;
        setup_keep_enemy_name_tiles(g, kind);
    }
    setup_enemy_names_converted(g);
    return true;
}

/* ---- The entries ---------------------------------------------------------------------------- */

// $CC:E2E4
/* The battle's state. Nothing the original leaves in the registers is read
 * after it (the registry's dont_care). */
void battle_setup_state(Snes *s)
{
    GameState *g = state_of(s);
    setup_reset_for_new_fight(g);
    setup_sprite_tables(g);
    setup_base_grid(g);
    setup_tilemaps(g);
    setup_copy_character_names(g);
    if (!convert_names(s, CHARACTER_NAMES, SETUP_CHARACTERS, CONVERT_CHARACTER_NAME))
        return;
    setup_party_slots(g);
    if (!convert_names(s, PARTY_NAMES, BATTLE_PARTY_SIZE, CONVERT_PARTY_NAME))
        return;
    setup_colors(g);
    setup_party_colors(g);
    setup_tech_lists(g);
    setup_remembered_cursors(g);
    setup_party_objects(g);
    setup_enemies(g);
    if (!convert_enemy_names(s))
        return;
    m_rtl(s);
}

/* It goes on to wait for the graphics to load, which reads nothing the setup
 * leaves in the registers. */
enum { WAIT_FOR_GRAPHICS = 0xCCF05B };

// $CC:ED87
void battle_setup_graphics(Snes *s)
{
    GameState *g = state_of(s);
    setup_sprite_entries(g);
    setup_size_bits(g);
    setup_enemy_sprite_attributes(g);
    setup_tiles(g);
    setup_cursor_sprites_and_colors(g);
    setup_uploads(g);
    m_stop_at(s, WAIT_FOR_GRAPHICS, DC_A | DC_X | DC_Y | DC_FLAGS);
}

/* ---- The guards ------------------------------------------------------------------------------- */

/* The setup runs on the battle's page with its data bank, on a party, enemies
 * and remembered cursors the game makes (setup_party.c, setup_enemies.c). */
bool battle_setup_inputs_are_real(Snes *s)
{
    if (!battle_on_page(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool real = setup_party_is_real(g) && setup_enemies_are_real(g) && setup_remembered_cursors_real(g);
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
