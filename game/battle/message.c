/* Battle: the battle message.
 *
 * The message line at the top of the battle screen shows four kinds of
 * message, each a string set into the message's tiles through the menus' text
 * code (menu/typeset.c); the vblank handler sends the tiles to VRAM, a half
 * each frame, while the message shows (battle/uploads.c):
 *
 *   - the battle's own messages (the party ran, a member is back on its
 *     feet), set whole with a sound, which the battle holds for: they stay up
 *     for the frames the player's message speed gives, or until A is pressed;
 *   - an enemy's message, set whole, which the vblank handler times at the
 *     player's speed and closes (battle/nmi.c) while the fight goes on, and
 *     which keeps the others away while it is up;
 *   - the window naming the combatant the cursor is on, with its HP or
 *     without, and a tech's or an item's description: these are typed a letter
 *     a frame as the frame work waits for each frame (battle/wait.c), and
 *     once all set, stay up for as long as the player's message speed says,
 *     which the vblank handler counts down before it closes them.
 *
 * The same message isn't opened twice running. The frame work's waits
 * (battle/wait.c) are made where the original makes them. */
#include "battle/message.h"

#include "battle/bridge.h"
#include "battle/frame_data.h"
#include "battle/frame.h"
#include "battle/program.h"
#include "battle/sound.h"
#include "battle/wait.h"
#include "charge.h"
#include "menu/typeset.h"
#include "platform/audio.h"
#include "platform/input.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

/* The menus' text code sets at most this many letters a call. */
enum { LETTERS_A_FRAME = 1 };

/* The player's battle message speed, 0 the fastest of eight (the settings'
 * second byte, bits 3-5). */
static uint8_t message_speed(const GameState *g) { return (uint8_t)((game_settings(g, 1) >> 3) & 0x07); }

// $CD:04DF
/* The next letter of a message being set. However far it has got, the message
 * is timed from the letter just set: it stays up for the frames its speed
 * gives once the setting stops. */
void battle_type_the_message(GameState *g)
{
    if (!battle_message_drawing(g))
        return;
    set_menu_text_budget(g, LETTERS_A_FRAME);
    TIMED_AS(g, 0xC25823, menu_text_go_on(g));
    set_battle_message_drawing(g, menu_text_stop(g));
    set_battle_message_frames(g, battle_message_frames_at_speed(g, message_speed(g)));
    set_battle_message_timed(g, (uint8_t)(battle_message_timed(g) + 1));
}

// $CD:0239
/* Clear the message's tiles, both halves, for a message about to be set. */
enum { MESSAGE_HALVES = 2, MESSAGE_HALF_BYTES = 0x200 };

void battle_clear_the_message_tiles(GameState *g)
{
    for (unsigned half = 0; half < MESSAGE_HALVES; half++)
        for (unsigned at = 0; at < MESSAGE_HALF_BYTES; at++)
            set_battle_message_tiles_byte(g, half, at, 0);
}

/* ---- Opening a message ------------------------------------------------------------------------- */

/* The tables of strings the messages come from: the battle's own messages (an
 * escape, a member back on its feet, the windows that name a combatant), an
 * enemy's, and the descriptions of techs and items. */
enum { BATTLE_MESSAGES = 0xCEEF11, ENEMY_MESSAGES = 0xCCCBC9, DESCRIPTIONS = 0xCC3A09 };
/* A message set all at once is given this many letters. */
enum { WHOLE_MESSAGE = 0x40 };
/* While a message shows, the vblank handler sends its tiles in place of
 * clearing that VRAM. */
enum { MESSAGE_SHOWN = 0x10, MESSAGE_HIDDEN = 0x00 };

// $CD:025E
/* Open a message: its string of the table already named, its tiles cleared,
 * and the setting started (the original leaves the field's message mark
 * clear, as the field does when it opens one). */
static void open_the_message(GameState *g, uint8_t string)
{
    set_menu_text_string(g, string);
    set_battle_message_timed(g, 0);
    set_field_message_mark(g, 0);
    set_menu_text_tiles(g, RAM_BATTLE_MESSAGE_TILES);
    set_menu_text_layout(g, MENU_TEXT_FOUR_COLOUR);
    TIMED_AS(g, 0xCD0239, battle_clear_the_message_tiles(g));
    TIMED_AS(g, 0xC257DF, menu_text_start(g));
}

