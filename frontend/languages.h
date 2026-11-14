/* The byte-coded script languages the game interprets from a table of rows,
 * a row an opcode (the battle's animations, battle/script_language.h; the
 * menus' object scripts, menu/script.h), described alike for their text
 * (frontend/commands.h): each language's rows, as frontend/battle_language.c
 * and frontend/menu_language.c read them from the game's own tables, so the
 * text names each command and its parameters as the port decodes them.
 *
 * The languages' tables share their C names (ScriptOpcode, OPERATION), so
 * each is read in a file of its own and handed over in these terms. */
#ifndef CT_FRONTEND_LANGUAGES_H
#define CT_FRONTEND_LANGUAGES_H

#include <stdbool.h>
#include <stdint.h>

/* A parameter of a row: fixed by the opcode (its value), or `bytes` operand
 * bytes from `operand` on, low first, and where `bank_at` isn't 0 the bank
 * the byte there names above them; `prefix` before its value says what it
 * names. */
typedef struct {
    const char *name;
    const char *prefix;
    bool fixed;
    uint32_t value;      /* a fixed parameter's */
    uint8_t operand, bytes, bank_at;
} LanguageParam;

enum { LANGUAGE_MOST_PARAMS = 12 };

/* An opcode's row: unknown where the language has none for it (the original
 * runs it, or no script has it), or the command's name, length and
 * parameters in the order a line gives them. */
typedef struct {
    bool known;
    uint8_t opcode;
    const char *name;
    uint8_t length;      /* its bytes, or the fewest where it is measured */
    uint16_t (*measure)(const uint8_t *command, uint16_t available);   /* NULL, or its length from its bytes */
    uint8_t params;
    LanguageParam param[LANGUAGE_MOST_PARAMS];
} LanguageRow;

/* The parameters in the order a line gives them: the fixed ones first, then
 * by operand, each kind in the order its table names them. */
void language_order_params(LanguageRow *row);

typedef struct {
    const char *what;                       /* "battle animation" */
    const LanguageRow *(*row)(uint8_t opcode);
} Language;

extern const Language battle_animation_language, menu_object_language;

#endif
