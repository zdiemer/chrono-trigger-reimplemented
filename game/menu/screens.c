/* The menus' screens: what the menus' main task does every frame.
 *
 * The menus proper are a set of screens: the main menu and the screens its
 * options open (the equipment, the items, the techs, the settings, the
 * party's exchange and the save screen), the exchange the End of Time's gate
 * opens, a save point's save screen, the shops, the naming of a character and
 * the load screen. Each screen is a run of steps -- its opening, then each
 * choice it waits on in turn, a member, a slot, an item, how many -- and a
 * step does one frame's work and moves the screen on by naming the step, or
 * the screen, that runs next (menu_screen, menu_screen_step). Some steps wait
 * for frames of their own inside (a panel drawn over three, a slide over six).
 *
 * The menus' main task (menu/tasks.c) runs the screen's step once a frame
 * and then waits for the next frame, for as long as the menus are open; this
 * file is how it finds and runs the step. The steps are found through a C
 * table by screen; those that are hand ports run as C where the runtime would run them
 * for the original's call (their guards hold), and the original's routine
 * runs, called through the machine, where a step's guard turns the state
 * away. */
#include "menu/menu.h"

#include "charge.h"
#include "core/program.h"
#include "menu/bridge.h"
#include "menu/equipment.h"
#include "menu/exchange.h"
#include "menu/item_screen.h"
#include "menu/load_screen.h"
#include "menu/main_menu.h"
#include "menu/menu_data.h"
#include "menu/naming.h"
#include "menu/saving.h"
#include "menu/settings_screen.h"
#include "menu/shop.h"
#include "menu/tech_screen.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* A step: the original's routine for it, in the menus' bank, its hand
 * port's machine entry, and the port as C where the program has it (from
 * inside the main task; false where it went no further). */
typedef struct {
    uint16_t routine;
    void (*port)(Snes *);
    bool (*as_c)(GameState *g, Platform *pf);
} Step;

/* Each screen's steps, in the order the screen names them. */
static const Step main_menu_steps[] = {
    { 0x99CE, menu_open_main_menu_entry, menu_open_main_menu },
    { 0x9A1B, menu_choose_on_main_menu_entry, menu_choose_on_main_menu },
};
static const Step equipment_steps[] = {
    { 0x9C31, menu_open_equipment_entry, menu_open_equipment },
    { 0x9C39, menu_choose_equipment_member_entry, menu_choose_equipment_member },
    { 0x9C70, menu_choose_equipment_slot, NULL }, { 0x9CA5, menu_choose_equipment_item, NULL },
};
static const Step item_steps[] = {
    { 0xAA40, menu_open_items_entry, menu_open_items },
    { 0xAA4A, menu_choose_item_command_entry, menu_choose_item_command },
    { 0xAA86, menu_choose_on_item_list_entry, menu_choose_on_item_list },
    { 0xAAC8, menu_move_picked_item, NULL }, { 0xAB4A, menu_sort_items_entry, menu_sort_items }, { 0xAC1D, menu_close_item_users, NULL },
};
static const Step tech_steps[] = {
    { 0xB35D, menu_open_techs_entry, menu_open_techs },
    { 0xB365, menu_lay_out_tech_members_entry, menu_lay_out_tech_members },
    { 0xB36D, menu_choose_tech_member_entry, menu_choose_tech_member },
    { 0xB3AE, menu_choose_tech, NULL }, { 0xB48E, menu_choose_tech_target, NULL },
};
static const Step settings_steps[] = {
    { 0xBF3D, menu_open_settings_entry, menu_open_settings },
    { 0xBF45, menu_choose_setting_entry, menu_choose_setting }, { 0xBF6D, menu_choose_buttons, NULL },
    { 0xBF9F, menu_choose_gauge_layout, NULL },
};
static const Step exchange_steps[] = {
    { 0xC627, menu_open_exchange, NULL }, { 0xC65D, menu_choose_first_to_exchange, NULL },
    { 0xC6B9, menu_choose_second_to_exchange, NULL },
};
/* The slots, then the question before a game is saved over and a step no
 * code moves to, which a save point's save screen shares. */
