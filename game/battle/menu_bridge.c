/* Battle menu input, with cursor clicks sent through the audio service. */
#include "battle/menu_bridge.h"

#include "battle/program.h"
#include "battle/window.h"
#include "battle/setup_data.h"
#include "vars.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "platform/audio.h"

/* ---- The click routine ($C1:1B55) --------------------------------------------------------------- */

/* The click: the driver's effect 0 on its second pair of effect voices, from
 * the middle. What the driver's command routine reads of the registers it is
 * called with, and what is read after it returns: nothing but A's high byte,
 * which it hands back (checked against liveness by the calling convention). */
enum { CURSOR_CLICK_EFFECT = 0x00, CLICK_CALL = 0xC11B62, CLICK_RETURN = 0xC11B66, CLICK = 0xC11B55 };

/* The menu frame's calls to the choosing of targets: for an attack, a tech
 * or an item just chosen, and for the choosing going on from an earlier
 * frame. Which one a frame makes is decided by where the menu stood as the
 * frame began (choosing_call_of). The choosing calls its kind's routine from
 * one place, and each kind that places an area on an enemy clicks from its
 * own two, as the enemy moves on or back. */
enum { CHOOSE_FOR_ATTACK = 0xC112B0, CHOOSE_FOR_TECH = 0xC11379, CHOOSE_FOR_ITEM = 0xC114C5, CHOOSE_GOING_ON = 0xC11561, NO_CHOOSING_CALL = 0, KIND_CALL = 0xC11FC9, BATTLE_JSR_RETURN = BATTLE_JSR_SIZE - 1 };

typedef struct {
    uint8_t kind;
    uint32_t on, back;
} KindClickSites;

/* The choosing of targets for the original's callers. It leaves A's high
 * byte clear and the carry of its last compare. */

/* The tech rows' marking for the original's callers: A's high byte clear,
 * the carry of the last entry's compare. */

/* ---- As C ------------------------------------------------------------------------------------ */

/* The click as C: the driver's command, from the click routine's call. */
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

/* The guard's state half: the menu window's state and the menu's frame as
 * the game keeps them. */
bool battle_menu_input_taken(GameState *g, Platform *pf)
{
    if (!battle_menu_window_fits(g) || !battle_menu_frame_fits(g))
        return battle_contract_broken(g, 0xC11153, "a menu state the menu's frame doesn't take");
    MenuClickContext c = { { click_the_menu }, pf };
    battle_menu_frame(g, battle_menu_buttons(g), &c.host);
    return true;
}
