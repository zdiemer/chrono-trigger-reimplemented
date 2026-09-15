/* Input service (docs/PLATFORM.md): the controllers as read once per frame. */
#ifndef CT_PLATFORM_INPUT_H
#define CT_PLATFORM_INPUT_H

#include "platform/platform.h"
#include "platform/timing.h"

/* Bits 15-4: B Y Select Start Up Down Left Right A X L R (JOY1 order). The low
 * byte is the half with A X L R, the high byte the half with B ... Right. */
typedef uint16_t PadBits;

/* True once the controllers have been read this frame. */
static inline bool in_ready(Platform *pf, PfOrigin o) { return !(tm_video_status(pf, o) & TM_STATUS_JOYPAD_BUSY); }

/* The buttons of controller `pad` (0-3) as last read. */
PF_API PadBits in_read(Platform *pf, PfOrigin o, uint8_t pad);
/* Each half alone, for code that reads one: in_read's low and high byte. */
PF_API uint8_t in_read_low(Platform *pf, PfOrigin o, uint8_t pad);
PF_API uint8_t in_read_high(Platform *pf, PfOrigin o, uint8_t pad);

/* Host side, once per frame. */
PF_API void in_host_set(Platform *pf, uint8_t pad, PadBits buttons);

#ifdef CT_PLATFORM_SNES
#include "platform/snes/input.h"
#endif

#endif
