/* Battle: the battle window's opening and closing, and the battle's display
 * closed down as the party goes back to the field.
 *
 * The battle window slides up into view as the fight starts and slides back
 * down as it ends (the slide itself is the frame work's, battle/frame.c):
 * opening it plays the window's sound, and closing it keeps Start from
 * pausing the battle while it goes.
 *
 * As the party leaves (battle/from_field.c), the display is closed down:
 *
 *   - the battlefield is put back if an effect drew over it
 *     (battlefield_restore.c), and a sound a screen script asked for is
 *     forgotten;
 *   - the music fades out, unless the event that started the battle kept the
 *     field's song playing through it;
 *   - the window slides away, and the picture is laid out for the battle's
 *     end;
 *   - a battle won on an HP gradient's battlefield plays out the boss's end
 *     there: the gradient settles to its lowest, the screen is held for two
 *     seconds, and the battlefield changes to the one the victory shows, with
 *     its song. A battle that didn't end in a win, on the gradient's
 *     battlefield or the screen effects', changes to a defeat's battlefield
 *     instead, with the same song;
 *   - the battle's small vblank handler takes over, fading the picture out
 *     (battle/display.c), and once the music's fade has run out the field's
 *     music is put back (field/sounds.c).
 *
 * The steps are C on the game's state; what reaches the machine is the bridge
 * that makes the frame work's waits where the original makes them (battle/
 * wait.c), from the original's call sites and with what it keeps on the stack
 * there (battle/bridge.h, BattleFrame), since shadow mode pairs a call with
 * the original's by its site and its stack. */
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame.h"
#include "battle/message.h"
#include "battle/program.h"
#include "battle/screen.h"
#include "battle/sound.h"
#include "battle/wait.h"
#include "charge.h"
#include "core/program.h"
#include "field/sounds.h"
#include "platform/audio.h"
#include "vars.h"

/* ---- The window's slide ------------------------------------------------------------------- */

/* The ways the battle lays its lines out as it ends (battle_arrange_line_
 * effects' arrangements, as the line handler takes them): while the window
 * goes, once it has gone, and for the fade to the field. */
enum { WINDOW_GOING = 0x0C, WINDOW_GONE = 0x0E, LEAVING = 0x08 };
/* The window's sound, played in the middle. */
enum { WINDOW_SOUND = 0x08 };

/* ---- Opening and closing the window ---------------------------------------------------------- */

/* The window's opening plays its sound and waits for two of the handler's
 * frames, each a routine of its own (battle/sound.c, battle/wait.c). */
typedef enum { OPENING_SOUND, OPENING_FIRST_FRAME, OPENING_SECOND_FRAME } OpeningCall;

// $CD:04FD
/* Open the window: its sound, and from the next frame its slide in. */
static bool open_the_window(GameState *g, bool (*call)(void *ctx, OpeningCall call), void *ctx)
{
    if (!call(ctx, OPENING_SOUND) || !call(ctx, OPENING_FIRST_FRAME))
        return false;
    battle_start_the_slide(g, BATTLE_SLIDE_STARTING_IN);
    return call(ctx, OPENING_SECOND_FRAME);
}

typedef struct {
    GameState *game;
    Platform *pf;
} BattleClosingContext;

static bool open_a_part(void *ctx, OpeningCall call)
{
    BattleClosingContext *c = ctx;
    if (call == OPENING_SOUND) {
        TIMED_AS(c->game, BATTLE_PLAY_A_SOUND_ENTRY, battle_play_a_sound(c->pf, WINDOW_SOUND, AU_CENTRE));
        return true;
    }
    bool done = false;
    TIMED_AS(c->game, BATTLE_HANDLER_WAIT, done = battle_wait_for_the_handler(c->game, c->pf));
    return done;
}

bool battle_window_opened(GameState *g, Platform *pf)
{
    BattleClosingContext c = { g, pf };
    return open_the_window(g, open_a_part, &c);
}

/* The closing's calls, each where the original makes it: the frame work's
 * waits for its handler's frame (battle/wait.c), the battle's small handler's
 * frame, the frame work's tail ($CD:0009), the battlefield put back
 * (battle/battlefield_restore.c) and changed (battle/battlefield_change.c),
 * the field's music back (field/sounds.c), and the steps that are routines of
 * their own on the machine: the music's fade, the window's closing, the
 * boss's end and the defeat's, the screen's settling, the change to another
 * battlefield; and what the original keeps on the stack and in the registers
 * around them. `arg` is the frames counted, or the battlefield. */
