/* The program from power-on: boot the services, then run field, scene and
 * warp modes as ordinary calls. Modes yield frames through the timing service
 * and install the callbacks the frame driver invokes. */
#include "core/program.h"
#include "charge.h"
#include "core/sprite_table.h"

#include "field/main_loop.h"
#include "scene/scene.h"
#include "warp/warp.h"
#include "platform/input.h"
#include "platform/timing.h"
#include "platform/video.h"

/* ---- The boot -------------------------------------------------------------------------------- */

/* The picture at the start: forced blank; the sprites 16x16 and 32x32 with
 * their tiles at VRAM word $6000; mode 1 with BG3 in front; every tilemap,
 * character base and scroll at 0 and the mosaic off; the Mode 7 plane
 * unflipped with the identity matrix; no window but window 1 on BG3, drawing
 * outside it; no color math; all five layers on both screens; and a black
 * fixed color (its red left as it was). It matters out of proportion to its
 * size: everything from power-on starts from the state the calls build. */
static const VidScreen power_on_screen = {
    .display = &(const VidDisplay){ .blank = true },
    .sprites = { .size_select = 3, .base = 0x6000 },
    .mode = { .mode = 1, .bg3_priority = true },
    .mosaic_size = 1,
    .bg = { { VID_TILEMAP(0x0000, 32, 32) }, { VID_TILEMAP(0x0000, 32, 32) }, { VID_TILEMAP(0x0000, 32, 32) },
            { VID_TILEMAP(0x0000, 32, 32) } },
    .mode7 = { .a = 0x0100, .d = 0x0100 },
    .windows = { .w1 = { [VID_BG3] = VID_WIN_OUT } },
    .main_layers = VID_ALL_LAYERS,
    .sub_layers = VID_ALL_LAYERS,
    /* VRAM written a byte at a time, the address stepping after the low one */
    .vram_port = { .step = 1, .after_high = false },
};

/* The controller port's programmable lines, all high: nothing latched. */
enum { IO_LINES_RELEASED = 0xFF };

// $FD:C016
void program_boot(GameState *g, Platform *pf)
{
    (void)g;
    tm_fast_rom(pf, PF_AT(0xFDC016), true);
    tm_enable_interrupts(pf, PF_AT(0xFDC01A), 0);
    vid_stop_uploads(pf, PF_AT(0xFDC01C));
    vid_enable_line_effects(pf, PF_AT(0xFDC01E), 0);

    tm_reset_timers(pf, PF_AT(0xFDC02A));
    in_set_io_lines(pf, PF_AT(0xFDC034), IO_LINES_RELEASED);
    vid_apply_screen(pf, PF_AT(0xFDC036), &power_on_screen);
}

/* Mode callbacks live in the program's own state. Preserve the established
 * work estimate for reading a four-byte vector on each enabled frame event;
 * selecting a callback no longer reads or executes that encoding. */
enum { HANDLER_SELECTION_ACCESSES = 4, IGNORE_HANDLERS_ACCESSES = 16 };

void program_install_vblank_handler(GameState *g, ProgramHandler handler)
{
    program_handlers(g)->vblank = handler;
}

void program_hand_the_vblank_to(GameState *g, ProgramHandler handler)
{
    program_install_vblank_handler(g, handler);
    sprite_table_forget_places(g);
}

void program_install_line_handler(GameState *g, ProgramHandler handler)
{
    program_handlers(g)->line = handler;
}

void program_ignore_interrupts(GameState *g)
{
    *program_handlers(g) = (ProgramHandlers){ 0 };
    charge_accesses(g, IGNORE_HANDLERS_ACCESSES);
}

void program_vblank(GameState *g, Platform *pf)
{
    charge_accesses(g, HANDLER_SELECTION_ACCESSES);
    ProgramHandler handler = program_handlers(g)->vblank;
    if (handler)
        handler(g, pf);
}

void program_line(GameState *g, Platform *pf)
{
    charge_accesses(g, HANDLER_SELECTION_ACCESSES);
    ProgramHandler handler = program_handlers(g)->line;
    if (handler)
        handler(g, pf);
}

/* ---- The main thread ------------------------------------------------------------------------- */

enum { FIELD_ENTER = 0xC0005D };

/* After power-on the game goes between the field and the scenes: the field
 * runs until it ends into a scene (an event's) or a time gate's tunnel, the
 * scene until its script ends it, and the field starts again. A game over
 * starts the game afresh from the title, as the field's first start did at
 * power-on; a scene or a script that resets the console starts it from the
 * boot. A driver may start the game in scenes after the power-on, each with
 * the song the field would have had playing (program_starts_in_a_scene). */
static bool first_scenes_run(GameState *g, Platform *pf)
{
    uint8_t scene, song;
    while (program_first_scene(g, &scene, &song)) {
        if (song)
            scene_start_song(pf, song);
        if (!scene_runs(g, pf, scene))
            return false;
        if (program_console_resets(g)) {
            program_boot(g, pf);
            field_power_on(g, pf);
        }
    }
    return true;
}

void program_main_thread(GameState *g, Platform *pf)
{
    program_boot(g, pf);
    field_power_on(g, pf);
    if (!first_scenes_run(g, pf))
        return;
    for (;;) {
        field_runs(g, pf);
        if (program_went_no_further(g))
            break;
        if (program_starts_afresh(g)) {
            field_power_on(g, pf);
            continue;
        }
        if (program_console_resets(g)) {
            program_boot(g, pf);
            field_power_on(g, pf);
            continue;
        }
        uint8_t scene;
        if (!program_next_scene(g, &scene))
            break;
        bool ran = scene < WARP_FIRST_SCENE ? scene_runs(g, pf, scene) : warp_runs(g, pf, scene);
        if (!ran)
            break;
        if (program_console_resets(g)) {
            program_boot(g, pf);
            field_power_on(g, pf);
        }
    }
    program_fail(g, FIELD_ENTER, "a mode returned without a continuation");
}
