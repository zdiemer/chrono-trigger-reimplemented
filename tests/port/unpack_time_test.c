/* Two made-up streams have the same control/reference counts but copy
 * different amounts of data. More output work must take more native time. */
#include <stdio.h>
#include "data/decompress.h"
#include "port/state_host.h"
#include "rom.h"
#include "vars.h"

static uint8_t stream[] = { 0, 0, 2, 9, 0, 2, 0x42, 1, 0, 0 };
static unsigned failures;

uint8_t rom_read8(const GameState *g, uint32_t at)
{
    state_host_game_access(g, 6);
    if (at < 0xC00000 || at - 0xC00000 >= sizeof stream) {
        failures++;
        return 0;
    }
    return stream[at - 0xC00000];
}

static uint32_t unpack(unsigned length, bool direct, bool entry, uint32_t destination)
{
    GameState *g = state_host_create(NULL);
    if (!g) { failures++; return 0; }
    stream[8] = (uint8_t)((length - 3) << 4);
    set_decompress_source(g, 0xC00000);
    set_decompress_dest(g, destination);
    uint16_t got = entry ? unpack_stream_run(g, direct) : direct ? unpack_stream_direct(g) : unpack_stream(g);
    uint32_t clocks = state_host_take_owed(g);
    if (got != length + 1 || decompress_length(g) != got)
        failures++;
    for (unsigned i = 0; i < got; i++)
        if (state_host_read8(g, destination + i) != 0x42)
            failures++;
    if (state_host_read8(g, destination + got) != 0x55)
        failures++;
    if (destination == 0x7E0700 &&
        (field_sprite_table_byte(g, got - 1) != 0x42 || oam_buffer_x(g, 0) != 0x42))
        failures++;
    state_host_destroy(g);
    return clocks;
}

int main(void)
{
    uint32_t short_run = unpack(3, false, false, 0x7F2000), long_run = unpack(18, false, false, 0x7F2000);
    uint32_t direct_short = unpack(3, true, false, 0x7F2000), direct_long = unpack(18, true, false, 0x7F2000);
    uint32_t with_entry = unpack(18, false, true, 0x7F2000);
    /* Literals use the streaming port; backreferences use raw RAM helpers.
     * Both must reach owner storage, with the same time as a residual buffer. */
    if (unpack(18, false, true, 0x7E0700) != with_entry)
        failures++;
    if (!short_run || long_run <= short_run || direct_long <= direct_short || with_entry <= long_run)
        failures++;
    printf("unpack time: short %u, long %u, direct short %u, direct long %u, entry %u; %u failures\n",
           short_run, long_run, direct_short, direct_long, with_entry, failures);
    return failures != 0;
}
