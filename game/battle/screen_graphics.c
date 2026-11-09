/* Battle: loading an effect's graphics.
 *
 * Before a spell or an attack's effect plays, its graphics are loaded: up to
 * four compressed blocks of tiles into VRAM $2000-$3FFF, one after the other,
 * the effect's 48 colors into background rows 2-4 of the palette as loaded,
 * and one more block into the tilemap area at $6800.
 * Each block is unpacked into a buffer in work RAM and sent to VRAM by the
 * frame work, which takes frames; the battle's frame wait doesn't start
 * another effect's load meanwhile.
 *
 * The unpacking is C (data/decompress.c); the sending is still a call to the
 * frame work on the machine, so the loader is a bridge: the machine appears
 * at that call and at the entry. */
#include "battle/screen.h"

#include "battle/bridge.h"
#include "data/decompress.h"

enum {
    FIRST_BLOCK_VRAM = 0x2000,
    BLOCK_VRAM_WORDS = 0x0800,
    BLOCK_BYTES = 2 * BLOCK_VRAM_WORDS,
    TILEMAP_GRAPHICS_VRAM = 0x6800,
    TILEMAP_GRAPHICS_BYTES = 0x0800,
    EFFECT_FIRST_COLOR = 2 * 16,
};

/* The blocks are unpacked here, then sent. */
#define UNPACK_BUFFER 0x7ED000u

/* ---- The load, in the game's terms ---------------------------------------------------------- */

/* The original keeps its place in the effect's list in three cells, which
 * nothing else reads: the list's offset, the VRAM address and the block. They
 * hold these at each of its calls. */

/* ---- The two calls to the original's code --------------------------------------------------- */

/* The carry and overflow the decompressor and the transfer hand back to the
 * loader's caller are those of the original's index arithmetic before each
 * unpacking (three bytes an entry). For a block id the index is a 16-bit sum,
 * which neither carries nor overflows. */

/* For the effect's tilemap graphics it is a byte, twice the effect plus it:
 * it carries past 255, and overflows where the two taken as signed bytes add
 * up past the signed range. */

/* Unpack a compressed block into the buffer (data/decompress.c), which
 * leaves the registers as they came: the carry and overflow among them, which
 * the endings' movies show the loader's callers reading. */

/* Send the buffer to VRAM and wait for it to go: the frame work's transfer
 * ($CD:3ECE) takes the buffer's bank in A and its address in X, the VRAM
 * address in Y and the size in the page's cells $53-$54. */

// $D1:ECF3
/* Load the effect in A's graphics and colors. Where a call doesn't come back
 * as the battle's code runs, the original goes on from it. */
