/* The SNES backend's shared helpers: the Platform is the development system's
 * machine, and calls write its registers through the bus as the original's
 * instructions did (docs/PLATFORM.md). Included by the service headers. */
#ifndef CT_PLATFORM_SNES_BACKEND_H
#define CT_PLATFORM_SNES_BACKEND_H

#include "platform/platform.h"
#include "port/machine.h"

static inline Snes *pf_snes(Platform *pf) { return (Snes *)pf; }

/* The API call in progress, for ct_dev --io-log: set for the length of a call
 * (and cleared before a call-out, whose callee runs as other code). Two stores
 * per call. */
typedef struct {
    const char *name;
    uint32_t site;
} PfSnesCall;
extern PfSnesCall pf_snes_call;

static inline void pf_snes_begin(const char *name, PfOrigin o)
{
    pf_snes_call.name = name;
    pf_snes_call.site = o.site;
}
static inline void pf_snes_end(void) { pf_snes_call.name = NULL; }

/* A register in bank $00, where the original's absolute and long addressing
 * reached it. */
static inline void pf_snes_put(Snes *s, uint16_t reg, uint8_t v) { m_write8(s, reg, v); }
static inline uint8_t pf_snes_get(Snes *s, uint16_t reg) { return m_read8(s, reg); }

#endif
