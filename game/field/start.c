/* Field mode: the field's state as it starts.
 *
 * Before the field sets up its first map (after the title, a load, or the
 * return from another mode), its state is put as a map's arrival expects it:
 * where the party is is also the place put aside; nothing asked of the main
 * loop, nothing running and nothing locked; the screen dark with no effects,
 * no dialog and no window; the player given the party; no objects in the
 * party's places or standing for the characters, the party as it is now taken
 * as the one the characters were loaded for; the sprite tables' places and
 * the sounds the field makes at their usual values; and the random numbers
 * started from the play time. */
#include "field/start.h"
#include "field/field.h"
#include "field/field_data.h"
#include "field/object.h"
#include "field/party.h"
#include "field/sprites.h"
#include "field/walk.h"

#include "field/bridge.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* Where the party is, also as the place put aside (event $DC's). */
static void put_the_location_aside(GameState *g)
{
    set_location_stored_id(g, location_id(g));
    set_location_stored_x(g, location_x(g));
    set_location_stored_y(g, location_y(g));
    set_location_stored_flags(g, location_flags(g));
}

/* Nothing asked of the main loop and nothing it runs under way; the scripts'
 * locks on the pause and the menus lifted; no screen effect, no tile copy
 * queued, no map transition, no tiles waiting to be redrawn. */
static void nothing_under_way(GameState *g)
{
    set_field_requests(g, 0);
    set_field_effect_status(g, 0);
    field_lock_menus(g, false);
    field_lock_pause(g, false);
    set_field_mode_flags(g, 0);
    screen_counts_as_recentred(g);
    set_field_screen_job(g, 0);
    set_field_screen_shake(g, 0);
    set_field_tile_copy_maps(g, 0);
    set_field_tile_copy_queued_maps(g, 0);
    set_field_map_transition(g, 0);
    set_field_tiles_to_redraw(g, 0);
    set_field_animation_queue_front(g, 0);
    count_as_on_the_move(g);
    set_field_input_latched(g, 0);
}

/* The screen dark and plain: no brightness, no tint, the per-line effects the
 * field keeps on, the line interrupt working, the effect tables' first set,
 * color math as the field starts it, the windows off and none of their
 * corners marked, the backdrop (the palette's first color) black, and BG3 as
 * the map gives it. */
enum { DARK = 0, WINDOW_SHADOW_BYTES = 3, BACKDROP = 0, BLACK = 0 };

static void dark_plain_screen(GameState *g)
{
    set_field_brightness(g, DARK);
    set_field_fixed_color(g, FIELD_NO_TINT);
    set_field_line_effect_enable(g, FIELD_LINE_EFFECTS_KEPT_ON);
    field_line_interrupt_works(g, true);
    set_field_display_flags(g, 0);
    set_field_color_math_control(g, field_starting_color_math());
    for (unsigned i = 0; i < WINDOW_SHADOW_BYTES; i++)
        set_field_window_shadows_byte(g, i, 0);
    set_field_window_tile_slot(g, FIELD_NO_WINDOW_MARKED);
    field_map_names_a_bg3_set(g, true);
    set_field_bg3_map_size(g, 0);
    set_palette_base(g, BACKDROP, BLACK);
}

/* No dialog: none open, no window, no choice, no string table. */
static void no_dialog(GameState *g)
{
    set_dialog_state(g, 0);
    set_dialog_window_position(g, WINDOW_NONE);
    set_dialog_box_position(g, 0);
    set_dialog_choice_active(g, DIALOG_NO_CHOICES);
    set_dialog_string_table(g, 0);
    set_dialog_speaker_high(g, 0);
}

/* The player has the party, which walks into the map and its objects on the
 * first level, up against nothing; scripts run five commands a frame. */
enum { COMMANDS_PER_TURN = 5 };

static void party_to_the_player(GameState *g)
{
    hand_the_party_to_the_player(g, true);
    field_leader_meets_the_map(g, true);
    set_field_leader_level(g, FIRST_LEVEL);
    leader_loses_contact(g);
    set_event_commands_per_turn(g, COMMANDS_PER_TURN);
}

/* No object in the party's places or standing for a character, none with a
 * sprite built in work RAM, and the party as it is now taken as the one the
 * characters were loaded for. */
static void no_objects_yet(GameState *g)
{
    for (unsigned place = 0; place < PARTY_PLACES; place++)
        party_place_left_empty(g, place);
    for (unsigned character = 0; character < FIELD_CHARACTER_OBJECTS_COUNT; character++)
        set_field_character_objects(g, character, field_link_byte(field_no_link()));
    set_field_built_sprite_object(g, field_link_byte(field_no_link()));
    party_taken_as_loaded(g);
}

/* The sprite tables' first frame hides everything it doesn't draw, and the
 * sprite loading's four tables are where the ROM says. */
static void sprites_from_scratch(GameState *g)
{
    sprites_hide_all_next_frame(g);
    for (unsigned t = 0; t < FIELD_SPRITE_DATA_TABLES_COUNT; t++)
        set_field_sprite_data_tables(g, t, field_sprite_data_table(g, t));
}

/* The sounds the field makes: a switch pressed, a chest opened, and a
 * battle's song; and no battle set up. */
enum { SWITCH_SOUND = 0x53, CHEST_SOUND = 0x65, BATTLE_SONG = 0x45 };

static void usual_sounds(GameState *g)
{
    set_field_switch_sound(g, SWITCH_SOUND);
    set_field_chest_sound(g, CHEST_SOUND);
    set_field_battle_song(g, BATTLE_SONG);
    set_battle_setup_flags(g, 0);
}

// $C0:0B86
void field_start_state(GameState *g)
{
    put_the_location_aside(g);
    nothing_under_way(g);
    dark_plain_screen(g);
    no_dialog(g);
    party_to_the_player(g);
    no_objects_yet(g);
    sprites_from_scratch(g);
    usual_sounds(g);
    set_field_random_index(g, play_time_frames(g));
}

/* The field's page. Nothing it leaves in the registers is read after it. */
void field_start(Snes *s)
{
    field_start_state(state_of(s));
    m_rts(s);
}
