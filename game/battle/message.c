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
 * The same message isn't opened twice running. The logic is C on the game's
 * state; what reaches the machine is the bridge that makes the frame work's
 * waits from the original's sites, with what it keeps on the stack there. */
#include "battle/message.h"

#include "battle/bridge.h"
#include "battle/frame_data.h"
#include "battle/sound.h"
#include "charge.h"
#include "menu/typeset.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "platform/input.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The menus' text code sets at most this many letters a call. */
enum { LETTERS_A_FRAME = 1 };

/* The player's battle message speed, 0 the fastest of eight (the settings'
 * second byte, bits 3-5). */
static uint8_t message_speed(const GameState *g) { return (uint8_t)((game_settings(g, 1) >> 3) & 0x07); }

/* The bit of the settings below the speed, which the original's shift of the
 * speed moves into the carry. */
enum { SPEED_SHIFTED_OUT = 0x04 };
static bool speed_shifted_out(const GameState *g) { return game_settings(g, 1) & SPEED_SHIFTED_OUT; }

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
/* The message's tiles, in work RAM, laid out as the menus' four-colour text. */
enum { MESSAGE_TILES = 0x7EB400 };
/* A message set all at once is given this many letters. */
enum { WHOLE_MESSAGE = 0x40 };
/* While a message shows, the vblank handler sends its tiles in place of
 * clearing that VRAM. */
enum { MESSAGE_SHOWN = 0x10, MESSAGE_HIDDEN = 0x00 };

// $CD:025E
/* Open a message: its string of the table already named, its tiles cleared,
 * and the setting started (the original leaves the field's message mark clear, as the field
 * does when it opens one). */
static void open_the_message(GameState *g, uint8_t string)
{
    set_menu_text_string(g, string);
    set_battle_message_timed(g, 0);
    set_field_message_mark(g, 0);
    set_menu_text_tiles(g, MESSAGE_TILES);
    set_menu_text_layout(g, MENU_TEXT_FOUR_COLOUR);
    TIMED_AS(g, 0xCD0239, battle_clear_the_message_tiles(g));
    TIMED_AS(g, 0xC257DF, menu_text_start(g));
}

/* Have the vblank handler show the message, and lay the lines out again. */
static void show_the_message(GameState *g, bool shown)
{
    set_battle_vram_copy_mode(g, shown ? MESSAGE_SHOWN : MESSAGE_HIDDEN);
    set_battle_alternate_apply(g, (uint8_t)(battle_alternate_apply(g) + 1));
}

/* The frames a message stays up for, at the player's speed. */
static uint8_t frames_up(const GameState *g) { return battle_message_frames_at_speed(g, message_speed(g)); }

/* The battle's waits pass the carry on to their callers, which read it, so
 * the bridge leaves it where the original's tests do: looking the speed up
 * shifts a bit of the settings into it, and a compare of the message leaves
 * whether it was at least the one compared with. */
static void leave_the_carry(Snes *s, bool carry) { m_set_flag(s, CPU_FLAG_C, carry); }

/* The frame work's waits for its handler ($CD:3E7D), and for the next frame
 * ($CD:3E75) and some frames ($CD:3E3B), which carry A's high byte on to the
 * battle's pause. */
enum { WAIT_READS = DC_AL | DC_X | DC_Y | DC_FLAGS, COUNTING_WAIT_READS = DC_X | DC_Y | DC_FLAGS };

static bool wait_for_the_handler(Snes *s, uint32_t site) { return battle_wait_frame(s, site, 0xCD3E7D, WAIT_READS); }

/* A wait for the next frame the original makes with a count in A, which it
 * keeps on the stack across the wait. */
static bool wait_a_frame_counting(Snes *s, uint32_t site, uint8_t left)
{
    m_set_a8(s, left);
    m_push8(s, left);
    if (!battle_wait_frame(s, site, 0xCD3E75, COUNTING_WAIT_READS))
        return false;
    m_pull8(s);
    return true;
}