typedef enum {
    CLOSING_WINDOW_FIRST_FRAME, CLOSING_WINDOW_SECOND_FRAME,
    CLOSING_PUT_BACK, CLOSING_FADE, CLOSING_THE_WINDOW, CLOSING_SLIDE_FRAME, CLOSING_GONE_FRAME,
    CLOSING_BOSS_END, CLOSING_DEFEAT_END, CLOSING_LEAVING_FRAME, CLOSING_SMALL_FRAME, CLOSING_MUSIC_BACK,
    CLOSING_LAST_SMALL_FRAME,
    BOSS_FADE, BOSS_FADE_FRAME, BOSS_SETTLE, BOSS_HOLD_FRAME, BOSS_CHANGE, BOSS_SETTLE_AGAIN,
    DEFEAT_FADE, DEFEAT_FADE_FRAME, DEFEAT_CHANGE, DEFEAT_SCRIPTS_FRAME,
    SETTLE_FIRST_FRAME, SETTLE_SECOND_FRAME, SETTLE_TAIL,
    SONG_KEEP, SONG_FRAME, SONG_KEPT, CHANGE_LOAD,
} ClosingCall;
typedef bool (*ClosingCalls)(void *ctx, ClosingCall call, uint16_t arg);

static bool closing_call(ClosingCalls call, void *ctx, ClosingCall what) { return call(ctx, what, 0); }

// $CD:0516
/* Close the window: no pausing from now on, the lines laid out for it to go,
 * and from the next frame its slide out. */
static bool close_the_window(GameState *g, ClosingCalls call, void *ctx)
{
    set_battle_pause_blocked(g, 1);
    if (!closing_call(call, ctx, CLOSING_WINDOW_FIRST_FRAME))
        return false;
    battle_arrange_the_lines(g, WINDOW_GOING);
    if (!closing_call(call, ctx, CLOSING_WINDOW_SECOND_FRAME))
        return false;
    battle_start_the_slide(g, BATTLE_SLIDE_OUT);
    return true;
}

/* ---- The music ------------------------------------------------------------------------------- */

/* The music fades out over 16 of the driver's steps. */
enum { MUSIC_FADE_STEPS = 0x10, SILENT = 0x00 };

static bool keeps_the_field_song(const GameState *g) { return battle_terms(g, BATTLE_TERMS_KEEP_THE_FIELD_SONG); }

// $CD:039E
/* Fade the music out (the wait after it, a quarter of a second, is the
 * caller's: the original jumps to it). */
static void fade_the_music_out(Platform *pf)
{
    au_fade(pf, PF_CALL(0xCD03AB, 0xC70004), AU_FADE_MUSIC, MUSIC_FADE_STEPS, SILENT);
}

enum { FADE_WAIT_FRAMES = 0x0F };

/* Fade the music out, and wait for its fade to run out, a frame at a time
 * (`fade` and `frame` the calls of each place it is made from). */
static bool fade_the_music_and_wait(Platform *pf, ClosingCall fade, ClosingCall frame, uint32_t checked_at,
                                    ClosingCalls call, void *ctx)
{
    if (!closing_call(call, ctx, fade))
        return false;
    do
        if (!closing_call(call, ctx, frame))
            return false;
    while (au_music_fade_left(pf, PF_AT(checked_at)) != 0);
    return true;
}

/* ---- The boss's end on its battlefield -------------------------------------------------------- */

/* The gradient steps every fourth frame, fading to the lowest level. */
enum { EVERY_FOURTH_FRAME = 0x03, LEVEL_GIVEN = 0x80, LOWEST_LEVELS = 2 };
/* Two seconds and more of the settled screen. */
enum { HOLD_FRAMES = 0x80 };
/* The battlefield the victory shows, and the song a changed battlefield
 * plays, after a win or a defeat alike. */
enum { VICTORY_BATTLEFIELD = 0x84, CHANGED_BATTLEFIELD_SONG = 0x3A };

// $CD:0DD8
/* Whether the screen has settled: the gradient at its lowest levels and not
 * fading, and no battlefield being put back, effect running or screen script
 * running. */
static bool screen_settled(const GameState *g)
{
    return battle_gradient_level(g) < LOWEST_LEVELS && !battle_gradient_fading(g)
           && !battle_background_restore_request(g) && !battle_effects_running(g)
           && !battle_screen_scripts_running(g);
}

/* Two frames at a time, each pair finished as the main loop's frame wait
 * finishes its own. */
static bool let_the_screen_settle(GameState *g, ClosingCalls call, void *ctx)
{
    do
        if (!closing_call(call, ctx, SETTLE_FIRST_FRAME) || !closing_call(call, ctx, SETTLE_SECOND_FRAME)
            || !closing_call(call, ctx, SETTLE_TAIL))
            return false;
    while (!screen_settled(g));
    return true;
}

/* The music's volume back to full at once, for the next song. */
static void full_volume(Platform *pf)
{
    au_fade(pf, PF_CALL(0xCD0D70, 0xC70004), AU_FADE_ALL, 0, AU_FULL_VOLUME);
}

