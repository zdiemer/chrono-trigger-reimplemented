/* Internal helpers shared by the event command handlers. */
#ifndef CT_GAME_EVENT_SCRIPT_H
#define CT_GAME_EVENT_SCRIPT_H

#include "port/machine.h"

#define SCRIPT 0x7F2001u     /* script buffer */
#define VARS 0x7F0200u       /* 2-byte script variables, addressed by index * 2 */

/* Direct-page cells the engine and handlers share. */
enum {
    DP_OBJECT = 0x6D,        /* 16-bit slot of the object whose script is running */
    DP_PARTY = 0x97,         /* slots of the party members, indexed by member */
};

static inline uint8_t script_byte(Snes *s, uint16_t offset) { return m_read8(s, SCRIPT + offset); }

/* LDX $6D */
static inline uint16_t ldx_current_object(Snes *s) { return m_ldx(s, m_read16(s, m_dp(s, DP_OBJECT))); }

/* A byte of an object table (`table`,X with the current X). */
static inline uint8_t object_byte(Snes *s, uint16_t table) { return m_read8(s, m_abs(s, table) + s->cpu.x); }

#endif
