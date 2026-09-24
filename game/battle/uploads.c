/* Battle: what the vblank sends to VRAM and the palette.
 *
 * The battle builds its sprites, its window and its colors in work RAM, and
 * the frame work sends them while the screen is blanked at the top of each
 * frame. Every frame of the fight: the sprite table, the working palette, and
 * whatever the rest of the battle has asked to be sent --
 *
 *   - a transfer a routine queued (battle_vram_request), which it waits on;
 *   - a block of sixteen tiles a routine asked for, which isn't sent again
 *     while the same block comes from the same place;
 *   - a battle message's tiles, a half each frame, while one shows, or
 *     otherwise the same VRAM cleared;
 *   - a part of the window's tilemap, described in full by the routine that
 *     asked for it (the window's rows, the rows the menu kept), and the frames
 *     of the party's windows;
 *   - one of four uploads in turn while the combatants animate, and one again
 *     when a countdown ends; and a combatant's graphics once they are built.
 *
 * Everything goes through the video service, which knows how the SNES made
 * each transfer by the site the call stands for (platform/snes/encodings.c). */
#include "battle/frame.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame_data.h"
#include "original_effects.h"
#include "platform/video.h"
#include "port/native.h"
#include "ram.h"
#include "vars.h"

/* The described transfers are in words to the VRAM port, as the channel takes
 * them. */
enum { DMA_WORDS = 0x01 };

/* A transfer's size: 0 is a whole 64 KiB, as the channel counts it. */
static uint32_t transfer_bytes(uint16_t size) { return size ? size : 0x10000u; }

/* ---- The palette ------------------------------------------------------------------------ */

enum { PALETTE_COLORS = 256 };

// $CD:09CE
void battle_send_palette(Platform *pf)
{
    vid_send_palette(pf, PF_AT(0xCD09CE), 0, PF_GAME(RAM_PALETTE_WORKING), PALETTE_COLORS);
}

/* ---- A queued transfer ------------------------------------------------------------------- */

// $CD:3E8E
/* Send `bytes` bytes (0 a whole bank) from `source` to VRAM. */
static void send_to_vram(Platform *pf, uint16_t vram, uint32_t source, uint16_t bytes)
{
    vid_send_vram(pf, PF_AT(0xCD3E95), vram, PF_GAME(source), transfer_bytes(bytes), NULL);
}

/* The transfer's size is handed over in the scratch cell $45, where the
 * original leaves it. */
static void leave_size(GameState *g, uint16_t bytes)
{
    set_battle_scratch(g, 0, (uint8_t)bytes);
    set_battle_scratch(g, 1, (uint8_t)(bytes >> 8));
}

// $CD:3EB4
void battle_send_queued_transfer(GameState *g, Platform *pf)
{
    if (!battle_vram_request_pending(g))
        return;
    uint16_t bytes = battle_vram_request_size(g);
    send_to_vram(pf, battle_vram_request_vram(g),
                 (uint32_t)battle_vram_request_bank(g) << 16 | battle_vram_request_source(g), bytes);
    ORIGINAL_EFFECT(leave_size(g, bytes));
    set_battle_vram_request_pending(g, 0);
}

/* ---- A block of tiles, and the message's --------------------------------------------------- */

/* A block is sixteen 32-byte tiles, one after another in memory, sent to
 * every other tile in VRAM from the block's place on. */
enum { BLOCK_TILES = 16, TILE_BYTES = 0x20, TILE_SPACING_WORDS = 0x20, BLOCK_BYTES = BLOCK_TILES * TILE_BYTES };

// $D1:E8D9
static void send_block(Platform *pf, uint32_t source, uint16_t vram)
{
    for (unsigned tile = 0; tile < BLOCK_TILES; tile++) {
        uint32_t from = (source & 0xFF0000u) | (uint16_t)(source + TILE_BYTES * tile);
        vid_send_vram(pf, PF_AT(tile == 0 ? 0xD1E8E0 : 0xD1E902), (uint16_t)(vram + TILE_SPACING_WORDS * tile),
                      PF_GAME(from), TILE_BYTES, NULL);
    }
}

/* The original counts the tiles down in the scratch cell $47, which it leaves
 * at 0. */
static void leave_tiles_counted(GameState *g)
{
    set_battle_scratch(g, 2, 0);
    set_battle_scratch(g, 3, 0);
}