/* Have the vblank handler show the message, and lay the lines out again. */
static void show_the_message(GameState *g, bool shown)
{
    set_battle_vram_copy_mode(g, shown ? MESSAGE_SHOWN : MESSAGE_HIDDEN);
    battle_lay_the_lines_out_again(g);
}

/* A message the battle held for, closed. */
static void close_the_message(GameState *g)
{
    show_the_message(g, false);
    set_battle_message_timed(g, 0);
}

/* The frames a message stays up for, at the player's speed. */
static uint8_t frames_up(const GameState *g) { return battle_message_frames_at_speed(g, message_speed(g)); }

// $CD:027F
/* A message's whole text set out, over two of the handler's frames
 * (battle/wait.c). */
static bool set_out_the_message(GameState *g, Platform *pf)
{
    set_menu_text_budget(g, WHOLE_MESSAGE);
    TIMED_AS(g, 0xC25823, menu_text_go_on(g));
    if (!battle_wait_for_the_handler(g, pf) || !battle_wait_for_the_handler(g, pf))
        return false;
    show_the_message(g, true);
    return true;
}

static bool set_the_message_out(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, 0xCD027F, done = set_out_the_message(g, pf));
    return done;
}

// $CD:020A
/* A pause of 16 frames around a message the battle holds for. */
enum { MESSAGE_PAUSE = 0x10 };

static bool pause_on_the_message(GameState *g, Platform *pf)
{
    for (uint8_t left = MESSAGE_PAUSE; left != 0; left--)
        if (!battle_wait_for_the_next_frame(g, pf))
            return false;
    return true;
}

static bool pause(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, 0xCD020A, done = pause_on_the_message(g, pf));
    return done;
}

/* ---- The battle's own messages, which the battle holds for ---------------------------------------- */

/* The escape's message stays a second, whatever the speed; the others for
 * the frames the speed gives, or until A is pressed. */
enum { RAN_AWAY = 0x08, RAN_AWAY_FRAMES = 0x3C };

static bool hold_the_message(GameState *g, Platform *pf, uint8_t message)
{
    uint8_t frames = frames_up(g);
    if (message == RAN_AWAY)
        return battle_wait_some_frames(g, pf, RAN_AWAY_FRAMES);
    uint8_t left = frames;
    do {
        if (!battle_wait_for_the_next_frame(g, pf))
            return false;
        if (battle_joypad(g) & IN_BUTTON_A)
            break;
    } while (--left != 0);
    return true;
}

// $CD:01B3 (through $CD:0021)
/* Show one of the battle's own messages and hold the battle while it is up:
 * its sound, its whole text at once, a pause, the frames the player's speed
 * gives, and a pause after it closes. */
static bool show_a_battle_message(GameState *g, Platform *pf, uint8_t message)
{
    set_menu_text_strings(g, BATTLE_MESSAGES);
    uint8_t sound = battle_message_sound(g, message);
    if (sound != BATTLE_MESSAGE_SILENT)
        TIMED_AS(g, BATTLE_PLAY_A_SOUND_ENTRY, battle_play_a_sound(pf, sound, AU_CENTRE));
    TIMED_AS(g, 0xCD025E, open_the_message(g, message));
    if (!set_the_message_out(g, pf) || !pause(g, pf) || !hold_the_message(g, pf, message))
        return false;
    close_the_message(g);
    return pause(g, pf);
}

/* ---- An enemy's message ------------------------------------------------------------------------- */

/* The frames between setting an enemy's message and timing it. */
enum { ENEMY_MESSAGE_PAUSE = 8 };

// $CD:0295 (through $CD:0033)
/* Show an enemy's message, unless it is the one up already: its whole text
 * set out, a pause, then the vblank handler times it at the player's speed
 * and closes it, and a frame (each a routine of its own). While it is up,
 * the messages naming combatants and the descriptions wait. */
static bool show_an_enemy_message(GameState *g, Platform *pf, uint8_t message)
{
    set_battle_message_by_id(g, (uint8_t)(battle_message_by_id(g) + 1));
    set_menu_text_strings(g, ENEMY_MESSAGES);
    uint8_t up = battle_message_shown(g);
    if (message == up)
        return true;
    TIMED_AS(g, 0xCD025E, open_the_message(g, message));
    if (!set_the_message_out(g, pf) || !battle_wait_some_frames(g, pf, ENEMY_MESSAGE_PAUSE))
        return false;
    set_battle_message_frames(g, frames_up(g));
    set_battle_message_shown(g, message);
    set_battle_message_timed(g, (uint8_t)(battle_message_timed(g) + 1));
    return battle_wait_for_the_next_frame(g, pf);
}