// $CD:0D62
/* Change to another battlefield as the battle ends, with its song, from the
 * music's volume back to full a frame before (the original keeps the
 * battlefield on the stack meanwhile): the battlefield is loaded as an
 * effect loads one, and its own screen scripts start. */
static bool change_to_the_battlefield(Platform *pf, uint16_t battlefield, ClosingCalls call, void *ctx)
{
    if (!call(ctx, SONG_KEEP, battlefield))
        return false;
    full_volume(pf);
    if (!closing_call(call, ctx, SONG_FRAME))
        return false;
    au_play_song(pf, PF_CALL(0xCD0D81, 0xC70004), CHANGED_BATTLEFIELD_SONG);
    return call(ctx, SONG_KEPT, battlefield) && call(ctx, CHANGE_LOAD, battlefield);
}

/* A run of frames the original counts down in A across the handler's waits. */
static bool hold_the_screen(ClosingCalls call, void *ctx)
{
    for (uint8_t left = HOLD_FRAMES; left != 0; left--)
        if (!call(ctx, BOSS_HOLD_FRAME, left))
            return false;
    return true;
}

// $CD:0DB1
/* The boss's end, as a battle won on an HP gradient's battlefield ends: the
 * music faded out, the gradient settled to its lowest, the screen held, and
 * the victory's battlefield shown and settled. */
static bool play_out_the_boss_end(GameState *g, Platform *pf, ClosingCalls call, void *ctx)
{
    if (!fade_the_music_and_wait(pf, BOSS_FADE, BOSS_FADE_FRAME, 0xCD0DB7, call, ctx))
        return false;
    set_battle_gradient_frame_mask(g, EVERY_FOURTH_FRAME);
    set_battle_gradient_level_override(g, LEVEL_GIVEN);
    if (!closing_call(call, ctx, BOSS_SETTLE) || !hold_the_screen(call, ctx)
        || !call(ctx, BOSS_CHANGE, VICTORY_BATTLEFIELD))
        return false;
    return closing_call(call, ctx, BOSS_SETTLE_AGAIN);
}

/* ---- A defeat's end on those battlefields -------------------------------------------------------- */

/* The battlefield a defeat shows. */
enum { DEFEAT_BATTLEFIELD = 0x87 };

// $CD:0DA2
/* Once the fade is silent, show the defeat battlefield and let its screen
 * animation finish before returning to the battle's closing sequence. */
static bool show_the_defeat(GameState *g, ClosingCalls call, void *ctx)
{
    if (!call(ctx, DEFEAT_CHANGE, DEFEAT_BATTLEFIELD))
        return false;
    do
        if (!closing_call(call, ctx, DEFEAT_SCRIPTS_FRAME))
            return false;
    while (battle_screen_scripts_running(g));
    return true;
}

// $CD:0D93
/* A battle not won on an HP gradient's battlefield or the screen effects'
 * ends on the defeat's battlefield: the enemies' rule dropped, the music
 * faded out, and the defeat's battlefield shown with its song, its screen
 * scripts let run out. */
static bool play_out_the_defeat_end(GameState *g, Platform *pf, ClosingCalls call, void *ctx)
{
    set_battle_enemy_rule(g, 0);
    if (!fade_the_music_and_wait(pf, DEFEAT_FADE, DEFEAT_FADE_FRAME, 0xCD0D9C, call, ctx))
        return false;
    bool shown;
    TIMED_AS(g, 0xCD0DA2, shown = show_the_defeat(g, call, ctx));
    return shown;
}

/* ---- The display closed down ------------------------------------------------------------------ */

/* The battle's background flags as it ends: bit 7 lets the field's song go
 * after all; the HP gradient and the screen effects' battlefield (frame.h)
 * each have an end of their own. */
enum { SONG_GOES = 0x80 };
/* The battle's small vblank handler (battle/display.c), which fades the
 * picture out from the brightness it is given. */
enum { FADE_FROM = 0 };

