/* Battle: the state every fight starts from.
 *
 * Before a battle takes in its party and its enemies (battle/setup_party.c,
 * battle/setup_enemies.c), everything a previous fight could have left is put
 * back as a new fight has it: nobody's menu open and no action waiting, the
 * tech lists blank, the objects' drawing state cleared, every combatant's
 * animation, pose and effects at rest, and the player's settings taken in for
 * the battle's menus. What each table starts as is its own "nothing yet":
 * $FF where the table names a member, a frame or a pose. */
#include "battle/battle.h"
#include "battle/setup.h"

#include "cpu_io.h"
#include "battle/setup_data.h"
#include "original_effects.h"
#include "vars.h"

/* ---- The settings --------------------------------------------------------------------------- */

/* The flags keep the setting's bit as it is: the menus test them for being
 * set. In wait mode (active time off) the timers hold while a tech or item
 * list is open. */
static void take_settings(GameState *g)
{
    uint8_t time_and_info = game_settings(g, SETTINGS_TIME_AND_INFO);
    uint8_t memory = game_settings(g, SETTINGS_MEMORY);
    set_battle_lists_hold_timers(g, (time_and_info & ACTIVE_TIME) ^ ACTIVE_TIME);
    set_battle_window_layout(g, setup_window_layout_for_gauge(g, game_settings(g, SETTINGS_GAUGE)));
    set_battle_remembers_commands(g, memory & REMEMBER_COMMANDS);
    set_battle_remembers_lists(g, memory & REMEMBER_LISTS);
    set_battle_show_descriptions(g, time_and_info & SHOW_DESCRIPTIONS);
}

/* ---- The menus ------------------------------------------------------------------------------ */

enum { MENU_QUEUE = 4, ACTIONS_QUEUED = 3, MENU_SLOTS = 3, LIST_CURSORS_SHOWN = 6, TECH_LIST_BYTES = 0x81 };
enum { BLINK_FRAMES = 8, CURSOR_PRIORITY_START = 2 };

/* No member's menu open or waiting to, no action waiting, no tech chosen, and
 * the tech lists blank until the party's are built. */
static void close_the_menus(GameState *g)
{
    set_battle_menu_member(g, SETUP_NONE);
    for (unsigned i = 0; i < MENU_QUEUE; i++)
        set_battle_menu_queue(g, i, SETUP_NONE);
    for (unsigned i = 0; i < ACTIONS_QUEUED; i++)
        set_battle_action_queue(g, i, SETUP_NONE);
    set_battle_action_queue_end(g, SETUP_NONE);
    set_battle_selected_tech_partners(g, SETUP_NONE);
    for (unsigned i = 0; i < TECH_LIST_BYTES; i++) {
        set_battle_tech_list_ids(g, i, SETUP_NONE);
        set_battle_tech_list(g, i, SETUP_NONE);
    }
    for (unsigned slot = 0; slot < MENU_SLOTS; slot++)
        set_battle_menu_slot_members(g, slot, SETUP_NONE);
    set_battle_menu_slot_mark(g, SETUP_NONE);
    set_battle_menu_slot(g, SETUP_NONE);
    set_battle_menu_slot_shown(g, SETUP_NONE);
    set_battle_status_member(g, SETUP_NONE);
    set_battle_menu_mark(g, SETUP_NONE);
    set_battle_item_arrows_shown(g, SETUP_NONE);
    for (unsigned i = 0; i < LIST_CURSORS_SHOWN; i++)
        set_battle_list_cursors_shown(g, i, SETUP_NONE);
    set_battle_item_cursor_shown(g, SETUP_NONE);
    set_battle_color_phase(g, SETUP_NONE);
    set_battle_blink_timer(g, BLINK_FRAMES);
    set_battle_cursor_priority(g, CURSOR_PRIORITY_START);
}

/* ---- The objects ---------------------------------------------------------------------------- */

enum { OBJECTS = 23, OBJECT_SLOTS = BATTLE_COMBATANTS };
enum { DRAW_WORK = 35, DRAW_LIST_WORK = 11, DRAW_PAGE_WORK = 49 };

/* The sprite drawers' page, every byte of it: the objects' positions and
 * everything the drawing keeps between frames. */
