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
bool scene_on_program_page(Snes *s)
{
    SceneProgramPage page;
    return scene_program_page_of(m_direct_page(s), &page);
}

bool scene_on_program_page_io_bank(Snes *s) { return scene_on_program_page(s) && m_bank_reaches_io(s); }

/* The sequence channel whose page is at `d`. */
bool scene_channel_at(uint16_t d, SceneChannel *ch)
{
    SceneProgramPage page;
    if (!scene_program_page_of(d, &page) || page.kind != SCENE_PROGRAM_CHANNEL)
        return false;
    *ch = page.index;
    return true;
}

/* A routine a script calls runs on its channel's page. */
bool scene_on_channel_page(Snes *s)
{
    SceneChannel ch;
    return scene_channel_at(m_direct_page(s), &ch);
}

/* The page an entry's caller has D on, which its guard has checked is one. */
SceneProgramPage scene_program_page_at_entry(const Snes *s)
{
    SceneProgramPage page = scene_program_own();
    scene_program_page_of(m_direct_page(s), &page);
    return page;
}