// $CD:03B4 (through $CD:0006)
static bool close_the_display(GameState *g, Platform *pf, ClosingCalls call, void *ctx)
{
    if (battle_background_flags(g) & SONG_GOES)
        set_battle_event_flags(g, battle_event_flags(g) & (uint8_t)~BATTLE_TERMS_KEEP_THE_FIELD_SONG);
    if (battle_battlefield_tiles_replaced(g) && !closing_call(call, ctx, CLOSING_PUT_BACK))
        return false;
    set_battle_script_sound_request(g, 0);
    if (!keeps_the_field_song(g) && !closing_call(call, ctx, CLOSING_FADE))
        return false;
    if (!closing_call(call, ctx, CLOSING_THE_WINDOW))
        return false;
    do
        if (!closing_call(call, ctx, CLOSING_SLIDE_FRAME))
            return false;
    while (battle_window_sliding(g));
    battle_arrange_the_lines(g, WINDOW_GONE);
    if (!closing_call(call, ctx, CLOSING_GONE_FRAME))
        return false;
    /* the original asks only whether the battle was won: a script's result
       (BATTLE_BACK_TO_THE_PLACE_PUT_ASIDE) ends here as a defeat does */
    bool won = battle_how_it_ended(g) == BATTLE_WON_OR_RAN;
    if ((battle_background_flags(g) & BATTLE_BACKGROUND_HAS_GRADIENT) && won) {
        if (!closing_call(call, ctx, CLOSING_BOSS_END))
            return false;
    } else if ((battle_background_flags(g) & (BATTLE_BACKGROUND_HAS_GRADIENT | BATTLE_BACKGROUND_EFFECTS_BATTLEFIELD)) && !won) {
        if (!closing_call(call, ctx, CLOSING_DEFEAT_END))
            return false;
    }
    battle_arrange_the_lines(g, LEAVING);
    if (!closing_call(call, ctx, CLOSING_LEAVING_FRAME))
        return false;
    program_install_vblank_handler(g, battle_closing_vblank);
    charge_legacy_accesses(g, 4);
    set_battle_scratch(g, FADE_FROM, battle_brightness(g));
    do
        if (!closing_call(call, ctx, CLOSING_SMALL_FRAME))
            return false;
    while (!keeps_the_field_song(g) && au_music_fade_left(pf, PF_AT(0xCD0432)) != 0);
    set_battle_background_flags(g, 0);
    return closing_call(call, ctx, CLOSING_MUSIC_BACK) && closing_call(call, ctx, CLOSING_LAST_SMALL_FRAME);
}

/* ---- As C ------------------------------------------------------------------------------------ */

/* Each with its guard's state half: the music's fade's, whose wait types the
 * message's next letter, a message the menus' text code goes on from; the
 * battlefield's change's, a layout the port builds. */
static bool close_a_part(void *ctx, ClosingCall call, uint16_t arg)
{
    BattleClosingContext *c = ctx;
    GameState *g = c->game;
    Platform *pf = c->pf;
    bool done = false;
    switch (call) {
    case CLOSING_PUT_BACK: TIMED_AS(g, 0xCD0453, done = battle_put_the_battlefield_back(g, pf)); return done;
    case CLOSING_FADE:
    case BOSS_FADE:
    case DEFEAT_FADE:
        if (!battle_message_typing_fits(g))
            return battle_contract_broken(g, 0xCD039E, "a message the menus' text code doesn't go on from");
        fade_the_music_out(pf);
        TIMED_AS(g, BATTLE_SOME_FRAMES_WAIT, done = battle_wait_some_frames(g, pf, FADE_WAIT_FRAMES));
        return done;
    case CLOSING_THE_WINDOW: TIMED_AS(g, 0xCD0516, done = close_the_window(g, close_a_part, ctx)); return done;
    case CLOSING_BOSS_END: TIMED_AS(g, 0xCD0DB1, done = play_out_the_boss_end(g, pf, close_a_part, ctx)); return done;
    case CLOSING_DEFEAT_END: TIMED_AS(g, 0xCD0D93, done = play_out_the_defeat_end(g, pf, close_a_part, ctx)); return done;
    case CLOSING_SMALL_FRAME:
    case CLOSING_LAST_SMALL_FRAME: TIMED_AS(g, 0xCD044A, battle_wait_for_the_small_handler(g, pf)); return true;
    case CLOSING_MUSIC_BACK: TIMED_AS(g, FIELD_MUSIC_BACK_ENTRY, field_bring_the_music_back(g, pf)); return true;
    case BOSS_SETTLE:
    case BOSS_SETTLE_AGAIN: TIMED_AS(g, 0xCD0DD8, done = let_the_screen_settle(g, close_a_part, ctx)); return done;
    case BOSS_CHANGE:
    case DEFEAT_CHANGE: TIMED_AS(g, 0xCD0D62, done = change_to_the_battlefield(pf, arg, close_a_part, ctx)); return done;
    case SETTLE_TAIL: TIMED_AS(g, 0xCD04AA, done = battle_frame_wait_finished(g, pf)); return done;
    case SONG_KEEP:
    case SONG_KEPT: return true;
    case CHANGE_LOAD: return battle_load_and_run_battlefield(g, pf, arg);
    default: TIMED_AS(g, BATTLE_HANDLER_WAIT, done = battle_wait_for_the_handler(g, pf)); return done;
    }
}

bool battle_display_closed(GameState *g, Platform *pf)
{
    BattleClosingContext c = { g, pf };
    return close_the_display(g, pf, close_a_part, &c);
}
