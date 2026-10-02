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
 * The field starts again on a fresh stack, which is the original's to do; so
 * are the routines the answers call out to. */
#include "field/field.h"
#include "field/menus.h"
#include "field/party.h"
#include "field/requests.h"
#include "field/switches.h"
#include "field/walk.h"

#include "charge.h"
#include "core/display.h"
#include "field/sprites.h"
#include "field/streaming.h"
#include "field/layers.h"
#include "field/bridge.h"
#include "field/display.h"
#include "field/parts.h"
#include "field/scratch.h"
#include "port/machine.h"
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

/* The switch's sound: the original's routine, called from each switch's
 * own place ($C0:1B90, which pans it on the leader). It reads A's high byte
 * (the sound driver keeps it and hands it back) and nothing else of the
 * registers. Two of the five switches note their shape before the sound and
 * three after; nothing reads it in between, but the call is compared, so the
 * shape is noted first where the original does. */
enum { SWITCH_SOUND = 0xC01B90, SOUND_READS_NOTHING = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
/* Where each switch calls the sound from, by its shape and the column
 * stepped into: its own place, which is also where the port stops if the
 * call comes back other than it went. */
static const struct {
    uint32_t left, right;   /* stepped into the switch's left column, or its right */
} sound_sites[] = {
    [SWITCH_ONE_BY_TWO] = { 0xC00D92, 0xC00D92 },
    [SWITCH_TWO_BY_TWO] = { 0xC00E8F, 0xC01040 },
    [SWITCH_TWO_BY_THREE] = { 0xC01212, 0xC0149D },
};

static void leave_shape_before_the_sound(GameState *g, SwitchPress p)
{
    if (p.shape == SWITCH_ONE_BY_TWO || (p.shape == SWITCH_TWO_BY_TWO && p.step_column == 0))
        set_field_switch_shape(g, (uint8_t)p.shape);
}

/* A chest asked for with the step event is opened after it, and its sound
 * reads A's high byte as the step event's code leaves it (field/leaves.c). */
static void leave_a_for_the_chest(Snes *s, uint16_t step, bool pressed)
{
    if (asked(state_of(s), FIELD_REQUEST_OPEN_CHEST))
        s->cpu.a = (uint16_t)(field_step_event_high_a(state_of(s), step, pressed) << 8 | (s->cpu.a & 0xFF));
}

// $C0:0D45-$C0:16DB
/* A step event: the step remembered, and a switch stepped onto pressed with
 * its sound. False where the sound's call came back other than it went, and
 * the port has stopped. */
static bool answer_step_event(Snes *s)
{
    GameState *g = state_of(s);
    answered(g, FIELD_REQUEST_STEP_EVENT);
    uint16_t step = field_step_tile(g);
    field_remember_step(g, step);
    SwitchPress p = field_press_switch(g, step);
    if (!p.pressed) {
        leave_a_for_the_chest(s, step, false);
        return true;
    }
    ORIGINAL_EFFECT(leave_shape_before_the_sound(g, p));
    s->cpu.a = (uint16_t)(field_switch_sound_high_a(g, p, step) << 8 | (s->cpu.a & 0xFF));
    unsigned shape = p.shape;
    uint32_t site = p.step_column == 0 ? sound_sites[shape].left : sound_sites[shape].right;
    if (!bridge_call(s, site, SWITCH_SOUND, SOUND_READS_NOTHING, on_the_field_page(s, false)))
        return false;
    field_queue_switch_redraw(g, p, step);
    m_set_flag(s, CPU_FLAG_V, field_switch_redraw_overflow(g));
    leave_a_for_the_chest(s, step, true);
    return true;
}

/* ---- Calling on the machine ----------------------------------------------------------------- */

/* The original's routines the answers call. */
enum {
    FRAME_WAIT = 0xC0EC60,          /* waits for the next frame */
    FRAME_WORK = 0xC000BF,          /* the objects' turns and sprites, and the line effects */
    REFRESH = 0xC000DE,             /* the line effects and the palette animation */
    PALETTE_ANIMATION = 0xFDFFF7,   /* the original's jump to it */
    SCROLL_FOLLOWS = 0xC087F1,      /* the layers' scroll put where the screen is */
    SEND_BG1 = 0xC074D4, SEND_BG2 = 0xC074E8, SEND_BG3 = 0xC074F7,   /* a layer's tile map sent after a copy */
    BATTLE_BEGINS = 0xC0024C,       /* the battle's setup on the field; carry set where none is fought */
    BATTLE = 0xC10000,
    FIELD_AFTER_BATTLE = 0xC00283, OBJECTS_AFTER_BATTLE = 0xC028E1, FIELD_TURN = 0xC000EB,
};
/* The ports the answers run as C. */
enum { RECENTRE = 0xC0885A, COPY_TILES = 0xC0AF4E, SCROLL_LAYERS = 0xC02C41, FREE_SPRITE_AREAS = 0xC0E935,
       DRAW_BG1 = 0xC075E9, DRAW_BG2 = 0xC078EC, DRAW_BG3 = 0xC07CB5 };
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
       READS_THE_OVERFLOW = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

static bool call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    return bridge_call(s, site, routine, dont_care, on_the_field_page(s, false));
}
static bool wait_frame(Snes *s, uint32_t site) { return call(s, site, FRAME_WAIT, READS_NOTHING); }

