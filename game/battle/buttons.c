/* Battle: the buttons the battle's menus take.
 *
 * Each frame the frame work turns the controller's held buttons into the
 * menus' buttons: those newly pressed, and, while buttons stay held, all of
 * them again once a delay runs out (a quarter second at first, then every
 * frame, so a held direction scrolls a list). Pressing A makes the next
 * repeat wait longer, so a confirmation isn't taken twice. The menus take the
 * buttons from battle_menu_buttons, where they gather until then. */
#include "battle/bridge.h"
#include "battle/entries.h"
#include "platform/input.h"
#include "vars.h"

enum { FIRST_REPEAT_DELAY = 15, REPEAT_EVERY_FRAME = 1, AFTER_A_DELAY = 24 };

// $CD:0340
static void take_buttons(GameState *g)
{
    uint16_t held_before = battle_buttons_held(g);
    uint16_t held = battle_joypad(g);
    set_battle_buttons_held(g, held);
    uint16_t buttons = 0;
    if (held == 0 && held_before == 0) {
        set_battle_repeat_delay(g, FIRST_REPEAT_DELAY);
    } else {
        uint16_t repeat_delay = battle_repeat_delay(g);
        bool still_held = (held_before & held) != 0;
        if (still_held)
            set_battle_repeat_delay(g, --repeat_delay);
        if (still_held && repeat_delay == 0) {
            set_battle_repeat_delay(g, REPEAT_EVERY_FRAME);
            buttons = held;
        } else {
            buttons = held & (uint16_t)~held_before;
        }
    }
    set_battle_repeat_buttons(g, buttons);
    set_battle_menu_buttons(g, battle_menu_buttons(g) | buttons);
    if (buttons & IN_BUTTON_A)
        set_battle_repeat_delay(g, (uint16_t)((battle_repeat_delay(g) & 0xFF00) | AFTER_A_DELAY));
}

void battle_update_button_repeat(Snes *s)
{
    take_buttons(state_of(s));
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}
