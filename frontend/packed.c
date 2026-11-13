#include "packed.h"

#include <stdlib.h>
#include <string.h>

#include "lz.h"

uint8_t *packed_unpack(const uint8_t *bytes, uint32_t len, size_t *unpacked_len, bool *long_runs)
{
    uint8_t *unpacked = bytes ? malloc(LZ_MOST) : NULL;
    if (unpacked && !lz_unpack(bytes, len, unpacked, unpacked_len, long_runs)) {
        free(unpacked);
        return NULL;
    }
    return unpacked;
}

void packed_write_form(DecodedText *t, bool long_runs) { decoded_put(t, long_runs ? "packed long\n" : "packed short\n"); }

const char *packed_read_form(const Decoded *d, DecodedReading *r, const char *s, const char *end)
{
    if (!decoded_word(&s, end, "packed"))
        return "a packed resource starts `packed short` or `packed long`";
    if (decoded_word(&s, end, "short"))
        r->form = PACKED_SHORT_RUNS;
    else if (decoded_word(&s, end, "long"))
        r->form = PACKED_LONG_RUNS;
    else
        return "its runs are `short` or `long`";
    if (s != end)
        return "nothing goes after a stream's runs";
    size_t len = 0;
    bool long_runs;
    uint8_t *unpacked = packed_unpack(decoded_beneath(d, r->entry), asset_catalogue[r->entry].length, &len, &long_runs);
    free(unpacked);
    r->most = (uint32_t)len;
    return unpacked ? NULL : "the store under the text hasn't this packed resource to unpack";
}

/* Whether the resource's unpacked bytes are the store beneath's: if so its
 * bytes are taken as they are. */
static bool unchanged(const Decoded *d, const DecodedReading *r)
{
    const AssetEntry *e = &asset_catalogue[r->entry];
    const uint8_t *was = decoded_beneath(d, r->entry);
    size_t len = 0;
    bool long_runs = false;
    uint8_t *unpacked = packed_unpack(was, e->length, &len, &long_runs);
    bool same = unpacked && len == r->made_len && long_runs == (r->form == PACKED_LONG_RUNS) &&
                !memcmp(unpacked, r->made, len);
    free(unpacked);
    if (same)
        memcpy(r->bytes, was, e->length);
    return same;
}

const char *packed_end(const Decoded *d, DecodedReading *r)
{
    const AssetEntry *e = &asset_catalogue[r->entry];
    if (r->form == PACKED_UNSAID)
        return "a packed resource starts `packed short` or `packed long`";
    if (r->made_len > r->most)
        return "more than the cartridge's stream unpacks to";
    if (!unchanged(d, r) && !lz_pack(r->made, r->made_len, r->form == PACKED_LONG_RUNS, r->bytes, e->length))
        return "packed, this takes more room than the cartridge has for it";
    return NULL;
}
