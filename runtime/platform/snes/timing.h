/* Timing service, SNES backend: $4200, $4207-$420A, $420D, $4210-$4212, $2137, $213C, $213D, $213E and
 * $213F, and the original's own wait routines, called where the game waited. Included by
 * platform/timing.h. */
#ifndef CT_PLATFORM_SNES_TIMING_H
#define CT_PLATFORM_SNES_TIMING_H

#include "platform/snes/backend.h"
#include "platform/snes/encodings.h"
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
PF_API uint8_t tm_reset_beam_latch(Platform *pf, PfOrigin o)
{
    return pf_snes_status_read(pf, o, "tm_reset_beam_latch", 0x213F);
}

/* The field's parity, bit 7 of $213F, read as the scenes read it: a 16-bit
 * read of $213E, whose high byte is $213F's (and which restarts the beam
 * position latch). The word read is kept for tm_snes_odd_frame_read. */
extern uint16_t pf_snes_odd_frame_word;

PF_API bool tm_odd_frame(Platform *pf, PfOrigin o)
{
    pf_snes_begin("tm_odd_frame", o);
    uint8_t low = pf_snes_get(pf_snes(pf), 0x213E);
    uint8_t high = pf_snes_get(pf_snes(pf), 0x213F);
    pf_snes_end();
    pf_snes_odd_frame_word = (uint16_t)(low | high << 8);
    return (high & 0x80) != 0;
}

/* SNES only: the word the last tm_odd_frame read, $213E's byte low and
 * $213F's high, for scaffolding that leaves it where the original's read did. */
static inline uint16_t tm_snes_odd_frame_read(void) { return pf_snes_odd_frame_word; }

PF_API void tm_sync(Platform *pf) { native_sync(pf_snes(pf)); }

/* ---- The frame's waits and its budget ------------------------------------------------------- */

/* The original's own wait routine, called from where the game called it: a
 * JSL where it lies in another bank, a JSR in the caller's own. The caller
 * goes on once the routine has returned, frames later. */
static inline void pf_snes_call_wait(Platform *pf, PfOrigin o)
{
    bool long_call = (o.site >> 16) != (o.target >> 16);
    native_call_dc(pf_snes(pf), o.site, o.target, long_call, o.dont_care);
}

PF_API void tm_wait_frame(Platform *pf, PfOrigin o) { pf_snes_call_wait(pf, o); }

/* The battle's wait for a number of frames ($CD:3E3B) takes the count in A. */
PF_API void tm_wait_frames(Platform *pf, PfOrigin o, uint8_t frames)
{
    m_set_a8(pf_snes(pf), frames);
    pf_snes_call_wait(pf, o);
}

/* The wait inside a mode's own wait routine: the machine takes its
 * interrupts where the port stands, and no original code runs; shadow mode
 * lets the original's loop run and compares the port's passes against it
 * (native_wait_loop). */
PF_API void tm_wait_for_handlers(Platform *pf, PfOrigin o)
{
    native_wait_loop(pf_snes(pf), o.site, o.target, NATIVE_WAKE_HANDLER);
}

/* The same where the shipping mode leaves the wait to the loop's idle entry,
 * which a machine copy holds: the port stops at the loop's head and returns
 * at once (native_wait_loop_held). */
PF_API bool tm_wait_for_handlers_held(Platform *pf, PfOrigin o)
{
    return native_wait_loop_held(pf_snes(pf), o.site, o.target, NATIVE_WAKE_HANDLER, o.dont_care);
}

/* The line handlers' wait for their line's horizontal blank: the machine
 * steps where the port stands until the line is in it, and no original code
 * runs; shadow mode lets the original's loop on $4212 run (native_wait_loop). */
PF_API void tm_wait_line_drawn(Platform *pf, PfOrigin o)
{
    native_wait_loop(pf_snes(pf), o.site, o.target, NATIVE_WAKE_HBLANK);
}

/* The original's routines wait on the vblank's flag and then turn the
 * handlers on themselves: the vblank handler and the controllers' reading,
 * with the field's line handler at line 211 ($C0:0B38), or, the time warp's,
 * no line handler but the picture turned on, dark ($FD:EADA). A call asking for
 * anything else has no routine of the original's to call. */
enum { SNES_SCREEN_ON_SOURCES = TM_INT_VBLANK | TM_INT_AUTO_JOYPAD, SNES_SCREEN_ON_LINE = 211 };

PF_API void tm_wait_frame_then_enable(Platform *pf, PfOrigin o, uint8_t sources, uint16_t line)
{
    if (sources != SNES_SCREEN_ON_SOURCES || (line != SNES_SCREEN_ON_LINE && line != TM_LINE_UNCHANGED))
        pf_snes_encoding_mismatch(o.site, "tm_wait_frame_then_enable");
    pf_snes_call_wait(pf, o);
}

