/* Native timing on synthetic work: nesting, unknown tags, excluded reads,
 * explicit charges and fractional clocks. Diagnostic totals must not change
 * the debt the frame scheduler takes. No cartridge or machine is needed. */
#include <stdio.h>

#include "charge.h"
#include "cpu_io.h"
#include "platform/time_model.h"
#include "port/state_host.h"
#include "port/time_rates.h"
#include "rom.h"
#include "wram.h"

const TimeRate time_rates[] = { { 0xC01234, 1500 }, { 0xC01235, 2500 } };
const unsigned time_rate_count = 2;

uint8_t rom_read8(const GameState *g, uint32_t at)
{
    (void)at;
    state_host_game_access(g, 6);
    return 0;
}

static int failures;
#define CHECK(test) do { if (!(test)) { printf("FAIL line %d: %s\n", __LINE__, #test); failures++; } } while (0)

static void work(GameState *g)
{
    state_host_game_access(g, 8); /* untagged: no estimate */
    int outer = charge_time_as_begin(g, 0xC01234);
    state_host_game_access(g, 8); /* 12 clocks */
    int inner = charge_time_as_begin(g, 0xC01235);
    state_host_game_access(g, 6); /* 15 clocks */
    uint32_t mark = charge_uncounted_begin(g);
    state_host_game_access(g, 8000);
    charge_uncounted_end(g, mark);
    charge_as(g, 0xC01234, 2); /* 24 clocks, independent of the inner scope */
    charge_time_as_end(g, inner);
    int unknown = charge_time_as_begin(g, 0xFFFFFF);
    state_host_game_access(g, 8); /* unknown tag retains the outer rate: 12 */
    charge_time_as_end(g, unknown);
    state_host_game_access(g, 1); /* 1.5 clocks */
    charge_time_as_end(g, outer);
    state_host_game_access(g, 8); /* restored untagged scope */
}

/* A transition clear wraps within its bank and costs the transfer's time,
 * not the caller's calibrated per-access rate. Zero bytes is no operation. */
static void transition_clear(void)
{
    GameState *g = state_host_create(NULL);
    CHECK(g);
    if (!g)
        return;
    int was = charge_time_as_begin(g, 0xC01235);
    state_host_write8(g, 0x7EFFFF, 0x12);
    state_host_write8(g, 0x7E0000, 0x34);
    state_host_write8(g, 0x7F0000, 0x56);
    wram_clear(g, PF_AT(0), 0x7EFFFF, 2);
    CHECK(state_host_read8(g, 0x7EFFFF) == 0 && state_host_read8(g, 0x7E0000) == 0);
    CHECK(state_host_read8(g, 0x7F0000) == 0x56);
    CHECK(state_host_take_owed(g) == time_of_transfer(2));
    wram_clear(g, PF_AT(0), 0x7F0000, 0);
    CHECK(state_host_read8(g, 0x7F0000) == 0x56 && state_host_take_owed(g) == 0);
    state_host_write8(g, 0x7E8000, 0x78);
    wram_clear(g, PF_AT(0), 0x7EFFFF, 0x10000u);
    CHECK(state_host_read8(g, 0x7EFFFF) == 0 && state_host_read8(g, 0x7E8000) == 0 && state_host_read8(g, 0x7E0000) == 0);
    CHECK(state_host_read8(g, 0x7F0000) == 0x56);
    CHECK(state_host_take_owed(g) == time_of_transfer(0x10000u));
    charge_time_as_end(g, was);
    state_host_destroy(g);
}

/* Moves through the work RAM port run on from bank $7E into $7F, and from the
 * end of $7F round to the start of $7E; a copy reads its resource's bytes, or
 * the game's where the resource is work RAM. Neither takes the game's own
 * time (the caller charges the transfer's). */
static const uint8_t *resource_byte(uint32_t at)
{
    static const uint8_t bytes[] = { 0xA1, 0xB2, 0xC3 };
    return &bytes[(uint16_t)at % 3];
}

