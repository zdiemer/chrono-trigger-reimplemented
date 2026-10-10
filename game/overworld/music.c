/* The world map's music: each region of the map plays its own song.
 *
 * An object of the overworld listens to where the party walks. While the
 * party is on foot on the map, it looks up the region the leader stands in
 * (terrain.c); stepping into a region whose song isn't the one playing, the
 * song fades out, and a second later the region's song starts and is brought
 * straight up. While a screen keeps the song that plays (a map screen's script
 * asks it), the object asks for that song instead, every frame.
 *
 * The object asks the sound driver through the sprite-object mode's request
 * (menu_sound_request), which it passes on to the driver's command cells when
 * the driver is free; the main loop sends a command waiting there once a
 * frame (main_loop.c). */
#include "overworld/frames.h"
#include "overworld/music.h"

#include "menu/map.h"
#include "overworld/terrain.h"
#include "platform/audio.h"
#include "vars.h"

/* ---- The sound request --------------------------------------------------------------------- */

/* The mailbox the request is passed on through: empty, a command waiting in
 * the driver's cells for the main loop to send, or being sent (its top bit:
 * nothing may be passed on meanwhile). */
enum { MAILBOX_EMPTY = 0, MAILBOX_WAITING = 1, MAILBOX_SENDING = 0xFF, MAILBOX_BUSY_BIT = 0x80 };

bool overworld_sound_waits(const GameState *g)
{
    return menu_sound_mailbox(g) != MAILBOX_EMPTY;
}

bool overworld_sound_is_being_sent(const GameState *g) { return (menu_sound_mailbox(g) & MAILBOX_BUSY_BIT) != 0; }

void overworld_mark_sound_sending(GameState *g, bool sending)
{
    set_menu_sound_mailbox(g, sending ? MAILBOX_SENDING : MAILBOX_EMPTY);
}

// $C2:2ED9
/* Pass the request on to the driver's cells, unless one is being sent, or
 * one waits there that is younger than it. */
bool overworld_pass_on_sound(GameState *g)
{
    if (overworld_sound_waits(g)
        && (overworld_sound_is_being_sent(g) || menu_sound_waiting_age(g) < menu_sound_request_age(g)))
        return false;
    set_sound_command(g, menu_sound_request_command(g));
    set_sound_command_parameters(g, 0, menu_sound_request_first(g));
    set_sound_command_parameters(g, 1, menu_sound_request_second(g));
    set_sound_command_parameters(g, 2, menu_sound_request_third(g));
    set_menu_sound_waiting_age(g, menu_sound_request_age(g));
    set_menu_sound_mailbox(g, MAILBOX_WAITING);
    return true;
}

/* A request, new (so of age 0), passed on at once if the mailbox lets it. */
static bool ask_for(GameState *g, AudioCommand c)
{
    set_menu_sound_request_command(g, c.command);
    set_menu_sound_request_first(g, c.p1);
    set_menu_sound_request_second(g, c.p2);
    set_menu_sound_request_third(g, c.p3);
    set_menu_sound_request_age(g, 0);
    return overworld_pass_on_sound(g);
}

/* ---- The region's song ----------------------------------------------------------------------- */

/* A song starts at full volume, its third parameter (docs/SOUND.md) the
 * middle one the map's songs all take; the music fades out over twelve steps,
 * and is brought back up at once. A fade doesn't read its third parameter,
 * which the requests set full. */
enum { SONG_THIRD = 0x80, FADE_OUT_STEPS = 0x0C, AT_ONCE = 0, SILENT = 0, FADE_THIRD = AU_FULL_VOLUME };

static AudioCommand song_request(uint8_t song) { return (AudioCommand){ AU_START_SONG, song, AU_FULL_VOLUME, SONG_THIRD, 3 }; }

static void play_song(GameState *g, uint8_t song)
{
    if (ask_for(g, song_request(song)))
        set_menu_song_playing(g, song);
}

void overworld_play_song(GameState *g, uint8_t song) { play_song(g, song); }

// $C2:2F4E
/* The song of what the screen shows (the Epoch has come down there), or the
 * one a screen keeps. The request is the last one's again: it keeps its
 * age. */
