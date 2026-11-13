/* Checks how an object gives up its sprite palette slot (field/palettes.c,
 * object_give_up_palette, $C0:734C): the sharers either side of it close up
 * round it, through links that name objects, and through an odd link the
 * way the original writes it, a byte along in the two interleaved link
 * tables (Truce Canyon, 600 A.D.). Exits non-zero on failure.
 *
 *   palette_sharers_test */
#include <stdio.h>

#include "field/palettes.h"
#include "port/state_host.h"
#include "vars.h"

enum { NONE = 0x80, LEAVING = 21, OLDER = 5, NEWER = 7 };

static int failures;

#define CHECK(cond, what)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what);                                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void clear_links(GameState *g)
{
    for (unsigned obj = 0; obj < 64; obj++) {
        set_field_object_palette_older(g, obj, NONE);
        set_field_object_palette_newer(g, obj, NONE);
    }
    set_field_object_palette(g, LEAVING, 0);
}

int main(void)
{
    GameState *g = state_host_create(NULL);
    if (!g) {
        printf("FAIL: no game state\n");
        return 1;
    }

    /* Between two objects: they link to each other. */
    clear_links(g);
    set_field_object_palette_older(g, LEAVING, OLDER * 2);
    set_field_object_palette_newer(g, LEAVING, NEWER * 2);
    CHECK(object_palette_links_in_the_tables(g, LEAVING), "links that name objects are in the tables");
    object_give_up_palette(g, LEAVING);
    CHECK(field_object_palette_newer(g, OLDER) == NEWER * 2, "the older sharer's newer is the newer sharer");
    CHECK(field_object_palette_older(g, NEWER) == OLDER * 2, "the newer sharer's older is the older sharer");

    /* The newest: the older sharer is the slot's newest now. */
    clear_links(g);
    set_field_object_palette_older(g, LEAVING, OLDER * 2);
    object_give_up_palette(g, LEAVING);
    CHECK(field_palette_slot_newest(g, 0) == OLDER * 2, "the older sharer is the slot's newest");
    CHECK(field_object_palette_newer(g, OLDER) == NONE, "the older sharer has no newer");

    /* An odd newer link (1): its "older" is the byte after object 0's older,
     * object 0's newer. */
    clear_links(g);
    set_field_object_palette_older(g, LEAVING, 0);
    set_field_object_palette_newer(g, LEAVING, 1);
    CHECK(object_palette_links_in_the_tables(g, LEAVING), "an odd link inside the tables is read as the original reads it");
    object_give_up_palette(g, LEAVING);
    CHECK(field_object_palette_newer(g, 0) == 1, "object 0's newer: written through the odd link and the even one");
    CHECK(field_object_palette_older(g, 0) == NONE, "object 0's older untouched");
    CHECK(field_object_palette_older(g, 1) == NONE, "object 1's older untouched");
    return failures != 0;
}
