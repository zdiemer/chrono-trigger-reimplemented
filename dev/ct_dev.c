/* ct_dev: runs the ROM on the development system (interpreter CPU and modeled
 * hardware) from an input movie, writing the same .cttr traces as ct_oracle so
 * the two can be diffed with tools/trace.py.
 *
 *   ct_dev --rom roms/ct-usa.sfc --frames 1800 [--movie m.ctm] [--trace out.cttr]
 *          [--shot-every N --shot-dir dir] [--coverage out.txt] [--status-every N]
 *          [--native off|shadow|on] [--calls out.txt] [--load-state in.state] [--save-state out.state]
 *          [--end-state out.bin]
 *          [--sram save.srm] [--wav out.wav] [--exclude name-prefix]... [--generated off|on|only]
 *          [--insn-stats] [--io-log out.txt] [--misses out.txt] [--no-interpreter strict|fallback]
 *          [--audio snes|direct] [--load-zst in.zst [--frame F]] [--vid-check [out.txt]] [--vid-check-from N]
 *          [--px-check [out.txt]] [--spc-coverage out.txt] [--dsp-log out.txt] [--timing-report]
 *          [--page-report] [--event-reads out.txt] [--sound-lockstep [out.txt]]
 *          [--sound-lockstep-level status|keys|dsp|state] [--sound-lockstep-audio exact|host]
 *          [--sound-lockstep-wav prefix] [--sound-driver original|c] [--mixer dsp|native|check]
 *          [--scaffold-reads [original]] [--fallbacks out.txt] [--apu-port-margins]
 *          [--as-port BBAAAA[,BBAAAA...]] [--poison-site BBAAAA]... [--poison-return BBAAAA]... [--poison-set AXYNVZC]
 *          [--index-check BBAAAA]... [--asset-reads out.txt]
 *
 * The framebuffer is only drawn for what reads it (--trace, --shot-every,
 * --px-check); otherwise each line computes just the sprite flags the game can
 * read ($213E), which leaves the machine as drawing would.
 * --native shadow runs the original code and checks each call of a ported
 * routine against its native version; --native on runs the ported routines.
 * --calls writes every executed JSR/JSL/JSR(abs,X) call site and target, so the
 * disassembler's liveness analysis knows about callers it can't see statically.
 * --load-state resumes from a state saved by --save-state (docs/RUNTIME.md, "Save states");
 * frame numbers, and so movie input, continue from the saved frame.
 * --end-state writes work RAM and then save RAM as they stand when the run ends
 * (raw, SNES_WRAM_SIZE and SNES_SRAM_SIZE bytes): what tools/strict_check.py
 * compares a run's end with the reference play's by. ROM-derived; keep it out
 * of the repository.
 * --load-zst starts from a ZSNES 1.51 save state (snes/zst.h) at frame F (--frame,
 * default 0; --frames stays the frame to stop before). Such a run can't be
 * replayed from power-on and the import approximates some state, so it's for
 * coverage, miss reports and exploring, never for validation.
 * --sram starts with that cartridge save (8 KiB) in place of blank SRAM; a movie
 * recorded from a save names its CRC-32 and must be replayed with the same one.
 * --wav writes the DSP's output (32000 Hz stereo) over the frames run.
 * --generated on (the default when the build generated blocks from this ROM)
 * registers the generated blocks after the ported routines (docs/GENERATED.md);
 * --generated only registers the generated blocks without the ported routines, for
 * checking that --native on produces the same traces as --native off.
 * --page-report prints, under each hand port in the native report, the direct
 * pages (D) its calls were made with and how many of each: the evidence for the
 * page instances in re/ram.toml (docs/DISASSEMBLY.md, "The accessors"). Every
 * run prints a "page refused" line for each page kind whose <page>_page_of
 * turned a D away (runtime/port/vars_pages.h).
 * --event-reads writes every read of what an event command leaves beyond its
 * protocol -- Y, the V flag, the field page's event cells $C1-$E4 -- by the command
 * and the reader (dev/event_reads.h; meaningful with --native off --generated off).
 * --insn-stats reports how many instructions the interpreter and generated
 * blocks executed.
 * --io-log writes every hardware register access counted by what made it
 * (dev/io_log.h; meaningful with --native on).
 * --misses writes every instruction the interpreter ran, with counts, as ROM or
 * RAM code and the RAM section it lies in (dev/misses.h; the miss report with
 * --native on --generated only).
 * --vid-check [out.txt] compares the VidFrame the video service calls build with
 * the one decoded from the PPU registers, frame by frame (dev/vid_check.h,
 * docs/PLATFORM.md P2); it needs --native on. --vid-check-from N seeds the
 * observer from the PPU at frame N and compares from there on, so a subsystem
 * can be checked from the frame it takes over.
 * --px-check [out.txt] draws every frame a second time with the native renderer
 * (platform/native/render.h), from the VidFrame the decoder builds, and compares
 * it with render.c's pixel for pixel (dev/px_check.h, docs/PLATFORM.md P3). It
 * works in any --native mode, and a run with a differing frame exits 1.
 * --no-interpreter strict|fallback (with --native on) runs the system loop the
 * way a player built without the interpreter does (docs/GENERATED.md, "Without the
 * interpreter"): interrupt entry, WAI and STP without the interpreter, and an
 * instruction no native code runs stops the run (strict, exit status 1) or runs
 * on the interpreter and is reported (fallback: each place the interpreter is
 * entered at, the first time, with its frame, up to 400 of them).
 * --audio direct selects the APU-direct audio backend (platform/direct/audio.h):
 * the sound driver's upload and commands take no main-CPU time. It needs
 * --native on with the ported routines, and can't keep the original's timing,
 * so it isn't for validation; snes (the default) is the SNES backend.
 * --spc-coverage writes every SPC700 address the sound driver executed an
 * instruction at, with its count (docs/SOUND.md; tools/spc700dis.py reads it).
 * --dsp-log writes every S-DSP register write the driver makes: the frame, the
 * SPC700 cycle, the register and the value (docs/SOUND.md, "Validating the port").
 * --sound-lockstep [out.txt] runs the C sound driver (game/sound/) beside the
 * original, a tick for each of its ticks, and compares the two after every tick
 * (dev/sound_lockstep.h): the status, and with --sound-lockstep-level keys or
 * dsp (the default) the key on and off writes, and then the S-DSP's registers and
 * the driver's own sample; state also compares the two drivers' state
 * (dev/sound_state.h), to find where they part. It prints a summary, writes
 * each differing tick to out.txt, and a run that differs exits 1. It needs a
 * run from power-on.
 * --sound-lockstep-audio exact|host also plays the C driver's writes into a
 * second S-DSP over the same sound memory and compares its every sample with
 * the original's (dev/sound_audio.h): exact lands each write where the
 * original made the same one (a measurement: a difference doesn't fail the run); host
 * lands a tick's writes where the tick starts, as ct_play's C driver does, to
 * hear what that changes. --sound-lockstep-wav prefix writes both outputs as
 * prefix-c.wav and prefix-original.wav.
 * --sound-driver c plays the C sound driver in the SPC700's place, as ct_play
 * does by default (frontend/sound_host.h), with --native on and the ported
 * routines. The game's timing is then not the original's, so a movie drifts;
 * it is for hearing the C driver over a movie (--wav) and for the host's own
 * checks: a run in which a sample was placed where the echo was writing, or a
 * command dropped from the host's full queue, exits 1 (sound_host_report).
 * --mixer (only with --sound-driver c) picks what makes the sound: dsp, the
 * emulated S-DSP (the default); native, the native mixer in its place; check,
 * both, the S-DSP heard (--wav) and the mixer compared with it frame by frame
 * (a `mixer check:` line; a run in which a frame differs exits 1).
 * --shadow-details N describes up to N mismatches in the native report, and
 * apart from them up to N notes (abandoned comparisons, timing), mismatches
 * first (default 16 each, at most 256).
 * --scaffold-reads (with --native on) prints a `scaffold read:` line for each
 * work RAM byte a port read whose latest write was scaffolding, a port's inside
 * ORIGINAL_EFFECT: what the native build would read stale (runtime/port/
 * scaffold_reads.h, tools/effects_check.py --runs). --scaffold-reads original
 * also prints a `scaffold read by the original:` line for each read the
 * original's code made of such a byte, by the instruction: what the
 * still-generated code reads of the scaffolding, which is where a failure of
 * the native-shape build is traced (not with --event-reads, whose observer it
 * takes).
 * --fallbacks out.txt (with --native on or shadow) counts every instruction
 * the original's code executes in place of hand ports -- generated or
 * interpreted -- by address and why: a call every hand entry refused, what a
 * port handed back (a call-out's callee, the rest after it stopped), or no
 * port at all; and records each refusal with its first few machine states
 * (dev/fallbacks.h). In shadow mode the original runs everything and what the
 * ports stand for isn't counted, so a movie measures its own play, the same
 * whatever the ports' timing (the gate's ratchet); in the shipping mode it
 * measures where the run went. tools/fallbacks.py reads the file.
 * --asset-reads out.txt writes every cartridge address the ports' accessors
 * read, a `BB:AAAA-BB:AAAA` range a line, which tools/assets.py builds the
 * catalogue of resources from. Whatever the options, a run whose accessors
 * read a byte the catalogue doesn't hold prints an `asset outside the
 * catalogue` line (dev/asset_report.h), and tools/suite.py fails it.
 * --apu-port-margins prints, at the end, how long after the SPC700 read a
 * message's last two bytes (ports 2-3, after its echo) the CPU's next ones
 * came, the least and their histogram, and how many came before it read them
 * (dev/apu_margins.h): the margin the CPU half's waits leave its late reads.
 * Every run also prints a `halted:` line if the machine ended stopped (STP, or
 * without the interpreter at an instruction nothing runs), which the gate's
 * native-shape check fails on.
 * --as-port BBAAAA[,...] runs each of those routines of the original as one
 * hand port would run: all at once with time stopped, its time owed after
 * (dev/as_port.h). For trying what the shipping mode does with a port of a
 * routine before writing it, a routine that spans frames above all.
 * --poison-site BBAAAA (with --native on) scrambles, at each call-out a hand
 * port makes from that site with a don't-care set, the registers and flags the
 * set names (native_site_poison); --poison-set AXYNVZC (any of those letters)
 * scrambles only those of the set. A declaration that holds leaves the run's
 * trace as it is without the scrambling; one the original's code reads changes
 * it. It prints each site's calls and how many had the overflow flag inverted.
 * --poison-return BBAAAA (with --native on) does the same where the hand port
 * registered at that address returns to the original's code, to the set its
 * entry declares (native_return_poison), and prints each entry's returns, how
 * many of them were to a bridge that ran it as C in place of a call (a
 * sibling's), and how many had the carry inverted.
 * --index-check BBAAAA (with --native off --generated off, where the interpreter
 * runs everything) notes the X register each time the instruction there runs,
 * and prints the largest and how often it was past $FE: the check of a jump
 * table's `index_reach` hint (re/hints.toml). Both are the evidence for a
 * declaration liveness can't prove (docs/PORTING.md, "Declarations liveness
 * can't prove"; tools/dc_evidence.py runs them).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apu_margins.h"
#include "event_reads.h"
#include "fallbacks.h"
#include "io_log.h"
#include "misses.h"
#include "px_check.h"
#include "asset_report.h"
#include "range_report.h"
#include "sound_lockstep.h"
#include "sound_audio.h"
#include "mixer_check.h"
#include "sound_host.h"
#include "vid_check.h"
#include "media.h"
#include "movie.h"
#include "natives.h"
#include "sram_find.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/scaffold_reads.h"
#include "port/vars_pages.h"
#include "snes/render.h"
#include "as_port.h"
#include "snes/system.h"
#include "snes/zst.h"
#include "trace.h"

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0755)
#endif

static IoLog *io_log;
static EventReads *event_reads;
static Misses *misses;
static Fallbacks *fallbacks;
static bool scaffold_reads_original;   /* --scaffold-reads original */
static VidCheck *vid_check;
static PxCheck *px_check;

