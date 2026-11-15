/* The interpreters' named parameters, worked out from the rows
 * (tests/port/named_parameters_test.c): each language's in a file of its
 * own, since the languages' headers name their operands' kinds alike. */
#pragma once

#include <stdint.h>

enum { NAMED_OPCODES = 256, NAMED_MOST = NAMED_OPCODES * 16 };

typedef struct {
    uint8_t list[NAMED_MOST];
    uint16_t from[NAMED_OPCODES + 1];
    const char *names[NAMED_MOST];   /* each entry's enumerator, for the file */
    const uint8_t *built;            /* the lists the game was built with */
    const uint16_t *built_from;
} Named;

void named_add(Named *n, unsigned *count, uint8_t p, const char *what);
void named_event_rows(Named *n);
void named_menu_rows(Named *n);
