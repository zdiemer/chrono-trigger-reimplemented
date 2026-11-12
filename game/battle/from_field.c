/* Battle: coming in from the field, and going back to it.
 *
 * The field starts a battle at $C1:0000, and the battle's last step ends at
 * $C1:0006, which hands the game back to the field.
 *
 * On the way in the battle's memory is cleared, the battle's screen set up,
 * and what the field and the story say about the fight taken in: the window
 * arrangement it starts in, its sprites' priority, and which members sit it
 * out. Then the main loop runs it (battle/main_loop.c).
 *
 * On the way out the battle hands back what the field keeps of it: the
 * members' colors as they were, where everyone stood (the field puts them
 * there again), the menu cursors the settings remember, and every character
 * with some HP. It waits for its last graphics uploads to go out first, and
 * hides the sprites the field doesn't draw over. */
#include "battle/animation.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/main_loop.h"
#include "battle/program.h"
#include "battle/screen.h"
#include "battle/wait.h"
#include "charge.h"
#include "core/program.h"
#include "battle/setup_data.h"
#include "vars.h"
#include "wram.h"

/* ---- Coming in ------------------------------------------------------------------------------ */

/* What the field's setup flags (battle_setup_flags) say besides what the
 * battle itself reads: the window arrangement the battle starts in, and
 * sprites a priority higher than the battle's own. */
enum { SETUP_ALTERNATE_ARRANGEMENT = 0x02, SETUP_SPRITES_HIGHER = 0x10, SPRITES_PRIORITY = 0x20 };

/* What the story says about the fight: in its rules byte, that some members
 * sit it out (each character's byte from the second says whether it does),
 * and that the fight isn't one of those its flags (battle_fight_flags) mark. */
enum { STORY_SITTING_OUT = 0xB4 };
enum { RULES_SOME_SIT_OUT = 0x01, RULES_UNMARKED_FIGHT = 0x02, CHARACTER_BITS = 0x07 };

/* The battle's memory: its low work cells from $80 on, and its
 * tables from $2D01 to $ACEC. Each is every battle variable there rather than
 * one the RAM map names, so it is cleared by address (game/wram.h). */
enum { PAGE_CELLS_AT = 0x7E0080, PAGE_CELLS_WORDS = 0x36, TABLES_AT = 0x7E2D01, TABLES_WORDS = 0x3FF6 };

/* The fill takes none of the port's own time, and the original's, a store a
 * word over 32 KiB, is nearly all of the way in's own (about four frames):
 * charged at the way in's rate, a byte cleared an access, so the battle
 * keeps its pace (charge.h). */
enum { THE_WAY_IN = 0xC10000 };

static void clear_battle_memory(GameState *g)
{
    wram_fill16(g, TABLES_AT, TABLES_WORDS, 0);
    wram_fill16(g, PAGE_CELLS_AT, PAGE_CELLS_WORDS, 0);
    charge_as(g, THE_WAY_IN, 2 * (TABLES_WORDS + PAGE_CELLS_WORDS));
}

// $CF:FB65
/* Before the battle's screen is set up: no frame or transfer pending, no
 * graphics loaded yet, and the window arrangement the field asks for. */
static void before_the_screen(GameState *g)
{
    set_battle_frame_pending(g, 0);
    set_battle_transfer_pending(g, 0);
    set_battle_graphics_load_step(g, 0);
    set_battle_alternate_frame(g, (battle_setup_flags(g) & SETUP_ALTERNATE_ARRANGEMENT) != 0);
}

/* After it: the battle's memory cleared, then what the field and the story
 * say about the fight. */
static void take_in_the_fight(GameState *g)
{
    clear_battle_memory(g);
    set_battle_sprite_attributes(g, (uint8_t)((battle_setup_flags(g) & SETUP_SPRITES_HIGHER) | SPRITES_PRIORITY));
    uint8_t rules = story_party_rules(g);
    uint8_t some_sit_out = rules & RULES_SOME_SIT_OUT;
    if (!some_sit_out)
        return;
    for (unsigned member = BATTLE_PARTY_SIZE; member-- > 0;)
        set_battle_member_sitting_out(
            g, member, event_memory_byte(g, STORY_SITTING_OUT + (party_members(g, member) & CHARACTER_BITS)));
    set_battle_fight_flags(g, (uint8_t)((rules & RULES_UNMARKED_FIGHT) ^ RULES_UNMARKED_FIGHT));
}

