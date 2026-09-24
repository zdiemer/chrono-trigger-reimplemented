/* Battle: the sprite table, cleared each frame before the objects draw.
 *
 * The battle builds its sprites afresh each frame in the sprite table
 * (oam_buffer, sent in the next vblank): first every sprite but the menu's
 * four cursors is hidden below the screen, all of them large, and then the
 * objects draw themselves into it (battle/draw_objects.c). */
#include "battle/bridge.h"
#include "battle/entries.h"
#include "vars.h"

enum { CURSOR_SPRITES = 4, HIDDEN_Y = 0xF0, ALL_LARGE = 0xAA, CURSORS_HIGH_BYTE = 0 };

// $CE:F066
static void hide_sprites(GameState *g)
{
    for (unsigned sprite = CURSOR_SPRITES; sprite < OAM_BUFFER_COUNT; sprite++)
        set_oam_buffer_y(g, sprite, HIDDEN_Y);
    for (unsigned at = CURSORS_HIGH_BYTE + 1; at < OAM_HIGH_TABLE_BYTES; at++)
        set_oam_high_table_byte(g, at, ALL_LARGE);
}

/* It leaves A the battle's page (a TDC) and 16-bit index registers. */
void battle_clear_oam_buffer(Snes *s)
{
    hide_sprites(state_of(s));
    s->cpu.a = BATTLE_PAGE;
    m_rep(s, CPU_FLAG_X);
    m_rtl(s);
}
