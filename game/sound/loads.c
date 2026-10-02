/* The game's side of the sound driver: what the original's CPU half did.
 *
 * In the original the driver ran on the sound CPU, and the game reached it
 * through a part on the main CPU that took the game's command and turned it
 * into the driver's, uploading through the ports what the driver needed
 * first. That part is here as calls, so nothing is uploaded and nothing
 * waits: sound_game_command is the game's command as the CPU half took it.
 *
 * Most commands go to the driver as they are. A song is the one with work of
 * its own: the CPU half looked up the volume and echo delay it plays with,
 * kept the sound memory its echo would need free of samples, and before the
 * song started made sure its samples were in sound memory. Those live in
 * sixteen places after the boot's own samples. The samples a song shares
 * with the one before stay where they are, moved down over the places the
 * old song's others leave; the ones it lacks are loaded after them; and the
 * directory's entries for the song's instruments ($20 on) are pointed at
 * them. Where a sample lies matters: one past the echo's start is played
 * from the buffer the echo writes, as in the original (battle-magus's do).
 * Then the song is loaded (songs.c), with the effects again first if the
 * last song was long enough to run over them.
 *
 * Commands $70 and $71 were the CPU half's own: a switch between the two
 * sets of effects, and one sample block loaded in place of the song's first
 * instrument. The field also asks for a set by leaving word of it for the
 * next song, which brings it. */
#include "core/sound_data.h"
#include "sound/driver.h"

enum {
    SONG_COMMANDS = 0x10,            /* $10-$17: songs; $10, $11, $14 and $15 start one */
    SAVES_SONG_FIRST = 0x04,         /* the bit that makes $10/$11 into $14/$15 */
    NOT_A_SONG_START = 0x02,         /* set in $12, $13, $16 and $17, which do nothing */
    FIRST_SENT_AS_IS = 0x18,         /* $18-$2F and $80-$FF go to the driver as they are */
    SHORTCUTS = 0x30,                /* $30-$3F stand for a command of four bytes each */
    LAST_SHORTCUT = 0x3F,
    SWITCH_EFFECTS = 0x70,
    LOAD_INSTRUMENT = 0x71,
    DRIVER_COMMANDS = 0x80,
    CHANGE_ECHO_DELAY = 0xFC,
    LIST_END = 0x80                  /* a negative byte ends the list of songs that save first */
};

/* ---- The places -------------------------------------------------------------------------- */

/* The echo's buffer, at a delay of `delay`, starts this many pages below the
 * top of what the samples may use (platform/voices_dsp.h keeps it there). */
enum { TOP_PAGE = 0xF1, PAGES_PER_ECHO_STEP = 8 };

static SoundSamples *places(SoundDriver *d) { return &d->samples; }

// $C7:0A12
/* The places from the first that ends past the echo's start on are
 * forgotten. An empty place (one that ends at page 0) ends the search: no
 * sample lies after it. */
static void forget_places_under_echo(SoundDriver *d)
{
    SoundSamples *p = places(d);
    for (uint8_t place = 0; place < SOUND_SAMPLE_PLACES; place++) {
        uint8_t end_page = (uint8_t)(p->start[place + 1] >> 8);
        if (end_page == 0)
            return;
        if (end_page < p->echo_page)
            continue;
        for (uint8_t gone = place; gone < SOUND_SAMPLE_PLACES; gone++) {
            p->block[gone] = 0;
            p->length[gone] = 0;
            p->start[gone + 1] = 0;
        }
        return;
    }
}

// $C7:09FD
void sound_keep_echo_free(SoundDriver *d, uint8_t echo_delay)
{
    SoundSamples *p = places(d);
    echo_delay = echo_delay_of(echo_delay);
    if (echo_delay == p->echo_delay)
        return;
    p->echo_delay = echo_delay;
    p->echo_page = (uint8_t)(TOP_PAGE - PAGES_PER_ECHO_STEP * echo_delay);
    forget_places_under_echo(d);
}

/* The place that holds `block`, among the first `count`, or SOUND_SAMPLE_PLACES:
 * the last of them if two do (an instrument loaded over the song's first
 * can be one of its others), and for the directory the first. */
