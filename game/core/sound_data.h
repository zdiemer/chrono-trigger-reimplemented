/* The sound driver's data: what the C sound driver (game/sound/) loads -- what
 * the cartridge's CPU half sent the sound processor -- and the tables it looks
 * things up in.
 *
 * Every read the driver makes of it goes through these accessors, named after
 * the resources MD2 typed (docs/MATCHING.md, "Sound blocks": sound_block_ptrs
 * and song_ptrs) or, for the smaller tables typed by hand, after what the
 * driver does with them. A build that loads the data from somewhere other than
 * the ROM image changes this file and nothing in the driver.
 *
 * They take the game's state as the handle to the cartridge (rom.h). */
#ifndef CT_GAME_CORE_SOUND_DATA_H
#define CT_GAME_CORE_SOUND_DATA_H

#include <stdbool.h>

#include "rom.h"

/* The SPC700 driver goes up in six blocks: block k is at $C7:(the word at
 * $C7:0010 + 2k), a length and that many bytes, for SPC700 address (the word
 * at $C7:001C + 2k). The driver's code is block 0, at $0200; block 1 is the
 * first of the two $2F00 sets below. (Their accessors are below, with what the
 * sound driver itself loads.) */
enum { SOUND_BOOT_BLOCKS = 6 };

/* Song `song`'s percussion: 12 three-byte entries (an instrument, a note and
 * a pan) for SPC700 address $F120, 36 bytes a song from $C7:1871. A track in
 * percussion mode plays its notes' pitch as an index into them
 * (game/sound/sequencer.c). */
enum { SOUND_PERCUSSION_ENTRIES = 12, SOUND_PERCUSSION_ENTRY_BYTES = 3 };

/* The pitch of each of the twelve semitones of the octave notes are counted
 * from, in 4096ths of a sample's own rate ($1D6D). */
enum { SOUND_SEMITONES = 12 };

/* The fourteen lengths a note can have, in ticks of the song's tempo ($1DB3). */
enum { SOUND_NOTE_LENGTHS = 14 };

/* How deep a vibrato is, in 256ths of the note's pitch ($1D87): the first for
 * music, one chosen by the effect for sound effects. An effect chooses among
 * twelve, and the twelfth is the first byte of the filter presets below, 0,
 * which the driver takes as the whole pitch. */
enum { SOUND_VIBRATO_SCALES = 12 };

/* The four echo filters a song can choose, eight taps each ($1D93). */
enum { SOUND_ECHO_FILTERS = 4, SOUND_ECHO_TAPS = 8 };

/* The driver's own sample: five BRR blocks that loop from the second, which
 * the driver rewrites as it plays ($1DC2). */
enum { SOUND_OWN_SAMPLE_BYTES = 45, SOUND_OWN_SAMPLE_LOOP = 9 };

/* The song's instruments $20 on, the blocks its samples are (a 0 ends them;
 * the driver reads 16 all the same). Blocks are numbered in a byte: the
 * cartridge keeps a word each, its high byte 0. */
enum { SOUND_SONG_INSTRUMENTS = 16, SOUND_FIRST_SONG_INSTRUMENT = 0x20 };

/* The boot's own samples (block 2, to $4700, a length and the bytes) and
 * their directory entries (block 3, to $1E00); the songs' samples go after
 * them. */
enum { SOUND_BOOT_SAMPLES_BLOCK = 2, SOUND_BOOT_DIRECTORY_BLOCK = 3 };

/* The boot's directory: for each of its samples, where it starts in sound
 * memory and where it loops from. */
typedef struct {
    uint16_t start, loop;
} SoundDirectoryEntry;
enum { SOUND_DIRECTORY_ENTRY_BYTES = 4 };

/* A set of sound effects, as it goes to SPC700 $2F00. */
enum { SOUND_EFFECT_SETS = 2 };


/* ---- The sets of sound effects ------------------------------------------------------- */

/* The two sets of sound effects, for SPC700 address $2F00, each a length and
 * the data: the field's (0) at $C7:4309 (also boot block 1), the battle's (1)
 * at $C7:5B0B: a table of the effects' tracks, then the tracks. */
static inline AssetId sound_effect_set(uint8_t set) { return set ? ASSET_SOUND_BATTLE_EFFECTS : ASSET_SOUND_FIELD_EFFECTS; }

/* ---- What the sound driver itself loads (game/sound/) ------------------------------
 *
 * The C sound driver loads the same resources the CPU half uploaded, by what
 * they are rather than where they go: a song's sequence and percussion, the
 * instruments' tunings and envelopes, a set of sound effects, and the tables
 * that are part of the driver's program (boot block 0, read by their SPC700
 * address in it). These take the game's state as the handle to the cartridge
 * (rom.h). */

/* The boot's blocks, each where the program's header places it in its bank,
 * a length word and then the bytes: the driver's program (0), the field's
 * sound effects (1), the samples (2), their directory (3), the instruments'
 * envelopes (4) and tunings (5). */
