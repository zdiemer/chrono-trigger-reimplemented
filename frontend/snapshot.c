/* Snapshots of the native program (snapshot.h): a header with the addresses
 * the bytes depend on, then each part's bytes behind its length. */
#include "snapshot.h"
#include "checksum.h"
#include "port/fiber.h"
#include "port/state_host.h"

#include <stdlib.h>
#include <string.h>

/* Where the run keeps what a snapshot's pointers name: the code (a function
 * of its own stands for the image), the game's state, the services, the
 * asset pack. The fiber stacks' place the fiber snapshot checks itself. */
typedef struct {
    uint64_t code, game, pf, pack;
} Layout;

static Layout layout_of(const SnapshotParts *p)
{
    return (Layout){ (uint64_t)(uintptr_t)&snapshot_take, (uint64_t)(uintptr_t)p->game,
                     (uint64_t)(uintptr_t)p->pf, (uint64_t)(uintptr_t)p->pack };
}

enum { PARTS = 5 };   /* the game, the services, the sound, the fibers, the driver's */
static const char MAGIC[8] = { 'C', 'T', 'S', 'N', 'A', 'P', '0', '1' };
enum { BUILD = 32, HEAD = sizeof MAGIC + BUILD + sizeof(Layout) + PARTS * sizeof(uint64_t) };

uint8_t *snapshot_take(const SnapshotParts *p, const char *build, const void *driver, size_t driver_bytes, size_t *len)
{
    if (strlen(build) != BUILD)
        return NULL;
    uint64_t size[PARTS] = { state_host_snapshot_bytes(), pf_host_snapshot(p->pf, NULL, 0),
                             sound_native_snapshot_bytes(), fiber_snapshot(NULL, 0), driver_bytes };
    if (!size[3])
        return NULL;
    size_t total = HEAD;
    for (unsigned i = 0; i < PARTS; i++)
        total += (size_t)size[i];
    uint8_t *out = malloc(total);
    if (!out)
        return NULL;
    Layout layout = layout_of(p);
    memcpy(out, MAGIC, sizeof MAGIC);
    memcpy(out + sizeof MAGIC, build, BUILD);
    memcpy(out + sizeof MAGIC + BUILD, &layout, sizeof layout);
    memcpy(out + sizeof MAGIC + BUILD + sizeof layout, size, sizeof size);
    uint8_t *at = out + HEAD;
    state_host_snapshot(p->game, at);
    at += size[0];
    pf_host_snapshot(p->pf, at, (size_t)size[1]);
    at += size[1];
    sound_native_snapshot(p->sound, at);
    at += size[2];
    fiber_snapshot(at, (size_t)size[3]);
    at += size[3];
    if (driver_bytes)
        memcpy(at, driver, driver_bytes);
    *len = total;
    return out;
}

bool snapshot_restore(const SnapshotParts *p, const char *build, const uint8_t *in, size_t len, void *driver,
                      size_t driver_bytes)
{
    Layout layout, here = layout_of(p);
    uint64_t size[PARTS];
    if (len < HEAD || strlen(build) != BUILD || memcmp(in, MAGIC, sizeof MAGIC) || memcmp(in + sizeof MAGIC, build, BUILD))
        return false;
    memcpy(&layout, in + sizeof MAGIC + BUILD, sizeof layout);
    memcpy(size, in + sizeof MAGIC + BUILD + sizeof layout, sizeof size);
    if (memcmp(&layout, &here, sizeof layout) || size[0] != state_host_snapshot_bytes() ||
        size[2] != sound_native_snapshot_bytes() || size[4] != driver_bytes)
        return false;
    size_t total = HEAD;
    for (unsigned i = 0; i < PARTS; i++) {
        if (size[i] > len - total)
            return false;
        total += (size_t)size[i];
    }
    if (total != len)
        return false;
    const uint8_t *part[PARTS];
    part[0] = in + HEAD;
    for (unsigned i = 1; i < PARTS; i++)
        part[i] = part[i - 1] + size[i - 1];
    /* The fibers check every record before they change a slot; the services
     * then fail only where memory for the observer's copies runs out. */
    if (!fiber_restore(part[3], (size_t)size[3]) || !pf_host_restore(p->pf, part[1], (size_t)size[1]))
        return false;
    state_host_restore(p->game, part[0]);
    sound_native_restore(p->sound, part[2]);
    if (driver_bytes)
        memcpy(driver, part[4], driver_bytes);
    return true;
}

/* ---- Files ---------------------------------------------------------------------------------- */

static const char FILE_MAGIC[8] = { 'C', 'T', 'N', 'A', 'T', '0', '0', '2' };
enum { FILE_HEAD = sizeof FILE_MAGIC + 4 };

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (i * 8));
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint8_t *snapshot_file(const uint8_t *snapshot, size_t len, size_t *file_len)
{
    if (len > SIZE_MAX - FILE_HEAD)
        return NULL;
    uint8_t *out = malloc(FILE_HEAD + len);
    if (!out)
        return NULL;
    memcpy(out, FILE_MAGIC, sizeof FILE_MAGIC);
    put32(out + sizeof FILE_MAGIC, ct_crc32(snapshot, len));
    memcpy(out + FILE_HEAD, snapshot, len);
    *file_len = FILE_HEAD + len;
    return out;
}

const uint8_t *snapshot_in_file(const uint8_t *file, size_t len, size_t *snapshot_len)
{
    if (len < FILE_HEAD || memcmp(file, FILE_MAGIC, sizeof FILE_MAGIC) ||
        get32(file + sizeof FILE_MAGIC) != ct_crc32(file + FILE_HEAD, len - FILE_HEAD))
        return NULL;
    *snapshot_len = len - FILE_HEAD;
    return file + FILE_HEAD;
}
