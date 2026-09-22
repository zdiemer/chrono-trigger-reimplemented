/* Scenes: the math scratch cells on the caller's direct page.
 *
 * The scene's multiply and divide helpers work on sixteen bytes at $F0-$FF of
 * whichever direct page their caller has: the scene's own (D = $0000,
 * scene_math_scratch) or a sequence channel's (D = $0A00 or $0B00, when a
 * script calls a routine of the program; scene_channels.math_scratch). The map
 * has no shape for a variable on a page the caller chooses, so the helpers name
 * the page and the offset, and these reach the cells through whichever
 * accessor that page's are. */
#include "scene/scene.h"

#include "vars.h"

enum { SCENE_OWN_PAGE_D = 0x0000, CHANNEL_PAGES_D = 0x0A00, PAGE_SIZE = 0x0100 };

bool scene_page_of(uint16_t d, ScenePage *page)
{
    if (d == SCENE_OWN_PAGE_D) {
        *page = SCENE_OWN_PAGE;
        return true;
    }
    uint16_t channel = (uint16_t)((d - CHANNEL_PAGES_D) / PAGE_SIZE);
    if (d < CHANNEL_PAGES_D || d % PAGE_SIZE || channel >= SCENE_CHANNELS_COUNT)
        return false;
    *page = (ScenePage)channel;
    return true;
}

uint8_t scene_scratch8(const GameState *g, ScenePage page, unsigned at)
{
    return page == SCENE_OWN_PAGE ? scene_math_scratch_byte(g, at) : scene_channels_math_scratch_byte(g, page, at);
}

void set_scene_scratch8(GameState *g, ScenePage page, unsigned at, uint8_t v)
{
    if (page == SCENE_OWN_PAGE)
        set_scene_math_scratch_byte(g, at, v);
    else
        set_scene_channels_math_scratch_byte(g, page, at, v);
}

uint16_t scene_scratch16(const GameState *g, ScenePage page, unsigned at)
{
    return (uint16_t)(scene_scratch8(g, page, at) | scene_scratch8(g, page, at + 1) << 8);
}

void set_scene_scratch16(GameState *g, ScenePage page, unsigned at, uint16_t v)
{
    set_scene_scratch8(g, page, at, (uint8_t)v);
    set_scene_scratch8(g, page, at + 1, (uint8_t)(v >> 8));
}

uint32_t scene_scratch32(const GameState *g, ScenePage page, unsigned at)
{
    return scene_scratch16(g, page, at) | (uint32_t)scene_scratch16(g, page, at + 2) << 16;
}

void set_scene_scratch32(GameState *g, ScenePage page, unsigned at, uint32_t v)
{
    set_scene_scratch16(g, page, at, (uint16_t)v);
    set_scene_scratch16(g, page, at + 2, (uint16_t)(v >> 16));
}
