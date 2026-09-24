/* Field mode: walking around maps. Interrupt handlers, frame sync, objects.
 * The event script engine is in event/. */
#ifndef CT_GAME_FIELD_H
#define CT_GAME_FIELD_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* field_display_flags, which the line effects' builders, the vblank handler
 * and the line interrupt share. */
enum {
    /* The line effects read table set 1 (the builders build the other set and
     * flip this every frame); the line interrupt's late work alternates with
     * it, one job each frame (field/display.c). */
    FIELD_SECOND_TABLE_SET = 0x01,
    /* What the vblank tests to program set 1: any bit of the low nibble. The
     * game only ever keeps bit 0 there. */
    FIELD_TABLE_SET_BITS = 0x0F,
    /* The field's setup builds both sets from its own scroll and color math
     * values, and nothing waves (field/line_effects.c). */
    FIELD_DISPLAY_SETUP = 0x80,
};

/* field_requests: what the event scripts ask of the field's main loop. A
 * battle is asked for with a tile copy: the main loop copies the queued
 * tiles (as the last queued copy described them) along with it. */
enum {
    FIELD_REQUEST_BATTLE = 0x01,
    FIELD_REQUEST_TILE_COPY = 0x20,         /* copy the queued tiles into the maps */
    FIELD_REQUEST_LOCATION_CHANGE = 0x80,   /* go where location_next says */
};

/* field_effect_status: what the field has running, or has finished, that a
 * script waits on. */
enum {
    FIELD_BATTLE_OVER = 0x01,
    FIELD_FADING = 0x02,                    /* the brightness moves to its target */
    FIELD_TINTING = 0x08,                   /* the tint moves to its target */
    FIELD_TINT_CHANGES = 0x18,              /* what a tint with a rate sets running */
};

/* The field's three layers, as the tables of layers index them. */
enum { FIELD_BG1, FIELD_BG2, FIELD_BG3, FIELD_LAYERS };

/* The screen's two axes, as the tables of scrolls index them. */
typedef enum { AXIS_X, AXIS_Y, AXES } Axis;

/* The part of a 4.4 fixed-point scroll accumulator below a pixel, kept with
 * the accumulator's sign so that a negative one goes on counting down. */
static inline uint8_t field_scroll_fraction(int8_t accumulator)
{
    if ((accumulator & 0x0F) == 0)
        return 0;
    return accumulator < 0 ? (uint8_t)(accumulator | 0xF0) : (uint8_t)(accumulator & 0x0F);
}

/* A screen coordinate as the original keeps it, for an object and for a
 * sprite: nine bits, the ninth set where it is negative. The game's screen,
 * 256 pixels, reads it as a signed number of nine bits. */
enum { SCREEN_NINE_BITS = 0x1FF, SCREEN_NEGATIVE = 0x100 };

static inline int16_t screen_coordinate_of(uint16_t stored)
{
    uint16_t nine = stored & SCREEN_NINE_BITS;
    return (int16_t)((nine & SCREEN_NEGATIVE) ? (int16_t)nine - 2 * SCREEN_NEGATIVE : (int16_t)nine);
}
static inline uint16_t stored_screen_coordinate(int16_t v)
{
    return (uint16_t)(((uint16_t)v & SCREEN_NINE_BITS) | (v < 0 ? SCREEN_NEGATIVE : 0));
}

/* A layer's tile map size, beside its base in the line effects' values: two
 * screens wide. */
enum { FIELD_MAP_TWO_SCREENS_WIDE = 0x01 };

/* The field's random numbers: a fixed table of 256 bytes stepped through, a
 * step a number (field/motion.c). */
uint8_t field_draw_random(GameState *g);

/* field/interrupts.c */
void field_install_nmi(Snes *s);
void field_install_irq(Snes *s);

/* field/nmi.c */
void field_nmi(Snes *s);

/* field/frame.c */
void field_wait_for_frame_loop(Snes *s);
void field_change_location_wait_for_vblank_loop(Snes *s);
void field_restart_wait_for_vblank_loop(Snes *s);

/* field/motion.c */
void field_direction_to_target(Snes *s);
void field_set_velocity(Snes *s);
void field_start_step(Snes *s);
void field_object_in_the_way(Snes *s);
void field_line_up_with_object(Snes *s);
void field_line_up_with_tile(Snes *s);
void field_move_object(Snes *s);
void field_move_object_unbounded(Snes *s);
bool field_heads_current_object(Snes *s);
bool field_object_checks_its_way(Snes *s);
bool field_lines_up_with_an_object(Snes *s);

/* field/animation.c */
void field_update_walk_animation(Snes *s);
void field_stand_animation(Snes *s);
void field_tick_object_animation(Snes *s);
void field_read_animation_frame(Snes *s);
bool field_object_delays_in_bank(Snes *s);
bool field_object_frames_in_bank(Snes *s);

/* field/object_view.c */
void field_update_object_visibility(Snes *s);
void field_update_object_in_window(Snes *s);

/* field/window_shape.c */
void field_rasterize_quad(Snes *s);
bool field_window_shape_table_in_place(Snes *s);

/* field/scroll.c */
void field_scroll_layers(Snes *s);

#endif
