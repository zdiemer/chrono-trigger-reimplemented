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
void event_cmd_wait_4(Snes *s);              /* $B9 */
void event_cmd_wait_8(Snes *s);              /* $BA */
void event_cmd_wait_16(Snes *s);             /* $BC */
void event_cmd_wait_32(Snes *s);             /* $BD */
void event_cmd_run_state_routine_once(Snes *s);  /* $AF */
void event_cmd_if_mem0_below(Snes *s);       /* $18 */
void event_cmd_if_object_value(Snes *s);     /* $1A */
void event_cmd_if_object_in_view(Snes *s);   /* $27 */
void event_cmd_if_object_on_screen(Snes *s); /* $28 */

/* event/calls.c: calling functions in other objects' scripts */
void event_cmd_return(Snes *s);                             /* $00 */
void event_cmd_call(Snes *s);                               /* $02 */
void event_cmd_call_when_accepted(Snes *s);                 /* $03 */
void event_cmd_call_and_wait(Snes *s);                      /* $04 */
void event_cmd_call_party_member(Snes *s);                  /* $05 */
void event_cmd_call_party_member_when_accepted(Snes *s);    /* $06 */
void event_cmd_call_party_member_and_wait(Snes *s);         /* $07 */
void event_cmd_refuse_calls(Snes *s);                       /* $08 */
void event_cmd_accept_calls(Snes *s);                       /* $09 */
void event_cmd_disable_object(Snes *s);                     /* $0A */
void event_cmd_set_object_flag(Snes *s);                    /* $0B */
void event_cmd_clear_object_flag(Snes *s);                  /* $0C */
void event_cmd_set_1c80(Snes *s);                           /* $0D */
void event_cmd_set_arrival_flags(Snes *s);                  /* $0E */

/* event/dialog.c: dialog boxes */
void event_cmd_set_dialog_table(Snes *s);    /* $B8 */
void event_cmd_dialog(Snes *s);              /* $BB */
void event_cmd_dialog_top(Snes *s);   /* $C1 */
void event_cmd_dialog_bottom(Snes *s);   /* $C2 */
void event_cmd_choice(Snes *s);              /* $C0 */
void event_cmd_choice_top(Snes *s);   /* $C3 */
void event_cmd_choice_bottom(Snes *s);   /* $C4 */

/* event/animation.c: object animation */
void event_cmd_loop_animation(Snes *s);      /* $AA */
void event_cmd_play_animation(Snes *s);      /* $AB */
void event_cmd_static_frame(Snes *s);        /* $AC */
void event_cmd_reset_animation(Snes *s);     /* $AE */
void event_cmd_loop_animation_0(Snes *s);    /* $B3 */
void event_cmd_loop_animation_1(Snes *s);    /* $B4 */
void event_cmd_play_animation_for(Snes *s);  /* $B7 */

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
void event_cmd_if_latched_51_bit3(Snes *s);  /* $40 */
void event_cmd_if_latched_51_bit6(Snes *s);  /* $41 */
void event_cmd_if_latched_51_bit2(Snes *s);  /* $42 */
void event_cmd_if_latched_51_bit5(Snes *s);  /* $43 */
void event_cmd_if_latched_51_bit4(Snes *s);  /* $44 */

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
void event_cmd_move_in_direction(Snes *s);                    /* $92 */
void event_cmd_move_to_tile_from_variables(Snes *s);          /* $97 */
void event_cmd_move_to_tile_counted(Snes *s);                 /* $9A */
void event_cmd_move_in_direction_keep_facing(Snes *s);        /* $9C */
void event_cmd_move_to_tile_keep_facing(Snes *s);             /* $A0 */
void event_cmd_move_to_tile_from_variables_keep_facing(Snes *s);  /* $A1 */
void event_cmd_move_to_tile_with_velocity(Snes *s);           /* $7A */
void event_cmd_move_with_velocity(Snes *s);                   /* $7B */

/* event/properties.c: sprite display, position and priority */
void event_cmd_show_object_sprite(Snes *s);  /* $7C */
void event_cmd_hide_object_sprite(Snes *s);  /* $7D */
void event_cmd_set_sprite_flag_80(Snes *s);  /* $7E */
void event_cmd_set_1b01(Snes *s);            /* $84 */
void event_cmd_set_step_count(Snes *s);      /* $87 */
void event_cmd_set_1a00(Snes *s);            /* $89 */
void event_cmd_set_1a00_from_variable(Snes *s);  /* $8A */
void event_cmd_set_tile_position(Snes *s);   /* $8B */
void event_cmd_set_tile_position_from_variables(Snes *s);  /* $8C */
void event_cmd_set_position(Snes *s);        /* $8D */
void event_cmd_set_sprite_priority(Snes *s); /* $8E */
void event_cmd_show_sprite(Snes *s);         /* $90 */
void event_cmd_hide_sprite(Snes *s);         /* $91 */