// $CD:027F
/* Set the message's text, as much of it as `letters` allows, and show it once
 * two frames have passed. */
static bool set_out_the_message(Snes *s, uint8_t letters)
{
    GameState *g = state_of(s);
    set_menu_text_budget(g, letters);
    TIMED_AS(g, 0xC25823, menu_text_go_on(g));
    if (!wait_for_the_handler(s, 0xCD0286) || !wait_for_the_handler(s, 0xCD0289))
        return false;
    show_the_message(g, true);
    return true;
}

// $CD:020A
/* A pause of 16 frames around a message the battle holds for. */
enum { MESSAGE_PAUSE = 0x10 };

static bool pause_on_the_message(Snes *s)
{
    for (uint8_t left = MESSAGE_PAUSE; left != 0; left--)
        if (!wait_a_frame_counting(s, 0xCD020D, left))
            return false;
    return true;
}

/* ---- The battle's own messages, which the battle holds for ---------------------------------------- */

/* The escape's message stays a second, whatever the speed; the others for
 * the frames the speed gives, or until A is pressed. */
enum { RAN_AWAY = 0x08, RAN_AWAY_FRAMES = 0x3C };
/* The original keeps the frames in a work cell of the battle's ($53). */
enum { LEFT_FRAMES = 0x53 - 0x45 };

static bool hold_the_message(Snes *s, uint8_t message)
{
    GameState *g = state_of(s);
    uint8_t left = frames_up(g);
    ORIGINAL_EFFECT(set_battle_scratch(g, LEFT_FRAMES, left));
    if (message == RAN_AWAY)
        return battle_wait_frames(s, 0xCD01E7, 0xCD3E3B, COUNTING_WAIT_READS, RAN_AWAY_FRAMES);
    do {
        if (!wait_a_frame_counting(s, 0xCD01EF, left))
            return false;
        if (battle_joypad(g) & IN_BUTTON_A)
            break;
    } while (--left != 0);
    return true;
}

// $CD:01B3 (through $CD:0021)
/* Show one of the battle's own messages and hold the battle while it is up:
 * its sound, its whole text at once, a pause, the frames the player's speed
 * gives, and a pause after it closes. The original keeps the message on the
 * stack, a copy for each use of it. */
static bool show_a_battle_message(Snes *s, uint8_t message)
{
    GameState *g = state_of(s);
    for (unsigned copy = 0; copy < 3; copy++)
        m_push8(s, message);
    set_menu_text_strings(g, BATTLE_MESSAGES);
    m_pull8(s);
    uint8_t sound = battle_message_sound(g, message);
    leave_the_carry(s, sound >= BATTLE_MESSAGE_SILENT);
    if (sound != BATTLE_MESSAGE_SILENT) {
        m_set_a8(s, sound);
        s->cpu.x = AU_CENTRE;
        if (!battle_run(s, 0xCD01CE, 0xCD3B71, battle_play_a_sound))
            return false;
    }
    m_pull8(s);
    TIMED_AS(g, 0xCD025E, open_the_message(g, message));
    m_set_a8(s, WHOLE_MESSAGE);
    if (!battle_run(s, 0xCD01D5, 0xCD027F, battle_set_out_the_message)
        || !battle_run(s, 0xCD01D8, 0xCD020A, battle_pause_on_the_message))
        return false;
    m_pull8(s);
    leave_the_carry(s, message >= RAN_AWAY);
    if (!hold_the_message(s, message))
        return false;
    show_the_message(g, false);
    set_battle_message_timed(g, 0);
    return battle_run(s, 0xCD0206, 0xCD020A, battle_pause_on_the_message);
}

/* ---- An enemy's message ------------------------------------------------------------------------- */

/* The frames between setting an enemy's message and timing it. */
enum { ENEMY_MESSAGE_PAUSE = 8 };

