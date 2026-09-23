/* Field mode: the four windows' corner tiles.
 *
 * Four windows each have a pair of corner tiles in VRAM. One window can be
 * the selected one (field_window_tile_slot), whose corners are drawn with the
 * marked tiles; the rest show their own. The field refreshes them in the time
 * after the picture (field/display.c's line interrupt), and each window's own
 * corners can be drawn on their own. */
#include "field/display.h"

#include "field/bridge.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* A window's corner pair is two words at $1C02 + $40 * window and two at
 * $1C22 + $40 * window in VRAM. */
enum { WINDOWS = 4, NO_WINDOW_SELECTED = 0x80 };
enum { CORNER_VRAM = 0x1C02, CORNER_TILE = 0x2902, SELECTED_TILE = 0x28FC, WINDOW_STRIDE = 0x40, SECOND_CORNER = 0x20 };

typedef struct {
    uint32_t address, first, second;   /* the origin tags of the three writes of a corner */
} CornerSites;

static void write_corner(Platform *pf, uint16_t vram, uint16_t tile, CornerSites at)
{
    vid_set_vram_address(pf, PF_AT(at.address), vram);
    vid_write_vram(pf, PF_AT(at.first), tile);
    vid_write_vram(pf, PF_AT(at.second), (uint16_t)(tile + 1));
}

/* A window's two corners with its own tiles, or the marked ones. */
static void write_window_corners(Platform *pf, unsigned window, bool selected, const CornerSites at[2])
{
    uint16_t vram = (uint16_t)(CORNER_VRAM + WINDOW_STRIDE * window);
    uint16_t own = (uint16_t)(CORNER_TILE + WINDOW_STRIDE * window);
    write_corner(pf, vram, selected ? SELECTED_TILE : own, at[0]);
    write_corner(pf, (uint16_t)(vram + SECOND_CORNER), selected ? (uint16_t)(SELECTED_TILE + 2) : (uint16_t)(own + 0x10),
                 at[1]);
}

/* Each window's corners as its own routine writes them ($C0:F159, $F142,
 * $F12B, $F110). */
static const CornerSites own_corners[WINDOWS][2] = {
    { { 0xC0F15C, 0xC0F161, 0xC0F164 }, { 0xC0F169, 0xC0F16E, 0xC0F128 } },
    { { 0xC0F145, 0xC0F14A, 0xC0F14D }, { 0xC0F152, 0xC0F157, 0xC0F128 } },
    { { 0xC0F12E, 0xC0F133, 0xC0F136 }, { 0xC0F13B, 0xC0F140, 0xC0F128 } },
    { { 0xC0F113, 0xC0F118, 0xC0F11B }, { 0xC0F120, 0xC0F125, 0xC0F128 } },
};

/* And the marked ones, as the refresh writes them for the selected window. */
static const CornerSites marked_corners[WINDOWS][2] = {
    { { 0xC0F097, 0xC0F09C, 0xC0F09F }, { 0xC0F0A4, 0xC0F0C9, 0xC0F0CC } },
    { { 0xC0F0FC, 0xC0F101, 0xC0F104 }, { 0xC0F109, 0xC0F0C9, 0xC0F0CC } },
    { { 0xC0F0DC, 0xC0F0E1, 0xC0F0E4 }, { 0xC0F0E9, 0xC0F0C9, 0xC0F0CC } },
    { { 0xC0F0B7, 0xC0F0BC, 0xC0F0BF }, { 0xC0F0C4, 0xC0F0C9, 0xC0F0CC } },
};

// $C0:F05E
/* Refresh the four windows' corners: the selected window's marked, the rest
 * their own; with no window selected (field_window_tile_slot past the four),
 * all their own, and the selection is cleared. The windows the refresh
 * writes with their own routines are written in the routines' order. */
static void refresh_window_corners(GameState *g, Platform *pf)
{
    vid_begin_vram_uploads(pf, PF_AT(0xC0F069), VID_VRAM_WORD_PORT, PF_GAME(0), NULL);
    unsigned selected = field_window_tile_slot(g);
    for (unsigned w = 0; w < WINDOWS; w++)
        if (w != selected)
            write_window_corners(pf, w, false, own_corners[w]);
    if (selected < WINDOWS)
        write_window_corners(pf, selected, true, marked_corners[selected]);
    else
        set_field_window_tile_slot(g, NO_WINDOW_SELECTED);
}

/* ---- Where it meets the original ------------------------------------------------------- */

/* The corner writers are entered with the direct page on the video
 * registers, which they write through; on any other page they write memory,
 * which stays the original's. */
bool field_window_corners_dp_is_ppu(Snes *s) { return s->cpu.d == M_DP_PPU; }

// $C0:F159, $C0:F142, $C0:F12B, $C0:F110
void field_write_window_corners_0(Snes *s) { write_window_corners(m_platform(s), 0, false, own_corners[0]); m_rts(s); }
void field_write_window_corners_1(Snes *s) { write_window_corners(m_platform(s), 1, false, own_corners[1]); m_rts(s); }
void field_write_window_corners_2(Snes *s) { write_window_corners(m_platform(s), 2, false, own_corners[2]); m_rts(s); }
void field_write_window_corners_3(Snes *s) { write_window_corners(m_platform(s), 3, false, own_corners[3]); m_rts(s); }

void field_refresh_window_corners(Snes *s)
{
    refresh_window_corners(state_of(s), m_platform(s));
    m_rts(s);
}

