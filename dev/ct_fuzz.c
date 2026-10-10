/* ct_fuzz: differential testing of a ported routine against the original, on
 * random machine states rather than states a movie happens to reach.
 *
 *   ct_fuzz --rom roms/ct-usa.sfc --load-state base.state --routine <name|BBAAAA>
 *           [--iterations N] [--seed S] [--long | --interrupt] [--budget N]
 *           [--keep AREA]... [--fill AREA=lo-hi|V,V...|@BBAAAA]... [--range A|X|Y=lo-hi]...
 *           [--db BB] [--d HHHH] [--widths M,X] [--apu-loopback] [--poison-dc] [--details N]
 *
 * Each iteration starts from the base state (or power-on), randomizes A, X, Y,
 * the N V Z C flags and all of work RAM except the stack page, applies the
 * routine's entry precondition on P, and calls the routine as a JSR (JSL with
 * --long) would, or as an interrupt (--interrupt). The original runs on the
 * interpreter; the native routine runs on a copy (call-outs interpreted
 * synchronously). The machines are compared where the native routine left
 * off, normally the return, as in shadow mode (at the first pass of the
 * original through that point where they are the same): ignoring the routine's
 * don't-care set (and what it declares dead where it stops,
 * native_dont_care_at_stop, or instead of it, native_own_dont_care_at_stop),
 * and the stack bytes the original pushed and
 * pulled again (native_copy_popped_stack). Nothing advances time, and
 * interrupts don't happen. A mismatching state is described in full: every
 * register and flag that differs, and every differing run of memory, grouped.
 * An idle entry is checked the same way over one pass of its loop: the
 * original runs until it is back at the loop head, or at the exit the pass took.
 *
 * Both sides' work is counted in bus accesses (dev/fuzz.h). The port, with its
 * call-outs, may do --budget of them (default 2000000), and is abandoned where
 * it is past that, in its own loop or in a call-out's: nothing else can stop a
 * C loop that runs away on a random state. The original may do the budget plus
 * 16 times what the port did (it fetches every instruction, and loops through
 * what a port does in a few accesses). A port that ran past its budget in its
 * own code where the original returns within a quarter of it is a finding,
 * counted with the mismatches; one where the original runs long too is
 * skipped, as are states where a call-out ran past the budget, or the port
 * returned and the original didn't within its budget. The report counts each.
 * --step-limit N, the old spelling, is --budget 4N.
 *
 * An AREA is a range of work RAM, BBAAAA-BBAAAA (bus addresses in $7E/$7F), or
 * COUNT bytes at a pointer the state holds, [BBAAAA]+OFF,COUNT: the 16-bit
 * pointer in the cell at BBAAAA, plus OFF (or -OFF), in the cell's bank, or in
 * the bank a "BB:" before the bracket names; [X], [Y] and [D] take the pointer
 * from a register (bank $7E). Hex throughout. --keep leaves an area as it is
 * in the base state, for routines that need some structure intact; --fill
 * gives it random bytes from lo to hi, for example to make a pointer in script
 * operands point into WRAM, or an object's fields at [$6D]+$20 stay in range.
 * Areas at pointers are applied after the rest (and after the registers), in
 * order, so a pointer can be filled first and the fields it reaches after.
 * An area's range can take every STEP-th byte of it, AREA:STEP, for a field
 * of each record in a table; and a fill can pick each element from a set,
 * AREA=V,V,... (as wide as the widest value written: 2 hex digits a byte, 4 a
 * word, low byte first), so a script's place or a record's offset is one of
 * the real ones; or copy another cell's bytes, AREA=@BBAAAA, which is applied
 * after every other area, so two cells the game keeps equal are.
 * --range A|X|Y=lo-hi limits a register's random value, for example to keep
 * script offsets inside the script buffer: indexing past the end of a data
 * area reaches I/O space, whose open bus reads depend on instruction fetches
 * native code doesn't do; --range X=lo-hi/step takes every step-th value
 * from lo, for a register holding a record's offset (a battle stat block,
 * 0-$500 by $80). --db sets the data bank, which the base state
 * otherwise decides, for entries that address registers through it; --d the
 * direct page likewise, for entries guarded on the page their subsystem runs
 * with. --widths M,X (8 or 16 each) sets the accumulator's and the index
 * registers' widths the routine is entered with, which the base state
 * otherwise decides, for entries whose callers come both ways (the menus'
 * services, called with 16-bit index registers by most and 8-bit by a New
 * Game+'s carrying the party over); the entry's own precondition on P still
 * wins. --apu-loopback makes reads of the APU ports return what the CPU last
 * wrote to them: the SPC700 doesn't run here, so the sound driver's echo waits
 * would otherwise never end. --details N describes the first N mismatches
 * (default 10). Accessors the port called out of range (port/vars_ram.h) are
 * listed after the counts, one `accessor out of range` line per variable.
 *
 * --poison-dc scrambles, at every call-out the port makes with a don't-care
 * set (m_jsr_dc, bridge_call, PF_CALL_DC and the rest), the registers and flags
 * the set names before the callee runs (native_call_poison). A callee that
 * reads one runs on a value other than the original's, and the state
 * mismatches: without it, a port that happens to leave the original's value
 * there hides a wrong declaration. It is the evidence a don't-care
 * declaration liveness can't prove carries (docs/PORTING.md, "Declarations
 * liveness can't prove"). The states are those of the same seed without it.
 * --stop-at-call BBAAAA (up to 8) ends a state's comparison at the port's
 * call-out to that routine: the machines are compared at the callee's entry,
 * with the call's don't-care set, the original run up to the same call. For a
 * port that yields to the next frame through a task switch (the menus'
 * `$C2:821E`), which never returns on the stack it was called on, so a state
 * would otherwise be skipped as a call-out past the budget.
 * --poison-site BBAAAA scrambles only at the call-outs made from that site
 * (the original's JSR or JSL), to tell which declaration a mismatch is in.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz.h"
#include "range_report.h"
#include "registry.h"
#include "snes/system.h"

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

/* --apu-loopback: the SNES's APU read hook, answering with the CPU's own writes. */
static uint8_t apu_loopback_read(void *ctx, uint8_t port)
{
    return ((Snes *)ctx)->apu.cpu_in[port & 3];
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *state_path = NULL, *routine = NULL;
    bool apu_loopback = false;
    int data_bank = -1, direct_page = -1, m_width = 0, x_width = 0;
    FuzzOptions o;
    fuzz_default_options(&o);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--long")) {
            o.long_call = true;
            continue;
        }
        if (!strcmp(a, "--interrupt")) {
            o.interrupt = true;
            continue;
        }
        if (!strcmp(a, "--apu-loopback")) {
            apu_loopback = true;
            continue;
        }
        if (!strcmp(a, "--poison-dc")) {
            o.poison_dc = true;
            continue;
        }
        if (!v)
            goto usage;
        i++;
        if (!strcmp(a, "--rom")) rom_path = v;
        else if (!strcmp(a, "--load-state")) state_path = v;
        else if (!strcmp(a, "--routine")) routine = v;
        else if (!strcmp(a, "--iterations")) o.iterations = strtoul(v, NULL, 0);
        else if (!strcmp(a, "--seed")) o.seed = strtoull(v, NULL, 0);
        else if (!strcmp(a, "--budget")) o.budget = strtoul(v, NULL, 0);
        else if (!strcmp(a, "--step-limit")) o.budget = 4 * strtoul(v, NULL, 0);
        else if (!strcmp(a, "--details")) o.details = (int)strtol(v, NULL, 0);
        else if (!strcmp(a, "--stop-at-call") && o.nstop_calls < FUZZ_MAX_STOP_CALLS)
            o.stop_calls[o.nstop_calls++] = (uint32_t)strtoul(v, NULL, 16);
        else if (!strcmp(a, "--poison-site")) {
            o.poison_dc = true;
            o.poison_site = (uint32_t)strtoul(v, NULL, 16);
        }
        else if (!strcmp(a, "--db")) data_bank = (int)(strtoul(v, NULL, 16) & 0xFF);
        else if (!strcmp(a, "--d")) direct_page = (int)(strtoul(v, NULL, 16) & 0xFFFF);
        else if (!strcmp(a, "--widths") && sscanf(v, "%d,%d", &m_width, &x_width) == 2 &&
                 (m_width == 8 || m_width == 16) && (x_width == 8 || x_width == 16))
            ;
        else if (!strcmp(a, "--range") && strchr("AXY", v[0]) && v[1] == '=' &&
                 sscanf(v + 2, "%x-%x", &o.range_lo[strchr("AXY", v[0]) - "AXY"],
                        &o.range_hi[strchr("AXY", v[0]) - "AXY"]) == 2) {
            const char *step = strchr(v + 2, '/');
            if (step)
                o.range_step[strchr("AXY", v[0]) - "AXY"] = (unsigned)strtoul(step + 1, NULL, 16);
        }
        else if ((!strcmp(a, "--fill") || !strcmp(a, "--keep")) && o.nareas < FUZZ_MAX_AREAS &&
                 fuzz_parse_area(v, !strcmp(a, "--keep"), &o.areas[o.nareas]))
            o.nareas++;
        else
            goto usage;
    }
    if (!rom_path || !routine)
        goto usage;

    const NativeEntry *e = NULL;
    uint32_t want = (uint32_t)strtoul(routine, NULL, 16);
    for (int i = 0; i < game_native_count; i++)
        if (!strcmp(game_natives[i].name, routine) || game_natives[i].addr == want)
            e = &game_natives[i];
    if (!e) {
        fprintf(stderr, "no routine %s\n", routine);
        return 2;
    }

    size_t rom_len;
    uint8_t *rom = read_file(rom_path, &rom_len);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 1;
    }
    size_t header = rom_len % 0x8000 == 0x200 ? 0x200 : 0;

    static Snes base;
    snes_init(&base, rom + header, rom_len - header);
    base.interpreter = cpu_step;   /* call-outs are interpreted (native_call) */
    snes_power_on(&base);
    if (state_path && snes_state_load(&base, state_path) != 0) {
        fprintf(stderr, "cannot load state %s\n", state_path);
        return 1;
    }
    base.cpu.e = false;
    if (data_bank >= 0)
        base.cpu.db = (uint8_t)data_bank;
    if (direct_page >= 0)
        base.cpu.d = (uint16_t)direct_page;
    if (m_width)
        base.cpu.p = (uint8_t)(m_width == 8 ? base.cpu.p | CPU_FLAG_M : base.cpu.p & ~CPU_FLAG_M);
    if (x_width)
        base.cpu.p = (uint8_t)(x_width == 8 ? base.cpu.p | CPU_FLAG_X : base.cpu.p & ~CPU_FLAG_X);
    if (apu_loopback)
        base.bus.io.apu_read = apu_loopback_read;

    FuzzResult r = fuzz_run(&base, e, &o, stderr);
    printf("%s: %lu iterations, matched %lu, mismatched %lu (the port looping where the original returns %lu), "
           "skipped %lu (guard %lu, a call-out past the budget %lu, the port past the budget and the original "
           "long %lu, the original past its budget %lu)\n",
           e->name, o.iterations, r.matched, r.mismatched, r.looped,
           r.guard + r.callout + r.both_long + r.original_long, r.guard, r.callout, r.both_long, r.original_long);
    if (o.poison_dc)
        printf("%s: scrambled the don't-care set of %lu call-outs in the states compared\n", e->name, r.scrambled);
    if (o.nstop_calls)
        printf("%s: %lu of the states compared where the port called a --stop-at-call routine\n", e->name,
               r.at_call);
    /* Random states reach indices no movie does: an accessor called past what
     * re/ram.toml declares, usually where no guard turns the state away. */
    range_report(stdout);
    return r.mismatched ? 1 : 0;

usage:
    fprintf(stderr, "usage: ct_fuzz --rom <rom> --routine <name|BBAAAA> [--load-state f] [--iterations N] "
                    "[--seed S] [--long | --interrupt] [--budget N] [--keep AREA]... [--fill AREA=lo-hi|V,V...|@BBAAAA]... "
                    "[--range A|X|Y=lo-hi[/step]]... [--db BB] [--d HHHH] [--widths M,X] [--apu-loopback] [--poison-dc | --poison-site BBAAAA]\n"
                    "  [--stop-at-call BBAAAA]...\n"
                    "  [--details N]\n"
                    "  AREA: BBAAAA-BBAAAA, or [BBAAAA]+OFF,COUNT at the pointer in a cell ([X], [Y] or [D]: in a "
                    "register; BB:[...]: in bank BB)\n");
    return 2;
}
