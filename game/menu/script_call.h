/* What a handler checks before it runs an object's command script, apart
 * from menu/script.h's interpreter so a caller needs the language's types
 * alone. */
#ifndef CT_GAME_MENU_SCRIPT_CALL_H
#define CT_GAME_MENU_SCRIPT_CALL_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/script.h"
#include "state.h"

/* Whether the running object's script `script` can run: read from the ROM or
 * work RAM, with the sprite list as the mode keeps it. */
bool menu_object_script_can_run(const GameState *g, uint32_t script);

#endif
