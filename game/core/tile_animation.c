/* Animated background tiles, uploaded from every mode's interrupt handlers.
 *
 * Twelve slots (tile_animations) each send 128 bytes of their current frame
 * to VRAM: a 16-bit accumulator, stepped every upload, whose bits 9-11 pick
 * one of eight frames; the frame's step and source address are in the slot's
 * table (tile_animation_tables). Six slots go on even frames of
 * tile_animation_frame, the other six on odd ones. */
#include "core/tile_animation.h"

#include "platform/video.h"
#include "port/machine.h"
#include "ram.h"

/* What a slot sends a frame: 128 bytes of tiles. */
enum { TILE_BYTES = 0x80 };
enum { SLOTS_PER_FRAME = 6, VRAM_UNUSED = 0x8000, FRAME_BITS = 0x0E00 };

/* One slot: when in use, step its accumulator and send 128 bytes of the frame
 * it was on. */
static void upload_slot(Snes *s, uint8_t slot, uint32_t site)
{
    uint32_t at = RAM_TILE_ANIMATIONS + slot * RAM_TILE_ANIMATIONS_STRIDE;
    uint16_t vram = m_read16(s, at + (RAM_TILE_ANIMATIONS_VRAM - RAM_TILE_ANIMATIONS));
    if (vram & VRAM_UNUSED)
        return;
    uint32_t table = RAM_TILE_ANIMATION_TABLES + slot * RAM_TILE_ANIMATION_TABLES_STRIDE;
    uint16_t accumulator = m_read16(s, at);
    uint8_t frame = (uint8_t)((accumulator & FRAME_BITS) >> 8);    /* frame * 2 */
    m_write16(s, at, (uint16_t)(accumulator + m_read16(s, table + frame)));
    uint16_t source = m_read16(s, table + (RAM_TILE_ANIMATION_TABLES_SOURCES - RAM_TILE_ANIMATION_TABLES) + frame);
    uint8_t bank = m_read8(s, at + (RAM_TILE_ANIMATIONS_BANK - RAM_TILE_ANIMATIONS));
    vid_send_vram(m_platform(s), PF_AT(site), vram, PF_GAME((uint32_t)bank << 16 | source), TILE_BYTES, NULL);
}

// $FD:E022
/* Upload animated tiles: count the frame on, and send slots 0-5 on even frames
 * and 6-11 on odd ones. Each slot's code in the original is a copy of the
 * others', which is where the call sites come from. */
static void upload(Snes *s)
{
    vid_start_vram_uploads(m_platform(s), PF_AT(0xFDE02B), VID_VRAM_WORD_PORT);
    uint8_t count = (uint8_t)(m_read8(s, RAM_TILE_ANIMATION_FRAME) + 1);
    m_write8(s, RAM_TILE_ANIMATION_FRAME, count);
    bool odd = count & 1;
    for (uint8_t k = 0; k < SLOTS_PER_FRAME; k++) {
        uint32_t site = (odd ? 0xFDE166 : 0xFDE051) + 0x2C * (uint32_t)k;
        upload_slot(s, (uint8_t)(k + (odd ? SLOTS_PER_FRAME : 0)), site);
    }
}

/* The original saves the caller's P, direct page and data bank on the stack
 * and restores them, and sets the data bank ($00) and the direct page ($0500)
 * by pushing and pulling them; what it leaves in A, X and Y nobody reads (the
 * entry's don't-care set). Those stack bytes are written as the original
 * writes them, since a slot whose source is the stack page sends them to VRAM
 * (fuzzing finds it). */
void tile_animation_upload(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_push16(s, c->d);
    m_push8(s, c->db);
    m_push8(s, 0x00);
    m_pull8(s);
    m_push16(s, RAM_PAGE_PALETTE_FADE);
    m_pull16(s);
    upload(s);
    m_pull8(s);
    m_pull16(s);
    m_pull8(s);
    m_rtl(s);
}
