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
void event_cmd_if_mem0_below(Snes *s);       /* $18 */
void event_cmd_if_object_value(Snes *s);     /* $1A */
void event_cmd_if_object_in_view(Snes *s);   /* $27 */
void event_cmd_if_object_on_screen(Snes *s); /* $28 */

/* event/input.c: conditions on controller input */
void event_cmd_if_any_input(Snes *s);        /* $2D */
void event_cmd_if_input_f8_bit1(Snes *s);    /* $30 */
void event_cmd_if_input_f8_a(Snes *s);       /* $31 */
void event_cmd_if_input_f2_a(Snes *s);       /* $34 */
void event_cmd_if_input_f2_bit3(Snes *s);    /* $35 */
void event_cmd_if_input_f2_bit6(Snes *s);    /* $36 */
void event_cmd_if_input_f2_bit2(Snes *s);    /* $37 */
void event_cmd_if_input_f2_bit5(Snes *s);    /* $38 */
void event_cmd_if_input_f2_bit4(Snes *s);    /* $39 */
void event_cmd_if_latched_50_bit1(Snes *s);  /* $3B */
void event_cmd_if_latched_50_bit7(Snes *s);  /* $3C */
void event_cmd_if_latched_51_bit7(Snes *s);  /* $3F */

/* event/facing.c: which way objects face */
void event_cmd_face_0(Snes *s);              /* $0F */
void event_cmd_face_1(Snes *s);              /* $17 */
void event_cmd_face_2(Snes *s);              /* $1B */
void event_cmd_face_3(Snes *s);              /* $1D */
void event_cmd_object_face_0(Snes *s);       /* $1E */
void event_cmd_object_face_1(Snes *s);       /* $1F */
void event_cmd_object_face_2(Snes *s);       /* $25 */
void event_cmd_object_face_3(Snes *s);       /* $26 */
void event_cmd_face_operand(Snes *s);        /* $A6 */
void event_cmd_face_variable(Snes *s);       /* $A7 */
void event_cmd_face_object(Snes *s);         /* $A8 */
void event_cmd_face_party_member(Snes *s);   /* $A9 */

/* event/variables.c: copying values */
void event_cmd_set_object_value_from_var(Snes *s);     /* $19 */
void event_cmd_set_object_value_from_memory(Snes *s);  /* $1C */
void event_cmd_set_var_from_2980(Snes *s);             /* $20 */
void event_cmd_get_object_tile(Snes *s);               /* $21 */
void event_cmd_get_party_member_tile(Snes *s);         /* $22 */
void event_cmd_get_object_facing(Snes *s);             /* $23 */
void event_cmd_get_party_member_facing(Snes *s);       /* $24 */

/* event/movement.c: moving objects */
void event_cmd_move_toward_party_member_until_on_screen(Snes *s);  /* $8F */
void event_cmd_move_toward_object(Snes *s);                   /* $94 */
void event_cmd_move_toward_party_member(Snes *s);             /* $95 */
void event_cmd_move_to_tile(Snes *s);                         /* $96 */
void event_cmd_move_toward_object_counted(Snes *s);           /* $98 */
void event_cmd_move_toward_party_member_counted(Snes *s);     /* $99 */
void event_cmd_move_by_variables(Snes *s);                    /* $9D */
void event_cmd_move_toward_object_keep_facing(Snes *s);       /* $9E */
void event_cmd_move_toward_party_member_keep_facing(Snes *s); /* $9F */
void event_cmd_run_state_routine(Snes *s);                    /* $B0 */
void event_cmd_follow_object(Snes *s);                        /* $B5 */
void event_cmd_follow_party_member(Snes *s);                  /* $B6 */

#endif
