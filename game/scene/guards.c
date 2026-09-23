/* Scenes: what the ports assume of the machine when they're entered.
 *
 * The ports name the scene's cells through game/vars.h, which knows them where
 * the scene engine keeps them: on its direct page, D = $0000 (re/ram.toml's
 * `scene` page), and through a data bank that reaches low RAM. The original's
 * code reaches the same cells relative to D and the data bank, so it is only
 * the same routine when those are what the game always enters it with; these
 * are the registry guards that say so, and a call under any other machine
 * falls back to the original. The movies never make one (a guard that turned
 * a call away shows as a drop in its shadow count). */
#include "scene/scene.h"

#include "port/machine.h"
#include "port/vars_ram.h"

bool scene_on_scene_page(Snes *s) { return m_direct_page(s) == 0x0000; }

bool scene_on_scene_page_low_bank(Snes *s) { return scene_on_scene_page(s) && m_bank_reaches_low_ram(s); }

/* The work RAM port and the multiplier are reached through the data bank too. */
bool scene_on_scene_page_io_bank(Snes *s) { return scene_on_scene_page(s) && m_bank_reaches_io(s); }

/* The arithmetic works on the cells of whichever page it's called on: the
 * scene's own or a sequence channel's, the instances re/ram.toml declares for
 * them (a D that is neither is refused, and counted). */
bool scene_on_math_page(Snes *s)
{
    SceneMathPage page;
    return scene_math_page_of(m_direct_page(s), &page);
}

bool scene_on_math_page_io_bank(Snes *s) { return scene_on_math_page(s) && m_bank_reaches_io(s); }

/* A routine a script calls runs on its channel's page. */
bool scene_on_channel_page(Snes *s)
{
    SceneMathPage page;
    return scene_math_page_of(m_direct_page(s), &page) && page.kind == SCENE_MATH_CHANNEL;
}

/* The page an entry's caller has D on, which its guard has checked is one. */
SceneMathPage scene_math_page_at_entry(const Snes *s)
{
    SceneMathPage page = scene_math_scene();
    scene_math_page_of(m_direct_page(s), &page);
    return page;
}

/* ---- The timing gate's estimate of a port's block moves --------------------------------------
 *
 * A port's time is estimated from its bus accesses times one scalar, calibrated
 * over its calls (docs/RECOMP.md, "The timing gate"). The circle and the scaler
 * copy and clear memory a byte at a time, as the original's block moves do, and
 * do their other work in locals where the original works in memory, so no one
 * scalar fits both. A block move's time is per byte whatever routine makes it,
 * so theirs is attributed to the block move stub ($7E:9690, core/memory.c),
 * whose own calls calibrate it. Only an entry's own wrapper names its machine
 * for this; a C caller (the menus' iris) leaves it unnamed, and the logic's
 * calls, which go through ORIGINAL_EFFECT, are gone from a native build. */
#include "port/native.h"

enum { BLOCK_MOVE_STUB = 0x7E9690 };

static Snes *timed_machine;
static int time_was;

void scene_time_entry(Snes *s) { timed_machine = s; }

void scene_time_block_move(bool starting)
{
    if (!timed_machine)
        return;
    if (starting)
        time_was = native_time_as(timed_machine, BLOCK_MOVE_STUB);
    else
        native_time_end(timed_machine, time_was);
}
