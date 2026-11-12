/* The memory a snapshot's raw pointers can name (docs/PLAYING.md, "Snapshots").
 *
 * A snapshot keeps the fibers' stacks as bytes, and those bytes hold pointers:
 * to other frames on the stacks, to the game's state, to the asset pack's
 * data. Within one run every such object stays where it is, so a snapshot
 * taken and restored in the same process is exact. Across processes the
 * objects must come back at the same addresses: the arena is one region,
 * reserved at the same address in every run where the host allows it, handed
 * out in order, so a build that makes the same allocations in the same order
 * lays them out the same. arena_fixed says whether the host gave the region
 * its usual place; a snapshot file records the addresses it depends on and a
 * run laid out otherwise refuses it rather than misreading it. */
#ifndef CT_PORT_ARENA_H
#define CT_PORT_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* `bytes` of zeroed memory that lives until the process ends, or NULL when
 * the arena is full. */
void *arena_take(size_t bytes);
/* Whether the arena is at its usual address (or not yet reserved). */
bool arena_fixed(void);

#endif