static inline AssetId sound_boot_block(uint8_t block)
{
    static const AssetId blocks[SOUND_BOOT_BLOCKS] = { ASSET_SOUND_BOOT_PROGRAM,   ASSET_SOUND_FIELD_EFFECTS,
                                                       ASSET_SOUND_BOOT_SAMPLES,   ASSET_SOUND_BOOT_DIRECTORY,
                                                       ASSET_SOUND_BOOT_ENVELOPES, ASSET_SOUND_BOOT_TUNINGS };
    return blocks[block];
}

/* Where the header places a block's bytes, past its length. */
static inline uint16_t sound_boot_block_place(const GameState *g, uint8_t block)
{
    enum { BLOCK_PLACES = 0x09 };   /* in the program's header */
    return (uint16_t)(asset_read16(g, ASSET_SOUND_PROGRAM_BYTE, BLOCK_PLACES + 2u * block) + 2);
}

static inline uint8_t sound_boot_block_at(const GameState *g, uint8_t block, uint16_t i)
{
    return asset_read8_at_place(g, sound_boot_block(block), (uint16_t)(sound_boot_block_place(g, block) + i));
}

static inline uint16_t sound_boot_block_word_at(const GameState *g, uint8_t block, uint16_t i)
{
    return asset_read16_at_place(g, sound_boot_block(block), (uint16_t)(sound_boot_block_place(g, block) + i));
}

/* A byte of the driver's program, boot block 0, by its SPC700 address. */
static inline uint8_t sound_program_byte(const GameState *g, uint16_t spc)
{
    uint16_t destination = asset_read16(g, ASSET_SOUND_PROGRAM_BYTE, 0x15u);
    return sound_boot_block_at(g, 0, (uint16_t)(spc - destination));
}

static inline uint16_t sound_program_word(const GameState *g, uint16_t spc)
{
    return (uint16_t)(sound_program_byte(g, spc) | sound_program_byte(g, (uint16_t)(spc + 1)) << 8);
}

static inline uint16_t sound_semitone_pitch(const GameState *g, uint8_t semitone)
{
    return sound_program_word(g, (uint16_t)(0x1D6D + 2 * semitone));
}

static inline uint8_t sound_note_length(const GameState *g, uint8_t i) { return sound_program_byte(g, (uint16_t)(0x1DB3 + i)); }

static inline uint8_t sound_vibrato_scale(const GameState *g, uint8_t i) { return sound_program_byte(g, (uint16_t)(0x1D87 + i)); }

static inline int8_t sound_echo_filter_tap(const GameState *g, uint8_t filter, uint8_t tap)
{
    return (int8_t)sound_program_byte(g, (uint16_t)(0x1D93 + SOUND_ECHO_TAPS * filter + tap));
}

static inline uint8_t sound_own_sample_byte(const GameState *g, uint8_t i)
{
    return sound_program_byte(g, (uint16_t)(0x1DC2 + i));
}

/* The instruments the boot uploads, their tunings (block 5, to $1F00) and
 * envelopes (block 4, to $1F80): a word each, as many as the tunings' length
 * holds. */
static inline uint8_t sound_boot_instruments(const GameState *g)
{
    return (uint8_t)(sound_boot_block_word_at(g, 5, (uint16_t)-2) / 2);
}

static inline uint16_t sound_boot_instrument_tuning(const GameState *g, uint8_t n)
{
    return sound_boot_block_word_at(g, 5, (uint16_t)(2u * n));
}

static inline uint16_t sound_boot_instrument_envelope(const GameState *g, uint8_t n)
{
    return sound_boot_block_word_at(g, 4, (uint16_t)(2u * n));
}

/* song_ptrs: a song's sequence, as it goes to SPC700 $2000. */
static inline uint32_t sound_song_at(const GameState *g, uint8_t song)
{
    uint32_t at = 3u * song;
    return asset_read16(g, ASSET_SOUND_SONG_AT, at) | (uint32_t)asset_read8(g, ASSET_SOUND_SONG_AT, at + 2) << 16;
}

static inline uint16_t sound_song_length(const GameState *g, uint8_t song) { return rom_read16(g, sound_song_at(g, song)); }

static inline uint8_t sound_song_byte(const GameState *g, uint8_t song, uint16_t i)
{
    uint32_t at = sound_song_at(g, song) + 2;
    return rom_read8(g, (at & 0xFF0000u) + (at & 0xFFFFu) + i);
}

static inline uint8_t sound_song_instrument_block(const GameState *g, uint8_t song, uint8_t i)
{
    return (uint8_t)asset_read16(g, ASSET_SOUND_SONG_INSTRUMENT_BLOCK, 32u * song + 2u * i);
}

/* A block's instrument: its tuning and envelope. The index is the original's,
 * so block 0 (no block) reads the word before each table, as the CPU half
 * sent it. */
