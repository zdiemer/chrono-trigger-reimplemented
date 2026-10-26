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
 * waits from the original's sites, each with what the original keeps on the
 * stack and leaves in the carry there (battle/bridge.h, BattleFrame). */
#include "battle/message.h"

#include "battle/bridge.h"
#include "battle/frame_data.h"
#include "battle/frame.h"
#include "battle/program.h"
#include "battle/sound.h"
#include "battle/wait.h"
#include "charge.h"
#include "menu/typeset.h"
#include "original_effects.h"
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

/* A wait for the next frame the original makes with a count in A, which it
 * keeps on the stack across the wait, with the carry `carry`. */
static bool wait_a_frame_counting(Snes *s, uint32_t site, uint8_t left, BattleCarry carry)
{
    m_set_a8(s, left);
    return battle_wait_under(s, site, BATTLE_NEXT_FRAME_WAIT, BATTLE_COUNTING_WAIT_READS, battle_counting(left, carry));
}

/* The battle's own message's calls, each where the original makes it: its
 * sound (battle/sound.c), its whole text set out ($CD:027F, above), the
 * pauses around it ($CD:020A, below), the hold's frames (battle/wait.c), and
 * what the original keeps on the stack and in the carry at each (the
 * message, as many copies as are still to be used; the carry of its last
 * compare). */
typedef enum { HELD_SOUND, HELD_SETTING_OUT, HELD_PAUSE, HELD_ESCAPE_FRAMES, HELD_FRAME, HELD_PAUSE_AFTER } HeldCall;
typedef struct {
    uint8_t message, sound, left;
    bool silent, first;
} HeldArgs;
typedef bool (*HeldCalls)(void *ctx, HeldCall call, HeldArgs args);

// $CD:027F
/* Set the message's text, as much of it as `letters` allows, and show it once
 * two of the handler's frames have passed (`frame`, the frame work's wait,
 * battle/wait.c, the first and the second). */
static bool set_out_the_message(GameState *g, uint8_t letters, bool (*frame)(void *ctx, bool second), void *ctx)
{
    set_menu_text_budget(g, letters);
    TIMED_AS(g, 0xC25823, menu_text_go_on(g));
    if (!frame(ctx, false) || !frame(ctx, true))
        return false;
    show_the_message(g, true);
    return true;
}

static bool set_out_frame_on_the_machine(void *ctx, bool second)
{
    return battle_wait_for_the_handler_from(ctx, second ? 0xCD0289 : 0xCD0286);
}

// $CD:020A
/* A pause of 16 frames around a message the battle holds for. */
enum { MESSAGE_PAUSE = 0x10 };

/* What the setting out and the pause don't read of the registers where they
 * are called: the index registers and the flags but the carry, which they
 * hand on to the waits (battle/wait.h); the pause loads its own count into A
 * too, where the setting out takes the letters to set. */
enum {
    SETTING_OUT_IGNORES = DC_X | DC_Y | DC_N | DC_V | DC_Z,
    PAUSE_IGNORES = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z,
};

static bool pause_on_the_message(bool (*frame)(void *ctx, uint8_t left), void *ctx)
{
    for (uint8_t left = MESSAGE_PAUSE; left != 0; left--)
        if (!frame(ctx, left))
            return false;
    return true;
}

static bool pause_frame_on_the_machine(void *ctx, uint8_t left)
{
    return wait_a_frame_counting(ctx, 0xCD020D, left, BATTLE_CARRY_AS_IT_IS);
}

/* Set out the whole of a message just opened, from the original's call at
 * `site`, with what the original keeps there. */
static bool set_out_the_whole_message(Snes *s, uint32_t site, BattleFrame kept)
{
    m_set_a8(s, WHOLE_MESSAGE);
    return battle_run_dc_under(s, site, 0xCD027F, SETTING_OUT_IGNORES, battle_set_out_the_message, kept);
}

/* ---- The battle's own messages, which the battle holds for ---------------------------------------- */

/* The escape's message stays a second, whatever the speed; the others for
 * the frames the speed gives, or until A is pressed. */
enum { RAN_AWAY = 0x08, RAN_AWAY_FRAMES = 0x3C };
/* The original keeps the frames in a work cell of the battle's ($53). */
enum { LEFT_FRAMES = 0x53 - 0x45 };

