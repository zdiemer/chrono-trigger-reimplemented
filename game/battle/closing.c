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
#include "battle/screen.h"
#include "battle/sound.h"
#include "battle/wait.h"
#include "charge.h"
#include "core/console.h"
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

/* ---- The waits ----------------------------------------------------------------------------- */

/* A run of frames the original counts down in A across the handler's waits
 * (battle/wait.h), which keep A for it. */
static bool wait_frames_counting(Snes *s, uint32_t site, uint8_t frames)
{
    for (uint8_t left = frames; left != 0; left--) {
        m_set_a8(s, left);
        if (!battle_wait_frame(s, site, BATTLE_HANDLER_WAIT, BATTLE_COUNTING_WAIT_READS))
            return false;
    }
    return true;
}

static bool wait_for_the_small_handler(Snes *s, uint32_t site)
{
    return battle_run(s, site, 0xCD044A, battle_wait_for_the_small_handler);
}

/* ---- Opening and closing the window ---------------------------------------------------------- */

// $CD:04FD
/* Open the window: its sound, and from the next frame its slide in. */
static bool open_the_window(Snes *s)
{
    if (!battle_play_a_sound_from(s, 0xCD0502, WINDOW_SOUND, AU_CENTRE, battle_carrying(BATTLE_CARRY_AS_IT_IS))
        || !battle_wait_for_the_handler_from(s, 0xCD0505))
        return false;
    battle_start_the_slide(state_of(s), BATTLE_SLIDE_STARTING_IN);
    return battle_wait_for_the_handler_from(s, 0xCD0512);
}

// $CD:0516
/* Close the window: no pausing from now on, the lines laid out for it to go,
 * and from the next frame its slide out. */
static bool close_the_window(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_pause_blocked(g, 1);
    if (!battle_wait_for_the_handler_from(s, 0xCD051B))
        return false;
    battle_arrange_the_lines(g, WINDOW_GOING);
    if (!battle_wait_for_the_handler_from(s, 0xCD0526))
        return false;
    battle_start_the_slide(g, BATTLE_SLIDE_OUT);
    return true;
}

/* ---- The music ------------------------------------------------------------------------------- */