/* --no-interpreter: 0 off, 1 fallback, 2 strict. */
static int no_interpreter;
static unsigned long miss_steps;

/* --no-interpreter fallback names each place the interpreter is entered at, the
 * first time it is: the start of a run of misses, with the frame, which is
 * where --no-interpreter strict would stop if the earlier ones were ported. */
enum { MISS_STARTS_SEEN = 1 << 13, MISS_STARTS_NAMED = 400 };
static uint32_t miss_starts[MISS_STARTS_SEEN];
static uint32_t miss_run_end;

static bool first_time_entered(uint32_t pc)
{
    for (uint32_t i = (pc * 0x9E3779B1u) >> 19;; i = (i + 1) & (MISS_STARTS_SEEN - 1)) {
        if (miss_starts[i] == pc + 1)
            return false;
        if (!miss_starts[i]) {
            miss_starts[i] = pc + 1;
            return true;
        }
    }
}

static bool on_miss(void *ctx, Snes *snes)
{
    (void)ctx;
    char what[160];
    uint32_t pc = cpu_pc24(&snes->cpu);
    bool m8 = snes->cpu.e || (snes->cpu.p & CPU_FLAG_M), x8 = snes->cpu.e || (snes->cpu.p & CPU_FLAG_X);
    bool starts_a_run = pc != miss_run_end;
    miss_run_end = pc + (uint32_t)misses_instruction_length(snes_bus_peek(&snes->bus, pc), m8, x8);
    if (no_interpreter == 2) {
        misses_describe(snes, what, sizeof what);
        fprintf(stderr, "frame %u: no generated code for %s; stopping (--no-interpreter strict)\n",
                (unsigned)snes->frame, what);
        return false;
    }
    if (starts_a_run && miss_steps < MISS_STARTS_NAMED && first_time_entered(pc)) {
        miss_steps++;
        misses_describe(snes, what, sizeof what);
        fprintf(stderr, "frame %u: no generated code for %s; interpreting it\n", (unsigned)snes->frame, what);
    }
    return true;   /* --misses counts it through the instruction hook */
}