static void clear_drawing(GameState *g)
{
    for (unsigned i = 0; i < OBJECTS; i++) {
        set_battle_object_x(g, i, 0);
        set_battle_object_y(g, i, 0);
        set_battle_draw_scratch(g, i, 0);
        set_battle_object_part_count(g, i, 0);
        set_battle_draw_list_depth(g, i, 0);
        set_battle_draw_list(g, i, 0);
    }
    for (unsigned slot = 0; slot < OBJECT_SLOTS; slot++)
        set_battle_object_extra_draw(g, slot, 0);
    for (unsigned i = 0; i < DRAW_WORK; i++)
        set_battle_draw_work(g, i, 0);
    for (unsigned i = 0; i < DRAW_LIST_WORK; i++)
        set_battle_draw_list_work(g, i, 0);
    for (unsigned i = 0; i < DRAW_PAGE_WORK; i++)
        set_battle_draw_page_work(g, i, 0);
    set_battle_draw_list_position(g, 0);
    set_battle_draw_object(g, 0);
    set_battle_draw_part_count(g, 0);
    set_battle_oam_next(g, 0);
    set_battle_afterimage_object(g, 0);
    set_battle_draw_tile_groups(g, 0);
}

/* Where a party member's cursor points from its position, how big it is to
 * aim at, and the effects' pace it starts with; the enemies' come from their
 * headers (battle/setup_enemies.c). */
enum { CURSOR_ABOVE = 0xF0, PARTY_HALF_SIZE = 8, PARTY_EFFECT_STEPS = 2, PARTY_EFFECT_PERIOD = 1 };

/* What sits in the slot byte a combatant's drawing mode starts with. */
enum { DRAWN_AS_USUAL = 0xC0 };

static void rest_the_objects(GameState *g)
{
    uint8_t attributes = battle_sprite_attributes(g);
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        set_battle_object_frame_shown(g, slot, SETUP_NONE);
        set_battle_object_pose(g, slot, SETUP_NONE);
        set_battle_object_pose_last(g, slot, SETUP_NONE);
        set_battle_object_attribute_bits(g, slot, attributes);
        set_battle_attribute_bits_start(g, slot, attributes);
        set_battle_slot_draw_bits(g, slot, DRAWN_AS_USUAL);
    }
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_cursor_offset_x(g, member, 0);
        set_battle_cursor_offset_y(g, member, CURSOR_ABOVE);
        set_battle_object_half_width(g, member, PARTY_HALF_SIZE);
        set_battle_object_half_height(g, member, PARTY_HALF_SIZE);
        set_battle_effect_steps_start(g, member, PARTY_EFFECT_STEPS);
        set_battle_effect_period_start(g, member, PARTY_EFFECT_PERIOD);
    }
}

/* ---- The enemies ---------------------------------------------------------------------------- */

/* The first four enemies' animations step a frame sooner than the last four,
 * so that they don't all move together. */
enum { EARLY_ENEMIES = 4, EARLY_FRAME_DELAY = 1, LATE_FRAME_DELAY = 2 };
enum { ENEMY_EFFECTS_WAIT = 0x20, KIND_SLOTS = 4, NUMBER_MARKS = 16 };

static void rest_the_enemies(GameState *g)
{
    uint8_t attributes = battle_sprite_attributes(g);
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        set_battle_enemy_frame_delay(g, enemy, enemy < EARLY_ENEMIES ? EARLY_FRAME_DELAY : LATE_FRAME_DELAY);
        set_battle_enemy_animation_loaded(g, enemy, SETUP_NONE);
        set_battle_enemy_link_seen(g, enemy, SETUP_NONE);
        set_battle_object_palette_initial(g, BATTLE_FIRST_ENEMY + enemy, SETUP_NONE);
        set_battle_anchor_sources(g, enemy, SETUP_NONE);
        set_battle_enemy_effect_delay(g, enemy, ENEMY_EFFECTS_WAIT);
        set_battle_enemy_effect_period(g, enemy, ENEMY_EFFECTS_WAIT);
        set_battle_effect_attribute_bits(g, enemy, attributes);
        set_battle_effect_attribute_bits_start(g, enemy, attributes);
    }
    for (unsigned kind = 0; kind < KIND_SLOTS; kind++)
        set_battle_enemy_kinds(g, kind, 0xFFFF);
    for (unsigned i = 0; i + 1 < NUMBER_MARKS; i++)
        set_battle_number_marks(g, i, SETUP_NONE);
    set_battle_number_marks(g, NUMBER_MARKS - 1, 0);
    set_battle_next_enemy_checked(g, BATTLE_FIRST_ENEMY);
}

/* What the original sets as it starts that nothing in the battle is known to
 * read (docs/TODO.md): a mark on the battle's page, and the work RAM port
 * pointed at the lower bank. */
enum { START_MARK = 0x40, LOWER_BANK = 0 };

// $CC:E2E4
void setup_reset_for_new_fight(GameState *g)
{
    set_battle_start_mark(g, START_MARK);
    ORIGINAL_EFFECT(hw_port_seek_bank(g, LOWER_BANK));
    clear_drawing(g);
    close_the_menus(g);
    rest_the_objects(g);
    rest_the_enemies(g);
    take_settings(g);
}
