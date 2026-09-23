/* Platform services: what game code calls instead of touching SNES hardware
 * registers (docs/PLATFORM.md). Five services, one header each: video.h,
 * audio.h, timing.h, input.h, save.h.
 *
 * One backend per build, bound at compile time. The development system (ct_dev,
 * ct_play, ct_fuzz) uses the SNES backend: `Platform` is the machine itself
 * (m_platform(s) in port/machine.h), every call performs the writes the
 * original code made at that point, through the bus, and the functions are
 * static inline, so a call costs what the direct writes cost. A build defining
 * CT_PLATFORM_EXTERN gets plain declarations instead, for a backend compiled
 * elsewhere (none exists yet).
 */
#ifndef CT_PLATFORM_PLATFORM_H
#define CT_PLATFORM_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Platform Platform;

/* Where the call stands in the original: the instruction whose hardware access
 * it replaces (PF_AT), or the JSR/JSL site and target for services the original
 * reaches by calling a routine (PF_CALL). The SNES backend uses it for
 * call-outs and ct_dev --io-log reports it; other backends ignore it. It keeps
 * `// $BB:AAAA` traceable below the routine level. */
typedef struct {
    uint32_t site, target;
    uint16_t dont_care;   /* PF_CALL_DC: registers and flags the callee doesn't read */
} PfOrigin;
#define PF_AT(addr) ((PfOrigin){ (addr), 0, 0 })
#define PF_CALL(site, target) ((PfOrigin){ (site), (target), 0 })
/* A call whose callee doesn't read the registers and flags in `dont_care`
 * (DC_* bits, port/native.h), which shadow mode then doesn't compare at the
 * call: a port written as logic apart from the registers sets only what the
 * callee takes. tools/dontcare_check.py checks the set against liveness. */
#define PF_CALL_DC(site, target, dont_care) ((PfOrigin){ (site), (target), (dont_care) })

/* The data a call refers to: a 24-bit game address while the WRAM layout is
 * kept, a host pointer once a subsystem owns its memory. The SNES backend only
 * takes game addresses. */
typedef struct {
    uint32_t addr;
    const uint8_t *ptr;
} PfData;
#define PF_GAME(a) ((PfData){ (a), NULL })

#ifdef CT_PLATFORM_EXTERN
#define PF_API extern
#else
#define CT_PLATFORM_SNES 1
#define PF_API static inline
#endif

#endif
