/* Checks a member's step in a place of the party emptied under it
 * (field/party.h): the place still steps, and a member waiting on the
 * trail shows its pose on the slot the empty place's marker names, the first
 * past the object tables, as the original writes it ($C0:9F6F; the Prophet's
 * ending rebuilds its party so, frame 133126). Exits non-zero on failure.
 *
 *   party_trail_test */
#include <stdio.h>

#include "field/party.h"
#include "port/state_host.h"
#include "vars.h"

enum { LEADER = 1, FIRST = 3, WAITING = 5, PAST_THE_TABLES = 64, SOMETHING_ELSE = 0x12 };

static int failures;

#define CHECK(cond, what)                                                                          \
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
    party_place_taken_by(g, PARTY_LEADER, LEADER);
    party_place_taken_by(g, PARTY_FIRST_MEMBER, FIRST);
    party_place_left_empty(g, PARTY_SECOND_MEMBER);
    for (PartyPlace place = 0; place < PARTY_PLACES; place++)
        set_field_trail_position(g, place, 0);   /* nobody draws away: the member waits */
    set_field_trail_ground(g, 0, 0);               /* on plain ground */
    set_field_trail_movement_x(g, 0, 0);
    set_field_trail_movement_y(g, 0, 0);
    set_field_idle_frames(g, 0);
    set_field_object_looped_animation_unchecked(g, PAST_THE_TABLES, SOMETHING_ELSE);
    set_field_object_animation_frame_unchecked(g, PAST_THE_TABLES, 7);

    CHECK(party_place_steps(g, PARTY_SECOND_MEMBER), "an emptied place steps");
    CHECK(!party_member_moves_on(g, PARTY_SECOND_MEMBER), "the member waits");
    party_member_follows(g, WAITING, PARTY_SECOND_MEMBER);
    CHECK(field_object_looped_animation_unchecked(g, PAST_THE_TABLES) == ANIMATION_STAND,
          "the slot past the tables shows the stand");
    CHECK(field_object_animation_frame_unchecked(g, PAST_THE_TABLES) == 0, "from its first frame");
    return failures != 0;
}
