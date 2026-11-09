/* Scenes: where the ports meet the scene program's calling conventions.
 *
 * Checked C calls and held waits retain the legacy caller frames and return
 * conventions while the shipping runtime still uses the machine. Those
 * mechanics are here, apart from the game operations (scene/scene.h). */
#include "scene/race.h"

#include "charge.h"
#include "core/program.h"
#include "holds.h"
#include "platform/audio.h"
#include "platform/timing.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"

/* A main loop hands a picture to the scaler from a routine of its scene's,
 * which keeps the data bank to come back to on the stack (the low byte of the
 * word it pushes to set the scaler's) and calls the scaler's start; the
 * scaler's rows then run as the original's from the head of its row loop,
 * the first taking the carry the routine left, and its return lands on the
 * routine's code after the call. */
enum { JSR_LENGTH = 3, JSL_LENGTH = 4, SCALER_ROWS = 0xC30309 };
enum { DATA_BANK_AFTER = 0x00, SCENE_PAGE = 0x0000 };

/* The direct page and the data bank the original has at a point. */
static void on_page(Snes *s, uint16_t page, uint8_t bank)
{
    s->cpu.d = page;
    s->cpu.db = bank;
}

static bool call_the_scaler(Snes *s, uint32_t call, bool row_nudge)
{
    m_push8(s, DATA_BANK_AFTER);
    m_push8(s, (uint8_t)(call >> 16));
    m_push16(s, (uint16_t)(call + JSL_LENGTH - 1));
    on_page(s, SCENE_PAGE, SCENE_UPPER_WORK_RAM);
    m_set_flag(s, CPU_FLAG_C, row_nudge);
    if (!scene_accept_continuation(s, SCALER_ROWS, scene_scale_bitmap_row)) {
        /* The original scaler reads the row nudge, not the setup's scratch
         * registers. Retain the original boundary's liveness contract. */
        m_stop_at(s, SCALER_ROWS, DC_A | DC_X | DC_Y | DC_N | DC_Z);
        return false;
    }
    TIMED_AS(state_of(s), SCALER_ROWS, scene_scale_bitmap_row(s));
    return true;
}

/* The race's routine ($7E:51F4) is called from one site per racer, and calls
 * the scaler from $7E:520D; its return lands on the routine's wait for the
 * upload ($7E:5211). */
enum { CRONO_SCALING_CALL = 0x7E51DC, JOHNNY_SCALING_CALL = 0x7E51ED, RACE_SCALER_CALL = 0x7E520D };

void scene_bridge_scale_picture(Snes *s, Racer r, bool row_nudge)
{
    uint32_t call = r == RACER_CRONO ? CRONO_SCALING_CALL : JOHNNY_SCALING_CALL;
    m_push16(s, (uint16_t)(call + JSR_LENGTH - 1));
    if (!call_the_scaler(s, RACE_SCALER_CALL, row_nudge))
        return;
    if (m_runs_retired_as_c(s)) {
        m_hold_at(s, 0x7E5211, DC_A | DC_X | DC_Y | DC_N | DC_Z, HOLD_SCENE_RACE_UPLOAD);
        return;
    }
    /* The two racers use this wait at the same stack depth. The shadow wait
     * service cannot yet distinguish Johnny's wait from Crono's already
     * satisfied one; keep this boundary until it can (docs/TODO.md). */
    m_stop_at(s, 0x7E5211, DC_A | DC_X | DC_Y | DC_N | DC_Z);
}

/* The shared endings' ($7E:8930) and the Epoch's dive's ($7E:8278) are the
 * main loop's call itself, and call the scaler from $7E:897F and $7E:82A0,
 * whose returns land on their gathering ($7E:8983, $7E:82A4). The carry the
 * first row takes is the last shift's of a zoom under $8000: clear. */
enum { ENDINGS_SCALER_CALL = 0x7E897F, DIVE_SCALER_CALL = 0x7E82A0 };

void scene_bridge_scale_scene_picture(Snes *s, ScenePictureScaling which)
{
    if (!call_the_scaler(s, which == SCALING_THE_EPOCH ? DIVE_SCALER_CALL : ENDINGS_SCALER_CALL, false))
        return;
    uint32_t at = which == SCALING_THE_EPOCH ? 0x7E82A4 : 0x7E8983;
    void (*gather)(Snes *) = which == SCALING_THE_EPOCH ? scene_gather_the_epoch : scene_ending_gather_picture;
    if (scene_accept_continuation(s, at, gather))
        TIMED_AS(state_of(s), at, gather(s));
}

/* ---- The race's setup ------------------------------------------------------------------------- */

enum { JSR_SIZE_HERE = 3 };

/* ---- The program's unpacker and a script's first step ----------------------------------------- */

void scene_bridge_unpack(Snes *s, uint32_t site, uint16_t resource)
{
    s->cpu.x = resource;
    if (m_runs_retired_as_c(s))
        (void)scene_run_called(s, site, SCENE_UNPACK, SCENE_UNPACK_IGNORES, scene_unpack_resource);
    else
        /* The still-gated caller keeps the unpacker's comparison boundary;
         * its existing timing allowance belongs to that resource load. */
        m_jsr_dc(s, site, SCENE_UNPACK, SCENE_UNPACK_IGNORES);
}