/* The sound driver's command reads only A's high byte. */
enum { DRIVER_IGNORES = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* The music fades out over 16 of the driver's steps. */
enum { MUSIC_FADE_STEPS = 0x10, SILENT = 0x00 };

static bool keeps_the_field_song(const GameState *g) { return battle_terms(g, BATTLE_TERMS_KEEP_THE_FIELD_SONG); }

/* What the display's steps don't read of the registers where they are
 * called: each loads A's low byte and the index registers before it reads
 * them; the music's fade and the window's closing hand only A's high byte on,
 * to waits that read none of the flags (battle/wait.h). */
enum {
    PUTTING_BACK_IGNORES = DC_AL | DC_X | DC_Y | DC_N | DC_Z,
    FADE_IGNORES = DC_AL | DC_X | DC_Y | DC_FLAGS,
    CLOSING_IGNORES = DC_AL | DC_X | DC_Y | DC_FLAGS,
};

// $CD:039E
/* Fade the music out (the wait after it, a quarter of a second, is the
 * caller's: the original jumps to it). */
static void fade_the_music_out(Platform *pf)
{
    au_fade(pf, PF_CALL_DC(0xCD03AB, 0xC70004, DRIVER_IGNORES), AU_FADE_MUSIC, MUSIC_FADE_STEPS, SILENT);
}

enum { FADE_WAIT_FRAMES = 0x0F };

/* Fade the music out, from the call at `site`, and wait for its fade to run
 * out, a frame at a time (checked at `checked_at`). */
static bool fade_the_music_and_wait(Snes *s, uint32_t site, uint32_t waits_at, uint32_t checked_at)
{
    if (!battle_run_dc(s, site, 0xCD039E, FADE_IGNORES, battle_fade_the_music_out))
        return false;
    do
        if (!battle_wait_for_the_handler_from(s, waits_at))
            return false;
    while (au_music_fade_left(m_platform(s), PF_AT(checked_at)) != 0);
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
static bool let_the_screen_settle(Snes *s)
{
    do
        if (!battle_wait_for_the_handler_from(s, 0xCD0DD8) || !battle_wait_for_the_handler_from(s, 0xCD0DDB)
            || !battle_run_long(s, 0xCD0DDE, 0xCD04AA, battle_frame_wait_tail))
            return false;
    while (!screen_settled(state_of(s)));
    return true;
}

/* The music's volume back to full at once, for the next song. */
static void full_volume(Platform *pf)
{
    au_fade(pf, PF_CALL_DC(0xCD0D70, 0xC70004, DRIVER_IGNORES), AU_FADE_ALL, 0, AU_FULL_VOLUME);
}

/* The changed battlefield's song, from the music's volume back to full a
 * frame before. The original keeps the battlefield it changes to on the
 * stack meanwhile (`kept`), under the driver's calls and the wait. */
static bool play_the_changed_battlefields_song(Snes *s, BattleFrame kept)
{
    battle_keep(s, kept);
    full_volume(m_platform(s));
    if (!battle_wait_for_the_handler_from(s, 0xCD0D74))
        return false;
    au_play_song(m_platform(s), PF_CALL_DC(0xCD0D81, 0xC70004, DRIVER_IGNORES), CHANGED_BATTLEFIELD_SONG);
    battle_stop_keeping(s, kept);
    return true;
}

// $CD:0D62
/* Change to another battlefield as the battle ends, with its song: the
 * battlefield is loaded as an effect loads one, and its own screen scripts
 * start. The battlefield comes in X. */
static bool change_to_the_battlefield(Snes *s, uint16_t battlefield)
{
    if (!play_the_changed_battlefields_song(s, battle_keeping_word(battlefield, BATTLE_CARRY_AS_IT_IS)))
        return false;
    if (!battle_load_and_run_battlefield(s, 0xCD0D86, battlefield))
        return false;
    s->cpu.a = 0; /* the scripts' start leaves A's high byte clear for the waits */
    return true;
}

// $CD:0DB1
/* The boss's end, as a battle won on an HP gradient's battlefield ends: the
 * music faded out, the gradient settled to its lowest, the screen held, and
 * the victory's battlefield shown and settled. */
static bool play_out_the_boss_end(Snes *s)
{
    GameState *g = state_of(s);
    if (!fade_the_music_and_wait(s, 0xCD0DB1, 0xCD0DB4, 0xCD0DB7))
        return false;
    set_battle_gradient_frame_mask(g, EVERY_FOURTH_FRAME);
    set_battle_gradient_level_override(g, LEVEL_GIVEN);
    if (!battle_run(s, 0xCD0DC7, 0xCD0DD8, battle_let_the_screen_settle))
        return false;
    if (!wait_frames_counting(s, 0xCD0DCC, HOLD_FRAMES))
        return false;
    s->cpu.x = VICTORY_BATTLEFIELD;
    if (!battle_run(s, 0xCD0DD5, 0xCD0D62, battle_change_to_the_battlefield))
        return false;
    return let_the_screen_settle(s);
}

/* ---- A defeat's end on those battlefields -------------------------------------------------------- */

/* The battlefield a defeat shows. */
enum { DEFEAT_BATTLEFIELD = 0x87 };

// $CD:0D93
/* A battle not won on an HP gradient's battlefield or the screen effects'
 * ends on the defeat's battlefield: the enemies' rule dropped, the music
 * faded out, and the defeat's battlefield shown with its song, its screen
 * scripts let run out. */
static bool play_out_the_defeat_end(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_enemy_rule(g, 0);
    if (!fade_the_music_and_wait(s, 0xCD0D96, 0xCD0D99, 0xCD0D9C))
        return false;
    s->cpu.x = DEFEAT_BATTLEFIELD;
    if (!battle_run(s, 0xCD0DA5, 0xCD0D62, battle_change_to_the_battlefield))
        return false;
    do
        if (!battle_wait_for_the_handler_from(s, 0xCD0DA8))
            return false;
    while (battle_screen_scripts_running(g));
    return true;
}

/* ---- The display closed down ------------------------------------------------------------------ */

/* The battle's background flags as it ends: bit 7 lets the field's song go
 * after all; the HP gradient and the screen effects' battlefield (frame.h)
 * each have an end of their own. */
enum { SONG_GOES = 0x80 };
/* The battle's small vblank handler (battle/display.c), which fades the
 * picture out from the brightness it is given. */
enum { SMALL_VBLANK_HANDLER = 0xD1F4C0, FADE_FROM = 0 };
/* The field's music after a battle (field/sounds.c). It takes the data bank
 * and its page back in the wrong order, so the battle keeps its own data
 * bank, page and flags on the stack around the call and takes them back
 * after: the call comes back to its return with any page and bank, and goes
 * on as the battle runs once they are pulled. */
enum { FIELD_MUSIC_CALL = 0xCD043E, KEPT_AGAIN = 0xCD0445 };

static bool put_the_field_music_back(Snes *s)
{
    uint16_t page = m_direct_page(s);
    uint8_t bank = s->cpu.db;
    m_push8(s, s->cpu.db);
    m_push16(s, s->cpu.d);
    m_push8(s, s->cpu.p);
    if (!battle_run_long_to_any_page(s, FIELD_MUSIC_CALL, FIELD_MUSIC_BACK_ENTRY, field_bring_the_music_back))
        return false;
    s->cpu.p = m_pull8(s);
    s->cpu.d = m_pull16(s);
    s->cpu.db = m_pull8(s);
    return battle_back(s, KEPT_AGAIN, page, bank);
}

// $CD:03B4 (through $CD:0006)
static bool close_the_display(Snes *s)
{
    GameState *g = state_of(s);
    if (battle_background_flags(g) & SONG_GOES)
        set_battle_event_flags(g, battle_event_flags(g) & (uint8_t)~BATTLE_TERMS_KEEP_THE_FIELD_SONG);
    if (battle_battlefield_tiles_replaced(g)
        && !battle_run_dc(s, 0xCD03C6, 0xCD0453, PUTTING_BACK_IGNORES, battle_put_the_battlefield_back))
        return false;
    set_battle_script_sound_request(g, 0);
    if (!keeps_the_field_song(g) && !battle_run_dc(s, 0xCD03D3, 0xCD039E, FADE_IGNORES, battle_fade_the_music_out))
        return false;
    if (!battle_run_long_dc(s, 0xCD03D6, 0xCD0516, CLOSING_IGNORES, battle_close_the_window))
        return false;
    do
        if (!battle_wait_for_the_handler_from(s, 0xCD03DA))
            return false;
    while (battle_window_sliding(g));
    battle_arrange_the_lines(g, WINDOW_GONE);
    if (!battle_wait_for_the_handler_from(s, 0xCD03EB))
        return false;
    /* the original asks only whether the battle was won: a script's result
       (BATTLE_BACK_TO_THE_PLACE_PUT_ASIDE) ends here as a defeat does */
    bool won = battle_how_it_ended(g) == BATTLE_WON_OR_RAN;
    if ((battle_background_flags(g) & BATTLE_BACKGROUND_HAS_GRADIENT) && won) {
        if (!battle_run(s, 0xCD03FF, 0xCD0DB1, battle_play_out_the_boss_end))
            return false;
    } else if ((battle_background_flags(g) & (BATTLE_BACKGROUND_HAS_GRADIENT | BATTLE_BACKGROUND_EFFECTS_BATTLEFIELD)) && !won) {
        if (!battle_run_dc(s, 0xCD040A, 0xCD0D93, BATTLE_WAIT_READS, battle_play_out_the_defeat_end))
            return false;
    }
    battle_arrange_the_lines(g, LEAVING);
    if (!battle_wait_for_the_handler_from(s, 0xCD0415))
        return false;
    console_install_vblank_handler(g, SMALL_VBLANK_HANDLER);
    set_battle_scratch(g, FADE_FROM, battle_brightness(g));
    do
        if (!wait_for_the_small_handler(s, 0xCD0428))
            return false;
    while (!keeps_the_field_song(g) && au_music_fade_left(m_platform(s), PF_AT(0xCD0432)) != 0);
    /* the flags as the wait's last test leaves them (the song kept, or no
       fade left), which the field music's call keeps on the stack */
    m_set_nz8(s, keeps_the_field_song(g) ? BATTLE_TERMS_KEEP_THE_FIELD_SONG : 0);
    set_battle_background_flags(g, 0);
    if (!put_the_field_music_back(s))
        return false;
    s->cpu.a = 0;
    return wait_for_the_small_handler(s, 0xCD0446);
}

/* ---- The entries ------------------------------------------------------------------------------ */

void battle_close_the_display(Snes *s)
{
    if (close_the_display(s))
        m_rtl(s);
}

/* The window's opening leaves its slide in A. */
void battle_open_the_window(Snes *s)
{
    if (!open_the_window(s))
        return;
    m_set_a8(s, BATTLE_SLIDE_STARTING_IN);
    m_rtl(s);
}

/* ... and its closing too. */
void battle_close_the_window(Snes *s)
{
    if (!close_the_window(s))
        return;
    m_set_a8(s, BATTLE_SLIDE_OUT);
    m_rtl(s);
}

/* The fade's wait is the frame work's for some frames, which the original
 * jumps to with the count in A, so that it returns to the fade's caller. */
void battle_fade_the_music_out(Snes *s)
{
    fade_the_music_out(m_platform(s));
    m_set_a8(s, FADE_WAIT_FRAMES);
    NATIVE_TIME_AS(s, 0xCD3E3B, battle_wait_some_frames(s));
}

/* The settling ends on the gradient level's compare, which leaves the carry
 * clear for the waits after the call; the original's boss's end calls the
 * settling and then runs on into it, so it ends there too. */
static void leave_the_settled_compare(Snes *s) { m_set_flag(s, CPU_FLAG_C, false); }

void battle_play_out_the_boss_end(Snes *s)
{
    if (!play_out_the_boss_end(s))
        return;
    leave_the_settled_compare(s);
    m_rts(s);
}

/* It ends on the scripts' flag's load. */
void battle_play_out_the_defeat_end(Snes *s)
{
    if (!play_out_the_defeat_end(s))
        return;
    m_set_a8(s, 0);
    m_set_nz8(s, 0);
    m_rts(s);
}

void battle_let_the_screen_settle(Snes *s)
{
    if (!let_the_screen_settle(s))
        return;
    leave_the_settled_compare(s);
    m_rts(s);
}

void battle_change_to_the_battlefield(Snes *s)
{
    if (change_to_the_battlefield(s, s->cpu.x))
        m_rts(s);
}
