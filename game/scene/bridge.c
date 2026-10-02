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
enum { DATA_BANK_AFTER = 0x00 };

void scene_bridge_scale_picture(Snes *s, Racer r, bool row_nudge)
{
    uint32_t call = r == RACER_CRONO ? CRONO_SCALING_CALL : JOHNNY_SCALING_CALL;
    m_push16(s, (uint16_t)(call + JSR_LENGTH - 1));
    m_push8(s, DATA_BANK_AFTER);
    m_push8(s, (uint8_t)(SCALER_CALL >> 16));
    m_push16(s, (uint16_t)(SCALER_CALL + JSL_LENGTH - 1));
    s->cpu.db = SCENE_UPPER_WORK_RAM;
    m_set_flag(s, CPU_FLAG_C, row_nudge);
    m_stop_at(s, SCALER_ROWS, DC_A | DC_X | DC_Y | DC_N | DC_Z);
}

/* ---- The race's setup ------------------------------------------------------------------------- */

enum { JSR_SIZE_HERE = 3 };

void scene_bridge_enter(Snes *s, uint32_t call) { m_push16(s, (uint16_t)(call + JSR_SIZE_HERE - 1)); }

void scene_bridge_leave(Snes *s) { (void)m_pull16(s); }

/* The program's unpacker ($7E:3691): the resource's place in the program's
 * resource list in X, and the unpacking into the destination the caller has
 * set; it reads nothing else but the overflow, the data bank and the direct
 * page, which it hands back. */
enum { UNPACK = 0x7E3691, UNPACK_IGNORES = DC_A | DC_Y | DC_N | DC_Z | DC_C };

void scene_bridge_unpack(Snes *s, uint32_t site, uint16_t resource)
{
    s->cpu.x = resource;
    m_jsr_dc(s, site, UNPACK, UNPACK_IGNORES);
}

/* While the setup works on the racers' pictures the program keeps the data
 * bank and the direct page on the stack, works through the work RAM port's
 * registers' page with the data bank on its pictures' bank, and its sums have
 * left the overflow clear. */
enum { REGISTERS_PAGE = 0x2100 };

void scene_bridge_unpack_holding(Snes *s, uint32_t site, uint16_t resource)
{
    uint8_t bank = s->cpu.db;
    uint16_t page = s->cpu.d;
    m_push8(s, bank);
    m_push16(s, page);
    s->cpu.d = REGISTERS_PAGE;
    s->cpu.db = SCENE_UPPER_WORK_RAM;
    m_set_flag(s, CPU_FLAG_V, false);
    scene_bridge_unpack(s, site, resource);
    s->cpu.d = m_pull16(s);
    s->cpu.db = m_pull8(s);
}

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

/* The camera script's first step: the program's routine for channel 0's
 * ($7E:3749), called with 16-bit A and its data bank kept on the stack. It
 * reads Y and the overflow: Y as the divisors' clear left it, past the table
 * (scene_race_divisors' 256 bytes from $1446, and one), and the overflow clear. */
enum { FIRST_STEP_CALL = 0x7E3CD8, CHANNEL_0_STEP = 0x7E3749, DIVISORS_CLEARED_TO = 0x1546 };
enum { FIRST_STEP_IGNORES = DC_A | DC_X | DC_N | DC_Z | DC_C };

void scene_bridge_first_script_step(Snes *s)
{
    uint8_t bank = s->cpu.db;
    m_push8(s, bank);
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    s->cpu.y = DIVISORS_CLEARED_TO;
    m_set_flag(s, CPU_FLAG_V, false);
    m_jsr_dc(s, FIRST_STEP_CALL, CHANNEL_0_STEP, FIRST_STEP_IGNORES);
    s->cpu.db = m_pull8(s);
}
