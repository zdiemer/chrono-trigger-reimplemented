/* The menu scripts' named parameters, from their rows. */
#include "named_parameters.h"

#include "menu/script.h"

void named_menu_rows(Named *n)
{
    unsigned count = 0;
    for (unsigned op = 0; op < NAMED_OPCODES; op++) {
        n->from[op] = (uint16_t)count;
        const ScriptOpcode *row = menu_script_opcode((uint8_t)op);
        if (!row)
            continue;
#define NAMED(type, name)           \
        if (row->name.kind != ARG_NONE) \
            named_add(n, &count, MENU_PARAMETER_##name, "MENU_PARAMETER_" #name);
        SCRIPT_PARAMETERS(NAMED)
#undef NAMED
    }
    n->from[NAMED_OPCODES] = (uint16_t)count;
    n->built = menu_named_parameters;
    n->built_from = menu_named_from;
}
