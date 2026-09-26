/* Field mode: the main loop's turn at the screen's changes and the requests.
 *
 * Every frame the field's main loop moves a brightness change and a tint
 * change that are under way a step toward their targets, and then answers
 * what the event scripts and the leader's walk have asked of it
 * (field_requests, field/field.h): a switch stepped onto is pressed, and
 * the step remembered (field/switches.c). The location change, the menus, a
 * chest opened, the tile copy and the battle are the original's still: where
 * one is asked for, the port hands the rest of the turn to the original's
 * code at the place it reads the requests. */
#include "field/field.h"
#include "field/menus.h"
#include "field/requests.h"
#include "field/switches.h"

#include "charge.h"
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
    if (!p.pressed)
        return true;
    ORIGINAL_EFFECT(leave_shape_before_the_sound(g, p));
    s->cpu.a = (uint16_t)(field_switch_sound_high_a(g, p, step) << 8 | (s->cpu.a & 0xFF));
    unsigned shape = p.shape;
    uint32_t site = p.step_column == 0 ? sound_sites[shape].left : sound_sites[shape].right;
    if (!bridge_call(s, site, SWITCH_SOUND, SOUND_READS_NOTHING, on_the_field_page(s, false)))
        return false;
    field_queue_switch_redraw(g, p, step);
    m_set_flag(s, CPU_FLAG_V, field_switch_redraw_overflow(g));
    return true;
}

/* ---- The entry ------------------------------------------------------------------------------ */

/* Where the original's code carries on with the requests the port leaves
 * it: the location change, where it reads the fade's brightness (A's high
 * byte and the overflow as they came, which the port leaves); a chest, where
 * it tests the requests in A for the step event before it (the port puts them
 * there); the tile copy and the battle, where it reads them after the chest
 * (the overflow as the switch's redraw left it, field/leaves.c). */
enum { THE_ORIGINALS_LOCATION_CHANGE = 0xC00C91, THE_ORIGINALS_CHEST = 0xC00D3E,
       THE_ORIGINALS_LATER_REQUESTS = 0xC01770 };
enum { FIRST_DEAD = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C, CHEST_DEAD = DC_X | DC_Y | DC_N | DC_Z | DC_C,
       LATER_DEAD = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

// $C0:0C76
/* The field's page, 8-bit A and 16-bit index registers. */
void field_handle_requests(Snes *s)
{
    GameState *g = state_of(s);
    field_step_screen_changes(g);
    if (field_requests(g) == 0) {
        m_rts(s);
        return;
    }
    if (asked(g, FIELD_REQUEST_LOCATION_CHANGE)) {
        m_stop_at_own(s, THE_ORIGINALS_LOCATION_CHANGE, FIRST_DEAD);
        return;
    }
    if (!asked(g, FIELD_REQUEST_MENUS) && asked(g, FIELD_REQUEST_OPEN_CHEST)) {
        m_set_a8(s, field_requests(g));
        m_stop_at_own(s, THE_ORIGINALS_CHEST, CHEST_DEAD);
        return;
    }
    if (asked(g, FIELD_REQUEST_MENUS)) {
        if (field_open_script_menu(s))
            m_rts(s);
        return;
    }
    if (asked(g, FIELD_REQUEST_STEP_EVENT) && !answer_step_event(s))
        return;
    if (asked(g, FIELD_REQUEST_TILE_COPY | FIELD_REQUEST_BATTLE)) {
        m_stop_at_own(s, THE_ORIGINALS_LATER_REQUESTS, LATER_DEAD);
        return;
    }
    m_rts(s);
}

/* The guard: on the field's page with the data bank $00, and the switch a
 * step event names inside the map's rows (a switch reaches up to two rows
 * above the tile stepped on), and not a one-by-two switch whose upper tile
 * is the tile set's last (moving it on would carry into the next tile, as
 * the original moves it, a word at a time). */
enum { MAP_ROWS = 64, SWITCH_ROWS_ABOVE = 2, LAST_TILE = 0xFF };

bool field_requests_can_run(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t requests = field_requests(g);
    bool ok = true;
    if ((requests & FIELD_REQUEST_STEP_EVENT) &&
        !(requests & (FIELD_REQUEST_LOCATION_CHANGE | FIELD_REQUEST_MENUS | FIELD_REQUEST_OPEN_CHEST))) {
        uint16_t step = field_step_tile(g);
        ok = (step >> 8) < MAP_ROWS && ((step >> 8) >= SWITCH_ROWS_ABOVE || !field_is_switch(g, step)) &&
             !(field_is_one_by_two_switch(g, step) && field_map_byte(g, field_switch_upper_tile(step)) == LAST_TILE);
    }
    vars_peek(false);
    return ok;
}
