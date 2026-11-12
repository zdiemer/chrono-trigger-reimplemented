/* Scenes: where the ports meet the scene program's calling conventions.
 *
 * Checked C calls and held waits retain the legacy caller frames and return
 * conventions while the shipping runtime still uses the machine. Those
 * mechanics are here, apart from the game operations (scene/scene.h). */
#include "scene/race.h"

#include "charge.h"
#include "core/program.h"
#include "platform/audio.h"
#include "platform/timing.h"
#include "port/vars_ram.h"

/* ---- The race's setup ------------------------------------------------------------------------- */

/* A scene's song, from the program's routine that starts one ($7E:375F,
 * called from a setup at `call`): the driver's command, with its volume and
 * echo parameters at $FF. */
enum { SONG_DRIVER_SITE = 0x7E3775, SOUND_DRIVER = 0xC70004 };

void scene_start_song(Platform *pf, uint8_t song)
{
    au_command(pf, PF_CALL(SONG_DRIVER_SITE, SOUND_DRIVER),
               (AudioCommand){ .command = AU_START_SONG, .p1 = song, .p2 = AU_FULL_VOLUME, .p3 = AU_FULL_VOLUME,
                               .count = 3 });
}

/* ---- The setups' loading and stars ------------------------------------------------------------- */

/* The stars scattered at random ($7E:5777), as a setup has them scattered:
 * their first draw from what the setup's call before left in A and the carry,
 * which it hands on unexamined; the overflow the last draw leaves is the
 * setup's after it. */
enum { SCATTER_STARS = 0x7E5777 };

