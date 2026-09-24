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

#include "menu/menu_data.h"
#include "menu/object.h"
#include "original_effects.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The display's bit that turns the screen off, the line effect's bit among
 * the mode's line effects, and the objects' held bit. */
enum { SCREEN_OFF = 0x80, WAVE_EFFECT = 0x02, OBJECTS_HELD = 0x10 };

/* The wave's line table: two runs of lines, each read from its half of the
 * scroll values, and its end. */
enum { WAVE_RUN_LINES = 0xF0, WAVE_TABLE_END = 0 };
enum { WAVE_FIRST_HALF = 0x8B56, WAVE_SECOND_HALF = 0x8C36 };
enum { WAVE_SLOT = 1, WAVE_TABLE = WAVE_SLOT - 1 };

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
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,     VID_STEP_DEST,          VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_INDIRECT_BANK, VID_STEP_END,
    };
    static const VidLineEffect wave = { .target = VID_LINE_SCROLL_BG3_H, .table = PF_GAME(0x7E8A35),
                                        .indirect = true, .indirect_bank = 0x7E };
    set_menu_object_line_slots_first_lines(g, WAVE_TABLE, WAVE_RUN_LINES);
    set_menu_object_line_slots_second_lines(g, WAVE_TABLE, WAVE_RUN_LINES);
    set_menu_object_line_slots_end(g, WAVE_TABLE, WAVE_TABLE_END);
    set_menu_object_hdma_pointers(g, 0, WAVE_FIRST_HALF);
    set_menu_object_hdma_pointers(g, 1, WAVE_SECOND_HALF);
    vid_set_line_effect(pf, PF_AT(0xC2779E), WAVE_SLOT, &wave, steps);
    set_menu_object_line_effects(g, (uint8_t)(menu_object_line_effects(g) | WAVE_EFFECT));
}

// $C2:7759
/* One frame of the wave. Its phase is where the wave starts at the top of
 * the screen; the original means to keep it in the object and start it from
 * nothing, but keeps it in a cell of its own (menu_wave_phase), so it starts
 * where the last wave left it. False where there is no wave this frame. */
static bool wave(GameState *g, Platform *pf, MenuObject self)
{
    if (menu_object_display(g) & SCREEN_OFF) {
        set_menu_object_line_effects(g, (uint8_t)(menu_object_line_effects(g) & ~WAVE_EFFECT));
        return false;
    }
    if (menu_objects_frozen(g) & OBJECTS_HELD)
        return false;
    if (menu_object_slots_step(g, self) == 0) {
        set_menu_object_slots_step(g, self, 1);
        set_menu_object_slots_phase(g, self, 0);
    }
    set_up_line_effect(g, pf);
    uint16_t scroll = menu_object_shadows_bg3_scroll_x(g);
    for (unsigned line = 0; line < MENU_OBJECT_WAVE_SCROLL_COUNT; line += 2)
        set_menu_object_wave_scroll(g, line, scroll);
    uint16_t phase = menu_wave_phase(g);
    set_menu_wave_phase(g, (uint16_t)(phase + WAVE_SPEED));
    for (unsigned line = 1; line < MENU_OBJECT_WAVE_SCROLL_COUNT; line += 2)
        set_menu_object_wave_scroll(g, line, (uint16_t)(scroll + sine(g, phase++)));
    ORIGINAL_EFFECT(set_menu_object_scratch_wave_phase(g, phase));
    return true;
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
