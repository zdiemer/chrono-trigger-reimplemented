/* The real program loop with small mode bodies: hand-offs, callback ownership,
 * replacement during dispatch, disabling, failure propagation, and the
 * sprites' places forgotten as a mode takes the frame over. No ROM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/program.h"
#include "core/sprite_table.h"
#include "charge.h"
#include "platform/native/host.h"
#include "port/state_host.h"
#include "port/time_rates.h"

const TimeRate time_rates[] = { { 0xC00000, 1000 } };
const unsigned time_rate_count = 1;
uint8_t rom_read8(const GameState *g, uint32_t at) { (void)g; (void)at; abort(); }

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static char order[64];
static unsigned used, turn;
static bool refuse_scene;
static void note(char c) { order[used++] = c; order[used] = 0; }

void field_power_on(GameState *g, Platform *pf) { (void)g; (void)pf; note('P'); }
void field_runs(GameState *g, Platform *pf)
{
    (void)pf;
    note('F');
    switch (turn++) {
    case 0: program_goes_on_to_a_scene(g, 1); break;
    case 1: program_goes_on_to_a_scene(g, 0x80); break;
    case 2: program_starts_afresh_from_the_title(g); break;
    case 3: program_resets_the_console(g); break;
    default: program_fail(g, 123, "test mode stopped"); break;
    }
}
bool scene_runs(GameState *g, Platform *pf, uint8_t scene)
{
    (void)pf;
    note('S');
    CHECK(scene == 1 || scene == 2);
    if (refuse_scene)
        program_fail(g, 456, "test scene stopped");
    return !refuse_scene;
}
bool warp_runs(GameState *g, Platform *pf, uint8_t scene)
{
    (void)g; (void)pf;
    CHECK(scene == 0x80);
    note('W');
    return true;
}
void scene_start_song(Platform *pf, uint8_t song) { (void)pf; CHECK(song == 7); note('A'); }

static void next_handler(GameState *g, Platform *pf) { (void)g; (void)pf; note('N'); }
static void replacing_handler(GameState *g, Platform *pf)
{
    (void)pf;
    note('V');
    program_install_vblank_handler(g, next_handler);
}
static void line_handler(GameState *g, Platform *pf) { (void)g; (void)pf; note('L'); }
static void read_game(void *g, uint32_t at, uint8_t *out, uint32_t len) { state_host_read_span(g, at, out, len); }
static Platform pf;

int main(void)
{
    GameState *g = state_host_create(NULL), *other = state_host_create(NULL);
    if (!g || !other) return 1;
    vid_host_start(&pf, read_game, g);
    int rate = charge_time_as_begin(g, 0xC00000);
    program_vblank(g, &pf);
    CHECK(used == 0 && state_host_take_owed(g) == 32);
    program_install_vblank_handler(g, replacing_handler);
    program_install_line_handler(g, line_handler);
    program_vblank(other, &pf);
    CHECK(used == 0);
    program_vblank(g, &pf);
    program_line(g, &pf);
    program_vblank(g, &pf);
    CHECK(strcmp(order, "VLN") == 0 && state_host_take_owed(g) == 96);
    program_ignore_interrupts(g);
    CHECK(state_host_take_owed(g) == 128);
    program_vblank(g, &pf);
    program_line(g, &pf);
    CHECK(strcmp(order, "VLN") == 0 && state_host_take_owed(g) == 64);
    charge_time_as_end(g, rate);
    used = 0;
    program_main_thread(g, &pf);
    CHECK(strcmp(order, "PFSFWFPFPF") == 0);
    CHECK(program_went_no_further(g));
    CHECK(!program_went_no_further(other));
    used = 0;
    refuse_scene = true;
    program_starts_in_a_scene(other, 2, 7);
    program_main_thread(other, &pf);
    CHECK(strcmp(order, "PAS") == 0 && program_went_no_further(other));

    /* A place a mode gave holds while that mode changes its own handler, and
     * is gone once another mode takes the frame: an entry the new mode writes
     * with the same X and Y reads as the table says. */
    sprite_table_place(g, 5, -20, 40);
    sprite_table_hide(g, 127, 0x100, 0xE0);
    program_install_vblank_handler(g, line_handler);
    CHECK(sprite_table_places(g)[5].kind == VID_PLACE_AT && sprite_table_places(g)[127].kind == VID_PLACE_HIDDEN);
    program_hand_the_vblank_to(g, next_handler);
    CHECK(program_handlers(g)->vblank == next_handler);
    CHECK(sprite_table_places(g)[5].kind == VID_PLACE_NONE && sprite_table_places(g)[127].kind == VID_PLACE_NONE);
    CHECK(!vid_place_holds(&sprite_table_places(g)[5], (uint16_t)-20 & 0x1FF, 40));
    vid_host_stop(&pf);
    state_host_destroy(g);
    state_host_destroy(other);
    return failures ? 1 : 0;
}
