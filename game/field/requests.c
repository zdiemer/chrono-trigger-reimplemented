/* Field mode: the main loop's turn at the screen's changes and the requests.
 *
 * Every frame the field's main loop moves a brightness change and a tint
 * change that are under way a step toward their targets, and then answers
 * what the event scripts and the leader's walk have asked of it
 * (field_requests, field/field.h): a location change alone, or the menus
 * alone, or else a step event, a chest, and then a tile copy or a battle,
 * in this order:
 * - a location change: the picture fades out a step a frame, then the party's
 *   place is kept as the one it came from, the next place becomes the
 *   party's, and the field starts again there;
 * - the menus a script asked for (field/menus.c);
 * - a switch stepped onto pressed, and the step remembered, and a chest the
 *   leader opened shown open (field/switches.c);
 * - a tile copy a script queued: the screen scrolls back onto the grid, the
 *   tiles are copied into the maps (field/tile_copy.c), and the layers they
 *   changed drawn again and sent in the vblank;
 * - a battle: the layers scrolled into place, the battle fought, and the
 *   field taken up again after it.
 * Starting the field again after a location change is the program's
 * (core/program.h). */
#include "field/field.h"
#include "field/battle_return.h"
#include "field/battle_start.h"
#include "field/main_loop.h"
#include "field/menus.h"
#include "field/party.h"
#include "field/requests.h"
#include "field/sounds.h"
#include "field/switches.h"
#include "field/walk.h"

#include "battle/program.h"
#include "battle/entries.h"
#include "charge.h"
#include "core/program.h"
#include "core/display.h"
#include "core/palette.h"
#include "field/layers.h"
#include "field/sprites.h"
#include "field/streaming.h"
#include "field/bridge.h"
#include "field/display.h"
#include "field/parts.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The screen's changes -------------------------------------------------------------------- */

/* A change under way waits its rate's frames between steps: the rate's low
 * byte is the wait, its high byte the frames still to wait. */
enum { BRIGHTNESS_BITS = 0x0F };

static bool change_waits(uint16_t *rate)
{
    uint8_t wait = (uint8_t)*rate, left = (uint8_t)(*rate >> 8);
    if (left != 0) {
        *rate = (uint16_t)((left - 1) << 8 | wait);
        return true;
    }
    *rate = (uint16_t)(wait << 8 | wait);
    return false;
}

// $C0:1F24
/* A step of the brightness toward its target, which ends the change there.
 * Down to nothing, the change ends whatever its target. */
static void step_brightness(GameState *g)
{
    uint8_t brightness = field_brightness(g);
    uint8_t now = (uint8_t)(brightness & BRIGHTNESS_BITS), target = field_brightness_target(g);
    if (now == target) {
        set_field_effect_status(g, (uint8_t)(field_effect_status(g) & ~FIELD_FADING));
        return;
    }
    uint16_t rate = field_brightness_rate(g);
    bool waits = change_waits(&rate);
    set_field_brightness_rate(g, rate);
    if (waits)
        return;
    if (now < target) {
        set_field_brightness(g, (uint8_t)(brightness + 1));
        return;
    }
    set_field_brightness(g, (uint8_t)(brightness - 1));
    if ((uint8_t)(brightness - 1) == 0)
        set_field_effect_status(g, (uint8_t)(field_effect_status(g) & ~FIELD_FADING));
}

// $C0:1F5A
/* A step of the tint (the fixed color) toward its target. */
static void step_tint(GameState *g)
{
    uint8_t tint = field_fixed_color(g), target = field_fixed_color_target(g);
    if (tint == target) {
        set_field_effect_status(g, (uint8_t)(field_effect_status(g) & ~FIELD_TINTING));
        return;
    }
    uint16_t rate = field_fixed_color_rate(g);
    bool waits = change_waits(&rate);
    set_field_fixed_color_rate(g, rate);
    if (!waits)
        set_field_fixed_color(g, (uint8_t)(tint < target ? tint + 1 : tint - 1));
}

void field_step_screen_changes(GameState *g)
{
    if (field_effect_status(g) & FIELD_FADING)
        step_brightness(g);
    if (field_effect_status(g) & FIELD_TINTING)
        step_tint(g);
}

/* ---- The requests' answers ------------------------------------------------------------------ */