static const Step save_steps[] = {
    { 0xCEDC, menu_open_saving_entry, menu_open_saving },
    { 0xCEFD, menu_choose_slot_to_save_in_entry, menu_choose_slot_to_save_in },
    { 0xCF61, menu_answer_whether_to_save_over_entry, menu_answer_whether_to_save_over },
    { 0xCF92, menu_close_the_question_on_a_button_entry, menu_close_the_question_on_a_button },
};
static const Step gate_exchange_steps[] = {
    { 0xC7AC, menu_open_exchange_at_the_gate, NULL }, { 0xC7E0, menu_choose_first_at_the_gate, NULL },
    { 0xC6B9, menu_choose_second_to_exchange, NULL },
};
static const Step save_point_steps[] = {
    { 0xD4F8, menu_open_save_screen_entry, menu_open_save_screen },
    { 0xD506, menu_choose_on_save_screen_entry, menu_choose_on_save_screen },
    { 0xCF61, menu_answer_whether_to_save_over_entry, menu_answer_whether_to_save_over },
    { 0xCF92, menu_close_the_question_on_a_button_entry, menu_close_the_question_on_a_button },
};
static const Step shop_steps[] = {
    { 0xD52B, menu_open_shop, NULL }, { 0xD546, menu_choose_shop_tab, NULL }, { 0xD58B, menu_choose_goods, NULL },
    { 0xD645, menu_choose_for_sale, NULL }, { 0xD690, menu_choose_how_many_to_sell, NULL },
    { 0xD618, menu_choose_how_many_to_buy, NULL }, { 0xD6C3, menu_choose_shop_member, NULL },
    { 0xD715, menu_choose_shop_slot, NULL }, { 0xD778, menu_choose_shop_equipment, NULL },
};
static const Step naming_steps[] = {
    { 0xE1ED, menu_open_naming_screen, menu_naming_step }, { 0xE21E, menu_name_the_character, menu_naming_step },
    { 0xE236, menu_choose_battle_mode, menu_naming_step },
    { 0xE201, menu_open_the_naming, menu_naming_step }, { 0xE209, menu_wait_to_name, menu_naming_step },
};
static const Step load_steps[] = {
    { 0xE613, menu_open_load_screen_entry, menu_open_load_screen },
    { 0xE61B, menu_choose_on_load_screen_entry, menu_choose_on_load_screen },
    { 0xE6AE, menu_choose_game_to_carry_on_from_entry, menu_choose_game_to_carry_on_from },
    { 0xE705, menu_answer_erase_entry, menu_answer_erase },
};

#define STEPS_OF(list) { list, sizeof list / sizeof list[0] }
static const struct {
    const Step *steps;
    uint8_t count;
} screens[MENU_SCREENS] = {
    [MENU_SCREEN_MAIN_MENU] = STEPS_OF(main_menu_steps),
    [MENU_SCREEN_EQUIPMENT] = STEPS_OF(equipment_steps),
    [MENU_SCREEN_ITEMS] = STEPS_OF(item_steps),
    [MENU_SCREEN_TECHS] = STEPS_OF(tech_steps),
    [MENU_SCREEN_SETTINGS] = STEPS_OF(settings_steps),
    [MENU_SCREEN_EXCHANGE] = STEPS_OF(exchange_steps),
    [MENU_SCREEN_SAVE] = STEPS_OF(save_steps),
    [MENU_SCREEN_EXCHANGE_AT_THE_GATE] = STEPS_OF(gate_exchange_steps),
    [MENU_SCREEN_SAVE_POINT] = STEPS_OF(save_point_steps),
    [MENU_SCREEN_SHOP] = STEPS_OF(shop_steps),
    [MENU_SCREEN_NAMING] = STEPS_OF(naming_steps),
    [MENU_SCREEN_LOAD] = STEPS_OF(load_steps),
};

static MenuScreen running_screen(const GameState *g) { return (MenuScreen)menu_screen(g); }

/* The step the running screen is on, or none where the screen has no such
 * step. */
