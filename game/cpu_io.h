/* The CPU's multiplier, divider and work RAM port, as the game drives them.
 *
 * The 8x8 multiplier ($4202/$4203, the product at $4216), the 16/8 divider
 * ($4204-$4206, the quotient at $4214) and the work RAM port ($2180-$2183) are
 * arithmetic and memory access, not platform services (docs/PLATFORM.md, "Not
 * services"). But their registers are part of what shadow mode and the fuzzer
 * compare, and a routine can leave an operand or a cursor behind for the next
 * one, so while the game's state lives in emulated work RAM a port drives them
 * as the original does: set an operand and read the result, move the cursor and
 * stream bytes through it. runtime/port/state_io.c is the backing, beside the one
 * for game/vars.h; a native backing computes the same values and keeps the
 * cursor as a value, with no registers at all.
 *
 * They take the state rather than the machine because that is what they are to
 * a port: values it leaves and reads back, like any other variable. */
#ifndef CT_GAME_CPU_IO_H
#define CT_GAME_CPU_IO_H

#include <stdint.h>

#include "state.h"

/* The multiplier: the first operand stays until it is set again, and every
 * second operand starts a product. */
void hw_set_multiplicand(GameState *g, uint8_t a);
uint16_t hw_multiply_by(GameState *g, uint8_t b);

/* The divider: the dividend a division starts from, the quotient of the
 * division started last, and a new division of the dividend already there by
 * `divisor`. */
void hw_set_dividend(GameState *g, uint16_t dividend);
uint16_t hw_quotient(const GameState *g);
void hw_divide_by(GameState *g, uint8_t divisor);

/* The work RAM port: a cursor into work RAM that steps on with every byte read
 * or written through it, which the game uses to stream a list or to write code.
 * `seek_address` moves the cursor within the bank it is already in, and
 * `seek_bank` to the other bank at the address it is already at. */
void hw_port_seek(GameState *g, uint16_t at, uint8_t bank);
void hw_port_seek_address(GameState *g, uint16_t at);
void hw_port_seek_bank(GameState *g, uint8_t bank);
uint8_t hw_port_read(GameState *g);
void hw_port_write(GameState *g, uint8_t v);

#endif
