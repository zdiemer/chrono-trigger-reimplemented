/* The cursors a screen holds while the player's cursor moves on, and the
 * scroll bar beside a list.
 *
 * A screen can hold a second cursor where the player's was (the shop's on
 * its buying tab while the player chooses a member), which blinks; hold one
 * on what the player picked up while the player chooses where it goes -- an
 * item of the items screen's list, a member of the party exchange's rows --
 * keeping it on its row as the list or the reserve scrolls and slides; or
 * put a cursor on every active member for a healing of them all (a
 * tonic's, a tech's), each blinking in its own time. The menus' seventh task draws
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

#include "cpu_io.h"
#include "menu/bridge.h"
#include "menu/menu.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* Where the tasks start. */
enum {
    DRAW_THE_SCROLL_BAR = 0x8BB8, DRAW_THE_HELD_CURSOR = 0x8AB2, DRAW_THE_MEMBERS_CURSORS = 0x8AF5,
    DRAW_THE_CURSOR_ON_THE_LIST = 0x89F2, DRAW_THE_CURSOR_ON_A_MEMBER = 0x8A45,
};

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

/* The player's cursor's sprite, its tile and attributes, which a held
 * cursor is drawn with. */
static uint16_t players_cursor_sprite(const GameState *g)
{
    return (uint16_t)(oam_buffer_tile(g, PLAYERS_CURSOR) | oam_buffer_attributes(g, PLAYERS_CURSOR) << 8);
}

static void put_sprite(GameState *g, unsigned sprite, uint16_t tile_and_attributes)
{
    set_oam_buffer_tile(g, sprite, (uint8_t)tile_and_attributes);
    set_oam_buffer_attributes(g, sprite, (uint8_t)(tile_and_attributes >> 8));
}