static uint8_t place_of(const SoundSamples *p, uint16_t block, uint8_t count)
{
    for (uint8_t place = count; place-- > 0;)
        if (p->block[place] == block)
            return place;
    return SOUND_SAMPLE_PLACES;
}

static uint8_t first_place_of(const SoundSamples *p, uint16_t block)
{
    for (uint8_t place = 0; place < SOUND_SAMPLE_PLACES; place++)
        if (p->block[place] == block)
            return place;
    return SOUND_SAMPLE_PLACES;
}

/* The places in use, up to the last that holds a block. */
static uint8_t places_used(const SoundSamples *p)
{
    uint8_t used = SOUND_SAMPLE_PLACES;
    while (used && !p->block[used - 1])
        used--;
    return used;
}

static uint8_t first_free_place(const SoundSamples *p)
{
    uint8_t place = 0;
    while (place < SOUND_SAMPLE_PLACES && p->block[place])
        place++;
    return place;
}

/* A block's sample, `length` bytes, into sound memory at `at`. */
static void write_sample(SoundDriver *d, uint16_t at, uint16_t block, uint16_t length)
{
    for (uint16_t i = 0; i < length; i++)
        au_sample_memory_write(d->voices, (uint16_t)(at + i), sound_block_byte(d->game, block, i));
}

// $C7:0655
/* A block's sample into a place, which then ends where the sample does. */
static void load_sample(SoundDriver *d, uint8_t place, uint16_t block)
{
    SoundSamples *p = places(d);
    uint16_t at = p->start[place], length = sound_block_length(d->game, block);
    write_sample(d, at, block, length);
    p->block[place] = block;
    p->length[place] = length;
    p->start[place + 1] = (uint16_t)(at + length);
}

// $C7:025E
/* The samples kept move down into the free places before them, each to
 * where the place before it ends, so they lie end to end from the first. */
static void close_up_places(SoundDriver *d)
{
    SoundSamples *p = places(d);
    for (uint8_t free = 0; free < SOUND_SAMPLE_PLACES; free++) {
        if (p->block[free])
            continue;
        uint8_t kept = (uint8_t)(free + 1);
        while (kept < SOUND_SAMPLE_PLACES && !p->block[kept])
            kept++;
        if (kept == SOUND_SAMPLE_PLACES)
            return;
        au_sample_memory_move(d->voices, p->start[free], p->start[kept], p->length[kept]);
        p->block[free] = p->block[kept];
        p->block[kept] = 0;
        p->length[free] = p->length[kept];
        p->start[free + 1] = (uint16_t)(p->start[free] + p->length[kept]);
    }
}

// $C7:0326
/* The samples the song lacks, into the places from the first free one (all
 * of them, forgotten, if none is). The places after the last loaded end
 * nowhere and hold nothing. */
static void load_missing_samples(SoundDriver *d, const uint16_t *missing, uint8_t count)
{
    SoundSamples *p = places(d);
    uint8_t place = first_free_place(p);
    if (place == SOUND_SAMPLE_PLACES) {
        for (uint8_t i = 0; i < SOUND_SAMPLE_PLACES; i++)
            p->block[i] = 0;
        place = 0;
    }
    /* a song has at most one sample a place (SOUND_SONG_INSTRUMENTS), and
     * the places before these hold only its own, closed up first, so the
     * places never run out (the original would run on into its table of
     * lengths) */
    for (uint8_t i = 0; i < count && place < SOUND_SAMPLE_PLACES; i++)
        load_sample(d, place++, missing[i]);
    for (; place < SOUND_SAMPLE_PLACES; place++) {
        p->length[place] = 0;
        p->start[place + 1] = 0;
    }
}

// $C7:037B
/* The directory's entries for the song's instruments: where each sample
 * starts and where it loops from. An instrument the song has no sample for
 * keeps the entry the last song with one left (the original sends its whole
 * table, built only as far as the song's samples go). */
