/* The CPU's multiplier, divider and work RAM port, as the original leaves them.
 *
 * The 8x8 multiplier ($4202/$4203, the product at $4216), the 16/8 divider
 * ($4204-$4206, the quotient at $4214) and the work RAM port ($2180-$2183) are
 * arithmetic and memory access, not platform services (docs/PLATFORM.md, "Not
 * services"). A port computes in C (`a * b`, `n / d`, a variable read through
 * its accessor) and never uses these to compute. What they are for is the
 * state the original leaves in them: their registers are part of what shadow
 * mode and the fuzzer compare, and a routine can leave an operand or a cursor
 * behind for the next one, so a port leaves the operands, the result and the
 * cursor as the original does, as scaffolding (ORIGINAL_EFFECT;
 * docs/PORTING.md, "Scaffolding"). The few ports that still read a stream
 * through the port, where its data has no entries in the RAM map yet, are
 * listed in docs/TODO.md; that is a debt, not a way to write a port.
 * runtime/port/state_io.c is the backing, beside the one for game/vars.h; a
 * native backing keeps what those streams need as values, with no
 * registers.
 *
 * They take the state rather than the machine because that is what they are to
 * a port: values it leaves, like any other variable. */
#ifndef CT_GAME_CPU_IO_H
#define CT_GAME_CPU_IO_H

#include <stdint.h>

#include "state.h"

/* The multiplier: the first operand stays until it is set again, and every
 * second operand starts a product. */
void hw_set_multiplicand(GameState *g, uint8_t a);
uint16_t hw_multiply_by(GameState *g, uint8_t b);
/* The first operand a caller left in the multiplier, for a routine that
 * multiplies by it itself (as hw_dividend: no time, the register can't be
 * read on the machine). */
uint8_t hw_multiplicand(const GameState *g);

/* The divider: the dividend a division starts from (which stays until it is
 * set again, so a routine can divide it by one divisor after another), the
 * quotient and the remainder of the division started last, and a new division
 * of the dividend already there by `divisor`. */
void hw_set_dividend(GameState *g, uint16_t dividend);
uint16_t hw_quotient(const GameState *g);
uint16_t hw_remainder(const GameState *g);
void hw_divide_by(GameState *g, uint8_t divisor);
/* A division started by a word store, as some of the scene program's are: the
 * divisor is the low byte, and the high byte lands in the next register, the
 * dot timer's low byte (which nothing enables while the scenes run; PLATFORM.md,
 * "The census", the divider spills). */
void hw_divide_by_word(GameState *g, uint16_t word);
/* The dividend a caller left in the divider, for a routine that divides it
 * itself (reading it costs no time: the register can't be read on the
 * machine, so the backing looks at what the divider holds). */
uint16_t hw_dividend(const GameState *g);

/* The work RAM port: a cursor into work RAM that steps on with every byte read
 * or written through it, which the game uses to stream a list or to write code.
 * `seek_address` moves the cursor within the bank it is already in, and
 * `seek_bank` to the other bank at the address it is already at. */
void hw_port_seek(GameState *g, uint16_t at, uint8_t bank);
void hw_port_seek_address(GameState *g, uint16_t at);
void hw_port_seek_bank(GameState *g, uint8_t bank);
uint8_t hw_port_read(GameState *g);
void hw_port_write(GameState *g, uint8_t v);
/* A 16-bit store at the address just below the port sends its high byte
 * through the port and leaves its low byte on the sound CPU's port 3, which
 * that address mirrors (no message: the driver reads port 3 only after a
 * message's other bytes; docs/PLATFORM.md, "P1 as built"). This leaves that
 * low byte on port 3, as such a store would: scaffolding only. */
void hw_port_leave_spill(GameState *g, uint8_t low);

#endif
