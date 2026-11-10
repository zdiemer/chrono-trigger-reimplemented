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

/* Run the current object's script for one turn, checking the interpreter's
 * full contract on the call frame. A rejected call reports the broken
 * contract and leaves that frame at the original entry; success returns to
 * the caller with the script's result and original live register outputs. */
ScriptEnd menu_run_object_script_from(Snes *s, uint32_t call_site);
bool menu_object_script_can_run(const GameState *g, uint32_t script);

#endif
