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
 * The frame work's waits (battle/wait.c) are made where the original makes
 * them. */
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

// $CD:04FD
/* Open the window: its sound (battle/sound.c), and from the next frame of
 * the handler's (battle/wait.c) its slide in. */
bool battle_window_opened(GameState *g, Platform *pf)
{
    TIMED_AS(g, BATTLE_PLAY_A_SOUND_ENTRY, battle_play_a_sound(pf, WINDOW_SOUND, AU_CENTRE));
    if (!battle_wait_for_the_handler(g, pf))
        return false;
    battle_start_the_slide(g, BATTLE_SLIDE_STARTING_IN);
    return battle_wait_for_the_handler(g, pf);
}

// $CD:0516
/* Close the window: no pausing from now on, the lines laid out for it to go,
 * and from the next frame its slide out. */
static bool close_the_window(GameState *g, Platform *pf)
{
    set_battle_pause_blocked(g, 1);
    if (!battle_wait_for_the_handler(g, pf))
        return false;
    battle_arrange_the_lines(g, WINDOW_GOING);
    if (!battle_wait_for_the_handler(g, pf))
        return false;
    battle_start_the_slide(g, BATTLE_SLIDE_OUT);
    return true;
}

/* ---- The music ------------------------------------------------------------------------------- */

/* The music fades out over 16 of the driver's steps. */
enum { MUSIC_FADE_STEPS = 0x10, SILENT = 0x00 };

static bool keeps_the_field_song(const GameState *g) { return battle_terms(g, BATTLE_TERMS_KEEP_THE_FIELD_SONG); }

enum { FADE_WAIT_FRAMES = 0x0F };

// $CD:039E
/* Fade the music out, and wait a quarter of a second (the original jumps to
 * the wait), whose frames type the message's next letter: refused for a
 * message the menus' text code doesn't go on from. */
static bool fade_the_music_out(GameState *g, Platform *pf)
{
    if (!battle_message_typing_fits(g))
        return battle_contract_broken(g, 0xCD039E, "a message the menus' text code doesn't go on from");
    au_fade(pf, PF_CALL(0xCD03AB, 0xC70004), AU_FADE_MUSIC, MUSIC_FADE_STEPS, SILENT);
    return battle_wait_some_frames(g, pf, FADE_WAIT_FRAMES);
}

/* Fade the music out, and wait for its fade to run out, a frame of the
 * handler's at a time. */
static bool fade_the_music_and_wait(GameState *g, Platform *pf, uint32_t checked_at)
{
    if (!fade_the_music_out(g, pf))
        return false;
    do
        if (!battle_wait_for_the_handler(g, pf))
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
 * finishes its own ($CD:0009). */
static bool let_the_screen_settle(GameState *g, Platform *pf)
{
    do {
        if (!battle_wait_for_the_handler(g, pf) || !battle_wait_for_the_handler(g, pf))
            return false;
        bool done = false;
        TIMED_AS(g, 0xCD04AA, done = battle_frame_wait_finished(g, pf));
        if (!done)
            return false;
    } while (!screen_settled(g));
    return true;
}

static bool settle_the_screen(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, 0xCD0DD8, done = let_the_screen_settle(g, pf));
    return done;
}

/* The music's volume back to full at once, for the next song. */
static void full_volume(Platform *pf)
{
    au_fade(pf, PF_CALL(0xCD0D70, 0xC70004), AU_FADE_ALL, 0, AU_FULL_VOLUME);
}

// $CD:0D62
/* Change to another battlefield as the battle ends, with its song, from the
 * music's volume back to full a frame before: the battlefield is loaded as
 * an effect loads one, and its own screen scripts start. */
static bool change_to_the_battlefield(GameState *g, Platform *pf, uint16_t battlefield)
{
    full_volume(pf);
    if (!battle_wait_for_the_handler(g, pf))
        return false;
    au_play_song(pf, PF_CALL(0xCD0D81, 0xC70004), CHANGED_BATTLEFIELD_SONG);
    return battle_load_and_run_battlefield(g, pf, battlefield);
}

static bool change_the_battlefield(GameState *g, Platform *pf, uint16_t battlefield)
{
    bool done = false;
    TIMED_AS(g, 0xCD0D62, done = change_to_the_battlefield(g, pf, battlefield));
    return done;
}

/* A run of frames counted down across the handler's waits. */
static bool hold_the_screen(GameState *g, Platform *pf)
{
    for (uint8_t left = HOLD_FRAMES; left != 0; left--)
        if (!battle_wait_for_the_handler(g, pf))
            return false;
    return true;
}

// $CD:0DB1
/* The boss's end, as a battle won on an HP gradient's battlefield ends: the
 * music faded out, the gradient settled to its lowest, the screen held, and
 * the victory's battlefield shown and settled. */