static bool hold_the_message(GameState *g, uint8_t message, HeldCalls call, void *ctx)
{
    uint8_t frames = frames_up(g);
    ORIGINAL_EFFECT(set_battle_scratch(g, LEFT_FRAMES, frames));
    if (message == RAN_AWAY)
        return call(ctx, HELD_ESCAPE_FRAMES, (HeldArgs){ .message = message });
    uint8_t left = frames;
    do {
        if (!call(ctx, HELD_FRAME, (HeldArgs){ .message = message, .left = left, .first = left == frames }))
            return false;
        if (battle_joypad(g) & IN_BUTTON_A)
            break;
    } while (--left != 0);
    return true;
}

/* What the original has in the machine at each of the battle message's calls
 * (battle/bridge.h, BattleFrame). It keeps the message on the stack while it
 * shows it, a copy for each of its later uses (the sound's look-up, the
 * opening, the hold's test), so at each call the copies not used yet are
 * under the call's return; and the carry its last compare left. */
static BattleFrame keeping_copies(uint8_t message, uint8_t copies, BattleCarry carry)
{
    BattleFrame f = { .kept_bytes = copies, .carry = carry };
    for (unsigned i = 0; i < copies; i++)
        f.kept[i] = message;
    return f;
}

/* At the sound: two copies, and the look-up's compare with none, which a
 * sound found leaves clear. */
static BattleFrame kept_at_the_sound(uint8_t message) { return keeping_copies(message, 2, BATTLE_CARRY_CLEAR); }

/* At the setting out: one copy, and where there was no sound, the look-up's
 * compare that found none, which leaves the carry set. */
static BattleFrame kept_at_the_setting_out(uint8_t message, bool silent)
{
    return keeping_copies(message, 1, silent ? BATTLE_CARRY_SET : BATTLE_CARRY_AS_IT_IS);
}

/* At the pause: the copy for the hold's test. */
static BattleFrame kept_at_the_pause(uint8_t message) { return keeping_copies(message, 1, BATTLE_CARRY_AS_IT_IS); }

/* The hold's first wait has the carry of the message's test against the
 * escape's. */
static BattleCarry tested_for_the_hold(uint8_t message) { return battle_carry_of(message >= RAN_AWAY); }

// $CD:01B3 (through $CD:0021)
/* Show one of the battle's own messages and hold the battle while it is up:
 * its sound, its whole text at once, a pause, the frames the player's speed
 * gives, and a pause after it closes. */
static bool show_a_battle_message(GameState *g, uint8_t message, HeldCalls call, void *ctx)
{
    set_menu_text_strings(g, BATTLE_MESSAGES);
    uint8_t sound = battle_message_sound(g, message);
    bool silent = sound == BATTLE_MESSAGE_SILENT;
    if (!silent && !call(ctx, HELD_SOUND, (HeldArgs){ .message = message, .sound = sound }))
        return false;
    TIMED_AS(g, 0xCD025E, open_the_message(g, message));
    if (!call(ctx, HELD_SETTING_OUT, (HeldArgs){ .message = message, .silent = silent })
        || !call(ctx, HELD_PAUSE, (HeldArgs){ .message = message }) || !hold_the_message(g, message, call, ctx))
        return false;
    close_the_message(g);
    return call(ctx, HELD_PAUSE_AFTER, (HeldArgs){ .message = message });
}

static bool held_on_the_machine(void *ctx, HeldCall call, HeldArgs a)
{
    Snes *s = ctx;
    switch (call) {
    case HELD_SOUND: return battle_play_a_sound_from(s, 0xCD01CE, a.sound, AU_CENTRE, kept_at_the_sound(a.message));
    case HELD_SETTING_OUT: return set_out_the_whole_message(s, 0xCD01D5, kept_at_the_setting_out(a.message, a.silent));
    case HELD_PAUSE:
        return battle_run_dc_under(s, 0xCD01D8, 0xCD020A, PAUSE_IGNORES, battle_pause_on_the_message,
                                   kept_at_the_pause(a.message));
    case HELD_ESCAPE_FRAMES:
        return battle_wait_frames_under(s, 0xCD01E7, BATTLE_SOME_FRAMES_WAIT, BATTLE_COUNTING_WAIT_READS, RAN_AWAY_FRAMES,
                                        battle_carrying(tested_for_the_hold(a.message)));
    case HELD_FRAME:
        /* the hold's waits pass on the carry of the original's test of the
         * message against the escape's, from the first */
        return wait_a_frame_counting(s, 0xCD01EF, a.left, a.first ? tested_for_the_hold(a.message) : BATTLE_CARRY_AS_IT_IS);
    default: return battle_run_dc(s, 0xCD0206, 0xCD020A, PAUSE_IGNORES, battle_pause_on_the_message);
    }
}

/* ---- An enemy's message ------------------------------------------------------------------------- */

/* The frames between setting an enemy's message and timing it. */
enum { ENEMY_MESSAGE_PAUSE = 8 };

