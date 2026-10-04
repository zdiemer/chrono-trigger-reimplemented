/* The sprite-object mode's sprites (menu/sprites.c): each object's sprite
 * script, which shows its sprite groups frame by frame, and the list they are
 * drawn from. */
#ifndef CT_GAME_MENU_SPRITES_H
#define CT_GAME_MENU_SPRITES_H

#include <stdint.h>

#include "menu/object.h"
#include "snes/system.h"
#include "state.h"

/* Give an object one of the sprite scripts by its number, from its start. */
void menu_give_sprite_script(GameState *g, MenuObject o, uint8_t script);

/* Run the running object's sprite script for this frame: it shows the
 * object's sprites where the object is. */
void menu_object_animate(GameState *g);

/* Whether the object's sprite script reads as the language: every command it
 * can reach one of its eight. */
bool menu_sprite_script_is_well_formed(const GameState *g, MenuObject o);
/* ... and whether one of the game's sprite scripts, by its number, does. */
bool menu_given_sprite_script_is_well_formed(const GameState *g, uint8_t script);

/* Whether the list the sprites are shown on is as the mode's start lays it
 * out: every link a node or a head, and a node free to show a group with. */
bool menu_sprite_list_is_sound(const GameState *g);

/* The menus' records' sprites (the characters on the load screen and the
 * menus' screens): the opening's seven, a record's load, and its animation a
 * frame on, or carried on; and their guards. */
void menu_load_record_sprites(Snes *s);
void menu_load_record_sprite(Snes *s);
void menu_animate_record(Snes *s);
void menu_step_record_animation(Snes *s);
void menu_run_record_task(Snes *s);
bool menu_record_task_is_runnable(Snes *s);
void menu_show_record_frame(Snes *s);
bool menu_record_frame_is_showable(Snes *s);
bool menu_record_sprites_are_loadable(Snes *s);
bool menu_record_sprite_is_loadable(Snes *s);
bool menu_record_is_animatable(Snes *s);

/* A screen of the party's members: its records reset to the seven
 * characters', each still and hidden until the screen places it ($C2:F566);
 * or every record emptied ($C2:F588); the records' task started, drawing
 * them where they stand ($C2:F643); and every character's sprite colours
 * loaded in its own palette, with its weapon's ($C2:F5A7). */
void menu_reset_character_records(GameState *g);
void menu_empty_the_records(GameState *g);
void menu_start_the_records(GameState *g);
void menu_load_character_colours(GameState *g);

/* The three colours of the weapon a character wears, which its sprite
 * palette ends with ($C2:F5ED). */
typedef struct {
    uint16_t colour[3];
} MenuWeaponColours;
MenuWeaponColours menu_weapon_colours(GameState *g, uint8_t character);

/* The registered entries of the four. */
void menu_reset_character_records_entry(Snes *s);
void menu_empty_the_records_entry(Snes *s);
void menu_start_the_records_entry(Snes *s);
void menu_load_character_colours_entry(Snes *s);
bool menu_character_colours_are_loadable(Snes *s);

/* For the original's code and shadow mode: a character's sprite record
 * found as the original finds it ($C2:F626), with the multiplier (the
 * character times a record's $40 bytes), its offset noted
 * (menu_record_offset). */
void menu_leave_record_found(GameState *g, uint8_t character);

/* A character's sprite colours put in its sprite palette, dimmed to half
 * where `dim` (a screen of the party's members dims those not chosen). */
void menu_put_character_colours(GameState *g, uint8_t character, bool dim);
void menu_put_character_colours_entry(Snes *s);
bool menu_character_colours_are_puttable(Snes *s);

#endif