/* ---- The messages typed a letter a frame ----------------------------------------------------------- */

/* A message typed a letter a frame by the frame wait (battle_type_the_message):
 * opened, shown, and left to the typing. The same message isn't opened twice
 * running, and none is for NO_MESSAGE. */
enum { NO_MESSAGE = 0xFF, NAME_WITH_HP = 0x0B, NAME = 0x0C };

static bool message_already_asked(GameState *g, uint8_t message)
{
    if (message == battle_message_asked(g))
        return true;
    set_battle_message_asked(g, message);
    return message == NO_MESSAGE;
}

static void type_out(GameState *g, uint32_t strings, uint8_t string)
{
    set_menu_text_strings(g, strings);
    TIMED_AS(g, 0xCD025E, open_the_message(g, string));
    show_the_message(g, true);
    set_battle_message_drawing(g, 1);
}

static bool enemy_message_up(const GameState *g) { return battle_message_by_id(g) || battle_message_by_id_high(g); }

// $CD:02CB (through $CD:0030), $CD:02D8 (through $CD:002D)
/* The window naming a combatant (`combatant`, as the string's codes read
 * it), with its HP or without. True if it was opened. */
static bool name_a_combatant(GameState *g, uint8_t combatant, uint8_t window)
{
    if (enemy_message_up(g))
        return false;
    if (message_already_asked(g, combatant))
        return false;
    type_out(g, BATTLE_MESSAGES, window);
    return true;
}

// $CD:0310 (through $CD:0027)
/* A tech's or an item's description, or a word from the menus (that the
 * party can't escape). True if it was opened. */
static bool describe(GameState *g, uint8_t description)
{
    if (enemy_message_up(g) || message_already_asked(g, description))
        return false;
    type_out(g, DESCRIPTIONS, description);
    return true;
}

/* ---- What the menus' text code takes ----------------------------------------------------------- */

/* The menus' text code takes a message being set up only where it can go on
 * from (menu/typeset.c). */
bool battle_message_typing_fits(const GameState *g) { return !battle_message_drawing(g) || menu_text_can_carry_on(g); }

/* ---- The messages shown ------------------------------------------------------------------------ */

/* The windows naming a combatant ($CD:0030, $CD:002D) and the descriptions
 * ($CD:0027), each refused unless its string is one the menus' text code
 * can type into the message's tiles. False where the program went no
 * further. */
static bool can_be_typed(const GameState *g, uint32_t strings, uint8_t string)
{
    return menu_text_can_be_typed(g, strings, string, RAM_BATTLE_MESSAGE_TILES);
}

bool battle_combatant_named(GameState *g, uint8_t combatant, bool with_hp)
{
    uint32_t routine = with_hp ? 0xCD002D : 0xCD0030;
    if (!can_be_typed(g, BATTLE_MESSAGES, NAME) || !can_be_typed(g, BATTLE_MESSAGES, NAME_WITH_HP))
        return battle_contract_broken(g, routine, "a combatant's name the menus' text code doesn't type");
    TIMED_AS(g, routine, (void)name_a_combatant(g, combatant, with_hp ? NAME_WITH_HP : NAME));
    return true;
}

bool battle_described(GameState *g, uint8_t description)
{
    if (!can_be_typed(g, DESCRIPTIONS, description))
        return battle_contract_broken(g, 0xCD0027, "a description the menus' text code doesn't type");
    TIMED_AS(g, 0xCD0027, (void)describe(g, description));
    return true;
}

/* Refused unless the menus' text code types the string. */
bool battle_show_an_enemy_message(GameState *g, Platform *pf, uint8_t message)
{
    if (!can_be_typed(g, ENEMY_MESSAGES, message))
        return battle_contract_broken(g, 0xCD0033, "an enemy's message the menus' text code doesn't type");
    return show_an_enemy_message(g, pf, message);
}

/* Refused unless the menus' text code types the string. */
bool battle_show_a_battle_message(GameState *g, Platform *pf, uint8_t message)
{
    if (!can_be_typed(g, BATTLE_MESSAGES, message))
        return battle_contract_broken(g, 0xCD0021, "a battle message the menus' text code doesn't type");
    return show_a_battle_message(g, pf, message);
}
