/* Checks when the world map's leader is one the port walks
 * (overworld/handlers.c, overworld_leader_takes, $C2:3404): a leader setting
 * off is halted before it moves, so a velocity with fractions left in its
 * object (as the scene out of Denadoro's cave leaves it) doesn't stop it,
 * while a leader already walking with one is still refused. Exits non-zero
 * on failure.
 *
 *   overworld_leader_test */
#include <stdio.h>

#include "menu/object.h"
#include "menu/sprites.h"
#include "overworld/handlers.h"
#include "overworld/overworld.h"
#include "port/state_host.h"
#include "vars.h"

enum { LEADER = 0, CRONO = 0, A_STEP_ON = 2, UPLOAD_BUFFER = 0x7E4000 };

static int failures;

#define CHECK(cond, what)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what);                                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* Crono on foot on the world map, no event, the sprite list laid out, the
 * upload queue empty and the map's planes at its corner. */
static void on_the_map(GameState *g)
{
    set_party_members(g, 0, CRONO);
    overworld_set_mode(g, OVERWORLD_ON_THE_MAP);
    set_overworld_travel(g, TRAVEL_ON_FOOT);
    set_overworld_travel_before(g, TRAVEL_ON_FOOT);
    set_overworld_event(g, 0);
    set_overworld_event_before(g, 0);
    set_menu_object_slots_facing(g, LEADER, 0);
    menu_lay_out_the_sprite_nodes(g);
    menu_empty_the_sprite_list(g);
    set_menu_object_vram_queue_end(g, 0);
    set_menu_object_upload_buffer(g, UPLOAD_BUFFER);
    for (unsigned plane = 0; plane < MENU_MAP_COLUMN_COUNT; plane++)
        set_menu_map_column(g, plane, 0);
    for (unsigned plane = 0; plane < MENU_MAP_ROW_COUNT; plane++)
        set_menu_map_row(g, plane, 0);
}

static bool takes_with(GameState *g, OverworldStep step, MenuPlace velocity)
{
    overworld_set_step(g, LEADER, step);
    menu_object_set_velocity(g, LEADER, velocity);
    return overworld_leader_takes(g, LEADER);
}

int main(void)
{
    GameState *g = state_host_create(NULL);
    if (!g) {
        printf("FAIL: no game state\n");
        return 1;
    }
    on_the_map(g);

    const MenuPlace still = { 0, 0 }, with_fractions = { 0xFFFFB426u, 0x0001F5F7u };
    CHECK(takes_with(g, 0, still), "a leader setting off on the map is walked");
    CHECK(takes_with(g, 0, with_fractions), "a leader setting off is walked whatever velocity it held");
    CHECK(!takes_with(g, A_STEP_ON, with_fractions), "a walking leader with fractions is refused");

    if (failures)
        return 1;
    printf("overworld leader ok\n");
    return 0;
}
