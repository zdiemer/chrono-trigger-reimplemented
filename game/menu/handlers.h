/* The sprite-object mode's objects as the program runs them with no machine
 * (game/menu/handlers.c).
 *
 * On the machine an object's handler, and a routine a command script calls,
 * is reached by its address in bank C2 (or anywhere, for a far call), and
 * the runtime finds the port registered there. As C each is a row of a
 * table: the handler's turn or the routine as a function of the game's
 * state, and the state half of the guard its entry has on the machine, which
 * the program checks before it runs it -- where a guard would hand the call
 * to the original, the program goes no further (program_needs_the_machine). */
#ifndef CT_GAME_MENU_HANDLERS_H
#define CT_GAME_MENU_HANDLERS_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/object.h"
#include "platform/platform.h"
#include "state.h"

/* How an object's turn ended: it goes on next frame, it is done (its slot
 * free), or it met a step only the machine runs yet. */
typedef enum { MENU_TURN_GOES_ON, MENU_TURN_DONE, MENU_TURN_WENT_NO_FURTHER } MenuTurn;

static inline MenuTurn menu_turn_done_if(bool done) { return done ? MENU_TURN_DONE : MENU_TURN_GOES_ON; }

/* A handler: the address the slots name it by, what it is, the guard's state
 * half (none where the guard's only check is the machine's), and its turn. */
typedef struct {
    uint16_t handler;
    const char *name;
    bool (*takes)(const GameState *g, MenuObject self);
    MenuTurn (*run)(GameState *g, Platform *pf, MenuObject self);
} MenuHandler;

/* The row of a handler, or NULL where it has none. */
const MenuHandler *menu_handler_as_c(uint16_t handler);

/* A file's handlers, which it lists beside its entries. */
typedef struct {
    const MenuHandler *rows;
    unsigned count;
} MenuHandlers;
#define MENU_HANDLERS(name, ...)                                    \
    static const MenuHandler name##_rows[] = { __VA_ARGS__ };       \
    const MenuHandlers name = { name##_rows, sizeof name##_rows / sizeof name##_rows[0] }

/* Every object's turn this frame as C (menu/objects.c): false where one went
 * no further. */
bool menu_objects_run_as_c(GameState *g, Platform *pf);

/* A routine a command script calls by its address (24-bit, `far` where the
 * command named its bank): the guard's state half, which takes the running
 * object, and the routine, false where it went no further. */
typedef struct {
    uint32_t routine;
    const char *name;
    bool (*takes)(const GameState *g, MenuObject self);
    bool (*run)(GameState *g, Platform *pf, MenuObject self, bool far);
} MenuScriptRoutine;

typedef struct {
    const MenuScriptRoutine *rows;
    unsigned count;
} MenuScriptRoutines;
#define MENU_SCRIPT_ROUTINES(name, ...)                                   \
    static const MenuScriptRoutine name##_rows[] = { __VA_ARGS__ };       \
    const MenuScriptRoutines name = { name##_rows, sizeof name##_rows / sizeof name##_rows[0] }

/* The routines the world maps' scenes call (menu/object_commands.c), their
 * per-line effects (menu/line_effects.c) and the map's scenes' own
 * (menu/map_scenes.c). */
extern const MenuScriptRoutines menu_scene_routines;
extern const MenuScriptRoutines menu_line_effect_routines;
extern const MenuScriptRoutines menu_map_scene_routines;

/* The overworld's handlers and the routines its scenes call of it
 * (overworld/handlers.c). */
extern const MenuHandlers overworld_handlers;
extern const MenuScriptRoutines overworld_routines;

/* The object's command script for this frame, each command as C, with the
 * services as C: whether the object waits for the next frame or is done. A
 * command the port doesn't run, or a service that meets a step only the
 * machine runs, goes no further. */
MenuTurn menu_run_object_script_as_c(GameState *g, Platform *pf, MenuObject self);

#endif
