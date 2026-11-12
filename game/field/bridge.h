/* Field object and row validation shared by native callers. */
#ifndef CT_GAME_FIELD_BRIDGE_H
#define CT_GAME_FIELD_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#include "field/object.h"
#include "field/scratch.h"
#include "port/vars_ram.h"
#include "state.h"
#include "vars.h"

enum { FIELD_PAGE = 0x0100, FIELD_LAYERS_PAGE = 0x1D00 };

enum { JSR_SIZE = 3, JSL_SIZE = 4 };

bool field_tick_is_exact(const GameState *g, unsigned obj);
bool field_rows_are_exact(const GameState *g, unsigned obj);

enum { FIELD_LEAVE_ROW_ENTRY = 0xC0A98A, FIELD_JOIN_ROW_ENTRY = 0xC0A9CD };

bool field_view_changes(const GameState *g, unsigned obj);
bool field_view_change_in_the_tables(const GameState *g, FieldObject obj);

#endif