void overworld_play_the_song_in_view(GameState *g)
{
    uint8_t song = menu_song_kept(g) ? current_song(g)
                                     : overworld_region_songs(g, overworld_region_in_view(g, menu_map_view_centre(g)));
    AudioCommand c = song_request(song);
    set_menu_sound_request_command(g, c.command);
    set_menu_sound_request_first(g, c.p1);
    set_menu_sound_request_second(g, c.p2);
    set_menu_sound_request_third(g, c.p3);
    if (overworld_pass_on_sound(g))
        set_menu_song_playing(g, song);
}

// $C2:4387-$C2:4395
/* Arriving through time, the music goes back to its own pace at once. A bend
 * reads two parameters, so the request keeps the third the last one had. */
bool overworld_restore_the_tempo(GameState *g)
{
    enum { AT_ONCE_STEPS = 0, AS_WRITTEN = 0 };
    set_menu_sound_request_command(g, AU_BEND_TEMPO);
    set_menu_sound_request_first(g, AT_ONCE_STEPS);
    set_menu_sound_request_second(g, AS_WRITTEN);
    set_menu_sound_request_age(g, 0);
    return overworld_pass_on_sound(g);
}

/* The object's steps: listening to where the party walks, then (a song
 * changing) the old song fading out, then the new one to bring up. */
enum { STARTS_LISTENING = 0, LISTENS = 1, FADES_THE_SONG_OUT = 2, BRINGS_THE_SONG_UP = 3, MUSIC_STEPS = 4 };

/* A second for the old song to fade. */
enum { FADE_FRAMES = 60 };

enum { NO_REGION = 0xFF };

/* Whether the object listens to the regions: the overworld on the map (or
 * back on it from the map view) and the party on foot. */
static bool listens_to_the_regions(const GameState *g)
{
    OverworldMode mode = overworld_mode(g);
    return (mode == OVERWORLD_STARTING || mode == OVERWORLD_ON_THE_MAP || mode == OVERWORLD_BACK_ON_THE_MAP)
           && overworld_party_travel(g) == TRAVEL_ON_FOOT;
}

// $C2:2FAE
static void listen(GameState *g, MenuObject self)
{
    if (menu_song_kept(g)) {
        play_song(g, current_song(g));
        return;
    }
    if (!listens_to_the_regions(g)) {
        set_overworld_region(g, NO_REGION);
        return;
    }
    uint8_t region = overworld_region_at(g, overworld_party_spot(g, PARTY_LEADER));
    set_overworld_region(g, region);
    if (overworld_region_songs(g, region) == menu_song_playing(g))
        return;
    overworld_set_step(g, self, FADES_THE_SONG_OUT);
    set_menu_object_slots_age(g, self, 0);
    (void)ask_for(g, (AudioCommand){ AU_FADE_MUSIC, FADE_OUT_STEPS, SILENT, FADE_THIRD, 3 });
}

// $C2:2F94
void overworld_play_the_region_song(GameState *g, MenuObject self)
{
    switch (overworld_step(g, self)) {
    case STARTS_LISTENING:
        overworld_set_step(g, self, LISTENS);
        set_overworld_region(g, NO_REGION);
        listen(g, self);
        break;
    case LISTENS:
        listen(g, self);
        break;
    case FADES_THE_SONG_OUT:
        // $C2:303C, $C2:3076
        if (menu_object_slots_age(g, self) < FADE_FRAMES)
            break;
        overworld_set_step(g, self, BRINGS_THE_SONG_UP);
        play_song(g, overworld_region_songs(g, overworld_region(g)));
        break;
    default:
        // $C2:304F
        if (overworld_sound_is_being_sent(g))
            break;
        (void)ask_for(g, (AudioCommand){ AU_FADE_MUSIC, AT_ONCE, AU_FULL_VOLUME, FADE_THIRD, 3 });
        overworld_set_step(g, self, LISTENS);
        break;
    }
}

bool overworld_region_song_can_play(const GameState *g, MenuObject self)
{
    OverworldStep step = overworld_step(g, self);
    if (step >= MUSIC_STEPS)
        return false;
    if (step == FADES_THE_SONG_OUT)
        return overworld_region(g) < OVERWORLD_REGION_SONGS_COUNT;
    if (step <= LISTENS && !menu_song_kept(g) && listens_to_the_regions(g))
        return overworld_spot_has_a_region(overworld_party_spot(g, PARTY_LEADER));
    return true;
}
