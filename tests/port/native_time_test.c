/* Native timing on synthetic work: nesting, unknown tags, excluded reads,
 * explicit charges and fractional clocks. Diagnostic totals must not change
 * the debt the frame scheduler takes. No cartridge or machine is needed. */
#include <stdio.h>

#include "charge.h"
#include "port/state_host.h"
#include "port/time_rates.h"
#include "rom.h"

const TimeRate time_rates[] = { { 0xC01234, 1500 }, { 0xC01235, 2500 } };
const unsigned time_rate_count = 2;

uint8_t rom_read8(const GameState *g, uint32_t at) { (void)g; (void)at; return 0; }

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

int main(void)
{
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
    CHECK(count == 4);
    CHECK(rows[0].tags == 2 && rows[0].bus_clocks == 18 && rows[0].charged_clocks == 16);
    CHECK(rows[0].milli_clocks == 51000);
    CHECK(rows[1].tags == 1 && rows[1].bus_clocks == 6 && rows[1].milli_clocks == 15000);
    was = charge_time_as_begin(reported, 0xC01235);
    charge_legacy_accesses(reported, 2);
    charge_legacy_io_accesses(reported, 3);
    charge_legacy_rom_accesses(reported, 4);
    charge_time_as_end(reported, was);
    CHECK(state_host_take_owed(reported) == 145); /* (16 + 18 + 24) at 2.5 */
    CHECK(rows[2].routine == 0 && rows[2].tags == 1 && rows[2].bus_clocks == 16 && rows[2].milli_clocks == 0);
    CHECK(state_host_record_time(reported)); /* enabling twice preserves the totals */
    CHECK(rows[0].milli_clocks == 51000);
    state_host_destroy(plain);
    state_host_destroy(reported);
    printf("native_time_test: %d failures\n", failures);
    return failures != 0;
}
