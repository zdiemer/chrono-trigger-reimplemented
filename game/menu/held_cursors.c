/* The cursors a screen holds while the player's cursor moves on, and the
 * scroll bar beside a list.
 *
 * A screen can hold a second cursor where the player's was (the shop's on
 * its buying tab while the player chooses a member), which blinks; or put a
 * cursor on every active member for a healing of them all (a tonic's, a
 * tech's), each blinking in its own time. The menus' seventh task draws
 * them a frame at a time (its turns are the original's loops, which call a
 * frame of each here), and stopping the task takes them away. A list that
 * scrolls has a scroll bar beside it, which the fifth task draws.
 *
 * Every blink is a countdown: a held cursor's runs down the bits its blink
 * speed leaves it, and each of the members' cursors down a count of four,
 * started out of step; at the end of a countdown the cursor goes out, or
 * comes back. The cursors are sprites after the player's (the 30th on),
 * placed where the screen's cursor places say. */
#include "menu/held_cursors.h"

#include "menu/bridge.h"
#include "menu/menu.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* Where the tasks start. */
enum { DRAW_THE_SCROLL_BAR = 0x8BB8, DRAW_THE_HELD_CURSOR = 0x8AB2, DRAW_THE_MEMBERS_CURSORS = 0x8AF5 };

/* The player's cursor is the 29th sprite; the held cursors follow it. */
enum { PLAYERS_CURSOR = 28, FIRST_HELD_CURSOR = 29 };

/* The members' cursors: a blink countdown each (a count of four), started
 * out of step, and which of them are out (a bit each, the first's lowest). */
enum { MEMBERS_CURSORS = 3, FOUR_FRAMES = 0x03, FAST_BLINK = 0x00FC };
static const uint8_t members_blinks_start[MEMBERS_CURSORS] = { 3, 1, 2 };
enum { MEMBERS_CURSORS_START_OUT = 0x02 };

/* The cursor sprites the members' cursors are: their tile and attributes;
 * the screen's own cursor is hidden with the tile's number. */
enum { MEMBER_CURSOR_SPRITE = 0x3020, CURSOR_HIDDEN_BY_THE_MEMBERS = 0x20 };

/* A cursor drawn at a place of the screen's cursor, two right of it: the
 * held one a line up, as the player's own is drawn; a member's on the
 * place's own line, the original working the place out as a word (a place
 * at the screen's right edge would carry the two into the line). */
enum { RIGHT_OF_THE_PLACE = 2, RIGHT_EDGE = 0x100 - RIGHT_OF_THE_PLACE };

static void draw_held_cursor_at(GameState *g, unsigned sprite, uint8_t place)
{
    set_oam_buffer_x(g, sprite, (uint8_t)(menu_cursor_places_x(g, place) + RIGHT_OF_THE_PLACE));
    set_oam_buffer_y(g, sprite, (uint8_t)(menu_cursor_places_y(g, place) - 1));
}

static void draw_member_cursor_at(GameState *g, unsigned sprite, uint8_t place)
{
    uint8_t x = menu_cursor_places_x(g, place), y = menu_cursor_places_y(g, place);
    set_oam_buffer_x(g, sprite, (uint8_t)(x + RIGHT_OF_THE_PLACE));
    set_oam_buffer_y(g, sprite, (uint8_t)(x >= RIGHT_EDGE ? y + 1 : y));
}

/* A countdown run down a frame: the bits `keep` leaves of it, less one; at
 * its end (it wraps), the cursor whose bit `bit` is in the blinks goes out
 * or comes back. */
static void count_down(GameState *g, unsigned which, uint8_t keep, uint8_t bit)
{
    uint8_t left = (uint8_t)((menu_cursor_blinks(g, which) & keep) - 1);
    set_menu_cursor_blinks(g, which, left);
    if (left & 0x80)
        set_menu_cursors_out(g, (uint8_t)(menu_cursors_out(g) ^ bit));
}

// $C2:8A98
void menu_hold_a_cursor(GameState *g)
{
    set_menu_held_cursor_blink(g, FAST_BLINK);
    set_menu_held_cursor_sprite(g, (uint16_t)(oam_buffer_tile(g, PLAYERS_CURSOR) | oam_buffer_attributes(g, PLAYERS_CURSOR) << 8));
    menu_start_task(g, MENU_HELD_CURSORS_TASK, DRAW_THE_HELD_CURSOR);
}

