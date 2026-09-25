/* Field mode: walking around maps. Interrupt handlers, frame sync, objects.
 * The event script engine is in event/. */
#ifndef CT_GAME_FIELD_H
#define CT_GAME_FIELD_H

#include <stdbool.h>
#include <stdint.h>

#include "field/object.h"
#include "snes/system.h"
#include "state.h"
#include "vars.h"

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

/* field_requests: what the event scripts and the leader's walk ask of the
 * field's main loop. A battle is asked for with a tile copy: the main loop
 * copies the queued tiles (as the last queued copy described them) along with
 * it. */
enum {
    FIELD_REQUEST_BATTLE = 0x01,
    FIELD_REQUEST_OPEN_CHEST = 0x02,        /* show the chest at field_opened_chest_tile open */
    FIELD_REQUEST_STEP_EVENT = 0x10,        /* run the step event at field_step_tile */
    FIELD_REQUEST_TILE_COPY = 0x20,         /* copy the queued tiles into the maps */
    FIELD_REQUEST_LOCATION_CHANGE = 0x80,   /* go where location_next says */
};

static inline void field_request(GameState *g, uint8_t request)
{
    set_field_requests(g, (uint8_t)(field_requests(g) | request));
}
static inline bool field_requested(const GameState *g, uint8_t request) { return (field_requests(g) & request) != 0; }

/* A location, as scripts and the map's exits give one: the map in the low 9
 * bits of a word whose high byte says how the change is made, and the tile the
 * party stands on there. The game keeps the how in a byte of its own: the high
 * byte's bits 1-4 shifted down, and its bit 7. */
typedef struct {
    uint16_t map;
    uint8_t x, y;
    uint8_t how;
} Location;
enum { LOCATION_MAP_BITS = 0x01FF };

static inline Location location_from(uint16_t map_and_how, uint8_t x, uint8_t y)
{
    uint8_t high = (uint8_t)(map_and_how >> 8);
    return (Location){ (uint16_t)(map_and_how & LOCATION_MAP_BITS), x, y,
                       (uint8_t)(((high >> 1) & 0x0F) | (high & 0x80)) };
}

/* Where the party goes next, which the main loop takes it to once asked. */
static inline void field_set_next_location(GameState *g, Location l)
{
    set_location_next_id(g, l.map);
    set_location_next_x(g, l.x);
    set_location_next_y(g, l.y);
    set_location_next_flags(g, l.how);
}

/* field_effect_status: what the field has running, or has finished, that a
 * script waits on. */
enum {
    FIELD_BATTLE_OVER = 0x01,
    FIELD_FADING = 0x02,                    /* the brightness moves to its target */
    FIELD_TINTING = 0x08,                   /* the tint moves to its target */
    FIELD_TINT_CHANGES = 0x18,              /* what a tint with a rate sets running */
};

/* What the field has under way that scripts and the field's own routines
 * look at (field_mode_flags): the party gathering to its leader (event $DA:
 * event/party.c), an object being pushed until its push script is done
 * (event/contact.c), and a dialog the field opened itself rather than a
 * script (event/dialog.c). */
enum { FIELD_PARTY_GATHERING = 0x01, FIELD_OBJECT_PUSHED = 0x02, FIELD_DIALOG_OPENED_BY_THE_FIELD = 0x20 };

static inline void field_mode_flag(GameState *g, uint8_t flag, bool on)
{
    uint8_t now = field_mode_flags(g);
    set_field_mode_flags(g, (uint8_t)(on ? now | flag : now & ~flag));
}
static inline bool field_party_gathering(const GameState *g) { return (field_mode_flags(g) & FIELD_PARTY_GATHERING) != 0; }
static inline void set_field_party_gathering(GameState *g, bool on) { field_mode_flag(g, FIELD_PARTY_GATHERING, on); }
static inline bool field_object_being_pushed(const GameState *g) { return (field_mode_flags(g) & FIELD_OBJECT_PUSHED) != 0; }
static inline void mark_field_object_pushed(GameState *g) { field_mode_flag(g, FIELD_OBJECT_PUSHED, true); }
static inline void forget_dialog_opened_by_the_field(GameState *g)
{
    field_mode_flag(g, FIELD_DIALOG_OPENED_BY_THE_FIELD, false);
}
static inline void mark_dialog_opened_by_the_field(GameState *g)
{
    field_mode_flag(g, FIELD_DIALOG_OPENED_BY_THE_FIELD, true);
}

/* The object the leader is up against (event/contact.c): the one its walk ran
 * into this frame, or the one before it when the player presses the confirm
 * button; or none. The game keeps the link in a word, whose high byte a
 * contact found clears and the walk's start, forgetting the last, leaves. */
static inline FieldLink leader_contact(const GameState *g) { return field_link_stored(field_leader_contact(g)); }
static inline void set_leader_contact(GameState *g, FieldLink object)
{
    set_field_leader_contact(g, field_link_byte(object));
    set_field_leader_contact_high(g, 0);
}
static inline void leader_loses_contact(GameState *g) { set_field_leader_contact(g, field_link_byte(field_no_link())); }

/* dialog_choice_active: where a dialog with choices is (event/dialog.c):
 * none open, its text still showing, the player picking an answer, or one
 * picked. */
enum { DIALOG_NO_CHOICES = 0, DIALOG_CHOICES_SHOWING = 1, DIALOG_CHOOSING = 2, DIALOG_CHOSEN = 3 };

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
