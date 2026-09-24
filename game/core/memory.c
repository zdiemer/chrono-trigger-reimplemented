/* The game's block move between banks it picks at run time: a copy of a run of
 * bytes, for the menus (a frame's upload buffer, a window's tiles) and the
 * save code (a save's blocks to and from the cartridge's save RAM).
 *
 * The original keeps it as two instructions in work RAM, a block move and a
 * return (the stub at $7E:9690), whose bank operands its callers write before
 * they call it; the entry reads them there. */
#include "core/memory.h"

#include "charge.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* A move: `bytes` bytes forward from `from` to `to`, each address a bank in
 * its top byte. */
typedef struct {
    uint32_t from, to, bytes;
} BlockMove;

// $7E:9690
/* Copy the bytes, each address staying in its bank (past the bank's end it
 * wraps to its start). A copy onto itself a byte on repeats its first byte, as
 * the original's does, and the callers that fill a buffer rely on it. The
 * banks are the caller's, save RAM and the cartridge as well as work RAM,
 * which wram.h's copy reaches alike. */
static void move_block(GameState *g, BlockMove m)
{
    wram_copy(g, m.from, m.to, m.bytes);
    ORIGINAL_EFFECT(charge_block_move(g, m.bytes));
}

/* The stub, where the original keeps its block move: the instruction, its two
 * bank operands, and a return. */
enum { STUB = 0x7E9690, STUB_BYTES = 4, BLOCK_MOVE_OPCODE = 0x54, RETURN_OPCODE = 0x6B };
enum { TO_BANK_SHIFT = 8, FROM_BANK_SHIFT = 16 };

/* Whether a move writes over the stub itself (the offsets wrap within the
 * bank, as the move's do). */
static bool overwrites_stub(BlockMove m)
{
    if (m.to >> 16 != STUB >> 16)
        return false;
    for (unsigned k = 0; k < STUB_BYTES; k++)
        if ((uint16_t)((STUB & 0xFFFF) + k - m.to) < m.bytes)
            return true;
    return false;
}

/* The move a caller asks for: the banks it wrote into the stub, and the count
 * less one in A, from X to Y. */
static BlockMove requested_move(Snes *s, uint32_t stub)
{
    uint32_t to_bank = (stub >> TO_BANK_SHIFT) & 0xFF, from_bank = (stub >> FROM_BANK_SHIFT) & 0xFF;
    return (BlockMove){ .from = from_bank << 16 | s->cpu.x, .to = to_bank << 16 | s->cpu.y, .bytes = s->cpu.a + 1u };
}

/* The stub in place, and a move that leaves it alone: one that writes over
 * the stub runs whatever it wrote, which no caller does. */
bool memory_move_stub_present(Snes *s)
{
    vars_peek(true);
    uint32_t stub = menu_block_move_stub(state_of(s));
    vars_peek(false);
    return (stub & 0xFF) == BLOCK_MOVE_OPCODE && stub >> 24 == RETURN_OPCODE && !overwrites_stub(requested_move(s, stub));
}

/* It comes back with A $FFFF, X and Y past the bytes moved and the data bank
 * the one moved to. */
void memory_move_stub(Snes *s)
{
    GameState *g = state_of(s);
    BlockMove m = requested_move(s, menu_block_move_stub(g));
    move_block(g, m);
    m_leave_mvn(s, m.bytes, (uint8_t)(m.to >> 16));
    m_rtl(s);
}
