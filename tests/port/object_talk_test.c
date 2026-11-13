/* Checks when an object takes the player's talk (event/calls.h,
 * object_takes_a_call_now, as $C0:5AC5 decides): a talk comes at level 1, so
 * an object running a function at a less urgent level -- Arris Dome's rat
 * running its path at level 3 -- takes it, and one already at level 1 or 0,
 * refusing calls, disabled or passed over does not. Exits non-zero on
 * failure.
 *
 *   object_talk_test */
#include <stdio.h>

#include "event/calls.h"
#include "port/state_host.h"
#include "vars.h"

enum { TALK_LEVEL = 1, RAT = 13, AT_REST = 7, RUNNING_ITS_PATH = 3 };
enum { DISABLED = 0x80, UNREACHABLE = 0x80, A_WALKER = 5, STEPS = 4 };

static int failures;

#define CHECK(cond, what)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what);                                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void reset(GameState *g)
{
    set_field_object_role(g, RAT, A_WALKER);
    set_field_object_step_length(g, RAT, STEPS);
    set_event_object_call_state_refusing(g, RAT, 0);
    set_event_object_call_state_level(g, RAT, AT_REST);
}

int main(void)
{
    GameState *g = state_host_create(NULL);
    if (!g) {
        printf("FAIL: no game state\n");
        return 1;
    }
    reset(g);
    CHECK(object_takes_a_call_now(g, RAT, TALK_LEVEL), "an object at rest takes a talk");
    set_event_object_call_state_level(g, RAT, RUNNING_ITS_PATH);
    CHECK(object_takes_a_call_now(g, RAT, TALK_LEVEL), "an object running a level 3 function takes a talk");
    set_event_object_call_state_level(g, RAT, TALK_LEVEL);
    CHECK(!object_takes_a_call_now(g, RAT, TALK_LEVEL), "an object already talking takes no second talk");
    set_event_object_call_state_level(g, RAT, 0);
    CHECK(!object_takes_a_call_now(g, RAT, TALK_LEVEL), "an object at the most urgent level takes no talk");
    reset(g);
    set_event_object_call_state_refusing(g, RAT, 1);
    CHECK(!object_takes_a_call_now(g, RAT, TALK_LEVEL), "an object refusing calls takes no talk");
    reset(g);
    set_field_object_role(g, RAT, A_WALKER | DISABLED);
    CHECK(!object_takes_a_call_now(g, RAT, TALK_LEVEL), "a disabled object takes no talk");
    reset(g);
    set_field_object_step_length(g, RAT, STEPS | UNREACHABLE);
    CHECK(!object_takes_a_call_now(g, RAT, TALK_LEVEL), "an object passed over takes no talk");
    return failures != 0;
}