/* Blocks come from bank $7E; a block is remembered by its offset into the
 * table of their places, twice its number. */
enum { BLOCK_BANK = 0x7E };

static uint8_t block_offset(uint8_t block) { return (uint8_t)(block << 1); }

static void send_requested_block(GameState *g, Platform *pf)
{
    if (!battle_block_upload_request(g))
        return;
    set_battle_block_upload_request(g, 0);
    uint8_t offset = block_offset(battle_block_upload_row(g));
    set_battle_block_upload_row(g, 0);
    uint16_t source = battle_block_upload_source(g);
    if (offset == battle_block_uploaded_row(g) && source == battle_block_uploaded_source(g))
        return;
    set_battle_block_uploaded_row(g, offset);
    set_battle_block_uploaded_source(g, source);
    send_block(pf, (uint32_t)BLOCK_BANK << 16 | source, battle_block_vram(g, offset));
    ORIGINAL_EFFECT(leave_tiles_counted(g));
    set_battle_block_uploads_made(g, (uint8_t)(battle_block_uploads_made(g) + 1));
}

/* While a battle message shows, its tiles go to VRAM a half a frame, the first
 * half on odd frames; otherwise the same VRAM is cleared, a half a frame. */
enum { MESSAGE_SHOWING = 0x10, MESSAGE_HALF_BYTES = 0x200 };
static const uint16_t message_half_vram[2] = { 0x5980, 0x5800 };   /* even frames, odd ones */

static void send_message_tiles(GameState *g, Platform *pf)
{
    uint8_t frame = (uint8_t)(battle_vram_alternate(g) + 1);
    set_battle_vram_alternate(g, frame);
    unsigned odd = frame & 1;
    uint16_t vram = message_half_vram[odd];
    if (battle_vram_copy_mode(g) == MESSAGE_SHOWING)
        send_to_vram(pf, vram, RAM_BATTLE_MESSAGE_TILES + (odd ? 0 : RAM_BATTLE_MESSAGE_TILES_STRIDE),
                     MESSAGE_HALF_BYTES);
    else
        vid_send_vram_fill(pf, PF_AT(0xCD066F), vram, PF_GAME(BATTLE_ZERO_BYTE), MESSAGE_HALF_BYTES, NULL);
}

// $CD:0607
void battle_send_block_and_message_tiles(GameState *g, Platform *pf)
{
    send_requested_block(g, pf);
    ORIGINAL_EFFECT(leave_size(g, MESSAGE_HALF_BYTES));
    send_message_tiles(g, pf);
}

/* ---- The transfer a request describes ------------------------------------------------------ */

/* A routine that wants part of the window's tilemap sent describes the
 * transfer in full, as the DMA channel takes it: the vblank hands the
 * description to the SNES-only transfer escape as it is (docs/PLATFORM.md).
 * Its nine values are kept one every sixth byte. */
typedef struct {
    uint8_t mode, port;
    uint32_t source;
    uint16_t size, vram;
} TransferDescription;

enum { DESCRIPTION_VALUES = 9, TRANSFER_PENDING = 0x80 };

static TransferDescription described_transfer(const GameState *g)
{
    uint8_t v[DESCRIPTION_VALUES];
    for (unsigned i = 0; i < DESCRIPTION_VALUES; i++)
        v[i] = battle_pending_transfer(g, i);
    return (TransferDescription){ v[0], v[1], (uint32_t)v[4] << 16 | (uint16_t)(v[3] << 8 | v[2]),
                                  (uint16_t)(v[6] << 8 | v[5]), (uint16_t)(v[8] << 8 | v[7]) };
}

static void describe_transfer(GameState *g, TransferDescription d)
{
    const uint8_t v[DESCRIPTION_VALUES] = {
        d.mode, d.port, (uint8_t)d.source, (uint8_t)(d.source >> 8), (uint8_t)(d.source >> 16),
        (uint8_t)d.size, (uint8_t)(d.size >> 8), (uint8_t)d.vram, (uint8_t)(d.vram >> 8),
    };
    for (unsigned i = 0; i < DESCRIPTION_VALUES; i++)
        set_battle_pending_transfer(g, i, v[i]);
    set_battle_transfer_pending(g, TRANSFER_PENDING);
}

