/* Timing service, SNES backend: $4200, $4207-$420A, $420D, $4210-$4212, $2137, $213D, $213E and $213F. Included by
 * platform/timing.h. */
#ifndef CT_PLATFORM_SNES_TIMING_H
#define CT_PLATFORM_SNES_TIMING_H

#include "platform/snes/backend.h"
#include "platform/timing.h"

PF_API void tm_enable_interrupts(Platform *pf, PfOrigin o, uint8_t sources)
{
    pf_snes_begin("tm_enable_interrupts", o);
    pf_snes_put(pf_snes(pf), 0x4200, sources);
    pf_snes_end();
}

PF_API void tm_set_line(Platform *pf, PfOrigin o, uint16_t line)
{
    pf_snes_begin("tm_set_line", o);
    pf_snes_put(pf_snes(pf), 0x4209, (uint8_t)line);
    pf_snes_put(pf_snes(pf), 0x420A, (uint8_t)(line >> 8));
    pf_snes_end();
}

PF_API void tm_reset_timers(Platform *pf, PfOrigin o)
{
    pf_snes_begin("tm_reset_timers", o);
    for (uint16_t reg = 0x4207; reg <= 0x420A; reg++)
        pf_snes_put(pf_snes(pf), reg, 0);
    pf_snes_end();
}

PF_API void tm_fast_rom(Platform *pf, PfOrigin o, bool fast)
{
    pf_snes_begin("tm_fast_rom", o);
    pf_snes_put(pf_snes(pf), 0x420D, fast ? 0x01 : 0x00);
    pf_snes_end();
}

static inline uint8_t pf_snes_status_read(Platform *pf, PfOrigin o, const char *name, uint16_t reg)
{
    pf_snes_begin(name, o);
    uint8_t v = pf_snes_get(pf_snes(pf), reg);
    pf_snes_end();
    return v;
}

PF_API uint8_t tm_ack_vblank(Platform *pf, PfOrigin o) { return pf_snes_status_read(pf, o, "tm_ack_vblank", 0x4210); }
PF_API uint8_t tm_ack_line_irq(Platform *pf, PfOrigin o)
{
    return pf_snes_status_read(pf, o, "tm_ack_line_irq", 0x4211);
}
PF_API uint8_t tm_video_status(Platform *pf, PfOrigin o)
{
    return pf_snes_status_read(pf, o, "tm_video_status", 0x4212);
}
PF_API uint8_t tm_reset_beam_latch(Platform *pf, PfOrigin o)
{
    return pf_snes_status_read(pf, o, "tm_reset_beam_latch", 0x213F);
}

/* A 16-bit read of $213E: the low byte $213E's, the high byte $213F's (whose
 * read also restarts the beam position latch). */
PF_API uint16_t tm_picture_status(Platform *pf, PfOrigin o)
{
    pf_snes_begin("tm_picture_status", o);
    uint8_t low = pf_snes_get(pf_snes(pf), 0x213E);
    uint8_t high = pf_snes_get(pf_snes(pf), 0x213F);
    pf_snes_end();
    return (uint16_t)(low | high << 8);
}

PF_API uint8_t tm_beam_line(Platform *pf, PfOrigin o)
{
    pf_snes_begin("tm_beam_line", o);
    pf_snes_get(pf_snes(pf), 0x2137);
    pf_snes_get(pf_snes(pf), 0x213F);
    uint8_t line = pf_snes_get(pf_snes(pf), 0x213D);
    pf_snes_end();
    return line;
}

PF_API void tm_sync(Platform *pf) { native_sync(pf_snes(pf)); }

#endif
