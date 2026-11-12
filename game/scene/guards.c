/* Scenes: what the ports assume of the machine when they're entered.
 *
 * The ports name the scene's cells through game/vars.h, which knows them where
 * the scene engine keeps them: on its direct page, D = $0000 (re/ram.toml's
 * `scene` page), and through a data bank that reaches low RAM. The original's
 * code reaches the same cells relative to D and the data bank, so it is only
 * the same routine when those are what the game always enters it with; these
 * are the contracts checked by the called machine forms. A refusal reports
 * a broken contract and leaves the original boundary intact. */
#include "scene/scene.h"

#include "port/vars_ram.h"

/* The sequence channel whose page is at `d`. */
bool scene_channel_at(uint16_t d, SceneChannel *ch)
{
    SceneProgramPage page;
    if (!scene_program_page_of(d, &page) || page.kind != SCENE_PROGRAM_CHANNEL)
        return false;
    *ch = page.index;
    return true;
}