static bool asked(const GameState *g, uint8_t request) { return (field_requests(g) & request) != 0; }
static void answered(GameState *g, uint8_t request) { set_field_requests(g, (uint8_t)(field_requests(g) & ~request)); }

/* A sound at the leader, played where the leader is one the port reads (the
 * program reports a broken contract otherwise). */
static bool sound_at_the_leader(GameState *g, Platform *pf, uint32_t entry, void (*play)(GameState *, Platform *))
{
    if (!field_leader_readable(g)) {
        program_breaks_a_contract(g, entry, "a sound at a leader the port doesn't read");
        return false;
    }
    TIMED_AS(g, entry, play(g, pf));
    return true;
}

// $C0:0D45-$C0:16DB
/* A step event: the step remembered, and a switch stepped onto pressed with
 * its sound and drawn again. False where the sound went no further. */
static bool answer_step_event(GameState *g, Platform *pf)
{
    answered(g, FIELD_REQUEST_STEP_EVENT);
    uint16_t step = field_step_tile(g);
    field_remember_step(g, step);
    SwitchPress p = field_press_switch(g, step);
    if (!p.pressed)
        return true;
    if (!sound_at_the_leader(g, pf, FIELD_SWITCH_SOUND_ENTRY, field_play_the_switch_sound))
        return false;
    field_queue_switch_redraw(g, p, step);
    return true;
}

// $C0:16DC
/* A chest the leader opened: its sound, and the chest shown open. The
 * chest's kind is forgotten before the sound; opening the chest sets it
 * again. False where the sound went no further. */
static bool answer_chest(GameState *g, Platform *pf)
{
    answered(g, FIELD_REQUEST_OPEN_CHEST);
    if (!sound_at_the_leader(g, pf, FIELD_CHEST_SOUND_ENTRY, field_play_the_chest_sound))
        return false;
    (void)field_open_chest(g);
    return true;
}

/* ---- The routines the answers call -------------------------------------------------------- */

/* Each timed as its own work (TIMED_AS). */
enum {
    FRAME_WAIT = 0xC0EC60,          /* waits for the next frame */
    PALETTE_ANIMATION = 0xFDFFF7,   /* the palettes' animation */
};
enum {
    RECENTRE = 0xC0885A,
    COPY_TILES = 0xC0AF4E,
    SCROLL_LAYERS = 0xC02C41,
    FREE_SPRITE_AREAS = 0xC0E935,
    DRAW_BG1 = 0xC075E9,
    DRAW_BG2 = 0xC078EC,
    DRAW_BG3 = 0xC07CB5,
};

static void wait_for_the_frame(GameState *g, Platform *pf) { TIMED_AS(g, FRAME_WAIT, field_finish_the_frame(g, pf)); }

static bool refresh_the_effects(GameState *g)
{
    bool done = false;
    TIMED_AS(g, FIELD_REFRESH_EFFECTS_ENTRY, done = field_effects_refreshed(g));
    return done;
}

static bool animate_the_palettes(GameState *g)
{
    bool done = false;
    TIMED_AS(g, PALETTE_ANIMATION, done = palette_animated(g));
    return done;
}

/* ---- A location change ($C0:0C8F) --------------------------------------------------------- */

/* Where the party is kept as the place it came from: the location, the
 * leader's tile, and the way it will face coming back (the opposite of its
 * facing: up and down, left and right pair off in the facing's low bit). */
enum { FACING_OPPOSITE = 0x01 };

static void keep_where_the_party_was(GameState *g)
{
    LeaderPlace at = field_leaders_place(g);
    set_location_previous_id(g, location_id(g));
    set_location_previous_x(g, at.x);
    set_location_previous_y(g, at.y);
    set_location_previous_flags(g, (uint8_t)(at.facing ^ FACING_OPPOSITE));
}

/* ---- A tile copy ($C0:1779) ----------------------------------------------------------------- */

/* The routines a copy calls, each a port: the screen's step back, the frame's
 * work, the tiles' copy (refused off the map), the palettes' animation, a
 * layer's drawing at the map's load, the effects' refresh, the columns right
 * of the view brought in (refused where the layers don't repeat within the
 * maps), and the wait for the next frame. */