/* The battle's wait for a line ($CD:3B82) takes the line in A, and only
 * waits: the machine waits where the port stands, the line left in A as the
 * original's compare leaves the line it reached; shadow mode and the tools
 * call the original's routine, whose run counts as the port's
 * (native_wait_call). */
PF_API void tm_wait_line(Platform *pf, PfOrigin o, uint16_t line)
{
    Snes *s = pf_snes(pf);
    s->cpu.a = line;
    native_wait_call(s, o.site, o.target, (o.site >> 16) != (o.target >> 16), o.dont_care, line);
}

/* The vertical counter, latched and read as the field's budgets read it: the
 * latch's read toggle restarted, then the counter latched ($2137) and read a
 * byte at a time ($213D twice: the low byte, then the ninth bit). Shadow mode
 * reads what the original latches (native_await_latch). */
static inline uint16_t pf_snes_beam_line(Snes *s)
{
    pf_snes_get(s, 0x213F);
    pf_snes_get(s, 0x2137);
    native_await_latch(s);
    uint8_t low = pf_snes_get(s, 0x213D);
    uint8_t high = pf_snes_get(s, 0x213D);
    return (uint16_t)(low | (high & 0x01) << 8);
}

/* The original's budgets stop deferrable work once the beam is at `line` or
 * below it in the picture, and go on again from line 240, in the vertical
 * blank. Set on the SNES backend (ct_drive --drain-budgets), the frame has
 * time for all of it, as on a host: the routes are checked so. */
enum { PF_SNES_BLANK_FROM_LINE = 240 };
extern bool pf_snes_drain_budgets;

PF_API bool tm_frame_time_left(Platform *pf, PfOrigin o, uint16_t line)
{
    pf_snes_begin("tm_frame_time_left", o);
    uint16_t beam = pf_snes_beam_line(pf_snes(pf));
    pf_snes_end();
    return pf_snes_drain_budgets || beam >= PF_SNES_BLANK_FROM_LINE || beam < line;
}

/* The scenes' noise, read as their stir ($C3:02DD) reads it, a byte or a word
 * at a time: the counters latched by a compare with $2137, then an add of the
 * horizontal counter ($213C) and two subtracts of the vertical ($213D). A
 * 16-bit read takes the next register's byte as its high byte ($2138, the
 * sprite memory, which steps its address; $213D; $213E), and $2137 reads as
 * the bus's last byte, the compare's operand's high byte. Shadow mode reads
 * what the original latches (native_await_latch). */
enum { PF_SNES_LATCH_OPERAND_HIGH = 0x21 };

static inline uint16_t pf_snes_entropy_read(Snes *s, uint16_t reg, TmEntropyWidth width)
{
    uint8_t low = pf_snes_get(s, reg);
    return width == TM_ENTROPY_WORDS ? (uint16_t)(low | pf_snes_get(s, (uint16_t)(reg + 1)) << 8) : low;
}

PF_API TmEntropy tm_entropy(Platform *pf, PfOrigin o, TmEntropyWidth width)
{
    Snes *s = pf_snes(pf);
    TmEntropy e;
    pf_snes_begin("tm_entropy", o);
    s->bus.open_bus = PF_SNES_LATCH_OPERAND_HIGH;
    uint32_t latches = s->ppu.latch_count;
    e.value[0] = pf_snes_entropy_read(s, 0x2137, width);
    if (s->ppu.latch_count != latches)   /* the read latches only where the game has latching on */
        native_await_latch(s);
    e.value[1] = pf_snes_entropy_read(s, 0x213C, width);
    e.value[2] = pf_snes_entropy_read(s, 0x213D, width);
    e.value[3] = pf_snes_entropy_read(s, 0x213D, width);
    pf_snes_end();
    return e;
}

/* ---- SNES only: the status the original's wait loops poll ---------------------------------- */

/* The idle entries that run the original's loops on the beam a pass at a
 * time are the SNES backend's half of the waits above and of the line
 * handlers' end of the picture: a host has no beam to poll, and its handlers
 * run after the picture rather than at a line of it (docs/PLATFORM.md,
 * "Timing without a beam"). They read $4212 here. */
enum { TM_SNES_IN_VBLANK = 0x80, TM_SNES_IN_HBLANK = 0x40, TM_SNES_JOYPAD_BUSY = 0x01 };

static inline uint8_t tm_snes_video_status(Platform *pf, PfOrigin o)
{
    return pf_snes_status_read(pf, o, "tm_snes_video_status", 0x4212);
}

#endif