static inline uint16_t sound_instrument_tuning(const GameState *g, uint16_t block)
{
    return asset_read16(g, ASSET_SOUND_INSTRUMENT_TUNING, (uint16_t)((block - 1) * 2));
}

static inline uint16_t sound_instrument_envelope(const GameState *g, uint16_t block)
{
    return asset_read16(g, ASSET_SOUND_INSTRUMENT_ENVELOPE, (uint16_t)((block - 1) * 2));
}

static inline uint8_t sound_song_percussion_byte(const GameState *g, uint8_t song, uint8_t i)
{
    return asset_read8(g, ASSET_SOUND_SONG_PERCUSSION_BYTES,
                       (uint32_t)(SOUND_PERCUSSION_ENTRIES * SOUND_PERCUSSION_ENTRY_BYTES * song) + i);
}

/* ---- What the CPU half's commands read (game/sound/loads.c) ------------------------ */

/* How many songs there are; and song `song`'s entry: the echo delay it plays
 * with, which is also how much sound memory it keeps free for the echo at the
 * top (the low nibble of the first byte), and the volume it starts at. */
static inline uint8_t sound_songs(const GameState *g) { return asset_read8(g, ASSET_SOUND_SONGS, 0); }

static inline uint8_t sound_song_echo_delay(const GameState *g, uint8_t song)
{
    return asset_read8(g, ASSET_SOUND_SONG_SETTINGS, 2u * (song & 0x7F)) & 0x0F;
}

static inline uint8_t sound_song_volume(const GameState *g, uint8_t song)
{
    return asset_read8(g, ASSET_SOUND_SONG_SETTINGS, 2u * (song & 0x7F) + 1u);
}

/* Commands $30-$3F stand for four bytes each, a command and its three
 * parameters (`i` 0-3); and the songs whose command saves the song playing
 * first, a list ended by a negative byte. */
static inline uint8_t sound_shortcut_byte(const GameState *g, uint8_t n, uint8_t i)
{
    return asset_read8(g, ASSET_SOUND_SHORTCUT_BYTE, 4u * n + i);
}

static inline uint8_t sound_song_saving_first(const GameState *g, uint16_t i)
{
    return asset_read8(g, ASSET_SOUND_SHORTCUT_BYTE, 0x40u + i);
}

/* A sample block (1-62): its length and BRR bytes, and where in it the sample
 * loops from (sound_block_ptrs and the table after it, as above). */
static inline uint32_t sound_block_data(const GameState *g, uint16_t id)
{
    uint32_t at = (uint16_t)((id - 1) * 3);
    return asset_read16(g, ASSET_SOUND_BLOCK_PTRS, at) | (uint32_t)asset_read8(g, ASSET_SOUND_BLOCK_PTRS, at + 2) << 16;
}

static inline uint16_t sound_block_length(const GameState *g, uint16_t id) { return rom_read16(g, sound_block_data(g, id)); }

static inline uint8_t sound_block_byte(const GameState *g, uint16_t id, uint16_t i)
{
    return rom_read8(g, sound_block_data(g, id) + 2 + i);
}

static inline uint16_t sound_block_loop(const GameState *g, uint16_t id)
{
    return asset_read16(g, ASSET_SOUND_BLOCK_LOOP, (uint16_t)((id - 1) * 2));
}

static inline uint16_t sound_boot_block_to(const GameState *g, uint8_t block)
{
    return asset_read16(g, ASSET_SOUND_PROGRAM_BYTE, 0x15u + 2u * block);
}

static inline uint16_t sound_boot_block_length(const GameState *g, uint8_t block)
{
    return sound_boot_block_word_at(g, block, (uint16_t)-2);
}

static inline uint8_t sound_boot_block_byte(const GameState *g, uint8_t block, uint16_t i)
{
    return sound_boot_block_at(g, block, i);
}

static inline uint8_t sound_boot_directory_entries(const GameState *g)
{
    return (uint8_t)(sound_boot_block_length(g, SOUND_BOOT_DIRECTORY_BLOCK) / SOUND_DIRECTORY_ENTRY_BYTES);
}

static inline SoundDirectoryEntry sound_boot_directory_entry(const GameState *g, uint8_t sample)
{
    uint16_t entry = (uint16_t)(SOUND_DIRECTORY_ENTRY_BYTES * sample);
    return (SoundDirectoryEntry){ sound_boot_block_word_at(g, SOUND_BOOT_DIRECTORY_BLOCK, entry),
                                  sound_boot_block_word_at(g, SOUND_BOOT_DIRECTORY_BLOCK, (uint16_t)(entry + 2)) };
}

static inline uint16_t sound_effect_set_length(const GameState *g, uint8_t set)
{
    return asset_read16(g, sound_effect_set(set), 0);
}

static inline uint8_t sound_effect_set_byte(const GameState *g, uint8_t set, uint16_t i)
{
    return asset_read8(g, sound_effect_set(set), 2u + i);
}

#endif