static void on_frame_top(void *ctx, Snes *snes)
{
    (void)ctx;
    (void)snes;
    if (vid_check)
        vid_check_frame_top(vid_check);
}

/* Whether anything reads the framebuffer: a trace (its hash), screenshots or
 * --px-check. Without them lines only get the sprite flags the game can read
 * (render_line_flags). */
static bool draw_pixels;

static void on_line(void *ctx, Snes *snes, uint16_t line)
{
    if (draw_pixels)
        render_line(ctx, &snes->ppu, line);
    else
        render_line_flags(ctx, &snes->ppu, line);
    if (vid_check)
        vid_check_line(vid_check, line);
    if (px_check)
        px_check_line(px_check, line);
    if (io_log)
        io_log_note_hdma(io_log);
}

/* Executed (address, M, X) triples, for seeding the disassembler. */
#define COVERAGE_BITS 26   /* 24-bit address + M + X */

#define CALL_SET_BITS 20

typedef struct {
    uint8_t *seen;       /* bitmap over COVERAGE_BITS, or NULL */
    uint64_t *calls;     /* open-addressed set of site << 24 | target, or NULL */
    uint64_t insns;      /* instructions the interpreter executed */
    uint64_t ram_insns;  /* of those, code in RAM (banks $7E-$7F, or below $8000 in banks $00-$3F/$80-$BF) */
    Snes *snes;
} Coverage;

static void record_call(Coverage *cov, uint32_t site, uint32_t target)
{
    uint64_t key = (uint64_t)site << 24 | target | (uint64_t)1 << 63;
    size_t mask = ((size_t)1 << CALL_SET_BITS) - 1;
    for (size_t i = (size_t)(key * 0x9E3779B97F4A7C15ull >> 40) & mask;; i = (i + 1) & mask) {
        if (cov->calls[i] == key)
            return;
        if (cov->calls[i] == 0) {
            cov->calls[i] = key;
            return;
        }
    }
}

/* --index-check: the instructions named, how often the interpreter ran each,
 * the largest X it ran with, and how often X was past the limit. */
typedef struct {
    uint32_t pc;
    unsigned long runs, over;
    uint16_t largest;
} IndexCheck;
static IndexCheck index_checks[16];
static int nindex_checks;
static uint16_t index_limit = 0xFE;

static void note_index(uint32_t pc, uint16_t x)
{
    for (int i = 0; i < nindex_checks; i++) {
        if (index_checks[i].pc != pc)
            continue;
        index_checks[i].runs++;
        if (x > index_checks[i].largest)
            index_checks[i].largest = x;
        if (x > index_limit)
            index_checks[i].over++;
    }
}

static void on_insn(void *ctx, uint32_t pc, uint8_t p, bool e)
{
    Coverage *cov = ctx;
    if (nindex_checks)
        note_index(pc, cov->snes->cpu.x);
    if (io_log)
        io_log_note_instruction(io_log, pc);
    if (event_reads)
        event_reads_note_instruction(event_reads, pc);
    if (misses)
        misses_note(misses, pc, p, e);
    if (fallbacks)
        fallbacks_note(fallbacks, pc, p, e);
    if (scaffold_reads_original)
        scaffold_reads_note_pc(pc);
    bool m = e || (p & CPU_FLAG_M), x = e || (p & CPU_FLAG_X);
    cov->insns++;
    if ((pc >> 16 & 0xFE) == 0x7E || (!(pc & 0x400000) && (pc & 0xFFFF) < 0x8000))
        cov->ram_insns++;
    if (cov->seen) {
        uint32_t key = pc << 2 | (uint32_t)m << 1 | (uint32_t)x;
        cov->seen[key >> 3] |= (uint8_t)(1 << (key & 7));
    }
    if (cov->calls) {
        const SnesBus *bus = &cov->snes->bus;
        uint8_t op = snes_bus_peek(bus, pc);
        if (op == 0x20) {
            record_call(cov, pc, (pc & 0xFF0000) | snes_bus_peek(bus, pc + 1) | snes_bus_peek(bus, pc + 2) << 8);
        } else if (op == 0x22) {
            record_call(cov, pc, snes_bus_peek(bus, pc + 1) | snes_bus_peek(bus, pc + 2) << 8 |
                                     (uint32_t)snes_bus_peek(bus, pc + 3) << 16);
        } else if (op == 0xFC) {
            uint16_t ptr = (uint16_t)((snes_bus_peek(bus, pc + 1) | snes_bus_peek(bus, pc + 2) << 8) +
                                      cov->snes->cpu.x);
            uint32_t bank = pc & 0xFF0000;
            record_call(cov, pc, bank | snes_bus_peek(bus, bank | ptr) |
                                     snes_bus_peek(bus, bank | (uint16_t)(ptr + 1)) << 8);
        }
    }
}

/* --spc-coverage, --dsp-log and --sound-lockstep: the SPC700's side, through the APU's hooks. */
typedef struct {
    uint32_t *counts;   /* instructions executed per SPC700 address, or NULL */
    FILE *dsp_log;
    SoundLockstep *lockstep;
    const Snes *snes;
} SpcWatch;

static void on_spc_insn(void *ctx, const Apu *apu)
{
    SpcWatch *w = ctx;
    if (w->counts && w->counts[apu->spc.pc] != UINT32_MAX)
        w->counts[apu->spc.pc]++;
    if (w->lockstep)
        sound_lockstep_insn(w->lockstep, apu);
}

static void on_dsp_write(void *ctx, const Apu *apu, uint8_t reg, uint8_t value)
{
    SpcWatch *w = ctx;
    if (w->dsp_log)
        fprintf(w->dsp_log, "%u %llu %02X %02X\n", (unsigned)w->snes->frame, (unsigned long long)apu->spc_cycles,
                reg, value);
    if (w->lockstep)
        sound_lockstep_dsp_write(w->lockstep, apu, reg, value);
}

static int write_spc_coverage(const SpcWatch *w, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# SPC700 addresses executed: AAAA count\n");
    for (uint32_t pc = 0; pc < 0x10000; pc++) {
        if (w->counts[pc])
            fprintf(f, "%04X %u\n", (unsigned)pc, (unsigned)w->counts[pc]);
    }
    return fclose(f);
}

static int write_calls(const Coverage *cov, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# executed calls: <site BB:AAAA> <target BB:AAAA>\n");
    for (size_t i = 0; i < (size_t)1 << CALL_SET_BITS; i++) {
        uint64_t k = cov->calls[i];
        if (!k)
            continue;
        uint32_t site = (uint32_t)(k >> 24) & 0xFFFFFF, target = (uint32_t)k & 0xFFFFFF;
        fprintf(f, "%02X:%04X %02X:%04X\n", site >> 16, site & 0xFFFF, target >> 16, target & 0xFFFF);
    }
    return fclose(f);
}