static void send_described_transfer(GameState *g, Platform *pf)
{
    TransferDescription d = described_transfer(g);
    const VidSnesTransfer t = { .dest = d.port, .source = d.source, .size = d.size,
                                .target_reg = VID_SNES_VRAM_ADDRESS, .target = d.vram,
                                .mode_from_data = true, .mode = d.mode };
    vid_snes_copy(pf, PF_AT(0xCFE33B), &t);
    set_battle_transfer_pending(g, 0);
}

/* The window's rows: 192 entries of its tilemap, six rows of the layer's
 * map, from its top or further down, or the rows the menu kept while it
 * covered them. The window's own tilemap is reached through bank $00, where
 * work RAM's first 8 KiB also are. */
enum { WINDOW_ROWS_BYTES = 0x0180, WINDOW_ROWS_VRAM = 0x7820, LOWER_ROWS = 0x01C0 };

static void request_window_rows(GameState *g, uint32_t source)
{
    describe_transfer(g, (TransferDescription){ DMA_WORDS, VID_SNES_VRAM_DATA_LOW, source, WINDOW_ROWS_BYTES,
                                                WINDOW_ROWS_VRAM });
}

static uint32_t window_tilemap_in_bank_0(uint16_t offset) { return (uint16_t)(RAM_BATTLE_WINDOW_TILEMAP + offset); }

/* ---- The vblank's uploads ------------------------------------------------------------------ */

/* The party's window frames, when asked for. */
enum { FRAMES_VRAM = 0x7A00, FRAMES_BYTES = 0x0180 };

static void send_window_frames(GameState *g, Platform *pf)
{
    if (!battle_frames_upload_request(g))
        return;
    vid_send_vram(pf, PF_AT(0xCFE386), FRAMES_VRAM, PF_GAME((uint16_t)RAM_BATTLE_WINDOW_FRAMES_TILEMAP),
                  FRAMES_BYTES, NULL);
    set_battle_frames_upload_request(g, 0);
}

/* One of the four queued uploads: its first transfer, and its second where
 * that has a size (an empty one only has its source set). */
static void send_queued_upload(const GameState *g, Platform *pf, uint8_t slot, uint32_t first_site,
                               uint32_t second_site)
{
    BattleQueuedTransfer first = battle_queued_transfer(g, slot, 0), second = battle_queued_transfer(g, slot, 1);
    vid_send_vram(pf, PF_AT(first_site), first.vram, PF_GAME((uint32_t)BLOCK_BANK << 16 | first.source),
                  transfer_bytes(first.size), NULL);
    vid_send_vram(pf, PF_AT(second_site), second.size != 0 ? second.vram : 0,
                  PF_GAME((uint32_t)BLOCK_BANK << 16 | second.source), second.size, NULL);
}

/* While the combatants animate and no effects run, one queued upload a frame,
 * in turn. */
static void send_next_queued_upload(GameState *g, Platform *pf)
{
    if (battle_effects_running(g) || !battle_queued_uploads_on(g))
        return;
    uint8_t slot = battle_frame_phase(g);
    send_queued_upload(g, pf, slot, 0xCFE40A, 0xCFE449);
    slot++;
    set_battle_frame_phase(g, slot == BATTLE_QUEUED_SLOTS ? 0 : slot);
}

/* A combatant's graphics, once built, as its two uploads: for the kinds whose
 * graphics are built at all. */
enum { GRAPHICS_UPLOAD_NOW = 1, KINDS_BUILT = 3 };

static void send_built_graphics(GameState *g, Platform *pf)
{
    if (battle_graphics_upload_request(g) != GRAPHICS_UPLOAD_NOW)
        return;
    set_battle_graphics_upload_request(g, 0);
    unsigned slot = (uint8_t)battle_graphics_object(g);
    if (battle_object_kind(g, slot) >= KINDS_BUILT)
        return;
    static const uint32_t sites[2] = { 0xCFE4A0, 0xCFE4E3 };
    for (unsigned second = 0; second < 2; second++) {
        unsigned upload = slot + second * BATTLE_COMBATANTS;
        vid_send_vram(pf, PF_AT(sites[second]), battle_upload_vram_address(g, upload),
                      PF_GAME((uint32_t)BLOCK_BANK << 16 | battle_upload_source(g, upload)),
                      transfer_bytes(battle_upload_size(g, upload)), NULL);
    }
}