static const Step *running_step(const GameState *g)
{
    MenuScreen screen = running_screen(g);
    uint8_t step = menu_screen_step(g);
    if (screen >= MENU_SCREENS || step >= screens[screen].count)
        return NULL;
    return &screens[screen].steps[step];
}

/* ---- The entry ------------------------------------------------------------------------------ */

enum { MENUS_BANK = 0xC20000 };

/* The original looks the step up and jumps to it through the cell it leaves
 * the step's routine in, having pushed a return into the main task's wait
 * for the next frame (menu/tasks.c) first: a step returns to the wait as from
 * a call. */
enum { THE_JUMP_TO_THE_STEP = 0xC2828E, THE_LOOP_S_WAIT = 0xC2826C };

/* A step's routine is entered with an 8-bit accumulator and 16-bit index
 * registers, its own address in A, as the original's jump leaves it: the
 * exchange's opening reads A's high byte where it refuses. */
static void enter_as_the_lookup_does(Snes *s, uint16_t routine)
{
    menu_set_widths(s, true, false);
    s->cpu.a = routine;
}

/* For shadow mode and the original's routines a guard hands a step to: what
 * else the lookup leaves, which no step's port reads -- the screen's and the
 * step's offsets into the tables in X and Y, the flags of the address's load
 * and of the screen's doubling, and the cells it looks the step up through,
 * the screen's list of steps and the routine it jumps through. */
static void leave_the_lookup(Snes *s, uint16_t routine)
{
    GameState *g = state_of(s);
    s->cpu.x = (uint16_t)(running_screen(g) * 2);
    s->cpu.y = (uint16_t)(menu_screen_step(g) * 2);
    m_set_nz16(s, routine);
    m_set_flag(s, CPU_FLAG_C, false);
    set_menu_screen_steps_at(g, (uint32_t)MENU_STEP_LISTS_BANK << 16 | menu_screen_step_list(g, (uint8_t)running_screen(g)));
    set_menu_scratch_step_routine(g, routine);
}

/* What the original's steps read of the registers the lookup leaves them:
 * all of them, as far as liveness can tell. */
enum { THE_STEPS_READ_EVERYTHING = 0 };

/* Run the step as the original's jump reaches it: as C where its port runs,
 * timed as the step's own, and otherwise as the original's routine. True if
 * it came back to the loop, on the menus' page; the loop's wait keeps
 * whatever widths a step comes back with (the item list's can leave a
 * 16-bit accumulator). */
static bool run_the_step(Snes *s, const Step *step)
{
    uint32_t routine = MENUS_BANK | step->routine;
    enter_as_the_lookup_does(s, step->routine);
    ORIGINAL_EFFECT(leave_the_lookup(s, step->routine));
    if (step->port && menu_runs_as_port(s, routine, step->port)) {
        bool back = false;
        NATIVE_TIME_AS_SIBLING(s, routine, back = menu_run_jumped(s, THE_LOOP_S_WAIT, routine, step->port));
        return back;
    }
    return menu_call_jumped(s, THE_JUMP_TO_THE_STEP, routine, THE_STEPS_READ_EVERYTHING, THE_LOOP_S_WAIT);
}

// $C2:826F
bool menu_run_the_screen_s_step(Snes *s) { return run_the_step(s, running_step(state_of(s))); }

/* As C, from inside the main task, timed as the step's own routine. */
bool menu_run_the_screen_step(GameState *g, Platform *pf)
{
    const Step *step = running_step(g);
    if (!step || !step->as_c) {
        program_needs_the_machine(g, step ? MENUS_BANK | step->routine : THE_JUMP_TO_THE_STEP,
                                  "a menu screen's step only the machine runs");
        return false;
    }
    bool back = false;
    TIMED_AS(g, MENUS_BANK | step->routine, back = step->as_c(g, pf));
    return back;
}

/* On the menus' page, the running screen on a step its table has. */
bool menu_screen_step_is_known(Snes *s)
{
    if (!menu_on_page(s))
        return false;
    vars_peek(true);
    bool ok = running_step(state_of(s)) != NULL;
    vars_peek(false);
    return ok;
}
