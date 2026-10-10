/* Input service (docs/PLATFORM.md): the controllers as read once per frame. */
#ifndef CT_PLATFORM_INPUT_H
#define CT_PLATFORM_INPUT_H

#include "platform/platform.h"
#include "platform/timing.h"

/* Bits 15-4: B Y Select Start Up Down Left Right A X L R (JOY1 order). The low
 * byte is the half with A X L R, the high byte the half with B ... Right. */
typedef uint16_t PadBits;

/* The buttons, as PadBits. */
enum {
    IN_BUTTON_B = 0x8000, IN_BUTTON_Y = 0x4000, IN_BUTTON_SELECT = 0x2000, IN_BUTTON_START = 0x1000,
    IN_BUTTON_UP = 0x0800, IN_BUTTON_DOWN = 0x0400, IN_BUTTON_LEFT = 0x0200, IN_BUTTON_RIGHT = 0x0100,
    IN_BUTTON_A = 0x0080, IN_BUTTON_X = 0x0040, IN_BUTTON_L = 0x0020, IN_BUTTON_R = 0x0010,
};

/* True once the controllers have been read this frame. */
PF_API bool in_ready(Platform *pf, PfOrigin o);

/* The buttons of controller `pad` (0-3) as last read. */
PF_API PadBits in_read(Platform *pf, PfOrigin o, uint8_t pad);
/* Each half alone, for code that reads one: in_read's low and high byte. */
PF_API uint8_t in_read_low(Platform *pf, PfOrigin o, uint8_t pad);
PF_API uint8_t in_read_high(Platform *pf, PfOrigin o, uint8_t pad);

/* The controller port's programmable lines (a light gun's latch among them),
 * as a bit each; the game sets them all high and never reads them. */
PF_API void in_set_io_lines(Platform *pf, PfOrigin o, uint8_t lines);

/* Host side, once per frame. */
PF_API void in_host_set(Platform *pf, uint8_t pad, PadBits buttons);

#ifdef CT_PLATFORM_SNES
#include "platform/snes/input.h"
#endif

#endif