static bool step_the_screen_back(GameState *g)
{
    bool done = true;
    TIMED_AS(g, RECENTRE, done = field_the_screen_recentres(g));
    return done;
}

static bool do_the_frame_work(GameState *g, Platform *pf)
{
    bool done = true;
    TIMED_AS(g, FIELD_FRAME_WORK_ENTRY, done = field_frame_work_done(g, pf));
    return done;
}

static bool copy_the_tiles_into_the_maps(GameState *g)
{
    if (!field_tile_copy_is_on_the_map(g)) {
        program_breaks_a_contract(g, COPY_TILES, "a tile copy off the map");
        return false;
    }
    TIMED_AS(g, COPY_TILES, map_copy_tiles(g));
    return true;
}

static bool bring_in_the_right_columns(GameState *g)
{
    if (!layers_repeat_within_the_maps(g)) {
        program_breaks_a_contract(g, FIELD_BRING_IN_THE_RIGHT_COLUMNS_ENTRY, "layers the port doesn't stream");
        return false;
    }
    TIMED_AS(g, FIELD_BRING_IN_THE_RIGHT_COLUMNS_ENTRY, stream_the_right_columns(g));
    return true;
}

/* The layers a copy changed (field_tile_copy_queued_maps), drawn again and
 * sent: BG1, BG2 or BG3 alone, or BG1 and BG2 (below). A layer is drawn on the
 * layers' page and put back at its map's start, and the vblank told to move it
 * to the layer's place (field_map_transition); after it the frame's effects
 * are refreshed and the layers' scroll put where the screen is. BG3's ends
 * with a frame wait of its own, the others by jumping to the main loop's,
 * which is the same wait. */
typedef enum { QUEUED_BG1 = 1, QUEUED_BG2 = 2, QUEUED_BG1_AND_BG2 = 3, QUEUED_BG3 = 4 } QueuedMaps;

static const FieldMapMove layer_moves[FIELD_LAYERS] = {
    [FIELD_BG1] = FIELD_MOVE_TO_BG1,
    [FIELD_BG2] = FIELD_MOVE_TO_BG2,
    [FIELD_BG3] = FIELD_MOVE_TO_BG3,
};

/* BG1 and the transition base trade places: the copy's second pass sends
 * BG1's map to where the first sent BG2's. */
static void trade_bg1_base(GameState *g)
{
    uint16_t bg1 = field_layer_map_base(g, 0);
    set_field_layer_map_base(g, 0, field_transition_map_base(g));
    set_field_transition_map_base(g, bg1);
}

/* The layers' end: the effects refreshed, the right columns brought in, and
 * the last frame's wait. */
static bool finish_the_layers(GameState *g, Platform *pf)
{
    if (!refresh_the_effects(g) || !bring_in_the_right_columns(g))
        return false;
    wait_for_the_frame(g, pf);
    return true;
}

// $C0:179A-$C0:17EB, $C0:1867-$C0:188B
/* One layer alone. */
static bool send_one_layer(GameState *g, Platform *pf, FieldLayer layer)
{
    if (!field_layer_drawn_at_the_load(g, layer))
        return false;
    field_put_the_layer_at_its_start(g, layer);
    if (!refresh_the_effects(g))
        return false;
    set_field_map_transition(g, layer_moves[layer]);
    set_field_tile_copy_queued_maps(g, 0);
    wait_for_the_frame(g, pf);
    return finish_the_layers(g, pf);
}

// $C0:17EC-$C0:1866
/* BG1 and BG2: BG1 drawn and sent; then BG2 drawn and sent to BG1's place
 * while BG1 goes again to the transition's; then BG1 drawn and sent back, a
 * frame each. */
static bool restore_first_layer(GameState *g, Platform *pf)
{
    if (!field_layer_drawn_at_the_load(g, FIELD_BG1))
        return false;
    trade_bg1_base(g);
    field_put_the_layer_at_its_start(g, FIELD_BG1);
    if (!refresh_the_effects(g))
        return false;
    set_field_map_transition(g, FIELD_MOVE_TO_BG1);
    wait_for_the_frame(g, pf);
    return finish_the_layers(g, pf);
}

