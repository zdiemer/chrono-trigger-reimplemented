/* The GBA's keypad as the SNES controller (targets/gba/README.md, "Controls"):
 * the driver's bindings, not the game's.
 *
 * The GBA has ten keys and the SNES twelve buttons. The pad, A, B, L, R,
 * Start and Select are the SNES's own; X and Y are a shoulder held with A or B:
 * L or R with A is X (the menu), with B is Y. A key keeps the button it began
 * as until it is let go, so letting the shoulder go first never turns a held
 * X into an A, and a shoulder held as a shift isn't pressed itself. */
#ifndef CT_GBA_KEYPAD_H
#define CT_GBA_KEYPAD_H

#include <stdint.h>

#include "platform/input.h"

typedef struct {
    PadBits a_as, b_as;   /* the button A and B are held as; 0 while let go */
} Keypad;

/* The keys held now (the GBA's own bits, 1 for held) as the SNES's buttons. */
PadBits keypad_buttons(Keypad *k, uint16_t held);

/* The keys held now, from the keypad's register. */
uint16_t keypad_held(void);

#endif
