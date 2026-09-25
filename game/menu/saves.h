/* The cartridge's saves as the menus see them, through the save service
 * (platform/save.h): three games, each $A00 bytes, and at the save memory's
 * end a record of them -- the slot saved last, how many times the game has
 * been finished and whether it has, and each slot's check and mark. The
 * games themselves are read and written by the save code in bank $FF, still
 * the original's. */
#ifndef CT_GAME_MENU_SAVES_H
#define CT_GAME_MENU_SAVES_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/save.h"

enum { MENU_SAVE_SLOTS = 3 };

enum { SAVES_LAST_SLOT = 0x1FE0, SAVES_TIMES_FINISHED = 0x1FE1, SAVES_FINISHED = 0x1FE2, SAVES_SLOT_MARKS = 0x1FF8 };

static inline uint8_t saves_last_slot(Platform *pf, PfOrigin o) { return sv_read8(pf, o, SAVES_LAST_SLOT); }
static inline uint8_t saves_times_finished(Platform *pf, PfOrigin o) { return sv_read8(pf, o, SAVES_TIMES_FINISHED); }

/* Once the game has been finished, the load screen offers a New Game+. */
static inline bool saves_finished(Platform *pf, PfOrigin o) { return sv_read8(pf, o, SAVES_FINISHED) != 0; }
static inline void saves_forget_finished(Platform *pf, PfOrigin o) { sv_write8(pf, o, SAVES_FINISHED, 0); }

/* A slot holds a game while its mark (a word) is the one the save writes; the
 * erase writes over the mark's first byte. */
static inline void saves_spoil_mark(Platform *pf, PfOrigin o, uint8_t slot, uint8_t v)
{
    sv_write8(pf, o, (uint16_t)(SAVES_SLOT_MARKS + 2 * slot), v);
}

#endif