static void point_directory_at_samples(SoundDriver *d, const uint16_t *blocks, uint8_t count)
{
    SoundSamples *p = places(d);
    for (uint8_t i = 0; i < count; i++) {
        /* every one of the song's samples is in a place by now */
        uint8_t place = first_place_of(p, blocks[i]);
        uint16_t start = 0, loop = 0;
        if (place < SOUND_SAMPLE_PLACES) {
            start = p->start[place];
            loop = (uint16_t)(start + sound_block_loop(d->game, blocks[i]));
        }
        p->song_start[i] = start;
        p->song_loop[i] = loop;
    }
    for (uint8_t i = 0; i < SOUND_SAMPLE_PLACES; i++)
        au_sample_place(d->voices, (uint8_t)(SOUND_FIRST_SONG_INSTRUMENT + i), p->song_start[i], p->song_loop[i]);
}

// $C7:01A1
/* The song's samples into sound memory. If it has them all already they stay
 * where they are; otherwise the places holding samples it doesn't use are
 * freed (up to the first empty one), the rest close up, and what it lacks
 * loads after them. */
static void place_song_samples(SoundDriver *d, uint8_t song)
{
    SoundSamples *p = places(d);
    uint16_t blocks[SOUND_SONG_INSTRUMENTS], missing[SOUND_SONG_INSTRUMENTS];
    uint8_t count = 0, missing_count = 0, used = places_used(p);
    while (count < SOUND_SONG_INSTRUMENTS) {
        uint16_t block = sound_song_instrument_block(d->game, song, count);
        if (!block)
            break;
        blocks[count++] = block;
        if (place_of(p, block, used) == SOUND_SAMPLE_PLACES)
            missing[missing_count++] = block;
    }
    if (missing_count) {
        for (uint8_t place = 0; place < SOUND_SAMPLE_PLACES && p->block[place]; place++) {
            bool song_uses = false;
            for (uint8_t i = 0; i < count; i++)
                song_uses |= blocks[i] == p->block[place];
            if (!song_uses)
                p->block[place] = 0;
        }
        if (places_used(p) != first_free_place(p))
            close_up_places(d);
        load_missing_samples(d, missing, missing_count);
    }
    point_directory_at_samples(d, blocks, count);
    // $C7:04A9
    if (count)
        p->instrument_place = place_of(p, blocks[0], SOUND_SAMPLE_PLACES);
}

/* ---- Songs --------------------------------------------------------------------------------- */

/* A song this long runs on over the effects, loaded after it at $2F00. */
enum { SONG_REACHES_EFFECTS = SOUND_EFFECTS - SOUND_SEQUENCES };

// $C7:0614
void sound_load_announced_song(SoundDriver *d)
{
    if (!d->song_announced)
        return;
    uint8_t song = d->song_id;
    place_song_samples(d, song);
    SoundSamples *p = places(d);
    if (p->effects_overwritten)
        sound_load_effects(d, p->effect_set);
    if (sound_song_length(d->game, song) >= SONG_REACHES_EFFECTS)
        p->effects_overwritten++;
    sound_start_announced_song(d);
    forget_places_under_echo(d);
}

// $C7:04A9
/* A set other than the one loaded comes with the song, as if a song had run
 * over the effects. */
void sound_ask_for_effects(SoundDriver *d, uint8_t effects_asked)
{
    SoundSamples *p = places(d);
    uint8_t set = effects_asked != 0;
    if (set == p->effect_set)
        return;
    p->effect_set = set;
    p->effects_overwritten++;
}

// $C7:0A39
void sound_play_song(SoundDriver *d, SoundCommand c, uint8_t effects_asked)
{
    uint8_t echo_delay = c.p3;
    sound_keep_echo_free(d, echo_delay);
    if (!sound_announce_song(d, c))
        return;
    sound_ask_for_effects(d, effects_asked);
    sound_load_announced_song(d);
}

// $C7:0734
/* A song in the list starts by saving the one playing, as $14 and $15 do. */
static bool saves_first(const SoundDriver *d, uint8_t song)
{
    /* (the original reads on until a negative byte; the list is the one
     * byte that ends it, and this stops after a page) */
    for (uint16_t i = 0; i < 0x100; i++) {
        uint8_t listed = sound_song_saving_first(d->game, i);
        if (listed & LIST_END)
            return false;
        if (listed == song)
            return true;
    }
    return false;
}

// $C7:01A1
/* A song the game asks for plays at its own volume and echo delay. */
static void start_song_asked_for(SoundDriver *d, uint8_t command, uint8_t song, uint8_t effects_asked)
{
    if (song >= sound_songs(d->game))
        return;
    if (saves_first(d, song))
        command |= SAVES_SONG_FIRST;
    SoundCommand c = { command, song, sound_song_volume(d->game, song), sound_song_echo_delay(d->game, song) };
    sound_play_song(d, c, effects_asked);
}