void scene_bridge_take_the_first_script_step(Snes *s, uint32_t site, uint16_t y, uint8_t kept_bank)
{
    m_push8(s, kept_bank);
    m_set_flag(s, CPU_FLAG_M, false);
    s->cpu.y = y;
    (void)scene_run_called(s, site, SCENE_FIRST_STEP, SCENE_FIRST_STEP_IGNORES, scene_run_channel_0_script);
    s->cpu.db = m_pull8(s);
}

void scene_bridge_back_on_the_scene_s_page(Snes *s)
{
    m_set_flag(s, CPU_FLAG_M, true);
    s->cpu.d = SCENE_PAGE;
}

void scene_bridge_first_script_step(Snes *s, uint32_t site, uint16_t y)
{
    scene_bridge_take_the_first_script_step(s, site, y, s->cpu.db);
    scene_bridge_back_on_the_scene_s_page(s);
}

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
    scene_start_song(m_platform(s), song);
    scene_bridge_leave(s);
}

void scene_start_song(Platform *pf, uint8_t song)
{
    au_command(pf, PF_CALL_DC(SONG_DRIVER_SITE, SOUND_DRIVER, SONG_DRIVER_IGNORES),
               (AudioCommand){ .command = AU_START_SONG, .p1 = song, .p2 = AU_FULL_VOLUME, .p3 = AU_FULL_VOLUME,
                               .count = 3 });
}

/* ---- The setups' loading and stars ------------------------------------------------------------- */

/* The stars scattered at random ($7E:5777), as a setup has them scattered:
 * their first draw from what the setup's call before left in A and the carry,
 * which it hands on unexamined; the overflow the last draw leaves is the
 * setup's after it. */
enum { SCATTER_STARS = 0x7E5777 };

void scene_bridge_scatter_stars(Snes *s, uint16_t left_in_a)
{
    GameState *g = state_of(s);
    SceneDraw first = scene_draw_from(left_in_a, m_flag(s, CPU_FLAG_C), TM_ENTROPY_BYTES);
    TIMED_AS(g, SCATTER_STARS, scene_scatter_stars(g, m_platform(s), first));
}

/* ---- A sibling run as C ------------------------------------------------------------------------ */

enum { JSL_SIZE_HERE = 4 };

/* A tail continuation has its own contract, even when its caller is C.
 * Leave the original at that entry, with its call frame intact, if the
 * registry refuses the widths, decimal mode, page, data or code image. */
bool scene_accept_continuation(Snes *s, uint32_t at, void (*entry)(Snes *))
{
    m_jump_to(s, at);
    const NativeEntry *e = native_continuation_entry_at(s, at);
    if ((entry && e && e->fn == entry) ||
        (m_runs_retired_as_c(s) && scene_machine_form_takes(s, entry)))
        return true;
    m_stop_at_own(s, at, 0);
    return false;
}

/* The call retains its return frame and calibrated time. Shipping calls
 * enforce the retired contracts; the reference path keeps the original
 * call and its comparison declarations for enclosing gated callers. */
static bool run_entry(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *),
                      bool long_call, bool replaces_call_out)
{
    uint16_t stack = s->cpu.s;
    uint32_t returns_to = site + (long_call ? JSL_SIZE_HERE : JSR_SIZE_HERE);
    if (long_call)
        m_push8(s, (uint8_t)(site >> 16));
    m_push16(s, (uint16_t)(returns_to - 1));
    s->cpu.pb = (uint8_t)(routine >> 16);
    s->cpu.pc = (uint16_t)routine;
    if (replaces_call_out)
        charge_settle(state_of(s));
    const NativeEntry *e = entry ? native_hand_entry(s) : NULL;
    if ((!e || e->fn != entry) && !(m_runs_retired_as_c(s) && scene_machine_form_takes(s, entry))) {
        if (m_runs_retired_as_c(s)) {
            if (!entry)
                program_breaks_a_contract(state_of(s), routine, "a scene call outside its C table");
            m_stop_at_own(s, routine, 0);
            return false;
        }
        s->cpu.s = stack;
        if (long_call)
            m_jsl_dc(s, site, routine, dont_care);
        else
            m_jsr_dc(s, site, routine, dont_care);
        return m_came_back(s, returns_to, stack);
    }
    NATIVE_TIME_AS(s, routine, entry(s));
    bool returned = m_came_back(s, returns_to, stack);
    if (replaces_call_out && returned)
        charge_settle(state_of(s));
    return returned;
}

bool scene_run(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, dont_care, entry, false, false);
}

bool scene_run_long(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, dont_care, entry, true, false);
}

/* A former machine call settled the caller before dispatch and the callee
 * before returning. Keep those boundaries when the call becomes C. */
bool scene_run_called(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, dont_care, entry, false, true);
}

bool scene_run_long_called(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *))
{
    return run_entry(s, site, routine, dont_care, entry, true, true);
}