/* While the message is set, the original keeps it in a cell of its own
 * (battle_message_opening), which its code reads back after the waits, where
 * one of them hands the machine to it. */
static void leave_the_message_opening(GameState *g, uint8_t message) { set_battle_message_opening(g, message); }

/* What the original has in the carry at the enemy message's calls: at the
 * setting out, its compare of the message with the one up; at the last wait,
 * the bit of the settings its look-up of the speed shifted out. */
static BattleFrame kept_at_the_enemys_setting_out(uint8_t message, uint8_t up)
{
    return battle_carrying(battle_carry_of(message >= up));
}
static BattleFrame kept_at_the_enemys_last_wait(const GameState *g)
{
    return battle_carrying(battle_carry_of(speed_shifted_out(g)));
}

// $CD:0295 (through $CD:0033)
/* Show an enemy's message, unless it is the one up already: its whole text
 * set out, a pause, then the vblank handler times it at the player's speed
 * and closes it, and a frame (each a routine of its own). While it is up,
 * the messages naming combatants and the descriptions wait. */
typedef enum { ENEMY_SETTING_OUT, ENEMY_PAUSE, ENEMY_LAST_WAIT } EnemyMessageCall;
typedef bool (*EnemyMessageCalls)(void *ctx, EnemyMessageCall call, uint8_t message, uint8_t up);

static bool show_an_enemy_message(GameState *g, uint8_t message, EnemyMessageCalls call, void *ctx)
{
    set_battle_message_by_id(g, (uint8_t)(battle_message_by_id(g) + 1));
    set_menu_text_strings(g, ENEMY_MESSAGES);
    uint8_t up = battle_message_shown(g);
    if (message == up)
        return true;
    ORIGINAL_EFFECT(leave_the_message_opening(g, message));
    TIMED_AS(g, 0xCD025E, open_the_message(g, message));
    if (!call(ctx, ENEMY_SETTING_OUT, message, up) || !call(ctx, ENEMY_PAUSE, message, up))
        return false;
    set_battle_message_frames(g, frames_up(g));
    set_battle_message_shown(g, message);
    set_battle_message_timed(g, (uint8_t)(battle_message_timed(g) + 1));
    return call(ctx, ENEMY_LAST_WAIT, message, up);
}

