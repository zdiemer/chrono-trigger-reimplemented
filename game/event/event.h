/* Event scripts: the bytecode that drives map objects, cutscenes and
 * dialog. Each object has a script in the buffer at $7F:2001 and a position
 * in it ($1180,X); commands are dispatched through the 256-entry handler table
 * at $C0:5D6E.
 *
 * Command handler convention: entered with 8-bit A and 16-bit X and Y, Y
 * holding the offset of the command byte. They return X = offset of the next
 * command to run, and carry set to keep running this object's script this
 * frame or clear to end its turn. */
#ifndef CT_GAME_EVENT_H
#define CT_GAME_EVENT_H

#include "snes/system.h"

/* event/engine.c */
void event_run_init_scripts(Snes *s);
void event_run_object_script(Snes *s);

/* event/flow.c: control flow commands */
void event_cmd_jump_forward(Snes *s);        /* $10 */
void event_cmd_jump_back(Snes *s);           /* $11 */
void event_cmd_if_var_const(Snes *s);        /* $12 */
void event_cmd_if_var_const16(Snes *s);      /* $13 */
void event_cmd_if_var_var(Snes *s);          /* $14 */
void event_cmd_if_var_var16(Snes *s);        /* $15 */
void event_cmd_if_mem_const(Snes *s);        /* $16 */
void event_cmd_wait(Snes *s);                /* $AD */
void event_cmd_yield(Snes *s);               /* $B1 */
void event_cmd_halt(Snes *s);                /* $B2 */

#endif
