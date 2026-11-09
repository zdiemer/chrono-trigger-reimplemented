/* Contracts at the battle's checked C handoffs. */
#ifndef CT_GAME_BATTLE_MACHINE_FORMS_H
#define CT_GAME_BATTLE_MACHINE_FORMS_H
#include <stdbool.h>
#include <stdint.h>
#include "snes/system.h"
bool battle_machine_form_takes(Snes *s, uint32_t address, void (*entry)(Snes *));
#endif