static bool enemy_message_on_the_machine(void *ctx, EnemyMessageCall call, uint8_t message, uint8_t up)
{
    Snes *s = ctx;
    switch (call) {
    case ENEMY_SETTING_OUT: return set_out_the_whole_message(s, 0xCD02B0, kept_at_the_enemys_setting_out(message, up));
    case ENEMY_PAUSE:
        return battle_wait_frames(s, 0xCD02B5, BATTLE_SOME_FRAMES_WAIT, BATTLE_COUNTING_WAIT_READS, ENEMY_MESSAGE_PAUSE);
    default:
        return battle_wait_under(s, 0xCD02C7, BATTLE_NEXT_FRAME_WAIT, BATTLE_WAIT_READS,
                                 kept_at_the_enemys_last_wait(state_of(s)));
    }
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

void battle_show_a_battle_message_entry(Snes *s)
{
    if (!show_a_battle_message(state_of(s), message_in_a(s), held_on_the_machine, s))
        return;
    m_rtl(s);
}

void battle_show_an_enemy_message_entry(Snes *s)
{
    if (!show_an_enemy_message(state_of(s), message_in_a(s), enemy_message_on_the_machine, s))
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
        m_set_flag(s, CPU_FLAG_C, !opened);
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
    if (!set_out_the_message(state_of(s), message_in_a(s), set_out_frame_on_the_machine, s))
        return;
    m_rts(s);
}

/* The pause's count runs out in A. */
void battle_pause_on_the_message(Snes *s)
{
    if (!pause_on_the_message(pause_frame_on_the_machine, s))
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
    bool ok = menu_text_can_be_typed(state_of(s), strings, string, RAM_BATTLE_MESSAGE_TILES);
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
bool battle_message_typing_fits(const GameState *g) { return !battle_message_drawing(g) || menu_text_can_carry_on(g); }

bool battle_message_can_be_typed(Snes *s)
{
    if (!battle_on_page(s))
        return false;
    vars_peek(true);
    bool ok = battle_message_typing_fits(state_of(s));
    vars_peek(false);
    return ok;
}

/* ---- As C ------------------------------------------------------------------------------------ */

/* The windows naming a combatant ($CD:0030, $CD:002D) and the descriptions
 * ($CD:0027), each with its guard's state half: a string the menus' text
 * code can type into the message's tiles. False where the program went no
 * further. */
static bool can_be_typed(const GameState *g, uint32_t strings, uint8_t string)
{
    return menu_text_can_be_typed(g, strings, string, RAM_BATTLE_MESSAGE_TILES);
}

bool battle_combatant_named(GameState *g, uint8_t combatant, bool with_hp)
{
    uint32_t routine = with_hp ? 0xCD002D : 0xCD0030;
    if (!can_be_typed(g, BATTLE_MESSAGES, NAME) || !can_be_typed(g, BATTLE_MESSAGES, NAME_WITH_HP))
        return battle_needs_the_machine(g, routine, "a combatant's name the menus' text code doesn't type");
    TIMED_AS(g, routine, (void)name_a_combatant(g, combatant, with_hp ? NAME_WITH_HP : NAME));
    return true;
}

bool battle_described(GameState *g, uint8_t description)
{
    if (!can_be_typed(g, DESCRIPTIONS, description))
        return battle_needs_the_machine(g, 0xCD0027, "a description the menus' text code doesn't type");
    TIMED_AS(g, 0xCD0027, (void)describe(g, description));
    return true;
}

typedef struct {
    GameState *game;
    Platform *pf;
} MessageAsC;

static bool set_out_frame_as_c(void *ctx, bool second)
{
    (void)second;
    MessageAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, BATTLE_HANDLER_WAIT, done = battle_wait_for_the_handler(c->game, c->pf));
    return done;
}

static bool enemy_message_as_c(void *ctx, EnemyMessageCall call, uint8_t message, uint8_t up)
{
    (void)message;
    (void)up;
    MessageAsC *c = ctx;
    GameState *g = c->game;
    bool done = false;
    switch (call) {
    case ENEMY_SETTING_OUT: TIMED_AS(g, 0xCD027F, done = set_out_the_message(g, WHOLE_MESSAGE, set_out_frame_as_c, ctx)); return done;
    case ENEMY_PAUSE: TIMED_AS(g, BATTLE_SOME_FRAMES_WAIT, done = battle_wait_some_frames(g, c->pf, ENEMY_MESSAGE_PAUSE)); return done;
    default: TIMED_AS(g, BATTLE_NEXT_FRAME_WAIT, done = battle_wait_for_the_next_frame(g, c->pf)); return done;
    }
}

/* Its guard's state half: a string the menus' text code types. */
bool battle_show_an_enemy_message(GameState *g, Platform *pf, uint8_t message)
{
    if (!can_be_typed(g, ENEMY_MESSAGES, message))
        return battle_needs_the_machine(g, 0xCD0033, "an enemy's message the menus' text code doesn't type");
    MessageAsC c = { g, pf };
    return show_an_enemy_message(g, message, enemy_message_as_c, &c);
}

static bool pause_frame_as_c(void *ctx, uint8_t left)
{
    (void)left;
    MessageAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, BATTLE_NEXT_FRAME_WAIT, done = battle_wait_for_the_next_frame(c->game, c->pf));
    return done;
}

static bool held_as_c(void *ctx, HeldCall call, HeldArgs a)
{
    MessageAsC *c = ctx;
    GameState *g = c->game;
    bool done = false;
    switch (call) {
    case HELD_SOUND: TIMED_AS(g, BATTLE_PLAY_A_SOUND_ENTRY, battle_play_a_sound(c->pf, a.sound, AU_CENTRE)); return true;
    case HELD_SETTING_OUT: TIMED_AS(g, 0xCD027F, done = set_out_the_message(g, WHOLE_MESSAGE, set_out_frame_as_c, ctx)); return done;
    case HELD_PAUSE:
    case HELD_PAUSE_AFTER: TIMED_AS(g, 0xCD020A, done = pause_on_the_message(pause_frame_as_c, ctx)); return done;
    case HELD_ESCAPE_FRAMES: TIMED_AS(g, BATTLE_SOME_FRAMES_WAIT, done = battle_wait_some_frames(g, c->pf, RAN_AWAY_FRAMES)); return done;
    default: TIMED_AS(g, BATTLE_NEXT_FRAME_WAIT, done = battle_wait_for_the_next_frame(g, c->pf)); return done;
    }
}

/* Its guard's state half: a string the menus' text code types. */
bool battle_show_a_battle_message(GameState *g, Platform *pf, uint8_t message)
{
    if (!can_be_typed(g, BATTLE_MESSAGES, message))
        return battle_needs_the_machine(g, 0xCD0021, "a battle message the menus' text code doesn't type");
    MessageAsC c = { g, pf };
    return show_a_battle_message(g, message, held_as_c, &c);
}
