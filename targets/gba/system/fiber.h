/* The GBA's fiber slots (targets/gba/system/fiber.c): the first, the game's main
 * thread's, is larger than the tasks'. */
#ifndef CT_GBA_FIBER_H
#define CT_GBA_FIBER_H

#include <stddef.h>

/* A slot's stack, in bytes. */
size_t gba_fiber_slot_size(unsigned slot);

#endif