static bool play_out_the_boss_end(GameState *g, Platform *pf)
{
    if (!fade_the_music_and_wait(g, pf, 0xCD0DB7))
        return false;
    set_battle_gradient_frame_mask(g, EVERY_FOURTH_FRAME);
    set_battle_gradient_level_override(g, LEVEL_GIVEN);
    if (!settle_the_screen(g, pf) || !hold_the_screen(g, pf) || !change_the_battlefield(g, pf, VICTORY_BATTLEFIELD))
        return false;
    return settle_the_screen(g, pf);
}

/* ---- A defeat's end on those battlefields -------------------------------------------------------- */

/* The battlefield a defeat shows. */
enum { DEFEAT_BATTLEFIELD = 0x87 };

// $CD:0DA2
/* Once the fade is silent, show the defeat battlefield and let its screen
 * animation finish before returning to the battle's closing sequence. */
static bool show_the_defeat(GameState *g, Platform *pf)
{
    if (!change_the_battlefield(g, pf, DEFEAT_BATTLEFIELD))
        return false;
    do
        if (!battle_wait_for_the_handler(g, pf))
            return false;
    while (battle_screen_scripts_running(g));
    return true;
}

// $CD:0D93
/* A battle not won on an HP gradient's battlefield or the screen effects'
 * ends on the defeat's battlefield: the enemies' rule dropped, the music
 * faded out, and the defeat's battlefield shown with its song, its screen
 * scripts let run out. */
static bool play_out_the_defeat_end(GameState *g, Platform *pf)
{
    set_battle_enemy_rule(g, 0);
    if (!fade_the_music_and_wait(g, pf, 0xCD0D9C))
        return false;
    bool shown;
    TIMED_AS(g, 0xCD0DA2, shown = show_the_defeat(g, pf));
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

/* The small handler's frame ($CD:044A). */
static void wait_for_the_small_handler(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xCD044A, battle_wait_for_the_small_handler(g, pf));
}

// $CD:03B4 (through $CD:0006)
/* The display closed down, each step where the original takes it. */
bool battle_display_closed(GameState *g, Platform *pf)
{
    bool done = true;
    if (battle_background_flags(g) & SONG_GOES)
        set_battle_event_flags(g, battle_event_flags(g) & (uint8_t)~BATTLE_TERMS_KEEP_THE_FIELD_SONG);
    if (battle_battlefield_tiles_replaced(g)) {
        TIMED_AS(g, 0xCD0453, done = battle_put_the_battlefield_back(g, pf));
        if (!done)
            return false;
    }
    set_battle_script_sound_request(g, 0);
    if (!keeps_the_field_song(g) && !fade_the_music_out(g, pf))
        return false;
    TIMED_AS(g, 0xCD0516, done = close_the_window(g, pf));
    if (!done)
        return false;
    do
        if (!battle_wait_for_the_handler(g, pf))
            return false;
    while (battle_window_sliding(g));
    battle_arrange_the_lines(g, WINDOW_GONE);
    if (!battle_wait_for_the_handler(g, pf))
        return false;
    /* the original asks only whether the battle was won: a script's result
       (BATTLE_BACK_TO_THE_PLACE_PUT_ASIDE) ends here as a defeat does */
    bool won = battle_how_it_ended(g) == BATTLE_WON_OR_RAN;
    if ((battle_background_flags(g) & BATTLE_BACKGROUND_HAS_GRADIENT) && won) {
        TIMED_AS(g, 0xCD0DB1, done = play_out_the_boss_end(g, pf));
        if (!done)
            return false;
    } else if ((battle_background_flags(g) & (BATTLE_BACKGROUND_HAS_GRADIENT | BATTLE_BACKGROUND_EFFECTS_BATTLEFIELD)) && !won) {
        TIMED_AS(g, 0xCD0D93, done = play_out_the_defeat_end(g, pf));
        if (!done)
            return false;
    }
    battle_arrange_the_lines(g, LEAVING);
    if (!battle_wait_for_the_handler(g, pf))
        return false;
    program_install_vblank_handler(g, battle_closing_vblank);
    charge_accesses(g, 4);
    set_battle_scratch(g, FADE_FROM, battle_brightness(g));
    do
        wait_for_the_small_handler(g, pf);
    while (!keeps_the_field_song(g) && au_music_fade_left(pf, PF_AT(0xCD0432)) != 0);
    set_battle_background_flags(g, 0);
    TIMED_AS(g, FIELD_MUSIC_BACK_ENTRY, field_bring_the_music_back(g, pf));
    wait_for_the_small_handler(g, pf);
    return true;
}
