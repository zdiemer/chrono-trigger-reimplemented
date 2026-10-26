/* The wave on the layer over a map screen's map: an object of the sprite-object
 * mode (menu/object.h) that bends the layer's lines from side to side, a
 * sine's worth down the screen, and moves the sine on a little every frame so
 * the bend ripples down.
 *
 * The layer is scrolled line by line from a table of 224 scroll values, one a
 * line, through a line effect the object sets up each frame; the even lines
 * keep the layer's own scroll and the odd ones take the wave, which gives the
 * layer the look of shimmering water. The effect is turned off while the
 * screen is off, and the object does nothing while the map screen's objects
 * are held still. */
#include "menu/objects.h"

#include "menu/line_effects.h"
#include "menu/menu_data.h"
#include "menu/object.h"
#include "menu/screen_objects.h"
#include "menu/top_layer.h"
#include "overworld/overworld.h"
#include "original_effects.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The wave is line effect slot 1, the layer's horizontal scroll, from the
 * slot's first table of values (menu/line_effects.h). */
enum { WAVE_SLOT = 1 };

/* The display's bit that turns the screen off, the line effect's bit among
 * the mode's line effects, and the objects' held bit. */
enum { WAVE_EFFECT = 1 << WAVE_SLOT };

/* The sine's steps a turn, and how far the wave moves on a frame. */
enum { SINE_STEPS = 0x400, HALF_TURN = 0x200, WAVE_SPEED = 4 };

static int16_t sine(const GameState *g, uint16_t phase)
{
    uint16_t step = phase & (SINE_STEPS - 1);
    int16_t size = menu_wave_size(g, step);
    return step >= HALF_TURN ? (int16_t)-size : size;
}

static void set_up_line_effect(GameState *g, Platform *pf)
{
    menu_start_line_effect(g, pf, 0xC2779E, WAVE_SLOT, VID_LINE_SCROLL_BG3_H);
    menu_show_line_values(g, WAVE_SLOT, menu_line_values(WAVE_SLOT, 0));
}

// $C2:7759
/* One frame of the wave. Its phase is where the wave starts at the top of
 * the screen; the original means to keep it in the object and start it from
 * nothing, but keeps it in a cell of its own (menu_wave_phase), so it starts
 * where the last wave left it. False where there is no wave this frame. */
static bool wave(GameState *g, Platform *pf, MenuObject self)
{
    if (menu_screen_is_off(g)) {
        set_menu_object_line_effects(g, (uint8_t)(menu_object_line_effects(g) & ~WAVE_EFFECT));
        return false;
    }
    if (overworld_epoch_leaving_through_time(g))
        return false;
    if (menu_object_slots_step(g, self) == 0) {
        set_menu_object_slots_step(g, self, 1);
        set_menu_object_slots_phase(g, self, 0);
    }
    set_up_line_effect(g, pf);
    uint16_t scroll = menu_object_shadows_bg3_scroll_x(g);
    for (unsigned line = 0; line < MENU_SLOT1_LINE_VALUES_COUNT; line += 2)
        set_menu_slot1_line_values(g, line, scroll);
    uint16_t phase = menu_wave_phase(g);
    set_menu_wave_phase(g, (uint16_t)(phase + WAVE_SPEED));
    for (unsigned line = 1; line < MENU_SLOT1_LINE_VALUES_COUNT; line += 2)
        set_menu_slot1_line_values(g, line, (uint16_t)(scroll + sine(g, phase++)));
    ORIGINAL_EFFECT(set_menu_object_scratch_wave_phase(g, phase));
    return true;
}

// $C2:7759
/* The object's turn as C: it is never done. */
MenuTurn menu_shimmer_turn(GameState *g, Platform *pf, MenuObject self)
{
    (void)wave(g, pf, self);
    return MENU_TURN_GOES_ON;
}

// $C2:7759
/* The object's handler: it is never done, so it hands back the carry clear;
 * it comes back with the accumulator's width the wave's drawing left. */
void menu_object_wave_hdma(Snes *s)
{
    GameState *g = state_of(s);
    if (wave(g, m_platform(s), menu_object_running(g)))
        m_rep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}
