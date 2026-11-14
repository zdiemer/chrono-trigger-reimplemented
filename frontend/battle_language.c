/* The battle's animation script language (battle/script_language.h), as its
 * text describes it (frontend/languages.h): each row from the game's table,
 * a parameter an operand byte or fixed by the command's byte. */
#include "languages.h"

#include "battle/script_language.h"

static void add(LanguageRow *row, const char *name, ScriptArg arg)
{
    if (arg.kind == ARG_NONE)
        return;
    if (row->params == LANGUAGE_MOST_PARAMS) {
        row->known = false;   /* more than the text can hold: its commands are written as bytes */
        return;
    }
    bool fixed = arg.kind == ARG_CONSTANT;
    row->param[row->params++] = (LanguageParam){ name, "", fixed, fixed ? arg.operand_or_constant : 0,
                                                 fixed ? 0 : arg.operand_or_constant, fixed ? 0 : 1, 0 };
}

static const LanguageRow *row_of(uint8_t opcode)
{
    static LanguageRow rows[256];
    static bool built;
    if (!built) {
        for (unsigned op = 0; op < 256; op++) {
            const ScriptOpcode *o = script_opcode((uint8_t)op);
            LanguageRow *row = &rows[op];
            *row = (LanguageRow){ .opcode = (uint8_t)op };
            if (!o || !o->name)
                continue;
            row->known = true;
            row->name = o->name;
            row->length = o->length;
            row->measure = o->measure;
#define ADD_PARAM(type, name) add(row, #name, o->name);
            SCRIPT_PARAMETERS(ADD_PARAM)
#undef ADD_PARAM
            language_order_params(row);
        }
        built = true;
    }
    return &rows[opcode];
}

const Language battle_animation_language = { "battle animation", row_of };
