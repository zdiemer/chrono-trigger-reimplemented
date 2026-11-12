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

/* A main loop hands a picture to the scaler from a routine of its scene's,
 * which keeps the data bank to come back to on the stack (the low byte of the
 * word it pushes to set the scaler's) and calls the scaler's start; the
 * scaler's rows then run as the original's from the head of its row loop,
 * the first taking the carry the routine left, and its return lands on the
 * routine's code after the call. */
enum { JSR_LENGTH = 3, JSL_LENGTH = 4, SCALER_ROWS = 0xC30309 };
enum { DATA_BANK_AFTER = 0x00, SCENE_PAGE = 0x0000 };

/* The race's routine ($7E:51F4) is called from one site per racer, and calls
 * the scaler from $7E:520D; its return lands on the routine's wait for the
 * upload ($7E:5211). */
enum { CRONO_SCALING_CALL = 0x7E51DC, JOHNNY_SCALING_CALL = 0x7E51ED, RACE_SCALER_CALL = 0x7E520D };

/* The shared endings' ($7E:8930) and the Epoch's dive's ($7E:8278) are the
 * main loop's call itself, and call the scaler from $7E:897F and $7E:82A0,
 * whose returns land on their gathering ($7E:8983, $7E:82A4). The carry the
 * first row takes is the last shift's of a zoom under $8000: clear. */
enum { ENDINGS_SCALER_CALL = 0x7E897F, DIVE_SCALER_CALL = 0x7E82A0 };

/* ---- The race's setup ------------------------------------------------------------------------- */

enum { JSR_SIZE_HERE = 3 };

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

/* ---- A sibling run as C ------------------------------------------------------------------------ */

enum { JSL_SIZE_HERE = 4 };
