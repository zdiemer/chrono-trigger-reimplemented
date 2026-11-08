/* The still-gated map menus keep their wave phase in two bytes also used
 * by field/event scratch. Preserve only those writes until the menus retire.
 * Every caller uses ORIGINAL_EFFECT, so native-shape play already proves the
 * field and events do not need this compatibility state. */
#ifndef CT_GAME_FIELD_MENU_COMPAT_H
#define CT_GAME_FIELD_MENU_COMPAT_H
#include "original_effects.h"
#include "vars.h"
static inline void field_leaves_menu_wave_low(GameState *g, uint8_t low)
{
    set_event_scratch_byte(g, 9, low);
}
static inline void field_leaves_menu_wave_high(GameState *g, uint8_t high)
{
    set_event_scratch_byte(g, 10, high);
}
#endif
