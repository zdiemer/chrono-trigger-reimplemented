/* The object-script interpreter's legacy adapter. Commands dispatch through
 * the C language table; named services check their complete contracts after
 * the call frame is built. Invalid input stops at its original boundary and
 * reports a contract failure. Individual command wrappers have retired. */
#ifndef CT_GAME_MENU_SCRIPT_BRIDGE_H
#define CT_GAME_MENU_SCRIPT_BRIDGE_H

#include "snes/system.h"

/* The interpreter ($C2:0F63), run as a slot's handler, and its guard. */
void menu_run_object_script_entry(Snes *s);
bool menu_object_script_runs(Snes *s);

#endif
