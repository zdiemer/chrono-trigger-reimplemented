/* The menus: queueing uploads to VRAM.
 *
 * What the menus draw -- a window's tilemap, a save slot's summary, a
 * portrait -- is built in work RAM and sent to VRAM in the next vblank, while
 * the screen is off (game/menu/display.c, menu_upload_vram_queue). Code that
 * wants something sent puts an upload in the queue: the menus' fixed uploads
 * are records in the ROM, and the rest are built in work RAM first; either way
 * the record goes into the queue as it is. */
#include "menu/vram.h"

#include "menu/menu_data.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* The queue counts what it holds in bytes, a record's 8 a record. */
enum { RECORD_BYTES = 8 };

static unsigned queued_uploads(const GameState *g) { return menu_vram_queue_bytes(g) / RECORD_BYTES; }

void menu_queue_upload(GameState *g, MenuUpload u)
{
    unsigned entry = queued_uploads(g);
    set_menu_vram_queue_vram(g, entry, u.vram);
    set_menu_vram_queue_source(g, entry, (uint16_t)u.source);
    set_menu_vram_queue_bank(g, entry, (uint8_t)(u.source >> 16));
    set_menu_vram_queue_size(g, entry, u.size);
    set_menu_vram_queue_spare(g, entry, u.spare);
    set_menu_vram_queue_bytes(g, (uint8_t)(menu_vram_queue_bytes(g) + RECORD_BYTES));
}

/* An upload record built in work RAM, at `at` in bank $7E. */
static MenuUpload built_upload(const GameState *g, uint16_t at)
{
    uint8_t b[RECORD_BYTES];
    for (unsigned i = 0; i < RECORD_BYTES; i++)
        b[i] = wram_read8(g, 0x7E0000u | (uint16_t)(at + i));
    return (MenuUpload){ (uint16_t)(b[0] | b[1] << 8), (uint32_t)b[4] << 16 | (uint16_t)(b[2] | b[3] << 8),
                         (uint16_t)(b[5] | b[6] << 8), b[7] };
}

/* ---- The entries ------------------------------------------------------------------------- */

/* Where the queue is, as the original's block move addresses it. */
enum { QUEUE_ADDRESS = 0x0C00 };

/* The original copies the record in with a block move, and leaves what the
 * move leaves: the data bank on work RAM's, A's high byte $FF, X past the
 * record and Y past the queue's new entry, at the index registers' width the
 * caller had (its P comes back as it was). */
static void leave_block_move(Snes *s, uint16_t record_at, unsigned queued_bytes)
{
    uint16_t x = (uint16_t)(record_at + RECORD_BYTES);
    uint16_t y = (uint16_t)(QUEUE_ADDRESS + queued_bytes + RECORD_BYTES);
    s->cpu.db = 0x7E;
    s->cpu.a = (uint16_t)(0xFF00 | (uint8_t)(queued_bytes + RECORD_BYTES));
    s->cpu.x = m_x8(s) ? (uint8_t)x : x;
    s->cpu.y = m_x8(s) ? (uint8_t)y : y;
}

static unsigned queued_bytes_now(Snes *s)
{
    vars_peek(true);
    unsigned queued = menu_vram_queue_bytes(state_of(s));
    vars_peek(false);
    return queued;
}

// $C2:8385
/* Queue the upload whose record is at X in bank C2. */
void menu_queue_vram_upload(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t record_at = s->cpu.x;
    unsigned queued = queued_bytes_now(s);
    menu_queue_upload(g, menu_upload_record(g, record_at));
    ORIGINAL_EFFECT(leave_block_move(s, record_at, queued));
    m_rts(s);
}

// $C2:838E
/* Queue the upload whose record the caller built at X in bank $7E. */
void menu_queue_built_vram_upload(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t record_at = s->cpu.x;
    unsigned queued = queued_bytes_now(s);
    menu_queue_upload(g, built_upload(g, record_at));
    ORIGINAL_EFFECT(leave_block_move(s, record_at, queued));
    m_rts(s);
}

/* The game queues whole records, never more than the queue holds, reaching
 * the count through a data bank on low RAM. */