// $C2:8A98
void menu_hold_a_cursor(GameState *g)
{
    set_menu_held_cursor_blink(g, FAST_BLINK);
    set_menu_held_cursor_sprite(g, players_cursor_sprite(g));
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

/* ---- A cursor held on what was picked up ------------------------------------------------------- */

/* Both blink every four frames, turning the held cursor's bit over; the
 * list's shows while the bit is set, the member's while it is clear. */
static bool blink_bit(GameState *g)
{
    count_down(g, 0, FOUR_FRAMES, 1);
    return (menu_cursors_out(g) & 1) != 0;
}

/* The items screen's list shows ten rows from its first place, cursor
 * places 0-9; its held cursor is a sprite of its own. */
enum { LIST_ROWS = 10, LIST_CURSOR_SPRITE = 0x2020 };

// $C2:89E4
/* Held on the picked item's row, with no slide under way. */
void menu_hold_the_cursor_on_the_list(GameState *g)
{
    set_menu_held_cursor_shift(g, 0);
    menu_start_task(g, MENU_HELD_CURSORS_TASK, DRAW_THE_CURSOR_ON_THE_LIST);
}

// $C2:89FA (with $C2:8B6E)
/* At the picked item's row as the list stands now, the list's first place
 * having moved since it was picked up, and the lines a slide has it off
 * that row; only while the row is among those the list shows. */
void menu_draw_the_cursor_held_on_the_list(GameState *g)
{
    if (!blink_bit(g))
        return;
    uint8_t place = (uint8_t)(menu_held_cursor_top(g) - menu_item_list_top(g) + menu_held_cursor(g));
    if (place >= LIST_ROWS)
        return;
    set_oam_buffer_x(g, FIRST_HELD_CURSOR, (uint8_t)(menu_cursor_places_x(g, place) + RIGHT_OF_THE_PLACE));
    set_oam_buffer_y(g, FIRST_HELD_CURSOR, (uint8_t)(menu_cursor_places_y(g, place) - 1 + menu_held_cursor_shift(g)));
    put_sprite(g, FIRST_HELD_CURSOR, LIST_CURSOR_SPRITE);
}

/* The party exchange's rows: the three active members' at their places,
 * then the reserve's, 48 lines a member, scrolling. A reserve member's row
 * scrolled further than 16 lines above the screen's top, or past its
 * bottom, has no cursor. */
enum { ACTIVE_PLACES = 3, RESERVE_ROW_LINES = 48, SHOWN_ABOVE_THE_TOP = 16, LINES = 0x100 };

/* For the original's code and shadow mode: it works the lines out with the
 * multiplier, which it leaves holding them. */
static void leave_rows_multiplied(GameState *g, uint8_t rows)
{
    hw_set_multiplicand(g, RESERVE_ROW_LINES);
    (void)hw_multiply_by(g, rows);
}

// $C2:8A31
/* Held on the member picked first, looking like the player's cursor. */
void menu_hold_the_cursor_on_a_member(GameState *g)
{
    set_menu_held_cursor_sprite(g, players_cursor_sprite(g));
    menu_start_task(g, MENU_HELD_CURSORS_TASK, DRAW_THE_CURSOR_ON_A_MEMBER);
}

// $C2:8A45 (with $C2:8B6E and $C2:8B85)
/* At the picked member's row: an active member's where it is; a reserve
 * member's moved by the rows the reserve has scrolled since it was picked
 * and by the lines a slide has it off that row. The cursor's column is
 * set even where its row is out of view. */
void menu_draw_the_cursor_held_on_a_member(GameState *g)
{
    if (blink_bit(g))
        return;
    uint8_t place = menu_held_cursor(g);
    set_oam_buffer_x(g, FIRST_HELD_CURSOR, (uint8_t)(menu_cursor_places_x(g, place) + RIGHT_OF_THE_PLACE));
    uint8_t y = (uint8_t)(menu_cursor_places_y(g, place) - 1);
    if (place >= ACTIVE_PLACES) {
        int scrolled = (int8_t)(menu_held_reserve_top(g) - menu_reserve_top(g));
        ORIGINAL_EFFECT(leave_rows_multiplied(g, (uint8_t)scrolled));
        int line = y + scrolled * RESERVE_ROW_LINES;
        if (line >= LINES || line < -SHOWN_ABOVE_THE_TOP)
            return;
        y = (uint8_t)(line + menu_held_cursor_shift(g));
    }
    set_oam_buffer_y(g, FIRST_HELD_CURSOR, y);
    put_sprite(g, FIRST_HELD_CURSOR, menu_held_cursor_sprite(g));
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

// $C2:89E4
/* It leaves 16-bit registers. */
void menu_hold_the_cursor_on_the_list_entry(Snes *s)
{
    menu_hold_the_cursor_on_the_list(state_of(s));
    menu_set_widths(s, false, false);
    m_rts(s);
}

// $C2:89FA
void menu_draw_the_cursor_held_on_the_list_entry(Snes *s)
{
    menu_draw_the_cursor_held_on_the_list(state_of(s));
    m_rts(s);
}

// $C2:8A31
void menu_hold_the_cursor_on_a_member_entry(Snes *s)
{
    menu_hold_the_cursor_on_a_member(state_of(s));
    m_rts(s);
}

/* The member's cursor's task draws a frame and waits for the next through
 * the task switch, which reads no register, and then draws again. */
enum { WAIT_FOR_THE_NEXT_FRAME = 0xC28A91, READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

// $C2:8A45
void menu_draw_the_cursor_held_on_a_member_entry(Snes *s)
{
    menu_draw_the_cursor_held_on_a_member(state_of(s));
    menu_set_widths(s, true, m_x8(s));
    m_stop_at(s, WAIT_FOR_THE_NEXT_FRAME, READS_NOTHING);
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

/* The list's cursor is drawn at one of the list's places, whatever the
 * sums give; the member's at a place of the cursor's table, a reserve one
 * as the reserve's six places scroll. */
bool menu_list_cursor_is_drawable(Snes *s) { return menu_on_page_work_ram(s); }

enum { RESERVE_PLACES = 6 };

bool menu_member_cursor_is_drawable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = menu_held_cursor(g) < MENU_CURSOR_PLACES_COUNT && menu_held_reserve_top(g) < RESERVE_PLACES
              && menu_reserve_top(g) < RESERVE_PLACES;
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