static bool send_second_layer(GameState *g, Platform *pf)
{
    if (!field_layer_drawn_at_the_load(g, FIELD_BG2))
        return false;
    trade_bg1_base(g);
    field_put_the_layer_at_its_start(g, FIELD_BG1);
    field_put_the_layer_at_its_start(g, FIELD_BG2);
    if (!refresh_the_effects(g))
        return false;
    set_field_map_transition(g, FIELD_MOVE_TO_BG2);
    wait_for_the_frame(g, pf);
    return restore_first_layer(g, pf);
}

static bool send_bg1_and_bg2(GameState *g, Platform *pf)
{
    if (!field_layer_drawn_at_the_load(g, FIELD_BG1) || !refresh_the_effects(g))
        return false;
    set_field_map_transition(g, FIELD_MOVE_TO_THE_LOADERS_PLACE);
    set_field_tile_copy_queued_maps(g, 0);
    wait_for_the_frame(g, pf);
    return send_second_layer(g, pf);
}

static bool send_copied_layers(GameState *g, Platform *pf)
{
    switch ((QueuedMaps)field_tile_copy_queued_maps(g)) {
    case QUEUED_BG1: return send_one_layer(g, pf, FIELD_BG1);
    case QUEUED_BG2: return send_one_layer(g, pf, FIELD_BG2);
    case QUEUED_BG3: return send_one_layer(g, pf, FIELD_BG3);
    case QUEUED_BG1_AND_BG2: return send_bg1_and_bg2(g, pf);
    default: return true;
    }
}

static bool centre_then_copy(GameState *g, Platform *pf)
{
    for (;;) {
        if (!step_the_screen_back(g) || !do_the_frame_work(g, pf))
            return false;
        if (screen_is_recentred(g))
            break;
        wait_for_the_frame(g, pf);
    }
    if (!copy_the_tiles_into_the_maps(g) || !animate_the_palettes(g))
        return false;
    wait_for_the_frame(g, pf);
    return send_copied_layers(g, pf);
}

// $C0:1779
/* Centre the screen, copy the tiles and send the changed layers. Each
 * transfer ends a frame, so its next stage can also be resumed on its own. */
static bool copy_the_tiles(GameState *g, Platform *pf)
{
    answered(g, FIELD_REQUEST_TILE_COPY);
    screen_starts_recentring(g);
    return centre_then_copy(g, pf);
}

/* ---- A battle ($C0:188C) ------------------------------------------------------------------- */

/* The battle's over, for the script that asked for it (event $D8). */
static void battle_over(GameState *g)
{
    answered(g, FIELD_REQUEST_BATTLE);
    set_field_effect_status(g, (uint8_t)(field_effect_status(g) | FIELD_BATTLE_OVER));
}

/* The switches the party stood on pressed again, where they are on the
 * map. */
static bool press_the_switches(GameState *g)
{
    if (!field_remembered_switches_on_the_map(g)) {
        program_breaks_a_contract(g, FIELD_PRESS_REMEMBERED_SWITCHES_ENTRY, "remembered switches off the map");
        return false;
    }
    TIMED_AS(g, FIELD_PRESS_REMEMBERED_SWITCHES_ENTRY, field_press_the_remembered_switches(g));
    return true;
}

// $C0:188C
/* The battle fought, and the field taken up again: the battle's start on the
 * field (field/battle_start.c), which finds none to fight (the field's turn
 * then) or puts the field aside; the layers scrolled into place and the
 * effects built with the field's setup's values, a frame; the battle (bank
 * $C1's); the field's interrupts; the field taken up again
 * (field/battle_return.c); the switches the party stood on pressed again and
 * the sprites' areas freed; and the field's turn after it. False where a
 * step went no further. */
static bool fight_a_battle(GameState *g, Platform *pf)
{
    FieldBattleStart start = FIELD_BATTLE_START_STOPPED;
    TIMED_AS(g, FIELD_BATTLE_BEGINS_ENTRY, start = field_battle_set_up(g, pf));
    if (start == FIELD_BATTLE_START_STOPPED)
        return false;
    if (start == FIELD_NO_BATTLE) {
        battle_over(g);
        return field_walked_a_frame(g, pf);
    }
    TIMED_AS(g, SCROLL_LAYERS, field_scroll_layers_by_themselves(g));
    set_field_display_flags(g, (uint8_t)(field_display_flags(g) | FIELD_DISPLAY_SETUP));
    if (!refresh_the_effects(g))
        return false;
    set_field_display_flags(g, (uint8_t)(field_display_flags(g) & ~FIELD_DISPLAY_SETUP));
    wait_for_the_frame(g, pf);
    if (!battle_fought(g, pf))
        return false;
    TIMED_AS(g, FIELD_INSTALL_NMI_ENTRY, field_take_the_vblank_interrupt(g));
    TIMED_AS(g, FIELD_INSTALL_IRQ_ENTRY, field_take_the_line_interrupt(g));
    battle_over(g);
    if (!field_taken_up_after_battle(g, pf) || !press_the_switches(g))
        return false;
    TIMED_AS(g, FREE_SPRITE_AREAS, sprites_free_every_frame_area(g));
    return field_walked_a_frame(g, pf);
}

