#include "ppu_state.h"

#include <stdlib.h>
#include <string.h>

ct_ppu_state_kind ct_ppu_state_detect(const char *library_name) {
    if (!library_name)
        return CT_PPU_STATE_NONE;
    if (strcmp(library_name, "Snes9x") == 0)
        return CT_PPU_STATE_SNES9X;
    return CT_PPU_STATE_NONE;
}

/* ---- Snes9x --------------------------------------------------------------
 * "#!s9xsnp:NNNN\n", then blocks of "TAG:NNNNNN:" followed by NNNNNN bytes.
 * The PPU block is a FreezeStruct of struct SPPU. Integers are big-endian
 * and uint16 arrays are big-endian per element. Offsets are for snapshot
 * versions 11-14:
 *   64    CGDATA[256]      uint16 BE
 *   576   OBJ[128]         11 bytes each: HPos(2) VPos(2) HFlip VFlip Name(2)
 *                          Priority Palette Size
 *   2003  OAMData[544]     raw
 * OBJ[] is decoded from OAMData by the core, so cross-checking them catches
 * layout drift in a future snapshot version. */

#define S9X_CGDATA 64
#define S9X_OBJ 576
#define S9X_OBJ_SIZE 11
#define S9X_OAMDATA 2003

static const uint8_t *s9x_block(const uint8_t *state, size_t len, const char *tag, size_t *block_len) {
    const uint8_t *nl = memchr(state, '\n', len < 32 ? len : 32);
    if (len < 9 || memcmp(state, "#!s9xsnp", 8) != 0 || !nl)
        return NULL;
    int version = atoi((const char *)state + 9);
    if (version < 11 || version > 14)
        return NULL;
    size_t pos = (size_t)(nl - state) + 1;
    while (pos + 11 <= len) {
        const uint8_t *h = state + pos;
        if (h[3] != ':' || h[10] != ':')
            return NULL;
        size_t size;
        if (memcmp(h + 4, "------", 6) == 0) {
            size = (size_t)h[6] << 24 | (size_t)h[7] << 16 | (size_t)h[8] << 8 | h[9];
        } else {
            char digits[7];
            memcpy(digits, h + 4, 6);
            digits[6] = 0;
            size = strtoul(digits, NULL, 10);
        }
        pos += 11;
        if (pos + size > len)
            return NULL;
        if (memcmp(h, tag, 3) == 0) {
            *block_len = size;
            return state + pos;
        }
        pos += size;
    }
    return NULL;
}

static int s9x_extract(const uint8_t *state, size_t len, uint8_t *cgram, uint8_t *oam, const char **why) {
    size_t ppu_len;
    const uint8_t *ppu = s9x_block(state, len, "PPU", &ppu_len);
    if (!ppu) {
        *why = "no PPU block in Snes9x snapshot (unsupported snapshot version?)";
        return -1;
    }
    if (ppu_len < S9X_OAMDATA + CT_OAM_SIZE) {
        *why = "Snes9x PPU block too short";
        return -1;
    }

    const uint8_t *oam_data = ppu + S9X_OAMDATA;
    for (int n = 0; n < 128; n++) {
        const uint8_t *obj = ppu + S9X_OBJ + n * S9X_OBJ_SIZE;
        const uint8_t *o = oam_data + n * 4;
        unsigned hpos_low = obj[1], vpos = (unsigned)obj[2] << 8 | obj[3];
        unsigned hflip = obj[4], vflip = obj[5], name = (unsigned)obj[6] << 8 | obj[7];
        unsigned priority = obj[8], palette = obj[9];
        if (hpos_low != o[0] || vpos != o[1] || (name & 0xFF) != o[2] || name > 0x1FF ||
            (name >> 8) != (o[3] & 1u) || palette != ((o[3] >> 1) & 7u) || priority != ((o[3] >> 4) & 3u) ||
            hflip != ((o[3] >> 6) & 1u) || vflip != (o[3] >> 7)) {
            *why = "Snes9x PPU block failed the OBJ/OAMData consistency check (layout changed?)";
            return -1;
        }
    }

    memcpy(oam, oam_data, CT_OAM_SIZE);
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = ppu + S9X_CGDATA + i * 2;
        if (c[0] & 0x80) {
            *why = "Snes9x CGDATA entry has bit 15 set (layout changed?)";
            return -1;
        }
        cgram[i * 2] = c[1];
        cgram[i * 2 + 1] = c[0];
    }
    return 0;
}

int ct_ppu_state_extract(ct_ppu_state_kind kind, const uint8_t *state, size_t state_len, uint8_t *cgram,
                         uint8_t *oam, const char **why) {
    switch (kind) {
    case CT_PPU_STATE_SNES9X:
        return s9x_extract(state, state_len, cgram, oam, why);
    default:
        *why = "no CGRAM/OAM adapter for this core";
        return -1;
    }
}