// $C2:8ABA (with $C2:8B69 and $C2:8B85)
void menu_draw_the_held_cursor(GameState *g)
{
    count_down(g, 0, (uint8_t)~menu_held_cursor_blink(g), 1);
    if (menu_cursors_out(g) & 1)
        return;
    draw_held_cursor_at(g, FIRST_HELD_CURSOR, menu_held_cursor(g));
    uint16_t sprite = menu_held_cursor_sprite(g);
    set_oam_buffer_tile(g, FIRST_HELD_CURSOR, (uint8_t)sprite);
    set_oam_buffer_attributes(g, FIRST_HELD_CURSOR, (uint8_t)(sprite >> 8));
}

// $C2:8AD5
void menu_put_cursors_on_every_member(GameState *g)
{
    menu_start_task(g, MENU_HELD_CURSORS_TASK, DRAW_THE_MEMBERS_CURSORS);
    for (unsigned i = 0; i < MEMBERS_CURSORS; i++)
        set_menu_cursor_blinks(g, i, members_blinks_start[i]);
    set_menu_cursors_out(g, MEMBERS_CURSORS_START_OUT);
}

/* For the original's code and shadow mode: where its loop over the members
 * got to in the menus' first cells -- the bits of those out it had left, the
 * members left (none), the next member's place -- with its sprite's place
 * past the last. */
static void leave_members_drawn(GameState *g)
{
    uint8_t members = menu_active_members(g);
    set_menu_scratch_cursors_out_left(g, (uint16_t)(menu_cursors_out(g) >> members));
    set_menu_scratch_cursors_left(g, 0);
    set_menu_scratch_cursor_place(g, (uint16_t)(menu_held_cursor(g) + members));
}

// $C2:8AFD (with $C2:8B85)
/* The members' cursors' sprites, then each one's blink; then each member's
 * that isn't out drawn at its row, the first's from the held cursor's place
 * on. A screen's own cursor stays hidden meanwhile. */
void menu_draw_cursors_on_every_member(GameState *g)
{
    for (unsigned i = 0; i < MEMBERS_CURSORS; i++) {
        set_oam_buffer_tile(g, FIRST_HELD_CURSOR + i, (uint8_t)MEMBER_CURSOR_SPRITE);
        set_oam_buffer_attributes(g, FIRST_HELD_CURSOR + i, (uint8_t)(MEMBER_CURSOR_SPRITE >> 8));
    }
    set_menu_cursor_hidden(g, CURSOR_HIDDEN_BY_THE_MEMBERS);
    for (unsigned i = 0; i < MEMBERS_CURSORS; i++)
        count_down(g, i, FOUR_FRAMES, (uint8_t)(1u << i));
    uint8_t out = menu_cursors_out(g);
    for (unsigned member = 0; member < menu_active_members(g); member++)
        if (!(out >> member & 1))
            draw_member_cursor_at(g, FIRST_HELD_CURSOR + member, (uint8_t)(menu_held_cursor(g) + member));
    ORIGINAL_EFFECT(leave_members_drawn(g));
}

// $C2:8B93
void menu_start_the_scroll_bar(GameState *g)
{
    set_menu_scroll_bar_hidden(g, 0);
    menu_start_task(g, MENU_SCROLL_BAR_TASK, DRAW_THE_SCROLL_BAR);
}

/* ---- The entries ------------------------------------------------------------------------------- */

// $C2:8A98
void menu_hold_a_cursor_entry(Snes *s)
{
    menu_hold_a_cursor(state_of(s));
    m_rts(s);
}

// $C2:8ABA
void menu_draw_the_held_cursor_entry(Snes *s)
{
    menu_draw_the_held_cursor(state_of(s));
    m_rts(s);
}

// $C2:8AD5
void menu_put_cursors_on_every_member_entry(Snes *s)
{
    menu_put_cursors_on_every_member(state_of(s));
    m_rts(s);
}

// $C2:8AFD
void menu_draw_cursors_on_every_member_entry(Snes *s)
{
    menu_draw_cursors_on_every_member(state_of(s));
    m_rts(s);
}

// $C2:8B93
void menu_start_the_scroll_bar_entry(Snes *s)
{
    menu_start_the_scroll_bar(state_of(s));
    m_rts(s);
}

/* The cursors are drawn on the menus' page with the data bank on work
 * RAM's, at places of the cursor's table (its 42); the members' for the
 * one to three active ones (with none, the original's loop runs round its
 * count's 65536). */
bool menu_held_cursor_is_drawable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_held_cursor(state_of(s)) < MENU_CURSOR_PLACES_COUNT;
    vars_peek(false);
    return ok;
}

bool menu_members_cursors_are_drawable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = menu_active_members(g) >= 1 && menu_active_members(g) <= MEMBERS_CURSORS
              && menu_held_cursor(g) + menu_active_members(g) <= MENU_CURSOR_PLACES_COUNT;
    vars_peek(false);
    return ok;
}