bool menu_upload_queue_has_room(Snes *s)
{
    return m_bank_reaches_low_ram(s) && queued_bytes_now(s) % RECORD_BYTES == 0;
}

/* And builds a record apart from the queue's entry it goes into, inside the
 * bank. */
bool menu_built_upload_is_apart(Snes *s)
{
    if (!menu_upload_queue_has_room(s))
        return false;
    uint16_t at = s->cpu.x;
    uint16_t entry = (uint16_t)(QUEUE_ADDRESS + queued_bytes_now(s));
    return at <= 0x10000 - RECORD_BYTES && (at + RECORD_BYTES <= entry || at >= entry + RECORD_BYTES);
}

/* ---- The sprite-object mode's queue --------------------------------------------------------- */

/* The sprite-object mode keeps a queue of its own on its direct page, which
 * its vblank handler sends (game/menu/display.c, menu_object_upload_vram_queue)
 * unless something is queueing at that moment. Its records are counted by the
 * byte offset of the queue's end. The queue holds sixteen; one filled past
 * them runs on over the upload buffer's cursor after it (the original's
 * writers know only the end offset), which no screen does. */
enum { OBJECT_RECORD_BYTES = MENU_OBJECT_UPLOAD_BYTES, OBJECT_QUEUE_RECORDS = 16 };

uint8_t menu_hold_object_queue(GameState *g)
{
    set_menu_object_vram_queue_busy(g, (uint8_t)(menu_object_vram_queue_busy(g) + 1));
    return menu_object_vram_queue_end(g);
}

void menu_release_object_queue(GameState *g, uint8_t end)
{
    set_menu_object_vram_queue_end(g, end);
    set_menu_object_vram_queue_busy(g, 0);
}

static unsigned object_record(uint8_t at) { return at / OBJECT_RECORD_BYTES; }
static bool in_the_queue(uint8_t at) { return object_record(at) < OBJECT_QUEUE_RECORDS; }

void menu_set_object_upload_bank(GameState *g, uint8_t at, uint8_t bank)
{
    if (in_the_queue(at))
        set_menu_object_vram_queue_bank(g, object_record(at), bank);
    else
        set_menu_object_vram_queue_bank_unchecked(g, object_record(at), bank);
}

void menu_set_object_upload_port(GameState *g, uint8_t at, uint8_t port)
{
    if (in_the_queue(at))
        set_menu_object_vram_queue_port(g, object_record(at), port);
    else
        set_menu_object_vram_queue_port_unchecked(g, object_record(at), port);
}

void menu_set_object_upload_source(GameState *g, uint8_t at, uint16_t source)
{
    if (in_the_queue(at))
        set_menu_object_vram_queue_source(g, object_record(at), source);
    else
        set_menu_object_vram_queue_source_unchecked(g, object_record(at), source);
}

void menu_set_object_upload_vram(GameState *g, uint8_t at, uint16_t vram)
{
    if (in_the_queue(at))
        set_menu_object_vram_queue_vram(g, object_record(at), vram);
    else
        set_menu_object_vram_queue_vram_unchecked(g, object_record(at), vram);
}

void menu_set_object_upload_size(GameState *g, uint8_t at, uint16_t size)
{
    if (in_the_queue(at))
        set_menu_object_vram_queue_size(g, object_record(at), size);
    else
        set_menu_object_vram_queue_size_unchecked(g, object_record(at), size);
}

void menu_queue_object_upload(GameState *g, uint32_t source, uint16_t vram, uint16_t size)
{
    uint8_t at = menu_hold_object_queue(g);
    menu_set_object_upload_bank(g, at, (uint8_t)(source >> 16));
    menu_set_object_upload_port(g, at, MENU_PORT_WORDS);
    menu_set_object_upload_source(g, at, (uint16_t)source);
    menu_set_object_upload_vram(g, at, vram);
    menu_set_object_upload_size(g, at, size);
    menu_release_object_queue(g, (uint8_t)(at + OBJECT_RECORD_BYTES));
}

bool menu_object_queue_is_whole(const GameState *g) { return menu_object_vram_queue_end(g) % OBJECT_RECORD_BYTES == 0; }
