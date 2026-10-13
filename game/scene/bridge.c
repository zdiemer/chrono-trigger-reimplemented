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
#include "port/native.h"

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

/* The shared endings' main loop hands their picture to the scaler from
 * $7E:8930, which keeps the data bank to come back to on the stack (the low
 * byte of the word it pushes to set the scaler's) and calls the scaler's start
 * from $7E:897F; the scaler's rows then run as the original's, and its return
 * lands on the routine's gathering ($7E:8983). The carry its first row takes is
 * the last shift's, of a zoom under $8000: clear. */
enum { ENDINGS_SCALER_CALL = 0x7E897F };

void scene_bridge_scale_ending_picture(Snes *s)
{
    m_push8(s, DATA_BANK_AFTER);
    m_push8(s, (uint8_t)(ENDINGS_SCALER_CALL >> 16));
    m_push16(s, (uint16_t)(ENDINGS_SCALER_CALL + JSL_LENGTH - 1));
    on_page(s, SCENE_PAGE, SCENE_UPPER_WORK_RAM);
    m_set_flag(s, CPU_FLAG_C, false);
    m_stop_at(s, SCALER_ROWS, DC_A | DC_X | DC_Y | DC_N | DC_Z);
}

/* ---- The race's setup ------------------------------------------------------------------------- */

enum { JSR_SIZE_HERE = 3 };

void scene_bridge_enter(Snes *s, uint32_t call) { m_push16(s, (uint16_t)(call + JSR_SIZE_HERE - 1)); }

void scene_bridge_leave(Snes *s) { (void)m_pull16(s); }

/* A scene's song, from the program's routine that starts one ($7E:375F,
 * called from a setup at `call`): the driver's command, with its volume and
 * echo parameters at $FF. */
enum { SONG_DRIVER_SITE = 0x7E3775, SOUND_DRIVER = 0xC70004 };
enum { SONG_DRIVER_IGNORES = DC_AL | DC_X | DC_N | DC_Z | DC_C | DC_D };

void scene_bridge_start_song(Snes *s, uint32_t call, uint8_t song)
{
    scene_bridge_enter(s, call);
    au_command(m_platform(s), PF_CALL_DC(SONG_DRIVER_SITE, SOUND_DRIVER, SONG_DRIVER_IGNORES),
               (AudioCommand){ .command = AU_START_SONG, .p1 = song, .p2 = AU_FULL_VOLUME, .p3 = AU_FULL_VOLUME,
                               .count = 3 });
    scene_bridge_leave(s);
}

/* ---- A sibling run as C ------------------------------------------------------------------------ */

enum { JSL_SIZE_HERE = 4 };

/* The call pushes its return address, under which the sibling runs as C,
 * timed as its routine, where the registry would run it there; anywhere else
 * (no port, or its guard or widths turn the state away) the call is a real
 * one, with the don't-care set it declares. */
static bool run_entry(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *),
                      bool long_call)
{
    uint16_t stack = s->cpu.s;
    uint32_t returns_to = site + (long_call ? JSL_SIZE_HERE : JSR_SIZE_HERE);
    if (long_call)
        m_push8(s, (uint8_t)(site >> 16));
    m_push16(s, (uint16_t)(returns_to - 1));
    s->cpu.pb = (uint8_t)(routine >> 16);
    s->cpu.pc = (uint16_t)routine;
    const NativeEntry *e = entry ? native_hand_entry(s) : NULL;
    if (!e || e->fn != entry) {
        s->cpu.s = stack;
        if (long_call)
            m_jsl_dc(s, site, routine, dont_care);
        else
            m_jsr_dc(s, site, routine, dont_care);
        return m_came_back(s, returns_to, stack);
    }
    NATIVE_TIME_AS(s, routine, entry(s));
    return m_came_back(s, returns_to, stack);
}

void scene_bridge_forget_the_stop(Snes *s) { native_dont_care_at_stop(s, 0, 0); }

bool scene_run(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, dont_care, entry, false);
}

bool scene_run_long(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, dont_care, entry, true);
}