/* ---- Going back ----------------------------------------------------------------------------- */

/* Each member's colors as the palette had them: a member whose colors cycled
 * (a status's glow) gets its own back. A member's sprite palette's colors
 * from the second on are its. */
enum { MEMBER_COLORS = 12, FIRST_SPRITE_COLOR = 128, PALETTE_COLORS = 256 };

static unsigned member_first_color(const GameState *g, unsigned member)
{
    return FIRST_SPRITE_COLOR + 1 + (uint8_t)(battle_object_palette(g, member) << 4) / 2;
}

static void restore_member_colors(GameState *g)
{
    for (unsigned member = BATTLE_PARTY_SIZE; member-- > 0;) {
        set_battle_party_palette_cycle_on(g, member, 0);
        unsigned first = member_first_color(g, member);
        for (unsigned c = 0; c < MEMBER_COLORS; c++)
            set_palette_working(g, first + c, palette_base(g, first + c));
    }
}

/* Where each one stood, in 16-pixel tiles and the pixels into the tile, with
 * the frame it showed: the field stands the party and the enemies there. */
enum { TILE_SHIFT = 4, IN_TILE = 0x0F };

static void keep_party_places(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t x = battle_object_x(g, battle_combatant_object(member));
        uint8_t y = battle_object_y(g, battle_combatant_object(member));
        set_battle_party_placement_tile_x(g, member, x >> TILE_SHIFT);
        set_battle_party_placement_x_offset(g, member, x & IN_TILE);
        set_battle_party_placement_tile_y(g, member, y >> TILE_SHIFT);
        set_battle_party_placement_y_offset(g, member, y & IN_TILE);
        set_battle_party_placement_frame(g, member, battle_object_frame_shown(g, member));
    }
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        unsigned slot = BATTLE_FIRST_ENEMY + enemy;
        uint8_t x = battle_object_x(g, battle_combatant_object(slot));
        uint8_t y = battle_object_y(g, battle_combatant_object(slot));
        set_battle_enemy_placement_tile_x(g, enemy, x >> TILE_SHIFT);
        set_battle_enemy_placement_x_offset(g, enemy, x & IN_TILE);
        set_battle_enemy_placement_tile_y(g, enemy, y >> TILE_SHIFT);
        set_battle_enemy_placement_y_offset(g, enemy, y & IN_TILE);
        set_battle_enemy_placement_frame(g, enemy, battle_object_frame_shown(g, slot));
    }
}

/* The menu cursors the settings have the battle remember, kept by character
 * for the next battle: the command cursor, and the tech list's and the item
 * list's. */
enum { NO_CHARACTER = 0x80 };

static void remember_cursors(GameState *g)
{
    bool commands = battle_remembers_commands(g) != 0, lists = battle_remembers_lists(g) != 0;
    for (unsigned member = BATTLE_PARTY_SIZE; member-- > 0;) {
        uint8_t character = party_members(g, member);
        if (character & NO_CHARACTER)
            continue;
        if (commands)
            set_battle_remembered_cursors_command(g, character, battle_remembered_command(g, member));
        if (!lists)
            continue;
        set_battle_remembered_cursors_tech_scroll(g, character, battle_remembered_tech_scroll(g, member));
        set_battle_remembered_cursors_tech_row(g, character, battle_remembered_tech_row(g, member));
        set_battle_remembered_cursors_tech(g, character, battle_remembered_tech(g, member));
    }
    if (lists) {
        set_battle_remembered_item_top(g, battle_item_top(g));
        set_battle_remembered_item_row(g, battle_item_row(g));
    }
}

// $CF:FBE5
/* What the field keeps of the battle. */
static void hand_back_to_the_field(GameState *g)
{
    restore_member_colors(g);
    keep_party_places(g);
    remember_cursors(g);
}

/* Every character out of HP gets up with 1: the field has no one knocked out. */
enum { CHARACTER_RECORD_BYTES = 0x50 };