/* ---- A location change ($C0:0C8F) --------------------------------------------------------- */

/* Where the party is kept as the place it came from: the location, the
 * leader's tile, and the way it will face coming back (the opposite of its
 * facing: up and down, left and right pair off in the facing's low bit). */
enum { FACING_OPPOSITE = 0x01 };

static void keep_where_the_party_was(GameState *g)
{
    FieldObject leader = field_linked_object(party_member_link(g, PARTY_LEADER));
    set_location_previous_id(g, location_id(g));
    set_location_previous_x(g, field_object_x_tile(g, leader));
    set_location_previous_y(g, field_object_y_tile(g, leader));
    set_location_previous_flags(g, (uint8_t)(field_object_facing(g, leader) ^ FACING_OPPOSITE));
}

/* The next place becomes the party's. */
static void party_moves_to_the_next_place(GameState *g)
{
    set_location_id(g, location_next_id(g));
    set_location_x(g, location_next_x(g));
    set_location_y(g, location_next_y(g));
    set_location_flags(g, location_next_flags(g));
}

/* The field starts again on a fresh stack at its start ($C0:0000), which the
 * port leaves to the original, from where it sets the stack. */
enum { FIELD_STARTS_AGAIN = 0xC00CC0, AS_THE_FIELD_STARTS = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

// $C0:0C8F
/* A step of the fade a frame; dark, the change made. False where the port
 * has handed the field's new start to the original. */
static bool change_location(Snes *s)
{
    GameState *g = state_of(s);
    if (field_dims_a_step(g))
        return true;
    display_go_dark(s);
    keep_where_the_party_was(g);
    party_moves_to_the_next_place(g);
    m_stop_at_own(s, FIELD_STARTS_AGAIN, AS_THE_FIELD_STARTS);
    return false;
}

/* ---- A chest ($C0:16DC) --------------------------------------------------------------------- */

/* The chest's sound, from the original's own call: the switch's sound's
 * routine with the chest's effect ($C0:1BA7). It reads A's high byte (the
 * sound driver keeps it and hands it back): the step event's where one came
 * before (leave_a_for_the_chest), else as the main loop called the requests.
 * The chest's kind is forgotten before the sound, which only shadow mode's
 * comparison at the call sees: opening the chest sets it again. */
enum { CHEST_SOUND = 0xC01BA7, CHEST_SOUND_SITE = 0xC016EB };

static bool answer_chest(Snes *s)
{
    GameState *g = state_of(s);
    ORIGINAL_EFFECT(field_chest_opening_starts(g));
    answered(g, FIELD_REQUEST_OPEN_CHEST);
    if (!call(s, CHEST_SOUND_SITE, CHEST_SOUND, SOUND_READS_NOTHING))
        return false;
    if (field_open_chest(g))
        m_set_flag(s, CPU_FLAG_V, field_chest_redraw_overflow(g));
    return true;
}

/* ---- A tile copy ($C0:1779) ----------------------------------------------------------------- */

/* The layers a copy changed (field_tile_copy_queued_maps), drawn again and
 * sent: BG1, BG2 or BG3 alone, or BG1 and BG2 (below). A layer is drawn on the
 * layers' page, its tile map prepared to send, and the vblank told to move it
 * to the layer's place (field_map_transition); after it the frame's effects
 * are refreshed and the layers' scroll put where the screen is. Each layer
 * alone has its own copy of the calls, by where each is; BG3's ends with a
 * frame wait of its own, the others by jumping to the main loop's. */
typedef enum { QUEUED_BG1 = 1, QUEUED_BG2 = 2, QUEUED_BG1_AND_BG2 = 3, QUEUED_BG3 = 4 } QueuedMaps;

static const struct {
    void (*draw)(Snes *);
    uint32_t draw_routine, prepare_routine;
    FieldMapMove move;
    bool waits_itself;
    uint32_t draw_site, prepare_site, refresh_site, wait_site, refresh_after_site, scroll_site, last_wait_site;
} alone[FIELD_LAYERS] = {
    [FIELD_BG1] = { field_draw_bg1_at_the_load, DRAW_BG1, SEND_BG1, FIELD_MOVE_TO_BG1, false,
                    0xC017A9, 0xC017AD, 0xC017B0, 0xC017B9, 0xC017BC, 0xC017BF, 0 },
    [FIELD_BG2] = { field_draw_bg2_at_the_load, DRAW_BG2, SEND_BG2, FIELD_MOVE_TO_BG2, false,
                    0xC017D2, 0xC017D6, 0xC017D9, 0xC017E2, 0xC017E5, 0xC017E8, 0 },
    [FIELD_BG3] = { field_draw_bg3_at_the_load, DRAW_BG3, SEND_BG3, FIELD_MOVE_TO_BG3, true,
                    0xC0186F, 0xC01873, 0xC01876, 0xC0187F, 0xC01882, 0xC01885, 0xC01888 },
};

/* BG1 and the transition base trade places: the copy's second pass sends
 * BG1's map to where the first sent BG2's. */
static void trade_bg1_base(GameState *g)
{
    uint16_t bg1 = field_layer_map_base(g, 0);
    set_field_layer_map_base(g, 0, field_transition_map_base(g));
    set_field_transition_map_base(g, bg1);
}

/* The original ends most sends by jumping to the frame wait, which returns
 * to the main loop itself; the port stops there. What the main loop reads
 * after it is what the calls before left. */
enum { THE_LAST_WAIT = DC_A | DC_N | DC_Z };

// $C0:1779 ($C0:179A-$C0:188B for the layers)
/* One layer alone. False where the port stopped. */
static bool send_one_layer(Snes *s, FieldLayer layer)
{
    GameState *g = state_of(s);
    if (!field_run_on_page(s, alone[layer].draw_site, alone[layer].draw_routine, READS_NOTHING, alone[layer].draw,
                           FIELD_LAYERS_PAGE) ||
        !call(s, alone[layer].prepare_site, alone[layer].prepare_routine, READS_NOTHING) ||
        !call(s, alone[layer].refresh_site, REFRESH, READS_NOTHING))
        return false;
    set_field_map_transition(g, alone[layer].move);
    set_field_tile_copy_queued_maps(g, 0);
    if (!wait_frame(s, alone[layer].wait_site) || !call(s, alone[layer].refresh_after_site, REFRESH, READS_NOTHING) ||
        !call(s, alone[layer].scroll_site, SCROLL_FOLLOWS, READS_NOTHING))
        return false;
    if (alone[layer].waits_itself)
        return wait_frame(s, alone[layer].last_wait_site);
    m_stop_at_own(s, FRAME_WAIT, THE_LAST_WAIT);
    return false;
}

/* BG1 and BG2: BG1 drawn and sent; then BG2 drawn and sent to BG1's place
 * while BG1 goes again to the transition's; then BG1 drawn and sent back, a
 * frame each. */
static bool send_bg1_and_bg2(Snes *s)
{
    GameState *g = state_of(s);
    if (!field_run_on_page(s, 0xC017FE, DRAW_BG1, READS_NOTHING, field_draw_bg1_at_the_load, FIELD_LAYERS_PAGE) ||
        !call(s, 0xC01802, REFRESH, READS_NOTHING))
        return false;
    set_field_map_transition(g, FIELD_MOVE_TO_THE_LOADERS_PLACE);
    set_field_tile_copy_queued_maps(g, 0);
    if (!wait_frame(s, 0xC0180B) ||
        !field_run_on_page(s, 0xC01817, DRAW_BG2, READS_NOTHING, field_draw_bg2_at_the_load, FIELD_LAYERS_PAGE))
        return false;
    trade_bg1_base(g);
    if (!call(s, 0xC01825, SEND_BG1, READS_NOTHING) || !call(s, 0xC01828, SEND_BG2, READS_NOTHING) ||
        !call(s, 0xC0182B, REFRESH, READS_NOTHING))
        return false;
    set_field_map_transition(g, FIELD_MOVE_TO_BG2);
    if (!wait_frame(s, 0xC01832) ||
        !field_run_on_page(s, 0xC0183E, DRAW_BG1, READS_NOTHING, field_draw_bg1_at_the_load, FIELD_LAYERS_PAGE))
        return false;
    trade_bg1_base(g);
    if (!call(s, 0xC0184C, SEND_BG1, READS_NOTHING) || !call(s, 0xC0184F, REFRESH, READS_NOTHING))
        return false;
    set_field_map_transition(g, FIELD_MOVE_TO_BG1);
    if (!wait_frame(s, 0xC01856) || !call(s, 0xC01859, REFRESH, READS_NOTHING) ||
        !call(s, 0xC0185C, SCROLL_FOLLOWS, READS_NOTHING))
        return false;
    m_stop_at_own(s, FRAME_WAIT, THE_LAST_WAIT);
    return false;
}

// $C0:1779
/* The screen scrolls back onto the grid a frame at a time, the frame's work
 * going on, then the tiles are copied and the layers they changed sent.
 * False where the port stopped. */
static bool answer_tile_copy(Snes *s)
{
    GameState *g = state_of(s);
    answered(g, FIELD_REQUEST_TILE_COPY);
    screen_starts_recentring(g);
    for (;;) {
        if (!field_run(s, 0xC01781, RECENTRE, READS_NOTHING, field_recentre_the_screen, on_the_field_page(s, false)) ||
            !call(s, 0xC01784, FRAME_WORK, READS_THE_OVERFLOW))
            return false;
        if (screen_is_recentred(g))
            break;
        if (!wait_frame(s, 0xC0178B))
            return false;
    }
    if (!field_run(s, 0xC01790, COPY_TILES, READS_NOTHING, field_copy_tiles, on_the_field_page(s, false)) ||
        !bridge_call_long(s, 0xC01793, PALETTE_ANIMATION, READS_NOTHING, on_the_field_page(s, false)) ||
        !wait_frame(s, 0xC01797))
        return false;
    switch ((QueuedMaps)field_tile_copy_queued_maps(g)) {
    case QUEUED_BG1: return send_one_layer(s, FIELD_BG1);
    case QUEUED_BG2: return send_one_layer(s, FIELD_BG2);
    case QUEUED_BG3: return send_one_layer(s, FIELD_BG3);
    case QUEUED_BG1_AND_BG2: return send_bg1_and_bg2(s);
    default: return true;
    }
}

/* ---- A battle ($C0:188C) ------------------------------------------------------------------- */

/* The battle's over, for the script that asked for it (event $D8). */
static void battle_over(GameState *g)
{
    answered(g, FIELD_REQUEST_BATTLE);
    set_field_effect_status(g, (uint8_t)(field_effect_status(g) | FIELD_BATTLE_OVER));
}

/* Where the battle's setup on the field finds none to fight, the field's turn
 * goes on by the original's jump to it, which returns to the main loop. */
enum { NO_BATTLE_TURN = DC_A | DC_N | DC_Z };

// $C0:188C
/* The layers scrolled into place and the effects built with the field's
 * setup's values, the battle fought (bank $C1's), and the field taken up
 * again: its interrupts, its objects and its turn. False where the port
 * stopped. */
static bool answer_battle(Snes *s)
{
    GameState *g = state_of(s);
    if (!call(s, 0xC01890, BATTLE_BEGINS, READS_NOTHING))
        return false;
    if (m_flag(s, CPU_FLAG_C)) {   /* the setup's answer: no battle */
        battle_over(g);
        m_stop_at_own(s, FIELD_TURN, NO_BATTLE_TURN);
        return false;
    }
    if (!field_run_long(s, 0xC01895, SCROLL_LAYERS, READS_NOTHING, field_scroll_layers, on_the_field_page(s, false)))
        return false;
    set_field_display_flags(g, (uint8_t)(field_display_flags(g) | FIELD_DISPLAY_SETUP));
    if (!call(s, 0xC0189D, REFRESH, READS_NOTHING))
        return false;
    set_field_display_flags(g, (uint8_t)(field_display_flags(g) & ~FIELD_DISPLAY_SETUP));
    if (!wait_frame(s, 0xC018A4))
        return false;
    s->cpu.a = field_frame_wait_leaves_a(g);
    if (!bridge_call_long(s, 0xC018A7, BATTLE, 0, on_the_field_page(s, false)))
        return false;
    TIMED_AS(g, FIELD_INSTALL_NMI_ENTRY, field_take_the_vblank_interrupt(g));
    TIMED_AS(g, FIELD_INSTALL_IRQ_ENTRY, field_take_the_line_interrupt(g));
    battle_over(g);
    return call(s, 0xC018C1, FIELD_AFTER_BATTLE, READS_NOTHING) &&
           call(s, 0xC018C4, OBJECTS_AFTER_BATTLE, READS_NOTHING) &&
           field_run(s, 0xC018C7, FREE_SPRITE_AREAS, READS_NOTHING, field_free_every_sprite_area,
                     on_the_field_page(s, false)) &&
           call(s, 0xC018CA, FIELD_TURN, READS_NOTHING);
}

/* ---- The entry ------------------------------------------------------------------------------ */

// $C0:0C76
/* The field's page, 8-bit A and 16-bit index registers. A's high byte and the
 * overflow are read after it, as the routines it calls leave them or as it
 * came. */
/* The requests asked for, in their order. False where the port stopped. */
static bool answer_requests(Snes *s)
{
    GameState *g = state_of(s);
    if (asked(g, FIELD_REQUEST_LOCATION_CHANGE))
        return change_location(s);
    if (asked(g, FIELD_REQUEST_MENUS))
        return field_open_script_menu(s);
    if (asked(g, FIELD_REQUEST_STEP_EVENT) && !answer_step_event(s))
        return false;
    if (asked(g, FIELD_REQUEST_OPEN_CHEST) && !answer_chest(s))
        return false;
    if (asked(g, FIELD_REQUEST_TILE_COPY))
        return answer_tile_copy(s);
    if (asked(g, FIELD_REQUEST_BATTLE))
        return answer_battle(s);
    return true;
}

void field_handle_requests(Snes *s)
{
    GameState *g = state_of(s);
    field_step_screen_changes(g);
    if (field_requests(g) == 0 || answer_requests(s))
        m_rts(s);
}

/* The guard: on the field's page with the data bank $00, and the switch a
 * step event names, and each of the switch's tiles, inside the map's rows
 * (a switch reaches up to two rows above the tile stepped on, and a column
 * left of it, which from a row's first column is the row above's last), and
 * not a one-by-two switch whose upper tile
 * is the tile set's last (moving it on would carry into the next tile, as
 * the original moves it, a word at a time); and a chest opened inside the
 * map's rows; for a location change made (the fade done), the leader an
 * object, its link a word (the original reads the leader's place with it as
 * a 16-bit index). */
enum { LAST_TILE = 0xFF };

bool field_requests_can_run(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t requests = field_requests(g);
    bool ok = true;
    if ((requests & FIELD_REQUEST_STEP_EVENT) &&
        !(requests & (FIELD_REQUEST_LOCATION_CHANGE | FIELD_REQUEST_MENUS))) {
        uint16_t step = field_step_tile(g);
        ok = (step >> 8) < FIELD_MAP_ROWS && field_switch_inside_the_map(g, step) &&
             !(field_is_one_by_two_switch(g, step) && field_map_byte(g, field_switch_upper_tile(step)) == LAST_TILE);
    }
    if ((requests & FIELD_REQUEST_LOCATION_CHANGE) && field_is_dark(g))
        ok = ok && field_party_objects_high(g, PARTY_LEADER) == 0 &&
             field_link_names_object(party_member_link(g, PARTY_LEADER));
    if ((requests & FIELD_REQUEST_OPEN_CHEST) && !(requests & (FIELD_REQUEST_LOCATION_CHANGE | FIELD_REQUEST_MENUS)))
        ok = ok && (field_opened_chest_tile(g) >> 8) < FIELD_MAP_ROWS;
    vars_peek(false);
    return ok;
}
