/* Battle: how the combatants animate, and the sprite graphics their frames
 * are built into.
 *
 * Every object on the battlefield shows a frame of an animation at a time;
 * a frame is a set of 16x16 sprite parts whose tiles are built into the
 * object's tile buffer in work RAM, from which the frame's graphics are
 * uploaded (battle/sprites.c). The combatants' objects step through their
 * animations a frame's time at a time (battle/animation.c): the party
 * members' animation follows from their pose, which their statuses decide,
 * and the enemies' from the animation their scripts give them.
 *
 * The combatants are objects 12-22 of the battle's objects, the battle slot
 * after 12 (battle_object_x); the objects' other tables go by battle slot. */
#ifndef CT_GAME_BATTLE_ANIMATION_H
#define CT_GAME_BATTLE_ANIMATION_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

/* The object that draws the combatant in a battle slot. */
enum { BATTLE_COMBATANT_FIRST_OBJECT = 12 };
static inline unsigned battle_combatant_object(unsigned slot) { return BATTLE_COMBATANT_FIRST_OBJECT + slot; }

/* ---- Screen bounds (battle/objects.c) ----------------------------------------------------- */

/* Work out a combatant's box on the screen around the position its bounds
 * were last given (battle_bounds_x, battle_bounds_y). */
void battle_update_bounds(GameState *g, unsigned slot);

/* ---- Poses (battle/animation.c) --------------------------------------------------------- */

/* The animations the pose code names: a party member's stance in its field
 * clothes (taking no part in the fight), its battle stance, its weak pose,
 * and the party turning to run while L and R are held. */
enum {
    ANIMATION_FIELD_STANCE = 0x00,
    ANIMATION_BATTLE_STANCE = 0x03,
    ANIMATION_WEAK = 0x12,
    ANIMATION_RUNNING = 0x2B,
};

/* Choose the pose the combatant's statuses give it (knocked out, asleep, ...;
 * its weak pose at low HP; none) and return the animation that goes with it;
 * a new pose starts its frame list over. */
uint8_t combatant_choose_pose(GameState *g, unsigned slot);

/* ---- Sprite graphics (battle/sprites.c) --------------------------------------------------- */

/* Build the object's graphics for an animation frame into its tile buffer,
 * unless it already holds them. Building clears the object's held frame
 * (battle_anim_held) and counts toward the builds of this frame
 * (battle_graphics_built). */
void battle_build_frame_graphics(GameState *g, unsigned object, uint8_t frame);

/* Whether the object's graphics kind has its frames built this way (the
 * largest enemies' don't). */
bool battle_object_builds_graphics(const GameState *g, unsigned object);

/* Put a color set of three colors, or of seven, into the first sprite
 * palette, from color 1 of the quarter (0-7: a quarter past the palette's
 * four runs on into the next). */
void battle_put_color_set(GameState *g, bool seven, uint8_t set, uint8_t quarter);

/* The frame the object shows next, which the graphics upload takes. */
void battle_show_object_frame(GameState *g, unsigned object, uint8_t frame);

#endif
