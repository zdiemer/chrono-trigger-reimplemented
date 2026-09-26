/* Field mode: walking around maps. Interrupt handlers, frame sync, objects.
 * The event script engine is in event/. */
#ifndef CT_GAME_FIELD_H
#define CT_GAME_FIELD_H

#include <stdbool.h>
#include <stdint.h>

#include "field/object.h"
#include "ram.h"
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
    FIELD_REQUEST_MENUS = 0x40,             /* open the menus a script asked for */
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
    FIELD_SCRIPT_MENU_SHOWN = 0x40,         /* the menus a script asked for (event $C8) were shown */
};

/* What the field has under way that scripts and the field's own routines
 * look at (field_mode_flags): the party gathering to its leader (event $DA:
 * event/party.c), an object being pushed until its push script is done
 * (event/contact.c), a dialog the field opened itself rather than a
 * script (event/dialog.c), and palettes a script loaded itself, which keeps
 * the objects out of the shared palette slots until the next map
 * (field/palettes.c). */
enum { FIELD_PARTY_GATHERING = 0x01, FIELD_OBJECT_PUSHED = 0x02, FIELD_DIALOG_OPENED_BY_THE_FIELD = 0x20,
       FIELD_PALETTES_SET_BY_SCRIPT = 0x80 };

static inline void field_mode_flag(GameState *g, uint8_t flag, bool on)
{
    uint8_t now = field_mode_flags(g);
    set_field_mode_flags(g, (uint8_t)(on ? now | flag : now & ~flag));
}
static inline bool field_party_gathering(const GameState *g) { return (field_mode_flags(g) & FIELD_PARTY_GATHERING) != 0; }
static inline void set_field_party_gathering(GameState *g, bool on) { field_mode_flag(g, FIELD_PARTY_GATHERING, on); }
static inline bool field_palettes_set_by_script(const GameState *g)
{
    return (field_mode_flags(g) & FIELD_PALETTES_SET_BY_SCRIPT) != 0;
}
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

/* Whether the player has the party (field_player_control): only then does
 * the pad walk the leader, the confirm button talk and open chests, and Start
 * pause the field. Scripts take it away for a scene and hand it back. */
static inline bool party_is_the_players(const GameState *g) { return field_player_control(g) != 0; }
static inline void hand_the_party_to_the_player(GameState *g, bool theirs) { set_field_player_control(g, theirs ? 1 : 0); }

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
static inline bool dialog_choice_waiting(const GameState *g) { return dialog_choice_active(g) != DIALOG_NO_CHOICES; }

/* dialog_window_position: where the dialog window opens, which the line
 * effects lay out (field/line_effects.c) and the Start assignment moves
 * (field/menus.c). */
typedef enum { WINDOW_NONE, WINDOW_AT_TOP, WINDOW_AT_BOTTOM, WINDOW_POSITIONS } WindowPosition;
static inline WindowPosition dialog_window_at(const GameState *g) { return (WindowPosition)dialog_window_position(g); }

/* Scripts can lock the pause and the menus (field_pause_locked,
 * field_menus_locked), through the event engine's memory writes. */
static inline bool field_pause_is_locked(const GameState *g) { return field_pause_locked(g) != 0; }
static inline bool field_menus_are_locked(const GameState *g) { return field_menus_locked(g) != 0; }

/* The story's progress the Start assignment's menu waits for (field/menus.c). */
enum { FIELD_START_MENU_FROM_STORY = 0x49 };

/* The field's geometry. A map tile is 16 pixels; a layer's tile map entry,
 * an 8-pixel tile, is a quarter of one. Each layer's tile map is 64 entries
 * across, in two halves of 32 side by side, and 32 down, round which the
 * layer's place wraps; the screen shows 32 entries across and 28 down. The
 * lines that stream in as the screen scrolls reach a map tile past the
 * screen: a row from a tile left of it to a tile right of it, a column from a
 * tile above it (field/streaming.c). */
enum {
    FIELD_MAP_TILE_PIXELS = 16,
    FIELD_ENTRY_PIXELS = 8,
    FIELD_TILE_MAP_COLUMNS = 64,
    FIELD_TILE_MAP_HALF_COLUMNS = 32,
    FIELD_TILE_MAP_ROWS = 32,
    FIELD_SCREEN_COLUMNS = 32,
    FIELD_SCREEN_ROWS = 28,
    FIELD_ENTRIES_A_MAP_TILE = FIELD_MAP_TILE_PIXELS / FIELD_ENTRY_PIXELS,
    FIELD_ROW_MAP_TILES = FIELD_SCREEN_COLUMNS / FIELD_ENTRIES_A_MAP_TILE + 2,
    FIELD_COLUMN_MAP_TILES = FIELD_SCREEN_ROWS / FIELD_ENTRIES_A_MAP_TILE + 1,
};

/* The frame builder's areas in bank $7F (field_frame_staging), where an
 * object's new frame is drawn before it goes to VRAM (field/sprites.c). */
enum { FIELD_FRAME_AREAS = RAM_FIELD_FRAME_STAGING_COUNT };
static inline uint16_t field_frame_area_at(unsigned area)
{
    return (uint16_t)(RAM_ABS(RAM_FIELD_FRAME_STAGING) + area * RAM_FIELD_FRAME_STAGING_STRIDE);
}

/* Where a layer's streamed lines are staged for VRAM (field_layer_staging):
 * a row from its buffer's start, a column from $80 in; the deferred uploads
 * send them from there (field/display.c). */
enum { FIELD_STAGED_ROW = 0x00, FIELD_STAGED_COLUMN = 0x80 };
static inline uint16_t field_staged_line(unsigned layer, bool column)
{
    return (uint16_t)(RAM_ABS(RAM_FIELD_LAYER_STAGING) + layer * RAM_FIELD_LAYER_STAGING_STRIDE +
                      (column ? FIELD_STAGED_COLUMN : FIELD_STAGED_ROW));
}

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

/* field/interrupts.c: the interrupt stubs pointed at the field's vblank
 * handler and its line interrupt. */
void field_take_the_vblank_interrupt(GameState *g);
void field_take_the_line_interrupt(GameState *g);
enum { FIELD_INSTALL_NMI_ENTRY = 0xC00B64, FIELD_INSTALL_IRQ_ENTRY = 0xC00B75 };
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
