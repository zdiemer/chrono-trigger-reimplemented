/* Contracts at the menus' checked C handoffs. */
#ifndef CT_GAME_MENU_MACHINE_FORMS_H
#define CT_GAME_MENU_MACHINE_FORMS_H
#include <stdbool.h>
#include <stdint.h>
#include "snes/system.h"
bool menu_machine_form_takes(Snes *s, uint32_t address, void (*entry)(Snes *));
void (*menu_object_machine_form(uint16_t handler))(Snes *);
#endif
