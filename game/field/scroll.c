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
enum { AUTO_BG2, AUTO_BG3 };   /* the auto-scroll tables have BG2 and BG3 only */
enum { AXIS_X, AXIS_Y };

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

/* A 4.4 accumulator's whole pixels, rounded toward zero. */
static int16_t whole_pixels(int8_t accumulated)
{
    return accumulated < 0 ? (int16_t)-(int16_t)(-accumulated >> 4) : (int16_t)(accumulated >> 4);
}

/* One axis: the accumulator's whole pixels move the layer, and its fraction
 * stays. Returns the accumulated speed it moved by. */
static int8_t scroll_axis(GameState *g, unsigned layer, unsigned axis)
{
    int8_t accumulated = (int8_t)accumulator(g, layer, axis);
    move_position(g, layer, axis, whole_pixels(accumulated));
    set_accumulator(g, layer, axis, field_scroll_fraction((int8_t)accumulator(g, layer, axis)));
    return accumulated;
}

/* The speeds add into the accumulators, 8 bits each, wrapping. */
static void accumulate(GameState *g, unsigned layer)
{
    set_field_auto_scroll_accumulator_x(
        g, layer, (uint8_t)(field_auto_scroll_speed_x(g, layer) + field_auto_scroll_accumulator_x(g, layer)));
    set_field_auto_scroll_accumulator_y(
        g, layer, (uint8_t)(field_auto_scroll_speed_y(g, layer) + field_auto_scroll_accumulator_y(g, layer)));
}

/* For the original's code and shadow mode (field/scratch.h): a move back
 * leaves its whole pixels at $D9 of the field's page, the last one's. */
static void leave_for_original(GameState *g, int8_t last_backward)
{
    if (last_backward < 0)
        original_cell16(g, 0xD9, (uint16_t)-whole_pixels(last_backward));
}

static int8_t backward(int8_t before, int8_t accumulated) { return accumulated < 0 ? accumulated : before; }

// $C0:2C41
/* Every frame, for a map that scrolls a layer by itself. BG2's horizontal
 * accumulator does nothing at all while it is 0 (the other axes write their
 * unchanged position back). */
static void scroll_layers(GameState *g)
{
    int8_t last_backward = 0;
    if (field_auto_scroll(g) & SCROLL_BG2) {
        accumulate(g, AUTO_BG2);
        if (field_auto_scroll_accumulator_x(g, AUTO_BG2) != 0)
            last_backward = backward(last_backward, scroll_axis(g, AUTO_BG2, AXIS_X));
        last_backward = backward(last_backward, scroll_axis(g, AUTO_BG2, AXIS_Y));
    }
    if (field_auto_scroll(g) & SCROLL_BG3) {
        accumulate(g, AUTO_BG3);
        last_backward = backward(last_backward, scroll_axis(g, AUTO_BG3, AXIS_X));
        last_backward = backward(last_backward, scroll_axis(g, AUTO_BG3, AXIS_Y));
    }
    leave_for_original(g, last_backward);
}

/* It sets its own direct page and data bank, and restores the caller's;
 * nothing it leaves in the registers is read after (the entry's don't-care
 * set). */
void field_scroll_layers(Snes *s)
{
    scroll_layers(state_of(s));
    m_rtl(s);
}
