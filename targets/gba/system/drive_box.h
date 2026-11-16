/* The box a runner drives the GBA build through from outside, as
 * `ct_native --drive` is driven over stdin (targets/gba/host/mgba_run.c).
 *
 * The driver names the box's address in its debug log as it starts; a runner
 * that reads the log arms it then, and from then on the pads each frame are
 * the runner's, not the keypad's. With nothing to arm it -- hardware, or mGBA
 * played by hand -- the driver reads the keypad. The runner puts each frame's
 * pads in a ring; between frames with the ring empty the driver waits, and
 * answers what the runner asks of the game's state. Both sides are
 * little-endian with 32-bit words, so the layout is the same to each. */
#ifndef CT_GBA_DRIVE_BOX_H
#define CT_GBA_DRIVE_BOX_H

#include <stdint.h>

/* A movie's decisions of the game's frames, as ct_native's time model made
 * them (ct_native --decisions, system/frames.h), go in the cartridge, far
 * past the program and its pack: the mark "CTDC", their count, then one a
 * bit, the first in the lowest. Only a runner puts them there. */
enum { DRIVE_DECISIONS_AT = 0x09800000u, DRIVE_DECISIONS_MARK = 0x43445443u };

enum {
    DRIVE_ARMED = 0x56495244u,   /* "DRIV": a runner drives */
    DRIVE_RING = 64,             /* frames of pads the runner can put ahead */
    DRIVE_BYTES = 256,           /* the most one read answers */
};

/* What the runner asks while the driver waits; the driver sets the command
 * back to DRIVE_IDLE once it has answered. */
enum {
    DRIVE_IDLE,
    DRIVE_READ,     /* `length` bytes of the game's memory from bus address `address`, into `bytes` */
    DRIVE_REPORT,   /* the report to the debug log: the frames, their work, the end state's CRCs */
};

typedef struct {
    uint32_t armed;
    uint32_t written;    /* the runner's: frames of pads put in the ring, all told */
    uint32_t taken;      /* the driver's: frames begun, all told */
    uint32_t waiting;    /* the driver's: 1 while it is between frames with the ring empty */
    uint32_t shown;      /* the driver's: the frame whose picture it put on the screen last, counted from 1 */
    uint32_t command, address, length;
    uint32_t pads[DRIVE_RING];   /* frame n's at n % DRIVE_RING: the first controller's low, the second's high */
    uint8_t bytes[DRIVE_BYTES];
} DriveBox;

#endif
