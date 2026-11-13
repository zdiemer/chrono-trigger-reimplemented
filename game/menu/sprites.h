/* The sprite-object mode's sprites (menu/sprites.c): each object's sprite
 * script, which shows its sprite groups frame by frame, and the list they are
 * drawn from. */
#ifndef CT_GAME_MENU_SPRITES_H
#define CT_GAME_MENU_SPRITES_H

#include <stdint.h>

#include "menu/object.h"
#include <stdbool.h>
#include "platform/platform.h"
#include "platform/video.h"
#include "state.h"

/* Give an object one of the sprite scripts by its number, from its start. */
void menu_give_sprite_script(GameState *g, MenuObject o, uint8_t script);

/* Lay out the links the sprite list's free nodes start every frame from, as
 * the sprite-object mode starts. */
void menu_lay_out_the_sprite_nodes(GameState *g);
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

/* The list as the vblank handler works it each frame, around the objects'
 * turns: emptied, every node back on the free list from the links the mode's
 * start laid out; the sprite table's sprites all hidden; and, once the
 * objects have shown their groups, drawn into the sprite table. And what
 * those need: the links laid out as the start lays them, and the list as
 * the mode keeps it. */
void menu_empty_the_sprite_list(GameState *g);
void menu_hide_the_sprites(GameState *g);
void menu_draw_the_sprite_list(GameState *g);

/* For a view wider than the original's screen (vid_view): the world map's
 * sprites past the original's screen, which the drawing leaves out, listed
 * as it draws (menu/sprites.c) and shown in the view's margins every vblank
 * until the next; and none, as the mode starts. The list is the program's
 * own state, beside the game's memory. */
typedef struct {
    uint8_t count;
    VidMarginSprite sprite[VID_MARGIN_SPRITES];
} MenuSpritesPast;
/* Module storage supplied by the state owner, independent of mapped RAM. */
MenuSpritesPast *menu_sprites_past(GameState *g);
void menu_list_the_sprites_past_the_screen(GameState *g, Platform *pf);
void menu_show_the_sprites_past_the_screen(GameState *g, Platform *pf);
void menu_forget_the_sprites_past_the_screen(GameState *g);
bool menu_sprite_list_can_be_emptied(const GameState *g);
bool menu_sprite_list_can_be_drawn(const GameState *g);

/* The menus' records' sprites (the characters on the load screen and the
 * menus' screens): the opening's seven, a record's load, and its animation a
 * frame on, or carried on. One record's sprite loaded ($C2:F422), and the
 * records set up and loaded as the menus open ($C2:F3CA); false where the
 * state is none the loading handles, the program going no further there. */
bool menu_record_sprite_loaded(GameState *g, unsigned record);
bool menu_record_sprites_loaded(GameState *g);

/* A screen of the party's members: its records reset to the seven
 * characters', each still and hidden until the screen places it ($C2:F566);
 * or every record emptied ($C2:F588), or the seven characters' ($C2:CE2A);
 * the records' task started, drawing
 * them where they stand ($C2:F643); and every character's sprite colours
 * loaded in its own palette, with its weapon's ($C2:F5A7). */
void menu_reset_character_records(GameState *g);
void menu_empty_the_records(GameState *g);
void menu_empty_the_character_records(GameState *g);
void menu_start_the_records(GameState *g);
void menu_load_character_colours(GameState *g);

/* A frame of the records' task ($C2:F657): each record's sprite drawn and
 * animated; and what a frame of it needs of the state. */
void menu_run_the_records(GameState *g);
bool menu_records_can_run(const GameState *g);

/* The three colours of the weapon a character wears, which its sprite
 * palette ends with ($C2:F5ED). */
typedef struct {
    uint16_t colour[3];
} MenuWeaponColours;
MenuWeaponColours menu_weapon_colours(GameState *g, uint8_t character);

/* A character's colours in its sprite palette (its sprite's and its
 * weapon's) dimmed to half where they are: the party exchange dims the
 * members the story keeps. And what the original does with an emptied
 * place's byte there, which it takes for a character, halving fifteen words
 * past the palette. */
void menu_dim_character_colours(GameState *g, uint8_t character);

/* A character's sprite colours put in its sprite palette, dimmed to half
 * where `dim` (a screen of the party's members dims those not chosen). */
void menu_put_character_colours(GameState *g, uint8_t character, bool dim);

#endif
