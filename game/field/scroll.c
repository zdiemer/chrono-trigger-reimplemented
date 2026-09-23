/* Field: layers that scroll on their own.
 *
 * A map can set BG2 or BG3 scrolling by itself (field_auto_scroll bits 1 and
 * 2), each axis at a signed speed in 1/16 pixel a frame. The speed adds into
 * an accumulator every frame; its whole pixels move the layer's 16-bit scroll
 * position, which is copied to the layer's scroll values, and the fraction
 * stays, sign-extended so that a negative accumulator keeps counting down. */
#include "field/field.h"

#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

enum { SCROLL_BG2 = 0x02, SCROLL_BG3 = 0x04 };   /* field_auto_scroll */
enum { LAYER_BG2, LAYER_BG3 };
enum { AXIS_X, AXIS_Y };

/* A 4.4 accumulator's fraction, sign-extended to the byte. */
static uint8_t fraction_of(uint8_t accumulator)
{
    if ((accumulator & 0x0F) == 0)
        return 0;
    return (accumulator & 0x80) ? (uint8_t)(accumulator | 0xF0) : (uint8_t)(accumulator & 0x0F);
}

static uint8_t accumulator(const GameState *g, unsigned layer, unsigned axis)
{
    return axis == AXIS_X ? field_auto_scroll_accumulator_x(g, layer) : field_auto_scroll_accumulator_y(g, layer);
}

static void set_accumulator(GameState *g, unsigned layer, unsigned axis, uint8_t v)
{
    if (axis == AXIS_X)
        set_field_auto_scroll_accumulator_x(g, layer, v);
    else
        set_field_auto_scroll_accumulator_y(g, layer, v);
}

/* For the original's code and shadow mode (field/scratch.h): a move back
 * keeps its pixel count at $D9 for its subtraction. */
static void leave_pixels_back(GameState *g, uint8_t pixels) { original_cell16(g, 0xD9, pixels); }

/* The layer's position on the axis moves, and its scroll value follows. */
static void move_position(GameState *g, unsigned layer, unsigned axis, int16_t by)
{
    if (axis == AXIS_X) {
        uint16_t at = (uint16_t)(field_auto_scroll_position_x(g, layer) + by);
        set_field_auto_scroll_position_x(g, layer, at);
        set_field_layer_scroll_values_x(g, layer, at);
    } else {
        uint16_t at = (uint16_t)(field_auto_scroll_position_y(g, layer) + by);
        set_field_auto_scroll_position_y(g, layer, at);
        set_field_layer_scroll_values_y(g, layer, at);
    }
}

/* One axis: the accumulator's whole pixels move the layer. */
static void scroll_axis(GameState *g, unsigned layer, unsigned axis)
{
    uint8_t acc = accumulator(g, layer, axis);
    if (!(acc & 0x80)) {
        move_position(g, layer, axis, (int16_t)(acc >> 4));
    } else {
        uint8_t back = (uint8_t)((uint8_t)-acc >> 4);
        leave_pixels_back(g, back);
        move_position(g, layer, axis, (int16_t)-back);
    }
    set_accumulator(g, layer, axis, fraction_of(accumulator(g, layer, axis)));
}

/* The speeds add into the accumulators, 8 bits each, wrapping. */
static void accumulate(GameState *g, unsigned layer)
{
    set_field_auto_scroll_accumulator_x(
        g, layer, (uint8_t)(field_auto_scroll_speed_x(g, layer) + field_auto_scroll_accumulator_x(g, layer)));
    set_field_auto_scroll_accumulator_y(
        g, layer, (uint8_t)(field_auto_scroll_speed_y(g, layer) + field_auto_scroll_accumulator_y(g, layer)));
}

// $C0:2C41
/* BG2's horizontal accumulator does nothing at all while it is 0 (the other
 * three axes write their unchanged position back). */
static void scroll_layers(GameState *g)
{
    if (field_auto_scroll(g) & SCROLL_BG2) {
        accumulate(g, LAYER_BG2);
        if (field_auto_scroll_accumulator_x(g, LAYER_BG2) != 0)
            scroll_axis(g, LAYER_BG2, AXIS_X);
        scroll_axis(g, LAYER_BG2, AXIS_Y);
    }
    if (field_auto_scroll(g) & SCROLL_BG3) {
        accumulate(g, LAYER_BG3);
        scroll_axis(g, LAYER_BG3, AXIS_X);
        scroll_axis(g, LAYER_BG3, AXIS_Y);
    }
}

/* It sets its own direct page and data bank, and restores the caller's;
 * nothing it leaves in the registers is read after (the entry's don't-care
 * set). */
void field_scroll_layers(Snes *s)
{
    scroll_layers(state_of(s));
    m_rtl(s);
}