/* event/arithmetic.c: variables */
void event_cmd_set_6b(Snes *s);  /* $47 */
void event_cmd_var_set(Snes *s);  /* $4F */
void event_cmd_var_set16(Snes *s);  /* $50 */
void event_cmd_var_copy(Snes *s);  /* $51 */
void event_cmd_var_copy16(Snes *s);  /* $52 */
void event_cmd_var_add(Snes *s);  /* $5B */
void event_cmd_var_add_var(Snes *s);  /* $5D */
void event_cmd_var_add_var16(Snes *s);  /* $5E */
void event_cmd_var_subtract(Snes *s);  /* $5F */
void event_cmd_var_subtract16(Snes *s);  /* $60 */
void event_cmd_var_subtract_var(Snes *s);  /* $61 */
void event_cmd_var_set_bit(Snes *s);  /* $63 */
void event_cmd_var_clear_bit(Snes *s);  /* $64 */
void event_cmd_var_and(Snes *s);  /* $67 */
void event_cmd_var_or(Snes *s);  /* $69 */
void event_cmd_var_xor(Snes *s);  /* $6B */
void event_cmd_var_shift_right(Snes *s);  /* $6F */
void event_cmd_var_increment(Snes *s);  /* $71 */
void event_cmd_var_increment16(Snes *s);  /* $72 */
void event_cmd_var_decrement(Snes *s);  /* $73 */
void event_cmd_var_set_1(Snes *s);  /* $75 */
void event_cmd_var_set16_1(Snes *s);  /* $76 */
void event_cmd_var_clear(Snes *s);  /* $77 */
void event_cmd_var_random(Snes *s);  /* $7F */

/* event/memory.c: memory and pointers */
void event_cmd_var_from_pointer(Snes *s);  /* $48 */
void event_cmd_var_from_pointer16(Snes *s);  /* $49 */
void event_cmd_poke(Snes *s);  /* $4A */
void event_cmd_poke16(Snes *s);  /* $4B */
void event_cmd_poke_variable(Snes *s);  /* $4C */
void event_cmd_poke_variable16(Snes *s);  /* $4D */
void event_cmd_copy_data(Snes *s);  /* $4E */
void event_cmd_var_from_memory(Snes *s);  /* $53 */
void event_cmd_var_from_memory16(Snes *s);  /* $54 */
void event_cmd_var_from_memory0(Snes *s);  /* $55 */
void event_cmd_memory_set(Snes *s);  /* $56 */
void event_cmd_memory_set_from_var(Snes *s);  /* $58 */
void event_cmd_memory_set_from_var16(Snes *s);  /* $59 */
void event_cmd_memory0_set(Snes *s);  /* $5A */
void event_cmd_memory_set_bit(Snes *s);  /* $65 */
void event_cmd_memory_clear_bit(Snes *s);  /* $66 */

/* event/sound.c: sound driver commands */
void event_cmd_play_sound(Snes *s);  /* $E8 */
void event_cmd_play_song(Snes *s);  /* $EA */
void event_cmd_sound_81_83(Snes *s);  /* $EB */
void event_cmd_sound_command(Snes *s);  /* $EC */
void event_cmd_wait_apu_port1(Snes *s);  /* $ED */
void event_cmd_wait_apu_port3(Snes *s);  /* $EE */

/* event/screen.c: screen effects */
void event_cmd_set_1f(Snes *s);  /* $E3 */
void event_cmd_brightness(Snes *s);  /* $F0 */
void event_cmd_wait_brightness(Snes *s);  /* $F2 */
void event_cmd_wait_color(Snes *s);  /* $F3 */
void event_cmd_fixed_color(Snes *s);  /* $F1 */
void event_cmd_set_bc(Snes *s);  /* $F4 */
void event_cmd_copy_tiles(Snes *s);  /* $E4 */
void event_cmd_queue_tile_copy(Snes *s);  /* $E5 */
void event_cmd_set_layer_scroll(Snes *s);  /* $E6 */
void event_cmd_scroll_screen(Snes *s);  /* $E7 */
void event_cmd_party_routine_6_7(Snes *s);  /* $F8 */
void event_cmd_party_routine_6(Snes *s);  /* $F9 */
void event_cmd_party_routine_7(Snes *s);  /* $FA */

/* event/locations.c: location changes, battles and party requests */
void event_cmd_party_routine_0a(Snes *s);  /* $D5 */
void event_cmd_remove_from_party(Snes *s);  /* $D6 */
void event_cmd_start_battle(Snes *s);  /* $D8 */
void event_cmd_change_location(Snes *s);  /* $E0 */
void event_cmd_set_location_0a(Snes *s);  /* $DC */
void event_cmd_set_location_00(Snes *s);  /* $DD */
void event_cmd_set_location_00_with_1e(Snes *s);  /* $DE */
void event_cmd_change_location_now(Snes *s);  /* $E1 */
void event_cmd_change_location_now_with_1e(Snes *s);  /* $DF */
void event_cmd_change_location_from_variables(Snes *s);  /* $E2 */

#endif
