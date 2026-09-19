/* Battle: the timed effect slots and the arithmetic they share with other
 * battle code (battle/effect_paths.c). */
#ifndef CT_GAME_BATTLE_EFFECTS_H
#define CT_GAME_BATTLE_EFFECTS_H

#include "port/machine.h"
#include "snes/system.h"

/* $CF:F957: are the points in the effect cells ($CC, $CD and $CE, $CF) less
 * than $D1 apart on both axes (also left in $D0)? */
bool battle_points_close(Snes *s);
/* $CF:FA17: the multiplier's product of $00 and the word at $02, 24-bit into
 * $04-$06; returns its middle byte. */
uint8_t battle_multiply_scaled(Snes *s, uint8_t factor, uint16_t v);
/* $CF:F9FB: the sine of `angle` (256 steps a turn; A with its high byte, as
 * the original shifts it) scaled by the radius at $00: the product's middle
 * byte, a signed offset. */
uint8_t battle_sine_scaled(Snes *s, uint16_t angle);

/* The guard of the effect entries: the battle page and data bank $7E. */
bool battle_effect_page(Snes *s);
/* And for the handlers on their own: A's high byte 0 as well. */
bool battle_effect_handler_ready(Snes *s);

/* ROM data the effects read (the asset seam): the sine table at $C0:F900,
 * whose low byte of each fourth word holds the magnitude for a step; the
 * arctangent table at $C0:F300 by the distances in eighths; the facing for
 * each angle at $C0:F700; and the two handler tables. */
static inline uint8_t battle_sine_entry(Snes *s, uint16_t at) { return m_read8(s, 0xC0F900u + at); }
static inline uint8_t battle_arctangent(Snes *s, uint16_t index) { return m_read8(s, 0xC0F300u + index); }
static inline uint8_t battle_facing_for_angle(Snes *s, uint8_t angle) { return m_read8(s, 0xC0F700u + angle); }
static inline uint16_t battle_effect_handler(Snes *s, uint16_t table, uint8_t type_x2)
{
    return m_read16(s, 0xCF0000u | (uint16_t)(table + type_x2));
}

void battle_step_effect_slots(Snes *s);
void battle_step_orbits(Snes *s);
void battle_place_formation(Snes *s);
void battle_points_are_close(Snes *s);
void battle_sine_scale(Snes *s);
void battle_multiply_scale(Snes *s);
void battle_chase_angle(Snes *s);
void battle_effect_combatant_line(Snes *s);
void battle_effect_combatant_jump(Snes *s);
void battle_effect_combatant_spiral(Snes *s);
void battle_effect_combatant_orbit(Snes *s);
void battle_effect_combatant_jump_to_target(Snes *s);
void battle_effect_combatant_orbit_flat(Snes *s);
void battle_effect_flicker(Snes *s);
void battle_effect_flicker_step(Snes *s);
void battle_effect_combatant_chase(Snes *s);
void battle_effect_anchor_line(Snes *s);
void battle_effect_anchor_jump(Snes *s);
void battle_effect_anchor_spiral(Snes *s);
void battle_effect_anchor_orbit(Snes *s);
void battle_effect_anchor_jump_to_target(Snes *s);
void battle_effect_anchor_orbit_facing(Snes *s);
void battle_effect_anchor_chase(Snes *s);

#endif
