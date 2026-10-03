/* Scenes: where the ports meet the scene program's calling conventions.
 *
 * A port whose work goes on in the original's code -- a loop that runs over
 * frames, entered as a routine the port's own original would have called --
 * leaves the machine as the original does at that point: the frames of the
 * calls it has inlined on the stack, the registers the code there reads. Those
 * mechanics are here, apart from the ports' logic (scene/scene.h). */
#include "scene/race.h"

#include "platform/audio.h"
#include "port/machine.h"

/* The main loop hands a racer's picture to the scaler from its routine at
 * $7E:51F4, called from one site per racer, which keeps the data bank to come
 * back to on the stack and calls the scaler's start; the scaler's rows then
 * run as the original's from the head of its row loop, and its return lands on
 * the routine's wait for the upload ($7E:5211). */
enum {
    CRONO_SCALING_CALL = 0x7E51DC, JOHNNY_SCALING_CALL = 0x7E51ED, JSR_LENGTH = 3,
    SCALER_CALL = 0x7E520D, JSL_LENGTH = 4,
    SCALER_ROWS = 0xC30309,
};

/* The data bank the scaling routine comes back to: the low byte of the word it
 * pushes to set the scaler's. */
enum { DATA_BANK_AFTER = 0x00, SCENE_PAGE = 0x0000 };

/* The direct page and the data bank the original has at a point. */
static void on_page(Snes *s, uint16_t page, uint8_t bank)
{
    s->cpu.d = page;
    s->cpu.db = bank;
}

void scene_bridge_scale_picture(Snes *s, Racer r, bool row_nudge)
{
    uint32_t call = r == RACER_CRONO ? CRONO_SCALING_CALL : JOHNNY_SCALING_CALL;
    m_push16(s, (uint16_t)(call + JSR_LENGTH - 1));
    m_push8(s, DATA_BANK_AFTER);
    m_push8(s, (uint8_t)(SCALER_CALL >> 16));
    m_push16(s, (uint16_t)(SCALER_CALL + JSL_LENGTH - 1));
    on_page(s, SCENE_PAGE, SCENE_UPPER_WORK_RAM);
    m_set_flag(s, CPU_FLAG_C, row_nudge);
    m_stop_at(s, SCALER_ROWS, DC_A | DC_X | DC_Y | DC_N | DC_Z);
}

/* ---- The race's setup ------------------------------------------------------------------------- */

enum { JSR_SIZE_HERE = 3 };

void scene_bridge_enter(Snes *s, uint32_t call) { m_push16(s, (uint16_t)(call + JSR_SIZE_HERE - 1)); }

void scene_bridge_leave(Snes *s) { (void)m_pull16(s); }

/* The race's song, from the program's routine that starts one ($7E:375F,
 * called from the setup at $7E:3B5A): the driver's command, with its volume
 * and echo parameters at $FF. */
enum { SONG_CALL = 0x7E3B5A, SONG_DRIVER_SITE = 0x7E3775, SOUND_DRIVER = 0xC70004 };
enum { SONG_DRIVER_IGNORES = DC_AL | DC_X | DC_N | DC_Z | DC_C };

void scene_bridge_start_song(Snes *s, uint8_t song)
{
    scene_bridge_enter(s, SONG_CALL);
    au_command(m_platform(s), PF_CALL_DC(SONG_DRIVER_SITE, SOUND_DRIVER, SONG_DRIVER_IGNORES),
               (AudioCommand){ .command = AU_START_SONG, .p1 = song, .p2 = AU_FULL_VOLUME, .p3 = AU_FULL_VOLUME,
                               .count = 3 });
    scene_bridge_leave(s);
}