/* ---- Effects and instruments --------------------------------------------------------------- */

// $C7:08E3
static void switch_effects(SoundDriver *d, uint8_t set)
{
    if (set != places(d)->effect_set)
        sound_load_effects(d, set);
}

// $C7:0755
bool sound_load_song_instrument(SoundDriver *d, uint16_t block)
{
    SoundSamples *p = places(d);
    if (!p->block[0] || !block)
        return false;
    uint8_t place = p->instrument_place;
    if (place >= SOUND_SAMPLE_PLACES)
        return false;   /* the song's first sample is always in a place once it has loaded */
    uint16_t at = p->start[place], length = sound_block_length(d->game, block);
    if ((uint16_t)(at + length - 1) >= p->start[place + 1])
        return false;
    write_sample(d, at, block, length);
    p->length[place] = length;
    au_sample_place(d->voices, SOUND_FIRST_SONG_INSTRUMENT, at, (uint16_t)(at + sound_block_loop(d->game, block)));
    sound_load_instrument(d, SOUND_FIRST_SONG_INSTRUMENT, block);
    p->block[place] = block;
    return true;
}

/* ---- The boot -------------------------------------------------------------------------------- */

/* The echo's delay the boot starts with. */
enum { BOOT_ECHO_DELAY = 5 };

// $C7:0000
/* The boot's own samples and their directory entries, and every place
 * empty, the first starting after them. */
void load_boot_samples(SoundDriver *d)
{
    const GameState *g = d->game;
    uint16_t samples_at = sound_boot_block_to(g, SOUND_BOOT_SAMPLES_BLOCK);
    uint16_t samples_length = sound_boot_block_length(g, SOUND_BOOT_SAMPLES_BLOCK);
    for (uint16_t i = 0; i < samples_length; i++)
        au_sample_memory_write(d->voices, (uint16_t)(samples_at + i), sound_boot_block_byte(g, SOUND_BOOT_SAMPLES_BLOCK, i));
    for (uint8_t sample = 0; sample < sound_boot_directory_entries(g); sample++) {
        SoundDirectoryEntry entry = sound_boot_directory_entry(g, sample);
        au_sample_place(d->voices, sample, entry.start, entry.loop);
    }

    SoundSamples *p = places(d);
    *p = (SoundSamples){ 0 };
    p->start[0] = (uint16_t)(samples_at + samples_length);
    p->echo_delay = BOOT_ECHO_DELAY;
    p->echo_page = (uint8_t)(TOP_PAGE - PAGES_PER_ECHO_STEP * BOOT_ECHO_DELAY);
}

/* ---- The game's command ------------------------------------------------------------------------ */

// $C7:0155
void sound_game_command(SoundDriver *d, SoundCommand c, uint8_t effects_asked)
{
    for (;;) {
        uint8_t command = c.command;
        if (command >= DRIVER_COMMANDS || (command >= FIRST_SENT_AS_IS && command < SHORTCUTS)) {
            sound_run_command(d, c);
            // $C7:0643
            if (command == CHANGE_ECHO_DELAY)
                sound_keep_echo_free(d, c.p1);
            return;
        }
        if (command >= SONG_COMMANDS && command < FIRST_SENT_AS_IS) {
            if (!(command & NOT_A_SONG_START))
                start_song_asked_for(d, command, c.p1, effects_asked);
            return;
        }
        if (command >= SHORTCUTS && command <= LAST_SHORTCUT) {
            /* the four bytes the shortcut stands for, taken as the command */
            uint8_t n = command - SHORTCUTS;
            c = (SoundCommand){ sound_shortcut_byte(d->game, n, 0), sound_shortcut_byte(d->game, n, 1),
                                sound_shortcut_byte(d->game, n, 2), sound_shortcut_byte(d->game, n, 3) };
            continue;
        }
        if (command == SWITCH_EFFECTS)
            switch_effects(d, c.p1 != 0);
        else if (command == LOAD_INSTRUMENT)
            sound_load_song_instrument(d, c.p1);
        return;
    }
}
