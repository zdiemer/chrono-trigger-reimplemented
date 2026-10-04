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

/* What the frame's input asks for, as the cursor's reading leaves it
 * (menu_cursor_buttons): choosing, going back, and the two shoulder
 * assignments, which page a list (or switch the equipment screen's member);
 * and the ways the direction pad pushed the cursor (menu_cursor_moves), with
 * the bit for a push past the menu's places that way (a list's edge). */
typedef enum { MENU_CHOOSE = 0x80, MENU_GO_BACK = 0x40, MENU_PAGE_ONE_WAY = 0x02, MENU_PAGE_THE_OTHER = 0x01 } MenuButton;
typedef enum {
    MENU_PUSHED_RIGHT = 0x01, MENU_PUSHED_LEFT = 0x02, MENU_PUSHED_DOWN = 0x04, MENU_PUSHED_UP = 0x08, MENU_PUSHED_PAST_AN_EDGE = 0x80,
} MenuPush;
bool menu_asked(const GameState *g, MenuButton b);
bool menu_pushed(const GameState *g, MenuPush way);

/* Whether the cursor is on one of `count` places from `first`, and every
 * neighbour a push could take it to is one too (a push that leaves the menu
 * keeps it where it is); the left one, where `left_free`, any. For a
 * screen's guard. */
bool menu_cursor_kept_among(const GameState *g, uint8_t first, uint8_t count, bool left_free);

/* Every sprite out of sight; the menus' own hidden and the cursor placed, as
 * every frame; or every sprite hidden and the cursor placed facing its own
 * way. */
void menu_clear_sprites(GameState *g);
void menu_reset_sprites_and_cursor(GameState *g);
void menu_clear_sprites_but_cursor(GameState *g);

/* The frame's input through the cursor: the direction pad's push moves it,
 * and the buttons the player assigned come out as menu_cursor_buttons. */
void menu_read_cursor(GameState *g);

/* No place of a menu: noted as where the cursor was as the frame began
 * (menu_cursor_frame_start), the screen counts the cursor moved and ticks. */
enum { MENU_CURSOR_MOVED_ANYWAY = 0xFF };

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