/* The first queued upload again, when its countdown runs out. */
static void count_down_to_reupload(GameState *g, Platform *pf)
{
    uint8_t left = battle_reupload_countdown(g);
    if (left == 0)
        return;
    set_battle_reupload_countdown(g, --left);
    if (left == 0)
        send_queued_upload(g, pf, 0, 0xCFE51C, 0xCFE55B);
}

// $CF:E300
/* The sprite table goes from work RAM's mirror in bank $00. */
void battle_send_vblank_uploads(GameState *g, Platform *pf)
{
    vid_send_oam(pf, PF_AT(0xCFE301), PF_GAME((uint16_t)RAM_OAM_BUFFER));
    if (battle_transfer_pending(g))
        send_described_transfer(g, pf);
    send_window_frames(g, pf);
    vid_send_palette(pf, PF_AT(0xCFE3C5), 0, PF_GAME(RAM_PALETTE_WORKING), PALETTE_COLORS);
    send_next_queued_upload(g, pf);
    send_built_graphics(g, pf);
    count_down_to_reupload(g, pf);
}

/* The game sends from memory, never from the I/O registers the low halves of
 * banks $00-$3F and $80-$BF reach from $2000 to $7FFF, where what a transfer
 * reads depends on when it reads it. */
enum { IO_START = 0x2000, IO_END = 0x8000 };

static bool from_memory(uint8_t bank, uint16_t source, uint32_t bytes)
{
    if (bank & 0x40)
        return true;
    for (uint32_t i = 0; i < bytes; i++) {
        uint16_t at = (uint16_t)(source + i);
        if (at >= IO_START && at < IO_END)
            return false;
    }
    return true;
}

bool battle_vblank_uploads_in_range(const GameState *g)
{
    bool object_is_a_combatant = battle_graphics_upload_request(g) != GRAPHICS_UPLOAD_NOW
                                 || (uint8_t)battle_graphics_object(g) < BATTLE_COMBATANTS;
    TransferDescription d = described_transfer(g);
    bool described_from_memory = !battle_transfer_pending(g)
                                 || from_memory((uint8_t)(d.source >> 16), (uint16_t)d.source, transfer_bytes(d.size));
    return object_is_a_combatant && described_from_memory;
}

/* What the game sends comes from memory apart from the scratch cells and the
 * top of the stack the transfers run on: the original counts a block's tiles
 * in a cell and keeps the data bank on the stack while a transfer goes, which
 * a transfer over them would send instead. The banks that reach the first
 * 8 KiB of work RAM are $7E and the low halves of $00-$3F and $80-$BF; a
 * transfer runs round its bank. */
enum { COUNT_CELLS = 0x0045, COUNT_CELLS_END = 0x0049, STACK_REACH = 0x20, LOW_RAM_END = 0x2000 };

static bool sends_cell(uint8_t bank, uint16_t source, uint32_t bytes, uint16_t cell)
{
    bool reaches = bank == 0x7E || ((bank & 0x7F) < 0x40 && cell < LOW_RAM_END);
    return reaches && (uint16_t)(cell - source) < bytes;
}

static bool transfer_apart(uint8_t bank, uint16_t source, uint32_t bytes, uint16_t stack)
{
    if (!from_memory(bank, source, bytes))
        return false;
    for (uint16_t cell = COUNT_CELLS; cell < COUNT_CELLS_END; cell++)
        if (sends_cell(bank, source, bytes, cell))
            return false;
    for (uint16_t below = 0; below < STACK_REACH; below++)
        if (sends_cell(bank, source, bytes, (uint16_t)(stack - below)))
            return false;
    return true;
}

static bool queued_transfer_apart(const GameState *g, uint16_t stack)
{
    return !battle_vram_request_pending(g)
           || transfer_apart(battle_vram_request_bank(g), battle_vram_request_source(g),
                             transfer_bytes(battle_vram_request_size(g)), stack);
}

static bool block_apart(const GameState *g, uint16_t stack)
{
    return !battle_block_upload_request(g)
           || transfer_apart(BLOCK_BANK, battle_block_upload_source(g), BLOCK_BYTES, stack);
}

bool battle_transfers_apart(const GameState *g, uint16_t stack)
{
    return queued_transfer_apart(g, stack) && block_apart(g, stack);
}

/* ---- The entries ------------------------------------------------------------------------ */

void battle_upload_palette(Snes *s)
{
    battle_send_palette(m_platform(s));
    m_rtl(s);
}

