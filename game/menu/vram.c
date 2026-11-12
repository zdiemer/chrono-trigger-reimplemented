/* The menus: queueing uploads to VRAM.
 *
 * What the menus draw -- a window's tilemap, a save slot's summary, a
 * portrait -- is built in work RAM and sent to VRAM in the next vblank, while
 * the screen is off (game/menu/display.c, menu_upload_vram_queue). Code that
 * wants something sent puts an upload in the queue: the menus' fixed uploads
 * are records in the ROM, and the rest are built in work RAM first; either way
 * the record goes into the queue as it is. */
#include "menu/vram.h"

#include "charge.h"

#include "menu/menu_data.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* The queue counts what it holds in bytes, a record's 8 a record. */
enum { RECORD_BYTES = 8 };

static unsigned queued_uploads(const GameState *g) { return menu_vram_queue_bytes(g) / RECORD_BYTES; }

// $C2:8385, $C2:838E
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

void menu_queue_fixed_upload(GameState *g, uint16_t record)
{
    TIMED_AS(g, 0xC28385, menu_queue_upload(g, menu_upload_record(g, record)));
}

/* ---- Room in the queue -------------------------------------------------------------------- */

/* A step that queues `uploads` finds the queue holding whole records, with
 * room for them (it holds 32). */
enum { QUEUE_HOLDS_BYTES = 32 * MENU_UPLOAD_BYTES };

bool menu_upload_queue_takes(const GameState *g, unsigned uploads)
{
    unsigned queued = menu_vram_queue_bytes(g);
    return queued % MENU_UPLOAD_BYTES == 0 && queued + MENU_UPLOAD_BYTES * uploads <= QUEUE_HOLDS_BYTES;
}

/* ---- The sprite-object mode's queue --------------------------------------------------------- */

/* The sprite-object mode keeps a queue of its own in its own cells, which
 * its vblank handler sends (game/menu/display.c, menu_object_upload_vram_queue)
 * unless something is queueing at that moment. Its records are counted by the
 * byte offset of the queue's end. The queue holds sixteen; one filled past
 * them runs on over the upload buffer's cursor after it (the original's
 * writers know only the end offset), which no screen does. */
enum { OBJECT_RECORD_BYTES = MENU_OBJECT_UPLOAD_BYTES, OBJECT_QUEUE_RECORDS = 16 };

static uint8_t hold_the_queue(GameState *g)
{
    set_menu_object_vram_queue_busy(g, (uint8_t)(menu_object_vram_queue_busy(g) + 1));
    return menu_object_vram_queue_end(g);
}

static void release_the_queue(GameState *g, uint8_t end)
{
    set_menu_object_vram_queue_end(g, end);
    set_menu_object_vram_queue_busy(g, 0);
}

static unsigned object_record(unsigned at) { return at / OBJECT_RECORD_BYTES; }
static bool in_the_queue(unsigned at) { return object_record(at) < OBJECT_QUEUE_RECORDS; }

/* A record's fields, by its byte offset in the queue: past the sixteen
 * records the same fields of the bytes after them (the map's `unchecked`). */
static void set_record(GameState *g, unsigned at, const MenuObjectUpload *u)
{
    unsigned r = object_record(at);
    if (in_the_queue(at)) {
        set_menu_object_vram_queue_bank(g, r, (uint8_t)(u->source >> 16));
        set_menu_object_vram_queue_port(g, r, (uint8_t)u->port);
        set_menu_object_vram_queue_source(g, r, (uint16_t)u->source);
        set_menu_object_vram_queue_vram(g, r, u->vram);
        set_menu_object_vram_queue_size(g, r, u->size);
    } else {
        set_menu_object_vram_queue_bank_unchecked(g, r, (uint8_t)(u->source >> 16));
        set_menu_object_vram_queue_port_unchecked(g, r, (uint8_t)u->port);
        set_menu_object_vram_queue_source_unchecked(g, r, (uint16_t)u->source);
        set_menu_object_vram_queue_vram_unchecked(g, r, u->vram);
        set_menu_object_vram_queue_size_unchecked(g, r, u->size);
    }
}

uint8_t menu_queue_object_uploads(GameState *g, const MenuObjectUpload *uploads, unsigned count)
{
    uint8_t first = hold_the_queue(g);
    unsigned at = first;
    for (unsigned i = 0; i < count; i++, at += OBJECT_RECORD_BYTES)
        set_record(g, at, &uploads[i]);
    release_the_queue(g, (uint8_t)at);
    return first;
}

void menu_queue_object_upload(GameState *g, uint32_t source, uint16_t vram, uint16_t size)
{
    const MenuObjectUpload u = { source, vram, size, MENU_PORT_WORD_BY_WORD };
    menu_queue_object_uploads(g, &u, 1);
}

bool menu_object_queue_is_whole(const GameState *g) { return menu_object_vram_queue_end(g) % OBJECT_RECORD_BYTES == 0; }
