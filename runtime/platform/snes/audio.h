/* Audio service, SNES backend: the command buffer at $1E00, the driver at
 * $C7:0004 and the APU ports; with APU-direct selected at run time
 * (platform/direct/audio.h), the driver's waits run the SPC700 instead of the
 * machine. Included by platform/audio.h. */
#ifndef CT_PLATFORM_SNES_AUDIO_H
#define CT_PLATFORM_SNES_AUDIO_H

#include "platform/audio.h"
#include "platform/direct/audio.h"
#include "platform/snes/backend.h"

PF_API void au_command(Platform *pf, PfOrigin o, AudioCommand c)
{
    Snes *s = pf_snes(pf);
    const uint8_t params[3] = { c.p1, c.p2, c.p3 };
    for (int i = 0; i < c.count && i < 3; i++)
        m_write8(s, 0x001E01 + (uint32_t)i, params[i]);
    m_write8(s, 0x001E00, c.command);
    m_jsl_dc(s, o.site, o.target, o.dont_care);
}

PF_API uint8_t au_status(Platform *pf, PfOrigin o, uint8_t port)
{
    pf_snes_begin("au_status", o);
    uint8_t v = pf_snes_get(pf_snes(pf), (uint16_t)(0x2140 + (port & 3)));
    pf_snes_end();
    return v;
}

PF_API size_t au_take_samples(Platform *pf, int16_t *out, size_t max_frames)
{
    return au_direct_take_samples(pf_snes(pf), out, max_frames);
}

/* Before a driver port access: the SPC700 sees the time taken so far. */
static inline void au_snes_driver_sync(Snes *s)
{
    if (au_direct_running(s))
        au_direct_pass(s);
    else
        native_sync(s);
}

PF_API void au_driver_write(Platform *pf, PfOrigin o, uint8_t port, uint8_t v)
{
    Snes *s = pf_snes(pf);
    au_snes_driver_sync(s);
    pf_snes_begin("au_driver_write", o);
    pf_snes_put(s, (uint16_t)(0x2140 + (port & 3)), v);
    pf_snes_end();
}

PF_API uint8_t au_driver_read(Platform *pf, PfOrigin o, uint8_t port)
{
    Snes *s = pf_snes(pf);
    au_snes_driver_sync(s);
    pf_snes_begin("au_driver_read", o);
    uint8_t v = pf_snes_get(s, (uint16_t)(0x2140 + (port & 3)));
    pf_snes_end();
    return v;
}

PF_API bool au_driver_run_direct(Platform *pf, uint32_t lo, uint32_t hi) { return au_direct_run(pf_snes(pf), lo, hi); }

PF_API void au_driver_call(Platform *pf, PfOrigin o, uint16_t dont_care)
{
    Snes *s = pf_snes(pf);
    if (au_direct_running(s))
        au_direct_call(s, o.site, o.target);
    else
        m_jsr_dc(s, o.site, o.target, dont_care);
}

/* The driver's exchange routines ($C7:09DA, $C7:09EA) take the value in an
 * 8-bit A, or from the cell they reset. Where the port waits in place, the
 * routine's return after its poll has seen the echo -- the poll's branch,
 * the counter's next value or its reset, the flags pulled and the return --
 * still takes its time: the SPC700 reads the rest of the message after it
 * echoes (ports 2-3, some 15 of its cycles after), and the next message's
 * bytes must not reach the ports before it has. Its port's own time does the
 * rest, the bytes' loads included (core/sound_driver.c, ACCESSES_A_LOAD):
 * over the movie library in the shipping mode the next message's byte reaches
 * port 3 8-15 SPC700 cycles after that read in all but 8,577 of 1,157,232
 * messages, and 3 (about 63 master clocks) at the least, as the original's
 * does (ct_dev --apu-port-margins; docs/PLATFORM.md, "The CPU half's waits,
 * as built"). */
enum { AU_SNES_EXCHANGE_RETURN_CLOCKS = 102 };

