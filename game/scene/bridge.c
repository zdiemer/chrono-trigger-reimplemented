/* Scenes: where the ports meet the scene program's calling conventions.
 *
 * A port whose work goes on in the original's code -- a loop that runs over
 * frames, entered as a routine the port's own original would have called --
 * leaves the machine as the original does at that point: the frames of the
 * calls it has inlined on the stack, the registers the code there reads. Those
 * mechanics are here, apart from the ports' logic (scene/scene.h). */
#include "scene/race.h"

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