// $CD:0295 (through $CD:0033)
/* Show an enemy's message, unless it is the one up already: its whole text,
 * then the vblank handler times it at the player's speed and closes it. While
 * it is up, the messages naming combatants and the descriptions wait. */
static bool show_an_enemy_message(Snes *s, uint8_t message)
{
    GameState *g = state_of(s);
    set_battle_message_by_id(g, (uint8_t)(battle_message_by_id(g) + 1));
    set_menu_text_strings(g, ENEMY_MESSAGES);
    leave_the_carry(s, message >= battle_message_shown(g));
    if (message == battle_message_shown(g))
        return true;
    set_battle_message_opening(g, message);
    TIMED_AS(g, 0xCD025E, open_the_message(g, message));
    m_set_a8(s, WHOLE_MESSAGE);
    if (!battle_run(s, 0xCD02B0, 0xCD027F, battle_set_out_the_message)
        || !battle_wait_frames(s, 0xCD02B5, 0xCD3E3B, COUNTING_WAIT_READS, ENEMY_MESSAGE_PAUSE))
        return false;
    set_battle_message_frames(g, frames_up(g));
    set_battle_message_shown(g, battle_message_opening(g));
    set_battle_message_timed(g, (uint8_t)(battle_message_timed(g) + 1));
    leave_the_carry(s, speed_shifted_out(g));
    return battle_wait_frame(s, 0xCD02C7, 0xCD3E75, WAIT_READS);
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
    ORIGINAL_EFFECT(set_battle_message_window_kind(g, window));
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

/* ---- The entries ------------------------------------------------------------------------------- */

/* A letter typed leaves the frames in A, its high byte clear as the menus'
 * text code leaves it, the speed in X, and in the carry the bit of the
 * settings the speed's shift moved out, which the callers' code after the
 * wait reads. */
static void leave_what_typing_leaves(Snes *s, const GameState *g)
{
    s->cpu.a = battle_message_frames(g);
    s->cpu.x = message_speed(g);
    m_set_flag(s, CPU_FLAG_C, speed_shifted_out(g));
}

void battle_type_the_message_entry(Snes *s)
{
    GameState *g = state_of(s);
    bool typing = battle_message_drawing(g) != 0;
    battle_type_the_message(g);
    if (typing)
        leave_what_typing_leaves(s, g);
    m_rts(s);
}

/* The clear leaves A and X 0, and the flags as a count run out to 0 leaves
 * them, which the battle's setup keeps on the stack. */
static void leave_the_count_run_out(Snes *s)
{
    s->cpu.a = 0;
    s->cpu.x = 0;
    m_set_nz8(s, 0);
}

void battle_clear_the_message_tiles_entry(Snes *s)
{
    battle_clear_the_message_tiles(state_of(s));
    leave_the_count_run_out(s);
    m_rts(s);
}

// $CD:0235
void battle_clear_the_message_tiles_long(Snes *s)
{
    battle_clear_the_message_tiles(state_of(s));
    leave_the_count_run_out(s);
    m_rtl(s);
}

/* ---- The messages' entries ------------------------------------------------------------------- */

/* The message comes in A's low byte. The battle's own messages and an
 * enemy's leave A as the original's last count leaves it, clear; the others
 * leave it 1, the typing asked for, or the message where it wasn't opened. */
static uint8_t message_in_a(const Snes *s) { return (uint8_t)s->cpu.a; }

void battle_show_a_battle_message(Snes *s)
{
    if (!show_a_battle_message(s, message_in_a(s)))
        return;
    m_rtl(s);
}

void battle_show_an_enemy_message(Snes *s)
{
    if (!show_an_enemy_message(s, message_in_a(s)))
        return;
    m_rtl(s);
}

/* A message typed a letter a frame leaves 1 in A where it was opened, and the
 * message where it wasn't; and the carry as the compares with the message
 * asked before and with NO_MESSAGE leave it, where they were made (not while
 * an enemy's message is up). */
static void leave_the_asking(Snes *s, uint8_t message, bool enemy_up, bool opened)
{
    m_set_a8(s, opened ? 1 : message);
    if (!enemy_up)
        leave_the_carry(s, !opened);
    m_rtl(s);
}

void battle_name_a_combatant(Snes *s)
{
    uint8_t combatant = message_in_a(s);
    bool enemy_up = enemy_message_up(state_of(s));
    leave_the_asking(s, combatant, enemy_up, name_a_combatant(state_of(s), combatant, NAME));
}

void battle_name_a_combatant_with_hp(Snes *s)
{
    uint8_t combatant = message_in_a(s);
    bool enemy_up = enemy_message_up(state_of(s));
    leave_the_asking(s, combatant, enemy_up, name_a_combatant(state_of(s), combatant, NAME_WITH_HP));
}

void battle_describe(Snes *s)
{
    uint8_t description = message_in_a(s);
    bool enemy_up = enemy_message_up(state_of(s));
    leave_the_asking(s, description, enemy_up, describe(state_of(s), description));
}

/* The letters to set come in A. */
void battle_set_out_the_message(Snes *s)
{
    if (!set_out_the_message(s, message_in_a(s)))
        return;
    m_rts(s);
}

/* The pause's count runs out in A. */
void battle_pause_on_the_message(Snes *s)
{
    if (!pause_on_the_message(s))
        return;
    m_set_a8(s, 0);
    m_set_nz8(s, 0);
    m_rts(s);
}

/* The string comes in A; the opening leaves the letters to set next in A. */
void battle_open_the_message(Snes *s)
{
    open_the_message(state_of(s), message_in_a(s));
    m_set_a8(s, WHOLE_MESSAGE);
    m_rts(s);
}

/* The messages' guards: on the battle's page, with the string one the menus'
 * text code can type into the message's tiles (and A's high byte clear for
 * the battle's own, whose sound the original looks up with all of A). */
static bool message_can_open(Snes *s, uint32_t strings, uint8_t string)
{
    if (!battle_on_page(s))
        return false;
    vars_peek(true);
    bool ok = menu_text_can_be_typed(state_of(s), strings, string, MESSAGE_TILES);
    vars_peek(false);
    return ok;
}

bool battle_battle_message_can_open(Snes *s)
{
    return (s->cpu.a & 0xFF00) == 0 && message_can_open(s, BATTLE_MESSAGES, message_in_a(s));
}
bool battle_enemy_message_can_open(Snes *s) { return message_can_open(s, ENEMY_MESSAGES, message_in_a(s)); }
bool battle_naming_can_open(Snes *s)
{
    return message_can_open(s, BATTLE_MESSAGES, NAME) && message_can_open(s, BATTLE_MESSAGES, NAME_WITH_HP);
}
bool battle_description_can_open(Snes *s) { return message_can_open(s, DESCRIPTIONS, message_in_a(s)); }
bool battle_message_can_go_on(Snes *s)
{
    if (!battle_on_page(s))
        return false;
    vars_peek(true);
    bool ok = menu_text_can_carry_on(state_of(s));
    vars_peek(false);
    return ok;
}
/* The opening alone takes the table already named: one of the three the
 * battle's messages come from. */
bool battle_message_string_can_open(Snes *s)
{
    vars_peek(true);
    uint32_t strings = menu_text_strings(state_of(s));
    vars_peek(false);
    bool battles = strings == BATTLE_MESSAGES || strings == ENEMY_MESSAGES || strings == DESCRIPTIONS;
    return battles && message_can_open(s, strings, message_in_a(s));
}

/* ---- The guard --------------------------------------------------------------------------------- */

/* The menus' text code takes a message being set up only where it can go on
 * from (menu/typeset.c); the battle's page has the message's cells. */
bool battle_message_can_be_typed(Snes *s)
{
    if (!battle_on_page(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = !battle_message_drawing(g) || menu_text_can_carry_on(g);
    vars_peek(false);
    return ok;
}