static void stand_characters_up(GameState *g)
{
    for (unsigned c = CHARACTER_RECORDS_COUNT; c-- > 0;) {
        unsigned record = battle_character_record_place(g, c) / CHARACTER_RECORD_BYTES;
        if (character_records_hp(g, record) == 0)
            set_character_records_hp(g, record, 1);
    }
}

/* The last frames' phase, and the first sprites hidden: their high bits set,
 * off the screen and large. */
enum { LAST_PHASE = 3, HIDDEN_SPRITES = 0xFF };

/* The battle's screen is set up ($CD:0000, battle/opening.c), the main loop
 * starts the battle ($C1:8000), the frames are waited for with the battle's
 * last frame wait ($CF:FCFB), which the vblank's work ends, and the display
 * is closed down ($CD:0006, battle/closing.c). */
enum { SET_UP_SCREEN = 0xCD0000, MAIN_LOOP = 0xC18000, WAIT_FOR_FRAME = 0xCFFCFB, RETURN_TO_THE_FIELD = 0xC10006 };

/* ---- The way in and the way out --------------------------------------------------------------- */

// $C1:0000
/* The battle's memory and screen set up (the screen's a routine of its own,
 * battle/screen_setup.c, $CD:0000 through to $D1:F1B0), and what the field
 * and the story say about the fight taken in. False where the screen's setup
 * went no further. */
static bool come_in_from_the_field(GameState *g, Platform *pf)
{
    before_the_screen(g);
    bool done = false;
    TIMED_AS(g, SET_UP_SCREEN, done = battle_screen_set_up(g, pf));
    if (!done)
        return false;
    take_in_the_fight(g);
    return true;
}

/* A frame waited for with the battle's last frame wait, which the vblank's
 * work ends. */
static void a_last_frame(GameState *g, Platform *pf) { TIMED_AS(g, WAIT_FOR_FRAME, battle_wait_for_a_last_frame(g, pf)); }

/* The last frames: the rotating graphics uploads are let run every other
 * frame until the frame's phase comes round to the last of the four, then
 * stopped, and the first sprites hidden for one more frame. */
static void last_frames(GameState *g, Platform *pf)
{
    do {
        set_battle_queued_uploads_on(g, 0);
        a_last_frame(g, pf);
        set_battle_queued_uploads_on(g, 1);
        a_last_frame(g, pf);
    } while (battle_frame_phase(g) != LAST_PHASE);
    set_battle_queued_uploads_on(g, 0);
    set_oam_high_table_byte(g, 0, HIDDEN_SPRITES);
    a_last_frame(g, pf);
}

// $C1:0006
/* What the field keeps handed back, the last frames, everyone with some HP,
 * and the display closed (battle/closing.c). False where its closing went no
 * further. */
static bool go_back_to_the_field(GameState *g, Platform *pf)
{
    hand_back_to_the_field(g);
    last_frames(g, pf);
    stand_characters_up(g);
    return battle_display_closed(g, pf);
}

/* What the way back takes: the party as the game has it, each
 * member a character or no one, and its colors in the sprite palettes (the
 * party's are the sixth to the eighth). */
static bool member_colors_in_palette(const GameState *g, unsigned member)
{
    return member_first_color(g, member) + MEMBER_COLORS <= PALETTE_COLORS;
}

static bool party_as_the_game_has_it(const GameState *g)
{
    bool characters = true;
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t character = party_members(g, member);
        characters = characters && ((character & NO_CHARACTER) || character < CHARACTER_RECORDS_COUNT)
                     && member_colors_in_palette(g, member);
    }
    return characters;
}

/* ---- The battle fought ------------------------------------------------------------------------ */

bool battle_contract_broken(GameState *g, uint32_t routine, const char *what)
{
    program_breaks_a_contract(g, routine, what);
    return false;
}

bool battle_fought(GameState *g, Platform *pf)
{
    if (!come_in_from_the_field(g, pf) || !battle_main_loop_runs(g, pf))
        return false;
    if (!party_as_the_game_has_it(g))
        return battle_contract_broken(g, RETURN_TO_THE_FIELD, "a battle's end with a party the port doesn't hand back");
    return go_back_to_the_field(g, pf);
}
