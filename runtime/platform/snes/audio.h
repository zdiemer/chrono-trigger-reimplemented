/* Audio service, SNES backend: the command buffer at $1E00, handed to the
 * host's C sound driver as a call where one plays (frontend/sound_host.h) or
 * to the cartridge's own CPU half at $C7:0004 where the console's sound
 * processor runs the cartridge's driver (the reference play); and the APU
 * ports. Included by platform/audio.h. */
#ifndef CT_PLATFORM_SNES_AUDIO_H
#define CT_PLATFORM_SNES_AUDIO_H

#include "platform/audio.h"
#include "platform/snes/backend.h"

PF_API void au_command(Platform *pf, PfOrigin o, AudioCommand c)
{
    Snes *s = pf_snes(pf);
    const uint8_t params[3] = { c.p1, c.p2, c.p3 };
    for (int i = 0; i < c.count && i < 3; i++)
        m_write8(s, 0x001E01 + (uint32_t)i, params[i]);
    m_write8(s, 0x001E00, c.command);
    if (s->apu.host_send) {
        /* A host plays the C driver: the command is a call. */
        s->apu.host_send(s->apu.host_ctx);
        return;
    }
    m_jsl_dc(s, o.site, o.target, o.dont_care);
}

PF_API uint8_t au_status(Platform *pf, PfOrigin o, uint8_t port)
{
    pf_snes_begin("au_status", o);
    uint8_t v = pf_snes_get(pf_snes(pf), (uint16_t)(0x2140 + (port & 3)));
    pf_snes_end();
    return v;
}

PF_API uint8_t au_wait_music_start(Platform *pf, PfOrigin o)
{
    uint8_t tracks;
    while (!(tracks = au_music_playing(pf, PF_AT(o.site))))
        native_wait_loop(pf_snes(pf), o.site, o.target, NATIVE_WAKE_MUSIC_START);
    return tracks;
}

PF_API size_t au_take_samples(Platform *pf, int16_t *out, size_t max_frames)
{
    return apu_take_samples(&pf_snes(pf)->apu, out, max_frames);
}

/* A port access sees the time taken so far: the sound processor watches the
 * ports as the time passes. */
PF_API void au_driver_write(Platform *pf, PfOrigin o, uint8_t port, uint8_t v)
{
    Snes *s = pf_snes(pf);
    native_sync(s);
    pf_snes_begin("au_driver_write", o);
    pf_snes_put(s, (uint16_t)(0x2140 + (port & 3)), v);
    pf_snes_end();
}

PF_API uint8_t au_driver_read(Platform *pf, PfOrigin o, uint8_t port)
{
    Snes *s = pf_snes(pf);
    native_sync(s);
    pf_snes_begin("au_driver_read", o);
    uint8_t v = pf_snes_get(s, (uint16_t)(0x2140 + (port & 3)));
    pf_snes_end();
    return v;
}

PF_API void au_driver_call(Platform *pf, PfOrigin o, uint16_t dont_care)
{
    m_jsr_dc(pf_snes(pf), o.site, o.target, dont_care);
}

/* A pass of the caller's poll: a compare of port 0 with A's low byte and a
 * branch back. Where the original's loop runs in the port's place (the
 * tools, outside the runtime), A is set as the original's code sets it
 * before the loop. */
PF_API void au_driver_wait_echo(Platform *pf, PfOrigin o, uint8_t value)
{
    Snes *s = pf_snes(pf);
    m_set_a8(s, value);
    native_wait_loop(s, o.site, o.target, native_wake_port0_shows(value));
}

/* The scene program retries its command after 1024 polls. Keep the held C
 * continuation while the host's C driver (or the reference SPC700) answers. */
PF_API bool au_driver_wait_echo_or_give_up(Platform *pf, PfOrigin o, uint32_t given_up, uint8_t value, uint16_t tries)
{
    Snes *s = pf_snes(pf);
    enum { SCENE_ECHO_POLL_CLOCKS = 82 };
    m_set_a8(s, value);
    s->cpu.x = tries;
    return native_wait_loop_or_give_up(s, o.site, o.target, given_up,
                                      native_wake_port0_shows(value), tries, SCENE_ECHO_POLL_CLOCKS);
}

PF_API bool au_driver_hosted(Platform *pf) { return pf_snes(pf)->apu.host_command != NULL; }

/* The host takes the work at once and answers the original's time for it
 * (platform/time_model.h), which the machine lets pass where the CPU half
 * stands, as it would have waited on the ports: the frames an upload took
 * still go by, and interrupts are taken meanwhile (native_wait_time; the
 * shipping mode only, as a port's own estimate is). */
PF_API bool au_driver_boot_taken(Platform *pf)
{
    Snes *s = pf_snes(pf);
    Apu *a = &s->apu;
    if (!a->host_boot)
        return false;
    native_wait_time(s, a->host_boot(a->host_ctx, a));
    return true;
}

PF_API bool au_driver_command_taken(Platform *pf, AudioCommand c, uint8_t effects_asked)
{
    Snes *s = pf_snes(pf);
    Apu *a = &s->apu;
    if (!a->host_command)
        return false;
    const uint8_t command[4] = { c.command, c.p1, c.p2, c.p3 };
    native_wait_time(s, a->host_command(a->host_ctx, a, command, effects_asked));
    return true;
}

#endif