static int write_coverage(const Coverage *cov, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# executed instructions: BB:AAAA m x (8 or 16)\n");
    size_t n = (size_t)1 << (COVERAGE_BITS - 3);
    for (size_t i = 0; i < n; i++) {
        if (!cov->seen[i])
            continue;
        for (int b = 0; b < 8; b++) {
            if (!(cov->seen[i] & (1 << b)))
                continue;
            uint32_t key = (uint32_t)(i << 3 | b);
            uint32_t pc = key >> 2;
            fprintf(f, "%02X:%04X %d %d\n", pc >> 16, pc & 0xFFFF, (key & 2) ? 8 : 16, (key & 1) ? 8 : 16);
        }
    }
    return fclose(f);
}

/* --end-state: work RAM, then save RAM. */
static int write_end_state(const char *path, const Snes *snes)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    bool ok = fwrite(snes->bus.wram, 1, SNES_WRAM_SIZE, f) == SNES_WRAM_SIZE
              && fwrite(snes->bus.sram, 1, SNES_SRAM_SIZE, f) == SNES_SRAM_SIZE;
    return fclose(f) == 0 && ok ? 0 : -1;
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(n > 0 ? (size_t)n : 1);
    if (n <= 0 || fread(data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return data;
}

/* A D that a <page>_page_of refused: a guard turned a call away because its
 * direct page is no instance re/ram.toml declares. In a movie that is a call
 * the game makes, so either the map is missing an instance or the port runs on
 * a page it shouldn't; tools/suite.py fails a movie that prints one. */
static void report_page_refusals(void)
{
    unsigned n;
    const VarsPageRefusals *r = vars_page_refusals(&n);
    for (unsigned i = 0; i < n; i++)
        if (r[i].refused)
            fprintf(stderr, "page refused: %s %lu (last D $%04X)\n", r[i].page, r[i].refused,
                    (unsigned)r[i].last_d);
}

/* --poison-site: the call sites named, how many calls each made and in how
 * many the overflow flag was among what was scrambled (so inverted). */
typedef struct {
    uint32_t site;
    unsigned long calls, v_inverted;
} PoisonSite;
static PoisonSite poison_sites[16];
static int npoison_sites;
static uint64_t poison_state = 0x9E3779B97F4A7C15ull;
static uint16_t poison_set = 0xFFFF;

/* --poison-return: the entries named, how many returns each made, how many
 * of them to a bridge, and in how many the carry was among what was
 * scrambled (so inverted). */
typedef struct {
    uint32_t entry;
    unsigned long returns, to_bridges, c_inverted;
} PoisonReturn;
static PoisonReturn poison_returns[16];
static int npoison_returns;

static uint64_t poison_next(void)
{
    poison_state ^= poison_state << 13;
    poison_state ^= poison_state >> 7;
    poison_state ^= poison_state << 17;
    return poison_state;
}

static bool poison_return(uint32_t entry, bool to_a_bridge, uint64_t *bits, uint16_t *set)
{
    for (int i = 0; i < npoison_returns; i++) {
        if (poison_returns[i].entry != entry)
            continue;
        *set &= poison_set;
        *bits = poison_next();
        poison_returns[i].returns++;
        poison_returns[i].to_bridges += to_a_bridge;
        if (*set & DC_C)
            poison_returns[i].c_inverted++;
        return *set != 0;
    }
    return false;
}

static bool poison_site(uint32_t site, uint32_t target, uint64_t *bits, uint16_t *set)
{
    (void)target;
    for (int i = 0; i < npoison_sites; i++) {
        if (poison_sites[i].site != site)
            continue;
        *set &= poison_set;
        *bits = poison_next();
        poison_sites[i].calls++;
        if (*set & DC_V)
            poison_sites[i].v_inverted++;
        return *set != 0;
    }
    return false;
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *movie_path = NULL, *trace_path = NULL, *coverage_path = NULL;
    const char *calls_path = NULL, *load_state = NULL, *save_state = NULL, *sram_path = NULL, *end_state = NULL;
    bool c_sound_driver = false;
    SoundMixer mixer = SOUND_MIXER_DSP;
    bool mixer_given = false;
    const char *shot_dir = NULL, *wav_path = NULL, *io_log_path = NULL, *misses_path = NULL;
    const char *zst_path = NULL, *vid_check_path = NULL, *px_check_path = NULL;
    const char *spc_coverage_path = NULL, *dsp_log_path = NULL, *event_reads_path = NULL;
    const char *lockstep_path = NULL, *fallbacks_path = NULL, *asset_reads_path = NULL;
    bool want_vid_check = false, want_px_check = false, want_lockstep = false, want_margins = false;
    SoundCompareLevel lockstep_level = SOUND_COMPARE_DSP;
    int audio_timing = -1;
    const char *audio_wav = NULL;
    long vid_check_from = 0;
    long start_frame = -1;
    NativeMode native_mode = NATIVE_OFF;
    bool use_generated = true, generated_only = false, insn_stats = false, audio_direct = false;
    long frames = -1, status_every = 0, shot_every = 0;
    const char *excludes[64], *includes[64];
    int nexcludes = 0, nincludes = 0;
    const char *as_ports = NULL;
    int shadow_details = -1;
    PortTiming port_timing = PORT_TIMING_ESTIMATE;
    bool timing_report = false, page_report = false, port_horizon_interrupts = false, scaffold_reads = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--timing-report")) {
            timing_report = true;
            continue;
        }
        if (!strcmp(a, "--page-report")) {
            page_report = true;
            continue;
        }
        if (!strcmp(a, "--insn-stats")) {
            insn_stats = true;
            continue;
        }
        if (!strcmp(a, "--scaffold-reads")) {
            scaffold_reads = true;
            if (v && !strcmp(v, "original")) {
                scaffold_reads_original = true;
                i++;
            }
            continue;
        }
        if (!strcmp(a, "--vid-check") && (!v || v[0] == '-')) {
            want_vid_check = true;
            continue;
        }
        if (!strcmp(a, "--px-check") && (!v || v[0] == '-')) {
            want_px_check = true;
            continue;
        }
        if (!strcmp(a, "--sound-lockstep") && (!v || v[0] == '-')) {
            want_lockstep = true;
            continue;
        }
        if (!strcmp(a, "--apu-port-margins")) {
            want_margins = true;
            continue;
        }
        if (!v) {
            fprintf(stderr, "missing value for %s\n", a);
            return 2;
        }
        if (!strcmp(a, "--rom")) rom_path = v;
        else if (!strcmp(a, "--frames")) frames = strtol(v, NULL, 10);
        else if (!strcmp(a, "--movie")) movie_path = v;
        else if (!strcmp(a, "--trace")) trace_path = v;
        else if (!strcmp(a, "--coverage")) coverage_path = v;
        else if (!strcmp(a, "--calls")) calls_path = v;
        else if (!strcmp(a, "--load-state")) load_state = v;
        else if (!strcmp(a, "--load-zst")) zst_path = v;
        else if (!strcmp(a, "--frame")) start_frame = strtol(v, NULL, 10);
        else if (!strcmp(a, "--save-state")) save_state = v;
        else if (!strcmp(a, "--end-state")) end_state = v;
        else if (!strcmp(a, "--sram")) sram_path = v;
        else if (!strcmp(a, "--status-every")) status_every = strtol(v, NULL, 10);
        else if (!strcmp(a, "--shot-every")) shot_every = strtol(v, NULL, 10);
        else if (!strcmp(a, "--shot-dir")) shot_dir = v;
        else if (!strcmp(a, "--wav")) wav_path = v;
        else if (!strcmp(a, "--sound-driver")) c_sound_driver = !strcmp(v, "c");
        else if (!strcmp(a, "--mixer")) {
            if (!sound_mixer_named(v, &mixer)) {
                fprintf(stderr, "--mixer must be dsp, native or check\n");
                return 2;
            }
            mixer_given = true;
        }
        else if (!strcmp(a, "--spc-coverage")) spc_coverage_path = v;
        else if (!strcmp(a, "--dsp-log")) dsp_log_path = v;
        else if (!strcmp(a, "--sound-lockstep")) { lockstep_path = v; want_lockstep = true; }
        else if (!strcmp(a, "--sound-lockstep-audio")) {
            if (!strcmp(v, "exact")) audio_timing = SOUND_AUDIO_EXACT;
            else if (!strcmp(v, "host")) audio_timing = SOUND_AUDIO_HOST;
            else {
                fprintf(stderr, "--sound-lockstep-audio must be exact or host\n");
                return 2;
            }
            want_lockstep = true;
        }
        else if (!strcmp(a, "--sound-lockstep-wav")) audio_wav = v;
        else if (!strcmp(a, "--sound-lockstep-level")) {
            if (!strcmp(v, "status")) lockstep_level = SOUND_COMPARE_STATUS;
            else if (!strcmp(v, "keys")) lockstep_level = SOUND_COMPARE_KEYS;
            else if (!strcmp(v, "dsp")) lockstep_level = SOUND_COMPARE_DSP;
            else if (!strcmp(v, "state")) lockstep_level = SOUND_COMPARE_STATE;
            else {
                fprintf(stderr, "--sound-lockstep-level must be status, keys, dsp or state\n");
                return 2;
            }
            want_lockstep = true;
        }
        else if (!strcmp(a, "--io-log")) io_log_path = v;
        else if (!strcmp(a, "--event-reads")) event_reads_path = v;
        else if (!strcmp(a, "--misses")) misses_path = v;
        else if (!strcmp(a, "--fallbacks")) fallbacks_path = v;
        else if (!strcmp(a, "--asset-reads")) asset_reads_path = v;
        else if (!strcmp(a, "--vid-check")) { vid_check_path = v; want_vid_check = true; }
        else if (!strcmp(a, "--vid-check-from")) { vid_check_from = strtol(v, NULL, 10); want_vid_check = true; }
        else if (!strcmp(a, "--px-check")) { px_check_path = v; want_px_check = true; }
        else if (!strcmp(a, "--exclude") && nexcludes < 64) excludes[nexcludes++] = v;
        else if (!strcmp(a, "--include") && nincludes < 64) includes[nincludes++] = v;
        else if (!strcmp(a, "--as-port")) as_ports = v;
        else if (!strcmp(a, "--poison-site") && npoison_sites < 16)
            poison_sites[npoison_sites++].site = (uint32_t)strtoul(v, NULL, 16);
        else if (!strcmp(a, "--poison-return") && npoison_returns < 16)
            poison_returns[npoison_returns++].entry = (uint32_t)strtoul(v, NULL, 16);
        else if (!strcmp(a, "--index-check") && nindex_checks < 16)
            index_checks[nindex_checks++].pc = (uint32_t)strtoul(v, NULL, 16);
        else if (!strcmp(a, "--poison-set")) {
            poison_set = 0;
            for (const char *c = v; *c; c++) {
                switch (*c) {
                case 'A': poison_set |= DC_A; break;
                case 'X': poison_set |= DC_X; break;
                case 'Y': poison_set |= DC_Y; break;
                case 'N': poison_set |= DC_N; break;
                case 'V': poison_set |= DC_V; break;
                case 'Z': poison_set |= DC_Z; break;
                case 'C': poison_set |= DC_C; break;
                default: break;
                }
            }
        }
        else if (!strcmp(a, "--shadow-details")) shadow_details = (int)strtol(v, NULL, 10);
        else if (!strcmp(a, "--port-timing")) {
            if (!strcmp(v, "estimate")) port_timing = PORT_TIMING_ESTIMATE;
            else if (!strcmp(v, "oracle")) port_timing = PORT_TIMING_ORACLE;
            else {
                fprintf(stderr, "--port-timing must be estimate or oracle\n");
                return 2;
            }
        }
        else if (!strcmp(a, "--port-horizon")) {
            if (!strcmp(v, "strict") || !strcmp(v, "interrupts")) {
                port_horizon_interrupts = strcmp(v, "interrupts") == 0;
            } else {
                fprintf(stderr, "--port-horizon must be strict or interrupts\n");
                return 2;
            }
        }
        else if (!strcmp(a, "--no-interpreter")) {
            if (!strcmp(v, "fallback")) no_interpreter = 1;
            else if (!strcmp(v, "strict")) no_interpreter = 2;
            else {
                fprintf(stderr, "--no-interpreter must be strict or fallback\n");
                return 2;
            }
        }
        else if (!strcmp(a, "--audio")) {
            if (!strcmp(v, "snes") || !strcmp(v, "direct")) {
                audio_direct = strcmp(v, "direct") == 0;
            } else {
                fprintf(stderr, "--audio must be snes or direct\n");
                return 2;
            }
        }
        else if (!strcmp(a, "--generated")) {
            if (!strcmp(v, "off") || !strcmp(v, "on") || !strcmp(v, "only")) {
                use_generated = strcmp(v, "off") != 0;
                generated_only = strcmp(v, "only") == 0;
            } else {
                fprintf(stderr, "--generated must be off, on or only\n");
                return 2;
            }
        }
        else if (!strcmp(a, "--native")) {
            if (!strcmp(v, "off")) native_mode = NATIVE_OFF;
            else if (!strcmp(v, "shadow")) native_mode = NATIVE_SHADOW;
            else if (!strcmp(v, "on")) native_mode = NATIVE_ON;
            else {
                fprintf(stderr, "--native must be off, shadow or on\n");
                return 2;
            }
        }
        else {
            fprintf(stderr, "unknown argument %s\n", a);
            return 2;
        }
        i++;
    }
    if (!rom_path || frames < 0 || (shot_every > 0 && !shot_dir)) {
        fprintf(stderr, "usage: ct_dev --rom <rom> --frames N [--movie m.ctm] [--trace out.cttr] "
                        "[--shot-every N --shot-dir dir] [--coverage out.txt] [--status-every N] "
                        "[--native off|shadow|on] [--calls out.txt] [--load-state f] [--save-state f] [--end-state f] "
                        "[--sram save.srm] [--wav out.wav] [--exclude name-prefix]... [--include name-prefix]... "
                        "[--generated off|on|only] [--port-timing estimate|oracle] [--port-horizon strict|interrupts] "
                        "[--timing-report] [--page-report] [--event-reads out.txt] "
                        "[--insn-stats] [--io-log out.txt] [--misses out.txt] [--no-interpreter strict|fallback] "
                        "[--audio snes|direct] [--load-zst in.zst [--frame F]] [--vid-check [out.txt]] "
                        "[--vid-check-from N] [--px-check [out.txt]] [--scaffold-reads [original]] [--fallbacks out.txt] [--apu-port-margins] [--shadow-details N] [--as-port BBAAAA[,BBAAAA...]] "
                        "[--poison-site BBAAAA]... [--poison-return BBAAAA]... [--poison-set AXYNVZC] [--index-check BBAAAA]...\n");
        return 2;
    }
    if (zst_path && load_state) {
        fprintf(stderr, "--load-zst and --load-state can't be combined\n");
        return 2;
    }
    if (start_frame >= 0 && !zst_path) {
        fprintf(stderr, "--frame sets the frame number of an imported state (--load-zst)\n");
        return 2;
    }
    if (no_interpreter && native_mode != NATIVE_ON) {
        fprintf(stderr, "--no-interpreter needs --native on\n");
        return 2;
    }

    size_t rom_len;
    uint8_t *rom = read_file(rom_path, &rom_len);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 1;
    }
    size_t header = rom_len % 0x8000 == 0x200 ? 0x200 : 0;
    const uint8_t *image = rom + header;
    size_t image_len = rom_len - header;

    ct_movie movie = { 0 };
    if (movie_path && ct_movie_load(&movie, movie_path) != 0)
        return 1;

    static Snes snes;   /* large; keep it off the stack */
    static Renderer renderer;
    snes_init(&snes, image, image_len);
    snes.interpreter = cpu_step;
    snes.native_only = no_interpreter != 0;
    snes.on_miss = on_miss;
    render_init(&renderer);
    snes.on_line = on_line;
    snes.on_frame_top = on_frame_top;
    snes.on_line_ctx = &renderer;
    snes_power_on(&snes);
    if (want_margins)
        apu_margins_attach(&snes);
    static char found_sram[1024];
    if (movie.has_sram && !sram_path) {
        if (ct_sram_find(movie.sram_crc, movie_path, rom_path, found_sram, sizeof found_sram) == 0) {
            sram_path = found_sram;
            fprintf(stderr, "using save %s (CRC-32 %08X)\n", sram_path, (unsigned)movie.sram_crc);
        }
    }
    if (sram_path) {
        size_t sram_len;
        uint8_t *sram = read_file(sram_path, &sram_len);
        if (!sram || sram_len != SNES_SRAM_SIZE) {
            fprintf(stderr, "cannot read %s as a %d-byte SRAM file\n", sram_path, SNES_SRAM_SIZE);
            return 1;
        }
        memcpy(snes.bus.sram, sram, SNES_SRAM_SIZE);
        free(sram);
    }
    if (movie.has_sram && (!sram_path || ct_crc32(snes.bus.sram, SNES_SRAM_SIZE) != movie.sram_crc)) {
        fprintf(stderr, "%s starts from SRAM with CRC-32 %08X; pass that save with --sram\n", movie_path,
                (unsigned)movie.sram_crc);
        return 1;
    }
    if (load_state && snes_state_load(&snes, load_state) != 0) {
        fprintf(stderr, "cannot load state %s (missing, damaged, or not a state for this ROM)\n", load_state);
        return 1;
    }
    if (zst_path) {
        char why[160];
        if (zst_import_file(&snes, zst_path, why, sizeof why) != 0) {
            fprintf(stderr, "cannot import %s: %s\n", zst_path, why);
            return 1;
        }
        snes.frame = (uint32_t)(start_frame > 0 ? start_frame : 0);
        fprintf(stderr, "imported ZSNES state %s as frame %u. Runs from it can't be replayed from power-on and "
                        "approximate some state: use them for coverage and exploring, never for validation.\n",
                zst_path, (unsigned)snes.frame);
    }
    /* The entries, as ct_play and ct_drive choose them (frontend/natives.h),
     * with --exclude and --include. --generated only leaves the ported routines
     * out in every mode (NATIVES_ONLY_ALWAYS), where those two keep them in
     * shadow and off modes and without generated blocks. */
    NativeChoice choice = { .mode = native_mode, .generated = use_generated, .generated_only = generated_only,
                            .only = NATIVES_ONLY_ALWAYS, .exclude = excludes, .nexclude = nexcludes,
                            .include = includes, .ninclude = nincludes };
    NativeTable table;
    if (natives_select(&table, image, image_len, &choice) != 0) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    if (as_ports && as_port_add(&table, as_ports) != 0)
        return 2;
    NativeRuntime *natives = natives_restart(NULL, &table, native_mode, &snes);
    native_set_port_timing(natives, port_timing);
    native_set_port_horizon(natives, port_horizon_interrupts);
    native_set_timing_report(natives, timing_report);
    native_set_page_report(natives, page_report);
    if (shadow_details >= 0)
        native_set_details(natives, shadow_details);
    if (audio_direct) {
        /* the sound driver's registry entries run the backend */
        if (native_mode != NATIVE_ON || generated_only) {
            fprintf(stderr, "--audio direct needs --native on with the ported routines (not --generated only)\n");
            return 2;
        }
        au_select_backend(m_platform(&snes), AU_BACKEND_DIRECT);
    }
    if (shot_dir)
        make_dir(shot_dir);
    draw_pixels = trace_path || shot_every > 0 || want_px_check;

    Coverage cov = { .snes = &snes };
    if (coverage_path)
        cov.seen = calloc((size_t)1 << (COVERAGE_BITS - 3), 1);
    if (calls_path)
        cov.calls = calloc((size_t)1 << CALL_SET_BITS, sizeof *cov.calls);
    if (want_vid_check) {
        if (native_mode != NATIVE_ON)
            fprintf(stderr, "warning: --vid-check needs --native on; the ports' calls are what it observes\n");
        vid_check = vid_check_create(&snes, (uint32_t)(vid_check_from > 0 ? vid_check_from : 0));
    }
    if (want_px_check)
        px_check = px_check_create(&snes, &renderer);
    if (io_log_path)
        io_log = io_log_create(&snes);
    if (misses_path)
        misses = misses_create(&snes);
    if (asset_reads_path && asset_reads_start() != 0)
        return 1;
    if (fallbacks_path) {
        if (native_mode == NATIVE_OFF) {
            fprintf(stderr, "--fallbacks needs --native on or shadow: it measures what runs in place of the hand "
                            "ports\n");
            return 2;
        }
        if (!(fallbacks = fallbacks_create(&snes))) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
        fallbacks_attach(fallbacks, natives, native_mode == NATIVE_SHADOW);
    }
    if (event_reads_path)
        event_reads = event_reads_create(&snes);
    if (nindex_checks && (native_mode != NATIVE_OFF || use_generated))
        fprintf(stderr, "warning: --index-check sees only what the interpreter runs; use --native off --generated off\n");
    if (coverage_path || calls_path || insn_stats || io_log_path || misses_path || event_reads_path || nindex_checks ||
        fallbacks_path || scaffold_reads_original) {
        snes.cpu.on_insn = on_insn;
        snes.cpu.on_insn_ctx = &cov;
    }

    SpcWatch spc = { .snes = &snes };
    SoundAudio *sound_audio = NULL;
    if (spc_coverage_path)
        spc.counts = calloc(0x10000, sizeof *spc.counts);
    if (dsp_log_path && !(spc.dsp_log = fopen(dsp_log_path, "w"))) {
        fprintf(stderr, "cannot open %s\n", dsp_log_path);
        return 1;
    }
    if (want_lockstep) {
        if (load_state || zst_path) {
            fprintf(stderr, "--sound-lockstep needs a run from power-on, not a loaded state\n");
            return 2;
        }
        spc.lockstep = sound_lockstep_create(&snes, lockstep_level);
        if (spc.lockstep)
            sound_lockstep_check_places(spc.lockstep);
        if (audio_timing >= 0) {
            if (wav_path) {
                fprintf(stderr, "--sound-lockstep-audio takes the S-DSP's output itself: use --sound-lockstep-wav, not --wav\n");
                return 2;
            }
            sound_audio = sound_audio_create((SoundAudioTiming)audio_timing);
            if (!sound_audio) {
                fprintf(stderr, "out of memory\n");
                return 1;
            }
            if (audio_wav) {
                char c_path[1024], original_path[1024];
                snprintf(c_path, sizeof c_path, "%s-c.wav", audio_wav);
                snprintf(original_path, sizeof original_path, "%s-original.wav", audio_wav);
                if (!sound_audio_wav(sound_audio, c_path, original_path)) {
                    fprintf(stderr, "cannot open %s\n", c_path);
                    return 1;
                }
            }
            sound_lockstep_listen(spc.lockstep, sound_audio);
        }
    }
    if (spc_coverage_path || dsp_log_path || spc.lockstep) {
        snes.apu.on_insn = spc_coverage_path || spc.lockstep ? on_spc_insn : NULL;
        snes.apu.on_dsp_write = dsp_log_path || spc.lockstep ? on_dsp_write : NULL;
        snes.apu.on_insn_ctx = &spc;
    }

    static const ct_region_desc regions[] = {
        { CT_REGION_WRAM, "WRAM", SNES_WRAM_SIZE },
        { CT_REGION_VRAM, "VRAM", 0x10000 },
        { CT_REGION_SRAM, "SRAM", SNES_SRAM_SIZE },
        { CT_REGION_CGRAM, "CGRAM", 0x200 },
        { CT_REGION_OAM, "OAM", 0x220 },
        { CT_REGION_WRAMW, "WRAMW", SNES_WRAM_SIZE },
    };
    static uint8_t wram_written[SNES_WRAM_SIZE];
    snes.bus.wram_written = wram_written;
    if (npoison_sites) {
        if (native_mode != NATIVE_ON) {
            fprintf(stderr, "--poison-site needs --native on (shadow mode runs ports on a copy)\n");
            return 2;
        }
        native_site_poison = poison_site;
    }
    if (npoison_returns) {
        if (native_mode != NATIVE_ON) {
            fprintf(stderr, "--poison-return needs --native on (shadow mode runs ports on a copy)\n");
            return 2;
        }
        native_return_poison = poison_return;
    }
    if (scaffold_reads) {
        if (native_mode != NATIVE_ON) {
            fprintf(stderr, "--scaffold-reads needs --native on (shadow mode runs ports on a copy)\n");
            return 2;
        }
        scaffold_reads_start(&snes);
        if (scaffold_reads_original)
            scaffold_reads_watch_original();
    }
    ct_trace_writer *trace = NULL;
    if (trace_path) {
        trace = ct_trace_open(trace_path, ct_crc32(image, image_len), "ct_dev; interpreter", regions,
                              (int)(sizeof regions / sizeof regions[0]));
        if (!trace) {
            fprintf(stderr, "cannot open %s\n", trace_path);
            return 1;
        }
    }

    if (mixer_given && !c_sound_driver) {
        fprintf(stderr, "--mixer picks what plays the C sound driver, which this run doesn't (see --sound-driver)\n");
        return 2;
    }
    SoundHost *sound_host = NULL;
    MixerCheck *mixer_check = NULL;
    if (c_sound_driver) {
        if (load_state || zst_path || want_lockstep || spc_coverage_path || dsp_log_path) {
            fprintf(stderr, "--sound-driver c runs from power-on, without the SPC700's hooks\n");
            return 2;
        }
        if (native_mode != NATIVE_ON || generated_only || audio_direct) {
            fprintf(stderr, "--sound-driver c takes the boot and the commands from the CPU half's ported entries: "
                            "it needs --native on with the ported routines (not --generated only or --audio direct)\n");
            return 2;
        }
        sound_host = sound_host_attach_mixer(&snes, mixer);
        if (!sound_host || (mixer == SOUND_MIXER_CHECK && !(mixer_check = mixer_check_attach(sound_host)))) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
    }
    static ct_wav wav;
    static int16_t audio[DSP_OUT_FRAMES * 2];
    if (wav_path) {
        if (ct_wav_open(&wav, wav_path, DSP_SAMPLE_RATE) != 0) {
            fprintf(stderr, "cannot open %s\n", wav_path);
            return 1;
        }
        au_take_samples(m_platform(&snes), audio, DSP_OUT_FRAMES);   /* left over in a loaded state */
    }

    int rc = 0;
    uint8_t cgram[0x200];
    for (long frame = (long)snes.frame; frame < frames; frame++) {
        uint16_t pad = ct_movie_buttons(&movie, (uint32_t)frame);
        snes.bus.pad[1] = ct_movie_buttons2(&movie, (uint32_t)frame);
        snes_run_frame(&snes, pad);
        if (snes.halted) {
            rc = 1;
            break;
        }
        if (wav_path)
            ct_wav_write(&wav, audio, au_take_samples(m_platform(&snes), audio, DSP_OUT_FRAMES));
        if (trace) {
            for (int i = 0; i < 256; i++) {
                cgram[i * 2] = snes.ppu.cgram[i] & 0xFF;
                cgram[i * 2 + 1] = snes.ppu.cgram[i] >> 8;
            }
            const uint8_t *data[] = { snes.bus.wram, snes.ppu.vram, snes.bus.sram, cgram, snes.ppu.oam,
                                      wram_written };
            uint64_t fb_hash = ct_framebuffer_hash(renderer.pixels, RENDER_WIDTH, RENDER_HEIGHT);
            if (ct_trace_write_frame(trace, (uint32_t)frame, pad, data, RENDER_WIDTH, RENDER_HEIGHT, fb_hash) != 0) {
                fprintf(stderr, "error writing trace\n");
                return 1;
            }
        }
        if (shot_every > 0 && frame % shot_every == 0) {
            char path[1024];
            snprintf(path, sizeof path, "%s/frame_%06ld.png", shot_dir, frame);
            if (ct_png_write(path, renderer.pixels, RENDER_WIDTH, RENDER_HEIGHT) != 0)
                fprintf(stderr, "warning: cannot write %s\n", path);
        }
        if (status_every > 0 && frame % status_every == 0) {
            fprintf(stderr, "frame %6ld  pc %02X:%04X  apu %s pc %04X  spc cycles %llu\n", frame, snes.cpu.pb,
                    snes.cpu.pc, snes.apu.phase == APU_DRIVER ? "driver" : "ipl", snes.apu.spc.pc,
                    (unsigned long long)snes.apu.spc_cycles);
        }
    }

    if (no_interpreter == 1 && miss_steps)
        fprintf(stderr, "--no-interpreter fallback: %lu instructions had no generated code\n", miss_steps);
    if (save_state && natives && native_suspended(natives))
        fprintf(stderr, "warning: saving with native routines in progress; they won't resume from the state\n");
    else if (save_state && natives && native_owed(natives))
        fprintf(stderr, "warning: saving with time the native routines owe; the state drops it\n");
    if (save_state && snes_state_save(&snes, save_state) != 0) {
        fprintf(stderr, "cannot save state %s\n", save_state);
        rc = 1;
    }
    if (end_state && write_end_state(end_state, &snes) != 0) {
        fprintf(stderr, "cannot write %s\n", end_state);
        rc = 1;
    }
    if (native_mode != NATIVE_OFF) {
        native_report(natives, stderr);
        if (native_failed(natives))
            rc = 1;
    }
    report_page_refusals();
    if (snes.halted || snes.cpu.stopped)
        fprintf(stderr, "halted: frame %u at %02X:%04X (%s)\n", (unsigned)snes.frame, snes.cpu.pb, snes.cpu.pc,
                snes.halted ? "no code runs the instruction there" : "STP");
    /* An accessor called past what re/ram.toml declares: the port reached the
     * next variable in work RAM, which only the original's layout makes work.
     * tools/suite.py fails a movie that prints one. */
    range_report(stderr);
    asset_report(stderr);
    if (asset_reads_path && asset_reads_write(asset_reads_path) != 0)
        rc = 1;
    if (scaffold_reads)
        scaffold_reads_report(stderr);
    if (scaffold_reads_original)
        scaffold_reads_original_report(stderr);
    for (int i = 0; i < npoison_sites; i++)
        fprintf(stderr, "poison-site: %06X calls %lu, V inverted %lu\n", (unsigned)poison_sites[i].site,
                poison_sites[i].calls, poison_sites[i].v_inverted);
    for (int i = 0; i < npoison_returns; i++)
        fprintf(stderr, "poison-return: %06X returns %lu (%lu to a bridge), C inverted %lu\n",
                (unsigned)poison_returns[i].entry, poison_returns[i].returns, poison_returns[i].to_bridges,
                poison_returns[i].c_inverted);
    for (int i = 0; i < nindex_checks; i++)
        fprintf(stderr, "index-check: %06X runs %lu, largest X $%04X, past $%02X %lu\n", (unsigned)index_checks[i].pc,
                index_checks[i].runs, (unsigned)index_checks[i].largest, (unsigned)index_limit, index_checks[i].over);
    au_direct_report(stderr);
    if (insn_stats) {
        uint64_t generated = native_generated_instructions();
        fprintf(stderr, "instructions: interpreted %llu (%llu in RAM), generated %llu (%.1f%% generated)\n",
                (unsigned long long)cov.insns, (unsigned long long)cov.ram_insns, (unsigned long long)generated,
                cov.insns + generated ? 100.0 * (double)generated / (double)(cov.insns + generated) : 0.0);
    }
    if (vid_check && vid_check_write(vid_check, vid_check_path, stderr) != 0) {
        fprintf(stderr, "cannot write %s\n", vid_check_path);
        rc = 1;
    }
    vid_check_destroy(vid_check);
    vid_check = NULL;
    if (px_check && px_check_write(px_check, px_check_path, stderr) != 0) {
        fprintf(stderr, "cannot write %s\n", px_check_path);
        rc = 1;
    }
    if (px_check && px_check_equal(px_check) != px_check_frames(px_check))
        rc = 1;
    px_check_destroy(px_check);
    px_check = NULL;
    if (io_log && io_log_write(io_log, io_log_path, stderr) != 0) {
        fprintf(stderr, "cannot write %s\n", io_log_path);
        rc = 1;
    }
    io_log_destroy(io_log);
    if (misses && misses_write(misses, misses_path, stderr) != 0) {
        fprintf(stderr, "cannot write %s\n", misses_path);
        rc = 1;
    }
    misses_destroy(misses);
    if (fallbacks) {
        fallbacks_summary(fallbacks, stderr);
        if (fallbacks_write(fallbacks, fallbacks_path) != 0) {
            fprintf(stderr, "cannot write %s\n", fallbacks_path);
            rc = 1;
        }
    }
    fallbacks_destroy(fallbacks);
    if (event_reads && event_reads_write(event_reads, event_reads_path, stderr) != 0) {
        fprintf(stderr, "cannot write %s\n", event_reads_path);
        rc = 1;
    }
    event_reads_destroy(event_reads);
    native_destroy(natives);
    natives_free(&table);
    if (ct_trace_close(trace) != 0)
        rc = 1;
    if (wav_path) {
        if (snes.apu.dsp.out_dropped)
            fprintf(stderr, "warning: %u audio frames dropped\n", (unsigned)snes.apu.dsp.out_dropped);
        if (ct_wav_close(&wav) != 0) {
            fprintf(stderr, "cannot write %s\n", wav_path);
            rc = 1;
        }
    }
    if (coverage_path && write_coverage(&cov, coverage_path) != 0) {
        fprintf(stderr, "cannot write %s\n", coverage_path);
        rc = 1;
    }
    if (calls_path && write_calls(&cov, calls_path) != 0) {
        fprintf(stderr, "cannot write %s\n", calls_path);
        rc = 1;
    }
    if (spc_coverage_path && write_spc_coverage(&spc, spc_coverage_path) != 0) {
        fprintf(stderr, "cannot write %s\n", spc_coverage_path);
        rc = 1;
    }
    if (spc.dsp_log && fclose(spc.dsp_log) != 0) {
        fprintf(stderr, "cannot write %s\n", dsp_log_path);
        rc = 1;
    }
    if (spc.lockstep && !sound_lockstep_report(spc.lockstep, lockstep_path, stderr))
        rc = 1;
    if (want_margins)
        apu_margins_report(stderr);
    if (sound_audio) {
        sound_audio_report(sound_audio, stderr);
        sound_audio_destroy(sound_audio);
    }
    if (sound_host) {
        if (!sound_host_report(sound_host, stderr))
            rc = 1;
        if (mixer_check && !mixer_check_report(mixer_check, stderr))
            rc = 1;
        mixer_check_free(mixer_check);
        sound_host_detach(sound_host);
    }
    sound_lockstep_destroy(spc.lockstep);
    free(spc.counts);
    free(cov.calls);
    ct_movie_free(&movie);
    free(cov.seen);
    free(rom);
    return rc;
}
