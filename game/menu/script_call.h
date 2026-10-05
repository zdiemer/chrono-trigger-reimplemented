/* The command scripts' interpreter called from another object's handler on
 * the original's machine (menu/script_bridge.c), apart from
 * menu/script_bridge.h so a caller needs the language's types alone. */
#ifndef CT_GAME_MENU_SCRIPT_CALL_H
#define CT_GAME_MENU_SCRIPT_CALL_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/script.h"
#include "snes/system.h"
#include "state.h"

/* The interpreter called from the body of another object's handler (an
 * overworld walker running the script an event gave it), as the handler's
 * JSR at `call_site` calls it: the running object's script for this frame,
 * and how it ended. SCRIPT_GAVE_BACK where a command handed the rest of the
 * frame to the original's interpreter: the port has stopped, and the
 * original goes on to return to the caller's code after the call. A script
 * at `script` runs here where the game keeps its scripts and the sprite list
 * is as the mode keeps it, which the caller's guard asks. */
ScriptEnd menu_run_object_script_from(Snes *s, uint32_t call_site);
bool menu_object_script_can_run(const GameState *g, uint32_t script);

#endif
