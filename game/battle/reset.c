/* Battle: the battlefield's animation state, reset.
 *
 * Before a battle's objects move, what animates them is put back to nothing:
 * the animation scripts (their channels, variables and waits, and the
 * combatant each animates), the combatants' animations and the script
 * commands' cells that wait on them, the effects and their paths, the anchors
 * the effects follow, the trails and afterimages, the palette animations, and
 * the numbers rising over the combatants. The modules that own them are
 * battle/scripts.c, animation.c, objects.c, effect_paths.c, object_motion.c
 * and numbers.c; this is where they all start from. */
#include "battle/animation.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "vars.h"

/* "None" for a channel's combatant and an effect's anchor, and the line that
 * puts a trail's afterimage below the picture. */
enum { NOBODY = 0xFF, NO_ANCHOR = 0xFF, TRAIL_OFF_SCREEN = 0xF0 };

static void reset_scripts(GameState *g)
{
    for (unsigned i = 0; i < BATTLE_SCRIPT_VARIABLES_COUNT; i++)
        set_battle_script_variables(g, i, 0);
    set_battle_screen_scripts_running(g, 0);
    for (unsigned i = 0; i < BATTLE_SCRIPT_MARKS_COUNT; i++)
        set_battle_script_marks(g, i, 0);
    set_battle_script_point_x(g, 0);
    set_battle_script_point_y(g, 0);
    for (unsigned i = 0; i < BATTLE_ANIMATION_POSITIONS_COUNT; i++)
        set_battle_animation_positions(g, i, 0);
    for (unsigned channel = 0; channel < BATTLE_SCRIPT_RUNNING_COUNT; channel++) {
        set_battle_script_running(g, channel, 0);
        set_battle_script_pointers(g, channel, 0);
        set_battle_script_waits(g, channel, 0);
    }
    for (unsigned i = 0; i < BATTLE_SCRIPT_POINTER_PADS_COUNT; i++)
        set_battle_script_pointer_pads(g, i, 0);
    for (unsigned channel = 0; channel < BATTLE_SCRIPT_SUBJECTS_COUNT; channel++)
        set_battle_script_subjects(g, channel, NOBODY);
    set_battle_script_signal(g, 0);
    set_battle_script_countdown_spare(g, 0);
    set_battle_throw_wait_before(g, 0);
    set_battle_script_effect_high(g, 0);
}

static void reset_combatants(GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        set_battle_animating(g, slot, 0);
        set_battle_animation_delay(g, slot, 0);
        set_battle_path_active(g, slot, 0);
        set_battle_trail_on(g, slot, 0);
        set_battle_effect_done(g, slot, 0);
        set_battle_prepared_sprites_on(g, slot, 0);
        set_battle_sprite_offset_x(g, slot, 0);
        set_battle_sprite_offset_y(g, slot, 0);
        set_battle_number_rise_alt(g, slot, 0);
    }
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        set_battle_animation_frame(g, slot, 0);
        set_battle_animation_shown_wait(g, slot, 0);
        set_battle_animation_wait(g, slot, 0);
        set_battle_blink_countdown(g, slot, 0);
        set_battle_blink_flashing(g, slot, 0);
    }
    for (unsigned i = 0; i < BATTLE_SCRIPT_COMMAND_CELLS_COUNT; i++)
        set_battle_script_command_cells(g, i, 0);
    set_battle_animation_cursor(g, 0);
    set_battle_graphics_upload_request(g, 0);
    for (unsigned i = 0; i < BATTLE_ANIMATION_POSITION_MODES_COUNT; i++)
        set_battle_animation_position_modes(g, i, 0);
}

static void reset_effects(GameState *g)
{
    for (unsigned effect = 0; effect < BATTLE_EFFECT_ANIMATING_COUNT; effect++) {
        set_battle_effect_animating(g, effect, 0);
        set_battle_effect_frame_time(g, effect, 0);
        set_battle_effect_shown(g, effect, 0);
    }
    for (unsigned effect = 0; effect < BATTLE_EFFECT_FRAME_COUNT; effect++) {
        set_battle_effect_frame(g, effect, 0);
        set_battle_effect_shown_wait(g, effect, 0);
        set_battle_effect_wait(g, effect, 0);
        set_battle_effect_spare_cells(g, effect, 0);
    }
    for (unsigned slot = 0; slot < BATTLE_EFFECT_ACTIVE_COUNT; slot++) {
        set_battle_effect_active(g, slot, 0);
        set_battle_effect_angle(g, slot, 0);
        set_battle_effect_base_x(g, slot, 0);
        set_battle_effect_base_y(g, slot, 0);
        set_battle_effect_target_x(g, slot, 0);
        set_battle_effect_target_y(g, slot, 0);
    }
    for (unsigned i = 0; i < BATTLE_OBJECT_EFFECT_SLOTS_COUNT; i++)
        set_battle_object_effect_slots(g, i, 0);
}

static void reset_anchors(GameState *g)
{
    for (unsigned anchor = 0; anchor < BATTLE_ANCHOR_X_COUNT; anchor++) {
        set_battle_anchor_x(g, anchor, 0);
        set_battle_anchor_y(g, anchor, 0);
        set_battle_anchor_offset_x(g, anchor, 0);
        set_battle_anchor_offset_y(g, anchor, 0);
        set_battle_anchor_done(g, anchor, 0);
        set_battle_anchor_sources(g, anchor, NO_ANCHOR);
    }
}

static void reset_trails(GameState *g)
{
    for (unsigned entry = 0; entry < BATTLE_TRAIL_LOW_COUNT; entry++)
        for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
            set_battle_trail_low_x_byte(g, entry, slot, TRAIL_OFF_SCREEN);
            set_battle_trail_low_y_byte(g, entry, slot, TRAIL_OFF_SCREEN);
        }
}

static void reset_palettes_and_numbers(GameState *g)
{
    for (unsigned i = 0; i < BATTLE_PALETTE_ANIM_ON_COUNT; i++) {
        set_battle_palette_anim_on(g, i, 0);
        set_battle_palette_anim_frame(g, i, 0);
    }
    set_battle_number_step(g, 0);
    set_battle_numbers_rising(g, 0);
    set_battle_frame_tick(g, 0);
}

// $CC:F06B
void battle_reset_objects(GameState *g)
{
    reset_scripts(g);
    reset_combatants(g);
    reset_effects(g);
    reset_anchors(g);
    reset_trails(g);
    reset_palettes_and_numbers(g);
}

void battle_reset_object_state(Snes *s)
{
    battle_reset_objects(state_of(s));
    m_rtl(s);
}
