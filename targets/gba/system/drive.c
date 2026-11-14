/* The GBA build driven from outside (drive.h, drive_box.h). */
#include "system/drive.h"

#include <stdio.h>

#include "port/state_host.h"
#include "system/drive_box.h"

static volatile DriveBox box;

bool drive_open(void)
{
    /* A runner arms the box as it reads this line, before the call returns. */
    fprintf(stderr, "ct_gba: drive box at %08X\n", (unsigned)(uintptr_t)&box);
    return box.armed == DRIVE_ARMED;
}

static void answer(GameState *g, void (*report)(void *), void *ctx)
{
    switch (box.command) {
    case DRIVE_READ:
        /* a byte at a time: the picture's span reads stay inlined into its own */
        for (uint32_t i = 0; i < box.length && i < DRIVE_BYTES; i++)
            box.bytes[i] = state_host_read8(g, box.address + i);
        break;
    case DRIVE_REPORT:
        report(ctx);
        break;
    default:
        break;
    }
    box.command = DRIVE_IDLE;
}

uint32_t drive_next_pads(GameState *g, void (*report)(void *), void *ctx)
{
    while (box.taken == box.written || box.command != DRIVE_IDLE) {
        box.waiting = 1;
        if (box.command != DRIVE_IDLE)
            answer(g, report, ctx);
    }
    box.waiting = 0;
    return box.pads[box.taken++ % DRIVE_RING];
}
