/* The menus' cursor, the menus' sprites each frame, and the menus' sounds
 * (game/menu/cursor.c). */
#ifndef CT_GAME_MENU_CURSOR_H
#define CT_GAME_MENU_CURSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* The menus' short sounds, by their number with the sound driver: the
 * cursor's tick as it moves, the buzz of a choice refused, and the two a
 * screen can queue (menu_queued_sounds). */
typedef enum { MENU_SOUND_CURSOR = 0, MENU_SOUND_REFUSAL = 1, MENU_SOUND_6 = 6, MENU_SOUND_7 = 7 } MenuSound;

/* Every sprite out of sight; the menus' own hidden and the cursor placed, as
 * every frame; or every sprite hidden and the cursor placed facing its own
 * way. */
void menu_clear_sprites(GameState *g);
void menu_reset_sprites_and_cursor(GameState *g);
void menu_clear_sprites_but_cursor(GameState *g);

/* The frame's input through the cursor: the direction pad's push moves it,
 * and the buttons the player assigned come out as menu_cursor_buttons. */
void menu_read_cursor(GameState *g);

/* What the original's reading leaves in the registers and its loop's cell,
 * noted before a read and left after it, for a caller that runs the read as
 * C (scaffolding: ORIGINAL_EFFECT). */
typedef struct {
    bool new_step, pushed, moved;
    uint8_t push, from;
} MenuCursorRead;
MenuCursorRead menu_note_cursor_read(const GameState *g);
void menu_leave_cursor_read(Snes *s, MenuCursorRead r);

/* The entries. */
void menu_clear_sprite_table(Snes *s);
void menu_reset_sprites(Snes *s);
void menu_clear_sprites_for_cursor(Snes *s);
void menu_read_cursor_input(Snes *s);
void menu_take_frame_input(Snes *s);
bool menu_frame_input_is_takeable(Snes *s);
void menu_send_sound(Snes *s);
void menu_send_sound_on_other_voices(Snes *s);
void menu_play_cursor_sound(Snes *s);
void menu_play_refusal_sound(Snes *s);
void menu_play_sound(Snes *s);
void menu_play_queued_sound(Snes *s);

bool menu_cursor_is_placeable(Snes *s);
bool menu_cursor_is_movable(Snes *s);
bool menu_sounds_on_page(Snes *s);

#endif