/* Entered with the source in A (bank) and X, the VRAM address in Y and the
 * size in the scratch cell $45, which is the caller's to set. */
void battle_vram_transfer(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t bytes = (uint16_t)(battle_scratch(g, 0) | battle_scratch(g, 1) << 8);
    send_to_vram(m_platform(s), s->cpu.y, (uint32_t)(uint8_t)s->cpu.a << 16 | s->cpu.x, bytes);
    m_rts(s);
}

/* A transfer that goes leaves its VRAM address in Y. */
void battle_flush_vram_transfer(Snes *s)
{
    GameState *g = state_of(s);
    if (battle_vram_request_pending(g))
        s->cpu.y = battle_vram_request_vram(g);
    battle_send_queued_transfer(g, m_platform(s));
    m_rts(s);
}

/* Y is left as the last transfer left it: the message's VRAM half where its
 * tiles went, the tiles' spacing after a block (the original counts the
 * spacing in Y), or as it was. */
void battle_upload_alternating_vram(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t blocks_before = battle_block_uploads_made(g);
    battle_send_block_and_message_tiles(g, m_platform(s));
    if (battle_vram_copy_mode(g) == MESSAGE_SHOWING)
        s->cpu.y = message_half_vram[battle_vram_alternate(g) & 1];
    else if (battle_block_uploads_made(g) != blocks_before)
        s->cpu.y = TILE_BYTES;
    m_rts(s);
}

/* Entered with the source in A (bank) and X and the VRAM address in the
 * scratch cell $45, as $CD:0607 calls it; it leaves Y the tiles' size. */
void battle_upload_tile_rows(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t vram = (uint16_t)(battle_scratch(g, 0) | battle_scratch(g, 1) << 8);
    send_block(m_platform(s), (uint32_t)(uint8_t)s->cpu.a << 16 | s->cpu.x, vram);
    ORIGINAL_EFFECT(leave_tiles_counted(g));
    s->cpu.y = TILE_BYTES;
    m_rtl(s);
}

void battle_upload_vram(Snes *s)
{
    battle_send_vblank_uploads(state_of(s), m_platform(s));
    m_rtl(s);
}

/* The requests leave A's high byte the page's (0), which the callers read. */
static void finish_request(Snes *s)
{
    s->cpu.a = BATTLE_PAGE;
    m_rtl(s);
}

// $CF:FD02
void battle_request_window_rows(Snes *s)
{
    request_window_rows(state_of(s), window_tilemap_in_bank_0(0));
    finish_request(s);
}

// $CF:FD36
void battle_request_saved_window_rows(Snes *s)
{
    request_window_rows(state_of(s), RAM_BATTLE_MENU_TILEMAP);
    finish_request(s);
}

// $CF:FD6A
void battle_request_lower_window_rows(Snes *s)
{
    request_window_rows(state_of(s), window_tilemap_in_bank_0(LOWER_ROWS));
    finish_request(s);
}

/* The guards: on the battle's page with the data bank $7E, what the uploads
 * read in range, and a block of tiles apart from what its upload uses. */
static bool peek_uploads_in_range(Snes *s)
{
    vars_peek(true);
    bool in_range = battle_vblank_uploads_in_range(state_of(s));
    vars_peek(false);
    return in_range;
}

bool battle_vblank_uploads_ready(Snes *s) { return battle_on_page(s) && peek_uploads_in_range(s); }

/* The block's place is looked up with A's high byte along, 0 as the frame
 * work calls it. */
bool battle_block_upload_ready(Snes *s)
{
    vars_peek(true);
    bool apart = block_apart(state_of(s), s->cpu.s);
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && apart;
}

bool battle_queued_transfer_ready(Snes *s)
{
    vars_peek(true);
    bool apart = queued_transfer_apart(state_of(s), s->cpu.s);
    vars_peek(false);
    return battle_on_page(s) && apart;
}

bool battle_vram_transfer_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    uint16_t bytes = (uint16_t)(battle_scratch(g, 0) | battle_scratch(g, 1) << 8);
    vars_peek(false);
    return battle_page(s) && transfer_apart((uint8_t)s->cpu.a, s->cpu.x, transfer_bytes(bytes), s->cpu.s);
}

bool battle_tile_rows_ready(Snes *s)
{
    return battle_page(s) && transfer_apart((uint8_t)s->cpu.a, s->cpu.x, BLOCK_BYTES, s->cpu.s);
}
