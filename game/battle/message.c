/* Battle: the battle message's text.
 *
 * A message the battle shows -- what an attack is called, what was found,
 * whose turn it is -- is set into the message's tiles through the menus' text
 * code (menu/typeset.c), a letter a frame, as the frame work waits for each
 * frame (battle/wait.c); the vblank handler sends the tiles to VRAM, a half
 * each frame, while the message shows (battle/uploads.c). Once the text is
 * all set, the message stays up for as long as the player's message speed
 * says, which the vblank handler counts down before it closes it
 * (battle/nmi.c). */
#include "battle/message.h"

#include "battle/bridge.h"
#include "battle/frame_data.h"
#include "charge.h"
#include "menu/typeset.h"
#include "port/vars_ram.h"
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

/* ---- The entries ------------------------------------------------------------------------------- */

/* A letter typed leaves the frames in A, its high byte clear as the menus'
 * text code leaves it, the speed in X, and in the carry the bit of the
 * settings the speed's shift moved out, which the callers' code after the
 * wait reads. */
enum { SPEED_SHIFTED_OUT = 0x04 };

static void leave_what_typing_leaves(Snes *s, const GameState *g)
{
    s->cpu.a = battle_message_frames(g);
    s->cpu.x = message_speed(g);
    m_set_flag(s, CPU_FLAG_C, (game_settings(g, 1) & SPEED_SHIFTED_OUT) != 0);
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
