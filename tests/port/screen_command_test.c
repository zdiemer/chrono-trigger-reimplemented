/* Checks that the screen command (event $FF, event/screen.c) reads no byte
 * past itself: a command that only names its effect is two bytes, and the
 * last command of a location's script is often one, with whatever an earlier
 * screen left past the script after it -- in Magus's castle, the time gate's
 * tunnel ($7F:3786, which the gate's warp builds its plane over). Its operands
 * read there would be a read of another owner's bytes. Exits non-zero on
 * failure.
 *
 *   screen_command_test */
#include <stdio.h>

#include "event/script.h"
#include "port/state_host.h"
#include "port/vars_host.h"
#include "vars.h"

enum { SCREEN_COMMAND = 0xFF, SCENE_COMMAND = 0x9B, AT = 0x1782, PLANE_ROW = 0x6F, PLANE_COLUMN = 6 };

static int failures;

#define EXPECT(cond, what)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what);                                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    GameState *g = state_host_create(NULL);
    if (!g) {
        printf("FAIL: no game state\n");
        return 1;
    }
    set_event_script_byte(g, AT, SCREEN_COMMAND);
    set_event_script_byte(g, AT + 1, SCENE_COMMAND);
    /* The three bytes after the command, last written by the gate's tunnel. */
    for (unsigned i = 0; i < 3; i++)
        set_warp_plane_map(g, PLANE_ROW, PLANE_COLUMN + i, (uint8_t)(0x40 + i));
    vars_host_crossings_reset(state_host_vars(g));
    const EventOpcode *row = event_opcode(SCREEN_COMMAND);
    EXPECT(row != NULL, "the screen command has a row");
    if (row) {
        EventCommand c = event_decode(g, NULL, row, AT);
        EXPECT(c.screen_command == SCENE_COMMAND, "the command's number decoded");
        EXPECT(vars_host_crossings(state_host_vars_const(g), NULL, NULL) == 0,
              "nothing past the two-byte command read");
    }
    return failures != 0;
}