static void port_moves(void)
{
    GameState *g = state_host_create(NULL);
    CHECK(g);
    if (!g)
        return;
    state_host_write8(g, 0x7F0002, 0x99);
    wram_port_fill(g, 0xFFFE, 4, 0x5A);
    CHECK(state_host_read8(g, 0x7EFFFE) == 0x5A && state_host_read8(g, 0x7EFFFF) == 0x5A);
    CHECK(state_host_read8(g, 0x7F0000) == 0x5A && state_host_read8(g, 0x7F0001) == 0x5A);
    CHECK(state_host_read8(g, 0x7F0002) == 0x99);
    wram_port_fill(g, 0x1FFFF, 2, 0x11);
    CHECK(state_host_read8(g, 0x7FFFFF) == 0x11 && state_host_read8(g, 0x7E0000) == 0x11);

    const uint8_t tiles[] = { 1, 2, 3, 4 };
    wram_port_copy(g, (PfData){ .ptr = tiles }, 0xFFFE, sizeof tiles);
    CHECK(state_host_read8(g, 0x7EFFFE) == 1 && state_host_read8(g, 0x7EFFFF) == 2);
    CHECK(state_host_read8(g, 0x7F0000) == 3 && state_host_read8(g, 0x7F0001) == 4);
    wram_port_copy(g, (PfData){ .addr = 0xFFFFFF, .byte_at = resource_byte }, 0x0100, 2);
    CHECK(state_host_read8(g, 0x7E0100) == resource_byte(0xFFFFFF)[0]);
    CHECK(state_host_read8(g, 0x7E0101) == resource_byte(0xFF0000)[0]);   /* within its bank */
    state_host_write8(g, 0x7E0200, 0x42);
    wram_port_copy(g, PF_GAME(0x7E0200), 0x0300, 1);
    CHECK(state_host_read8(g, 0x7E0300) == 0x42);
    CHECK(state_host_take_owed(g) == 0);
    state_host_destroy(g);
}

int main(void)
{
    transition_clear();
    port_moves();
    GameState *plain = state_host_create(NULL), *reported = state_host_create(NULL);
    if (!plain || !reported)
        return 1;
    /* The time model is the game's, in every build; the report of it per
     * routine isn't the lean build's (CT_LEAN). */
#ifdef CT_LEAN
    CHECK(!state_host_record_time(reported));
#else
    if (!state_host_record_time(reported))
        return 1;
#endif
    work(plain);
    work(reported);
    CHECK(state_host_take_owed(plain) == 64);
    CHECK(state_host_take_owed(reported) == 64);
    int was = charge_time_as_begin(reported, 0xC01234);
    state_host_game_access(reported, 1);
    charge_time_as_end(reported, was);
    CHECK(state_host_take_owed(reported) == 2); /* half clock carried across the take */
#ifndef CT_LEAN
    unsigned count;
    const StateHostTime *rows = state_host_time(reported, &count);
    CHECK(count == 3);
    CHECK(rows[0].tags == 2 && rows[0].bus_clocks == 18 && rows[0].charged_clocks == 16);
    CHECK(rows[0].milli_clocks == 51000);
    CHECK(rows[1].tags == 1 && rows[1].bus_clocks == 6 && rows[1].milli_clocks == 15000);
#endif
    was = charge_time_as_begin(reported, 0xC01235);
    charge_accesses(reported, 2);
    charge_io_accesses(reported, 3);
    charge_rom_accesses(reported, 4);
    charge_time_as_end(reported, was);
    CHECK(state_host_take_owed(reported) == 145); /* (16 + 18 + 24) at 2.5 */
    was = charge_time_as_begin(reported, 0xC01235);
    charge_time_as_end(reported, was);
    CHECK(state_host_take_owed(reported) == 0); /* never counted by the host */
#ifndef CT_LEAN
    CHECK(rows[2].routine == 0 && rows[2].tags == 1 && rows[2].bus_clocks == 16 && rows[2].milli_clocks == 0);
    CHECK(state_host_record_time(reported)); /* enabling twice preserves the totals */
    CHECK(rows[0].milli_clocks == 51000);
#endif
    GameState *retired = state_host_create(NULL);
    CHECK(retired);
#ifndef CT_LEAN
    CHECK(state_host_record_time(retired));
#endif
    int parent = charge_time_as_begin(retired, 0xC01235);
    uint32_t begin = charge_retired_begin(retired);
    state_host_game_access(retired, 6);
    hw_port_seek(retired, 0x2000, 0);
    hw_port_write(retired, 0x42);
    (void)hw_port_read(retired);
    hw_port_seek_address(retired, 0x3000);
    hw_port_seek_bank(retired, 1);
    uint32_t skip = charge_uncounted_begin(retired);
    state_host_game_access(retired, 100);
    charge_uncounted_end(retired, skip);
    (void)state_host_read8(retired, 0xC00000); /* host inspection is excluded even inside a retired span */
    charge_retired_end(retired, begin, 0xC01234, 2);
    state_host_game_access(retired, 4);
    charge_time_as_end(retired, parent);
    CHECK(state_host_take_owed(retired) == 115);
#ifndef CT_LEAN
    const StateHostTime *retired_rows = state_host_time(retired, &count);
    CHECK(retired_rows[0].bus_clocks == 54 && retired_rows[0].charged_clocks == 16);
    CHECK(retired_rows[0].milli_clocks == 105000);
    CHECK(retired_rows[1].milli_clocks == 10000);
#endif
    state_host_destroy(retired);
    state_host_destroy(plain);
    state_host_destroy(reported);
    printf("native_time_test: %d failures\n", failures);
    return failures != 0;
}
