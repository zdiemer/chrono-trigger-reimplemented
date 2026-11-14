/* The menus' object script language (menu/script.h), as its text describes
 * it (frontend/languages.h): each row from the game's table, a parameter a
 * byte, a word or an address of the command's operands, a byte of the running
 * object's (`slot:`) or a cell of low RAM (`cell:`), or fixed by the opcode. */
#include "languages.h"

#include "menu/script.h"

static void add(LanguageRow *row, const char *name, ScriptArg arg)
{
    if (arg.kind == ARG_NONE)
        return;
    if (row->params == LANGUAGE_MOST_PARAMS) {
        row->known = false;   /* more than the text can hold: its commands are written as bytes */
        return;
    }
    LanguageParam p = { name, "", false, 0, arg.operand, 1, 0 };
    switch (arg.kind) {
    case ARG_CONSTANT: p = (LanguageParam){ name, "", true, arg.constant, 0, 0, 0 }; break;
    case ARG_WORD: p.bytes = 2; break;
    case ARG_LONG: p.bytes = 3; break;
    case ARG_WORD_AND_BANK: p.bytes = 2, p.bank_at = arg.bank_at; break;
    case ARG_SLOT_BYTE: p.prefix = "slot:"; break;
    case ARG_CELL: p.prefix = "cell:", p.bytes = 2; break;
    default: break;
    }
    row->param[row->params++] = p;
}

static const LanguageRow *row_of(uint8_t opcode)
{
    static LanguageRow rows[256];
    static bool built;
    if (!built) {
        for (unsigned op = 0; op < 256; op++) {
            const ScriptOpcode *o = menu_script_opcode((uint8_t)op);
            LanguageRow *row = &rows[op];
            *row = (LanguageRow){ .opcode = (uint8_t)op };
            if (!o || !o->name)
                continue;
            row->known = true;
            row->name = o->name;
            row->length = o->length;
#define ADD_PARAM(type, name) add(row, #name, o->name);
            SCRIPT_PARAMETERS(ADD_PARAM)
#undef ADD_PARAM
            language_order_params(row);
        }
        built = true;
    }
    return &rows[opcode];
}

const Language menu_object_language = { "menu object", row_of };
