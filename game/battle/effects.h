/* Battle: the effects a tech or an attack shows, the points they are shown
 * at, and what moves those points and the combatants while they play.
 *
 * - An effect is an animated group of sprites: a flash, a slash, a fireball.
 *   There are eight at a time. Each shows a sequence of frames, a sprite set
 *   drawn at an offset from the effect's anchor for a number of frames
 *   (battle/effect_animation.c). The eight records a combatant's object
 *   carries name the effects drawn with it, under its own sprites or over them
 *   (battle/draw_objects.c).
 * - An anchor is the point an effect is drawn at, one an effect. An anchor
 *   follows a combatant, or another anchor, or stays where it was put
 *   (battle/object_motion.c), and it can be sent along a path.
 * - A path moves a combatant or an anchor: a straight line, a jump, a spiral
 *   out, an orbit, a jump onto a target, a chase, or, for a combatant, a
 *   flicker of its sprite (battle/effect_paths.c). Each of the nineteen movers,
 *   the eleven combatants and the eight anchors, runs one path at a time, on
 *   a timer of its own.
 *
 * The animation scripts (battle/animation.c) start the effects and the paths
 * and set the anchors; this header is what the drawing and the motion share
 * of them. A combatant is its battle slot (battle.h). */
#ifndef CT_GAME_BATTLE_EFFECTS_H
#define CT_GAME_BATTLE_EFFECTS_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/battle.h"
#include "snes/system.h"
#include "state.h"
#include "vars.h"

enum { BATTLE_EFFECTS = 8, BATTLE_ANCHORS = 8 };

/* The battlefield's objects: the combatants' are the eleven from $0C on, in
 * battle slot order. */
enum { BATTLE_FIRST_COMBATANT_OBJECT = 0x0C };

static inline unsigned battle_object_of(unsigned slot) { return BATTLE_FIRST_COMBATANT_OBJECT + slot; }

/* Where a combatant stands. Positions on the battlefield are a byte each,
 * which wraps. */
static inline uint8_t combatant_x(const GameState *g, unsigned slot) { return battle_object_x(g, battle_object_of(slot)); }
static inline uint8_t combatant_y(const GameState *g, unsigned slot) { return battle_object_y(g, battle_object_of(slot)); }
static inline void place_combatant_x(GameState *g, unsigned slot, uint8_t x)
{
    set_battle_object_x(g, battle_object_of(slot), x);
}
static inline void place_combatant_y(GameState *g, unsigned slot, uint8_t y)
{
    set_battle_object_y(g, battle_object_of(slot), y);
}

/* How far a combatant's sprite is drawn from where it stands, signed: a
 * jump lifts it, an orbit or a flicker moves it about, and its trail records
 * it. */
static inline int8_t combatant_sprite_offset_x(const GameState *g, unsigned slot)
{
    return (int8_t)battle_sprite_offset_x(g, slot);
}
static inline int8_t combatant_sprite_offset_y(const GameState *g, unsigned slot)
{
    return (int8_t)battle_sprite_offset_y(g, slot);
}
static inline void set_combatant_sprite_offset(GameState *g, unsigned slot, int8_t x, int8_t y)
{
    set_battle_sprite_offset_x(g, slot, (uint8_t)x);
    set_battle_sprite_offset_y(g, slot, (uint8_t)y);
}
static inline void set_combatant_sprite_offset_y(GameState *g, unsigned slot, uint8_t y)
{
    set_battle_sprite_offset_y(g, slot, y);
}

/* The effects drawn with a combatant: its records, a row an effect and a
 * byte a combatant in each row. A record names an effect (its low three
 * bits) and the side of the combatant it is drawn on: in front of it, or
 * behind it; 0 is none. */
enum { BATTLE_RECORD_EFFECT = 0x07, BATTLE_RECORD_IN_FRONT = 0x80, BATTLE_RECORD_BEHIND = 0x40 };

static inline unsigned battle_record_of(unsigned slot, unsigned row) { return row * BATTLE_COMBATANTS + slot; }

/* An anchor's position is a word a coordinate: a place on the battlefield
 * whose high byte is 0, unless a chase took it off the edge. */
static inline int16_t anchor_x(const GameState *g, unsigned anchor) { return (int16_t)battle_anchor_x(g, anchor); }
static inline int16_t anchor_y(const GameState *g, unsigned anchor) { return (int16_t)battle_anchor_y(g, anchor); }
static inline void place_anchor(GameState *g, unsigned anchor, int16_t x, int16_t y)
{
    set_battle_anchor_x(g, anchor, (uint16_t)x);
    set_battle_anchor_y(g, anchor, (uint16_t)y);
}

/* ---- The guards (registry .guard) ------------------------------------------------------ */

/* On the battle's page with the data bank at $7E, as battle/bridge.h's
 * battle_on_page; other files' entries take it by this name too. */
bool battle_effect_page(Snes *s);
/* ... with A's high byte 0, as the frame's update calls the motion. */
bool battle_effect_handler_ready(Snes *s);
/* ... and every running mover on one of the eight kinds of path, as the game
 * starts them (battle/effect_paths.c). */
bool battle_effect_paths_ready(Snes *s);
/* ... and the effect loop's cells naming a combatant or an anchor, as the loop
 * sets them for a path's routine. */
bool battle_path_cells_name_combatant(Snes *s);
bool battle_path_cells_name_anchor(Snes *s);

#endif