PF_API void au_driver_exchange(Platform *pf, PfOrigin o, uint8_t value)
{
    Snes *s = pf_snes(pf);
    if (native_waits_in_place(s)) {
        au_driver_write(pf, PF_AT(o.target), 0, value);
        native_wait_call(s, o.site, o.target, false, o.dont_care, native_wake_port0_shows(value));
        native_wait_time(s, AU_SNES_EXCHANGE_RETURN_CLOCKS);
        return;
    }
    s->cpu.p |= CPU_FLAG_M;
    m_set_a8(s, value);
    if (au_direct_running(s))
        au_direct_call(s, o.site, o.target);
    else
        native_wait_call(s, o.site, o.target, false, o.dont_care, native_wake_port0_shows(value));
}

/* A pass of the CPU half's polls: a compare with port 0 and a branch back,
 * and in a poll that gives up, the count down too. The loops compare port 0
 * with A's low byte, and count down X: where the original's loop runs in the
 * port's place (the tools, outside the runtime), they are set as the
 * original's code before the loop sets them. */
enum { AU_SNES_POLL_CLOCKS = NATIVE_PORT0_POLL_CLOCKS, AU_SNES_COUNTED_POLL_CLOCKS = 66 };

static inline void au_snes_wait_on_port0(Platform *pf, PfOrigin o, uint8_t value, int32_t wake)
{
    Snes *s = pf_snes(pf);
    if (au_direct_running(s)) {
        au_direct_wait_port0(s, wake, 0, AU_SNES_POLL_CLOCKS);
        return;
    }
    m_set_a8(s, value);
    native_wait_loop(s, o.site, o.target, wake);
}

PF_API void au_driver_wait_echo(Platform *pf, PfOrigin o, uint8_t value)
{
    au_snes_wait_on_port0(pf, o, value, native_wake_port0_shows(value));
}

PF_API void au_driver_wait_moved(Platform *pf, PfOrigin o, uint8_t value)
{
    au_snes_wait_on_port0(pf, o, value, native_wake_port0_leaves(value));
}

PF_API bool au_driver_wait_echo_or_give_up(Platform *pf, PfOrigin o, uint32_t given_up, uint8_t value, uint16_t tries)
{
    Snes *s = pf_snes(pf);
    int32_t wake = native_wake_port0_shows(value);
    if (au_direct_running(s))
        return au_direct_wait_port0(s, wake, tries, AU_SNES_COUNTED_POLL_CLOCKS);
    m_set_a8(s, value);
    s->cpu.x = tries;
    return native_wait_loop_or_give_up(s, o.site, o.target, given_up, wake, tries, AU_SNES_COUNTED_POLL_CLOCKS);
}

PF_API bool au_driver_hosted(Platform *pf) { return pf_snes(pf)->apu.host_command != NULL; }

/* The host takes the work at once and answers the original's time for it
 * (platform/time_model.h), which the machine lets pass where the CPU half
 * stands, as it would have waited on the ports: the frames an upload took
 * still go by, and interrupts are taken meanwhile, their time on top of it
 * (native_wait_own_time; the shipping mode only, as a port's own estimate
 * is). */
PF_API bool au_driver_boot_taken(Platform *pf)
{
    Snes *s = pf_snes(pf);
    Apu *a = &s->apu;
    if (!a->host_boot)
        return false;
    native_wait_own_time(s, a->host_boot(a->host_ctx, a));
    return true;
}

PF_API bool au_driver_command_taken(Platform *pf, AudioCommand c, uint8_t effects_asked)
{
    Snes *s = pf_snes(pf);
    Apu *a = &s->apu;
    if (!a->host_command)
        return false;
    const uint8_t command[4] = { c.command, c.p1, c.p2, c.p3 };
    native_wait_own_time(s, a->host_command(a->host_ctx, a, command, effects_asked));
    return true;
}

PF_API void au_select_backend(Platform *pf, AuBackend backend)
{
    au_direct_select(pf_snes(pf), backend == AU_BACKEND_DIRECT);
}

#endif
