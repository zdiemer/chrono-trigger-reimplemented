/* Scenes: a scene's song, as the setups start it. */
#include "scene/race.h"

#include "charge.h"
#include "core/program.h"
#include "platform/audio.h"
#include "platform/timing.h"
#include "port/vars_ram.h"

/* Where the program's routine calls the sound driver. */
enum { SONG_DRIVER_SITE = 0x7E3775, SOUND_DRIVER = 0xC70004 };

// $7E:375F
/* A scene's song started: the driver's command, with its volume and echo
 * parameters at $FF. */
void scene_start_song(Platform *pf, uint8_t song)
{
    au_command(pf, PF_CALL(SONG_DRIVER_SITE, SOUND_DRIVER),
               (AudioCommand){ .command = AU_START_SONG, .p1 = song, .p2 = AU_FULL_VOLUME, .p3 = AU_FULL_VOLUME,
                               .count = 3 });
}

