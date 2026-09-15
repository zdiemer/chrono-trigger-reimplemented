/* Scenes: sound driver messages and waits (scene/sound.c for the bank C3 script
 * command; the RAM program's are in scene/program.c). */
#ifndef CT_GAME_SCENE_SOUND_H
#define CT_GAME_SCENE_SOUND_H

#include <stdbool.h>

#include "snes/system.h"

/* scene/sound.c */
void scene_script_wait_sound_port2(Snes *s);

/* scene/program.c (RAM code; guarded) */
bool scene_send_sound_command_present(Snes *s);
void scene_send_sound_command(Snes *s);
void scene_send_sound_command_again(Snes *s);
void scene_wait_sound_command_echo_loop(Snes *s);
bool scene_race_sound_present(Snes *s);
void scene_race_send_sound_parameter(Snes *s);

#endif
