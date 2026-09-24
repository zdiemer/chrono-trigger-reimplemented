/* The sprite-object mode's objects.
 *
 * Bank C2's second mode ($C2:0000) runs the Epoch's screens, the End of
 * Time's gate and the menus' overworld scenes: a map of three planes
 * (menu/map.c) behind up to 64 objects. An object is a slot of state and a
 * handler that runs it once a frame (menu/objects.c). Most objects run a
 * command script (menu/script.c, menu/object_script.c): it moves them, starts
 * other objects, scrolls the map and changes the screen around them. The
 * Epoch's screens have handlers of their own (menu/epoch.c).
 *
 * An object has a place on the map and a velocity, in 16.16 fixed point
 * (MENU_POSITION_UNITS a pixel). Its sprites are its sprite script's
 * (menu/sprites.c): each frame the script shows one of its sprite groups at
 * the object's place, with the object's look -- its palette and priority,
 * whether it is drawn in front of the others, whether it moves with the map's
 * scroll.
 *
 * A new object is started in the first free slot and takes on a copy of the
 * state of the object that starts it. Four slots at the start are kept for
 * the objects a screen wants run before the rest.
 *
 * The original names a slot by its address, which it keeps in
 * menu_object_current while the slot's handler runs; this header names it by
 * a MenuObject, and the address is the storage functions' alone. */
#ifndef CT_GAME_MENU_OBJECT_H
#define CT_GAME_MENU_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

typedef uint8_t MenuObject;
enum { MENU_OBJECTS = 64, MENU_FIRST_OBJECTS = 4 };

/* The object whose handler is running. */
MenuObject menu_object_running(const GameState *g);
void menu_object_make_running(GameState *g, MenuObject o);

/* The original's name for a slot: its address in bank $00/$7E. */
uint16_t menu_object_address(MenuObject o);
bool menu_object_address_names_one(uint16_t address);
/* Whether a bus address is in the objects' slots (in work RAM, or low RAM
 * through a bank that reaches it). */
bool menu_objects_hold(uint32_t address);
MenuObject menu_object_at_address(uint16_t address);

/* A byte of an object's state by its offset from the slot's start, as the
 * scripts name them. An offset past the slot's 64 reaches on into the slots
 * after it, and past the last slot into what follows it in work RAM; no
 * script names one, but nothing stops a script from doing it. */
uint8_t menu_object_byte(const GameState *g, MenuObject o, uint16_t at);
void menu_set_object_byte(GameState *g, MenuObject o, uint16_t at, uint8_t v);

/* ---- How it looks ------------------------------------------------------------------------ */

/* An object's look, the byte its sprites are drawn with (menu/sprites.c):
 * whether it is drawn in front of the objects that aren't, its palette and
 * priority (the sprites' attribute bits, as they sit there), whether it holds
 * the frame its sprite script shows rather than counting the frame's time
 * down, and whether it is placed on the map, moving with its scroll, rather
 * than on the screen. */
enum {
    MENU_LOOK_IN_FRONT = 0x01,
    MENU_LOOK_PALETTE = 0x0E,
    MENU_LOOK_PRIORITY = 0x30,
    MENU_LOOK_HELD = 0x40,
    MENU_LOOK_ON_THE_MAP = 0x80,
    /* The sprites' attribute bits the look gives them. */
    MENU_LOOK_ATTRIBUTES = MENU_LOOK_PALETTE | MENU_LOOK_PRIORITY,
};

/* ---- Starting objects -------------------------------------------------------------------- */

/* Where a new object goes: the ordinary slots, or the four a screen runs
 * first. */
typedef enum { MENU_ORDINARY_SLOT, MENU_FIRST_SLOT } MenuSlotKind;

/* Start an object running `handler`, in the first free slot of the kind,
 * with a copy of the running object's state. False where every slot is
 * busy. */
bool menu_start_object(GameState *g, uint16_t handler, MenuSlotKind kind, MenuObject *started);

/* Start an object on a command script (a 24-bit address), and say which
 * object has it. Where every slot is busy, the original still gives the
 * script to the last slot it looked at, over whatever that object was
 * running; so does this. */
MenuObject menu_start_script(GameState *g, uint32_t script, MenuSlotKind kind);

/* The slot an object would start in: the first free one of the kind, or
 * MENU_OBJECTS where there is none. */
unsigned menu_first_free_object(const GameState *g, MenuSlotKind kind);
unsigned menu_last_object_of(MenuSlotKind kind);

/* The handler that runs a command script ($C2:0F63). */
enum { MENU_SCRIPT_HANDLER = 0x0F63 };

/* ---- Where it is and how it moves -------------------------------------------------------- */

/* Positions and velocities are 16.16 fixed point. The map an object moves on
 * is MENU_MAP_WIDTH pixels round and MENU_MAP_HEIGHT pixels high. */
enum { MENU_POSITION_UNITS = 0x10000 };
enum { MENU_MAP_WIDTH = 0x600, MENU_MAP_HEIGHT = 0x400 };

typedef struct {
    uint32_t x, y;
} MenuPlace;

MenuPlace menu_object_place(const GameState *g, MenuObject o);
void menu_object_set_place(GameState *g, MenuObject o, MenuPlace p);
MenuPlace menu_object_velocity(const GameState *g, MenuObject o);
void menu_object_set_velocity(GameState *g, MenuObject o, MenuPlace v);

/* The pixel part of a place. */
static inline uint16_t menu_pixel(uint32_t at) { return (uint16_t)(at >> 16); }

typedef enum { MENU_ACROSS, MENU_DOWN } MenuAxis;

/* Turn the object's velocity round across or down. */
void menu_object_turn_round(GameState *g, MenuObject o, MenuAxis axis);

/* Move the object by its velocity (a frame's move). */
void menu_object_move(GameState *g, MenuObject o);

/* Bring an object that has moved off the map back onto it: round the map
 * across, and inside its height down. */
void menu_object_wrap_onto_map(GameState *g, MenuObject o);

#endif