/* ---- The requests answered ------------------------------------------------------------------ */

// $C0:0C8F
static FieldPass change_location(GameState *g, Platform *pf)
{
    if (field_dims_a_step(g))
        return FIELD_PASS_GOES_ON;
    TIMED_AS(g, DISPLAY_BLANK_AND_STOP_ENTRY, display_blank_and_stop(pf));
    keep_where_the_party_was(g);
    field_move_to_the_next_place(g);
    return FIELD_STARTS_ANEW;
}

FieldPass field_requests_handled(GameState *g, Platform *pf)
{
    field_step_screen_changes(g);
    if (field_requests(g) == 0)
        return FIELD_PASS_GOES_ON;
    if (!field_requests_can_be_answered(g)) {
        program_breaks_a_contract(g, FIELD_HANDLE_REQUESTS_ENTRY, "requests the port doesn't answer");
        return FIELD_PASS_STOPS;
    }
    if (asked(g, FIELD_REQUEST_LOCATION_CHANGE))
        return change_location(g, pf);
    if (asked(g, FIELD_REQUEST_MENUS))
        return field_script_menu_opened(g, pf);
    if (asked(g, FIELD_REQUEST_STEP_EVENT) && !answer_step_event(g, pf))
        return FIELD_PASS_STOPS;
    if (asked(g, FIELD_REQUEST_OPEN_CHEST) && !answer_chest(g, pf))
        return FIELD_PASS_STOPS;
    if (asked(g, FIELD_REQUEST_TILE_COPY))
        return copy_the_tiles(g, pf) ? FIELD_PASS_GOES_ON : FIELD_PASS_STOPS;
    if (asked(g, FIELD_REQUEST_BATTLE))
        return fight_a_battle(g, pf) ? FIELD_PASS_GOES_ON : FIELD_PASS_STOPS;
    return FIELD_PASS_GOES_ON;
}

/* The requests the port answers: the switch a step event names, and each of the switch's tiles, inside the map's rows
 * (a switch reaches up to two rows above the tile stepped on, and a column
 * left of it, which from a row's first column is the row above's last), and
 * not a one-by-two switch whose upper tile
 * is the tile set's last (the original moves it on a word at a time, into
 * the next tile); and a chest opened inside the
 * map's rows; for a location change made (the fade done), the leader an
 * object or none, its link a word (the original reads the leader's place
 * with it as a 16-bit index). */
enum { LAST_TILE = 0xFF };

bool field_requests_can_be_answered(const GameState *g)
{
    uint8_t requests = field_requests(g);
    bool ok = true;
    if ((requests & FIELD_REQUEST_STEP_EVENT) &&
        !(requests & (FIELD_REQUEST_LOCATION_CHANGE | FIELD_REQUEST_MENUS))) {
        uint16_t step = field_step_tile(g);
        ok = (step >> 8) < FIELD_MAP_ROWS && field_switch_inside_the_map(g, step) &&
             !(field_is_one_by_two_switch(g, step) && field_map_byte(g, field_switch_upper_tile(step)) == LAST_TILE);
    }
    if ((requests & FIELD_REQUEST_LOCATION_CHANGE) && field_is_dark(g))
        ok = ok && field_leader_readable(g);
    if ((requests & FIELD_REQUEST_OPEN_CHEST) && !(requests & (FIELD_REQUEST_LOCATION_CHANGE | FIELD_REQUEST_MENUS)))
        ok = ok && (field_opened_chest_tile(g) >> 8) < FIELD_MAP_ROWS;
    return ok;
}
