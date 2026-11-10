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

int main(void)
{
    transition_clear();
    GameState *plain = state_host_create(NULL), *reported = state_host_create(NULL);
    if (!plain || !reported || !state_host_record_time(reported))
        return 1;
    work(plain);
    work(reported);
    CHECK(state_host_take_owed(plain) == 64);
    CHECK(state_host_take_owed(reported) == 64);
    int was = charge_time_as_begin(reported, 0xC01234);
    state_host_game_access(reported, 1);
    charge_time_as_end(reported, was);
    CHECK(state_host_take_owed(reported) == 2); /* half clock carried across the take */
    unsigned count;
    const StateHostTime *rows = state_host_time(reported, &count);
    CHECK(count == 3);
    CHECK(rows[0].tags == 2 && rows[0].bus_clocks == 18 && rows[0].charged_clocks == 16);
    CHECK(rows[0].milli_clocks == 51000);
    CHECK(rows[1].tags == 1 && rows[1].bus_clocks == 6 && rows[1].milli_clocks == 15000);
    was = charge_time_as_begin(reported, 0xC01235);
    charge_legacy_accesses(reported, 2);
    charge_legacy_io_accesses(reported, 3);
    charge_legacy_rom_accesses(reported, 4);
    charge_time_as_end(reported, was);
    CHECK(state_host_take_owed(reported) == 145); /* (16 + 18 + 24) at 2.5 */
    was = charge_time_as_begin(reported, 0xC01235);
    charge_machine_accesses(reported, 2);
    charge_machine_io_accesses(reported, 3);
    charge_machine_rom_accesses(reported, 4);
    charge_time_as_end(reported, was);
    CHECK(state_host_take_owed(reported) == 0); /* never counted by the host */
    CHECK(rows[2].routine == 0 && rows[2].tags == 1 && rows[2].bus_clocks == 16 && rows[2].milli_clocks == 0);
    CHECK(state_host_record_time(reported)); /* enabling twice preserves the totals */
    CHECK(rows[0].milli_clocks == 51000);
    GameState *retired = state_host_create(NULL);
    CHECK(retired && state_host_record_time(retired));
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
    const StateHostTime *retired_rows = state_host_time(retired, &count);
    CHECK(retired_rows[0].bus_clocks == 54 && retired_rows[0].charged_clocks == 16);
    CHECK(retired_rows[0].milli_clocks == 105000);
    CHECK(retired_rows[1].milli_clocks == 10000);
    state_host_destroy(retired);
    state_host_destroy(plain);
    state_host_destroy(reported);
    printf("native_time_test: %d failures\n", failures);
    return failures != 0;
}
