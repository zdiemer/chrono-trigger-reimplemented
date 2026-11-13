/* Checks that the native sound player lives in the arena (runtime/port/arena.h)
 * beside the game's state: a game call that hands it a command can be where a
 * fiber stops for the frame, so a snapshot of that fiber names the player's
 * address, and a later run that loads the snapshot must find the player
 * there. Exits non-zero on failure.
 *
 *   sound_native_test */
#include <stdint.h>
#include <stdio.h>

#include "port/arena.h"
#include "port/state_host.h"
#include "sound_native.h"

int main(void)
{
    static Platform pf;
    GameState *g = state_host_create_lasting(NULL);
    SoundNative *n = g ? sound_native_create(g, &pf) : NULL;
    uint8_t *next = arena_take(1);
    int failures = 0;
    if (!g || !n || !next) {
        printf("FAIL: the game, its player or the arena's next bytes not made\n");
        return 1;
    }
    if (!((uintptr_t)n > (uintptr_t)g && (uintptr_t)n < (uintptr_t)next)) {
        printf("FAIL: the sound player at %p is not in the arena between the game at %p and %p\n", (void *)n,
               (void *)g, (void *)next);
        failures++;
    }
    if (pf.audio.ctx != n) {
        printf("FAIL: the platform's audio hooks don't name the player\n");
        failures++;
    }
    sound_native_destroy(n);
    if (pf.audio.ctx) {
        printf("FAIL: the player's hooks left on the platform after it is destroyed\n");
        failures++;
    }
    return failures != 0;
}
