/* The interpreters' named parameters: the lists the event and menu engines
 * walk (game/event/named_parameters.c, game/menu/named_parameters.c) are the
 * parameters each opcode's row names, in the language's order. This test
 * works them out from the rows and fails where the built lists differ;
 * `--write DIR` writes both files under DIR (the repository's root) from the
 * rows instead:
 *
 *     named_parameters_test --write .
 */
#include <stdio.h>
#include <string.h>

#include "named_parameters.h"

#include "event/script.h"

void named_add(Named *n, unsigned *count, uint8_t p, const char *what)
{
    n->names[*count] = what;
    n->list[(*count)++] = p;
}

void named_event_rows(Named *n)
{
    unsigned count = 0;
    for (unsigned op = 0; op < NAMED_OPCODES; op++) {
        n->from[op] = (uint16_t)count;
        const EventOpcode *row = event_opcode((uint8_t)op);
        if (!row)
            continue;
#define NAMED(type, name)           \
        if (row->name.kind != ARG_NONE) \
            named_add(n, &count, EVENT_PARAMETER_##name, "EVENT_PARAMETER_" #name);
        EVENT_PARAMETERS(NAMED)
#undef NAMED
    }
    n->from[NAMED_OPCODES] = (uint16_t)count;
    n->built = event_named_parameters;
    n->built_from = event_named_from;
}

static int same(const char *what, const Named *n)
{
    for (unsigned op = 0; op <= NAMED_OPCODES; op++)
        if (n->built_from[op] != n->from[op]) {
            fprintf(stderr, "named_parameters: %s: opcode %02X's list starts at %u, its row's at %u: "
                            "run named_parameters_test --write .\n", what, op, n->built_from[op], n->from[op]);
            return 0;
        }
    for (unsigned i = 0; i < n->from[NAMED_OPCODES]; i++)
        if (n->built[i] != n->list[i]) {
            fprintf(stderr, "named_parameters: %s: entry %u is %u, its row's %s: run named_parameters_test --write .\n",
                    what, i, n->built[i], n->names[i]);
            return 0;
        }
    return 1;
}

static int write_named(const char *root, const char *language, const Named *n)
{
    char file[1024];
    snprintf(file, sizeof file, "%s/game/%s/named_parameters.c", root, language);
    FILE *f = fopen(file, "w");
    if (!f) {
        fprintf(stderr, "named_parameters: can't write %s\n", file);
        return 0;
    }
    fprintf(f, "/* Written by tests/port/named_parameters_test --write from the rows: the\n"
               " * parameters each opcode's row names, in the language's order (%s/script.h). */\n"
               "#include \"%s/script.h\"\n\n", language, language);
    fprintf(f, "const uint8_t %s_named_parameters[] = {\n", language);
    for (unsigned op = 0; op < NAMED_OPCODES; op++) {
        if (n->from[op] == n->from[op + 1])
            continue;
        int column = fprintf(f, "    /* %02X */", op);
        for (unsigned i = n->from[op]; i < n->from[op + 1]; i++) {
            if (column + 2 + (int)strlen(n->names[i]) > 110)
                column = fprintf(f, "\n            ");
            column += fprintf(f, " %s,", n->names[i]);
        }
        fprintf(f, "\n");
    }
    if (n->from[NAMED_OPCODES] == 0)
        fprintf(f, "    0,\n");
    fprintf(f, "};\n\nconst uint16_t %s_named_from[256 + 1] = {", language);
    for (unsigned op = 0; op <= NAMED_OPCODES; op++)
        fprintf(f, "%s%u,", op % 16 ? " " : "\n    ", n->from[op]);
    fprintf(f, "\n};\n");
    fclose(f);
    return 1;
}

static Named event_named, menu_named;

int main(int argc, char **argv)
{
    named_event_rows(&event_named);
    named_menu_rows(&menu_named);
    if (argc == 3 && strcmp(argv[1], "--write") == 0)
        return write_named(argv[2], "event", &event_named) && write_named(argv[2], "menu", &menu_named) ? 0 : 1;
    if (!same("event", &event_named) || !same("menu", &menu_named))
        return 1;
    printf("named_parameters: %u event and %u menu parameters named, as the rows name them\n",
           (unsigned)event_named.from[NAMED_OPCODES], (unsigned)menu_named.from[NAMED_OPCODES]);
    return 0;
}
