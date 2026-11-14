/* Battle menu input, with cursor clicks sent through the audio service. */
#include "battle/menu_bridge.h"

#include "battle/program.h"
#include "battle/window.h"
#include "battle/setup_data.h"
#include "port/vars_ram.h"
#include "vars.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "platform/audio.h"

/* ---- The click routine ($C1:1B55) --------------------------------------------------------------- */

/* The click: the driver's effect 0 on its second pair of effect voices, from
 * the middle. */
enum { CURSOR_CLICK_EFFECT = 0x00, CLICK_CALL = 0xC11B62 };

/* ---- The menu's frame, with its clicks --------------------------------------------------------- */

/* The click played: the driver's command, from the click routine's call. */
typedef struct {
    MenuHost host;
    Platform *pf;
} MenuClickContext;

static void click_the_menu(MenuHost *host, MenuClick click)
{
    (void)click;
    MenuClickContext *c = (MenuClickContext *)host;
    au_command(c->pf, PF_CALL(CLICK_CALL, 0xC70004),
               (AudioCommand){ .command = AU_START_OTHER_EFFECT, .p1 = CURSOR_CLICK_EFFECT, .p2 = AU_CENTRE, .count = 2 });
}

/* Refused unless the menu window's state and the menu's frame are as the
 * game keeps them. */
bool battle_menu_input_taken(GameState *g, Platform *pf)
{
    if (!PORT_CHECK(battle_menu_window_fits(g)) || !PORT_CHECK(battle_menu_frame_fits(g)))
        return battle_contract_broken(g, 0xC11153, "a menu state the menu's frame doesn't take");
    MenuClickContext c = { { click_the_menu }, pf };
    battle_menu_frame(g, battle_menu_buttons(g), &c.host);
    return true;
}
