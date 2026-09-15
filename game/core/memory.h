/* Memory helpers shared by the menus and the save code. */
#ifndef CT_GAME_CORE_MEMORY_H
#define CT_GAME_CORE_MEMORY_H

#include <stdbool.h>

#include "snes/system.h"

bool memory_move_stub_present(Snes *s);
void memory_move_stub(Snes *s);

#endif
