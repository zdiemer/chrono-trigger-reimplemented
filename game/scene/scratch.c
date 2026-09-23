/* Scenes: the math scratch cells, as the helpers use them.
 *
 * The scene's multiply and divide helpers keep their operands and results in
 * sixteen bytes on whichever page their caller has: the scene's own, or a
 * sequence channel's when a script calls a routine of the program. The map
 * names that as a page kind with both as instances (re/ram.toml,
 * scene_math_scratch), so a port says which page it means with a
 * SceneMathPage and the generated accessor knows where that is. What is left
 * here is the widths: the helpers read and write the cells as bytes, words and
 * 32-bit pairs of words, low byte first as the 65816 stores them. */
#include "scene/scene.h"

#include "vars.h"

uint8_t scene_scratch8(const GameState *g, SceneMathPage page, unsigned at)
{
    return scene_math_scratch_byte(g, page, at);
}

void set_scene_scratch8(GameState *g, SceneMathPage page, unsigned at, uint8_t v)
{
    set_scene_math_scratch_byte(g, page, at, v);
}

uint16_t scene_scratch16(const GameState *g, SceneMathPage page, unsigned at)
{
    return (uint16_t)(scene_scratch8(g, page, at) | scene_scratch8(g, page, at + 1) << 8);
}

void set_scene_scratch16(GameState *g, SceneMathPage page, unsigned at, uint16_t v)
{
    set_scene_scratch8(g, page, at, (uint8_t)v);
    set_scene_scratch8(g, page, at + 1, (uint8_t)(v >> 8));
}

uint32_t scene_scratch32(const GameState *g, SceneMathPage page, unsigned at)
{
    return scene_scratch16(g, page, at) | (uint32_t)scene_scratch16(g, page, at + 2) << 16;
}

void set_scene_scratch32(GameState *g, SceneMathPage page, unsigned at, uint32_t v)
{
    set_scene_scratch16(g, page, at, (uint16_t)v);
    set_scene_scratch16(g, page, at + 2, (uint16_t)(v >> 16));
}
