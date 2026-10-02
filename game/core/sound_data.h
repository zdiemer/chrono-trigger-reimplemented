/* The sound driver's data: what the CPU half of the driver (sound_driver.c)
 * sends the SPC700, and the tables it looks things up in.
 *
 * Every read the driver makes of it goes through these accessors, named after
 * the resources MD2 typed (docs/MATCHING.md, "Sound blocks": sound_block_ptrs
 * and song_ptrs) or, for the smaller tables typed by hand, after what the
 * driver does with them. A build that loads the data from somewhere other than
 * the ROM image changes this file and nothing in the driver.
 *
 * The resources are located by their address in the ROM, and a resource's
 * bytes are read with sound_data_byte from that address on: the driver keeps
 * its upload position in work RAM as a long pointer
 * (RAM_SOUND_DRIVER_DATA_POINTER), which the game can see, so the address is
 * the one piece of the ROM's layout the driver itself still carries. */
#ifndef CT_GAME_CORE_SOUND_DATA_H
#define CT_GAME_CORE_SOUND_DATA_H

#include "port/machine.h"
#include "rom.h"

/* A byte of any of the resources below, at `at`. */
static inline uint8_t sound_data_byte(Snes *s, uint32_t at) { return m_read8(s, at & 0xFFFFFF); }

/* A resource's 16-bit length prefix: the blocks, the songs, the $2F00 sets and
 * the boot blocks all start with one. */
static inline uint16_t sound_data_length(Snes *s, uint32_t at)
{
    return (uint16_t)(sound_data_byte(s, at) | sound_data_byte(s, at + 1) << 8);
}

/* ---- The boot upload ($C7:0000) -------------------------------------------------- */

/* The SPC700 driver goes up in six blocks: block k is at $C7:(the word at
 * $C7:0010 + 2k), a length and that many bytes, for SPC700 address (the word
 * at $C7:001C + 2k). The driver's code is block 0, at $0200; block 1 is the
 * first of the two $2F00 sets below. `at` is the original's index, 2k. */
enum { SOUND_BOOT_BLOCKS = 6 };
static inline uint32_t sound_boot_block(Snes *s, uint16_t at) { return 0xC70000u | m_read16(s, 0xC70010u + at); }
static inline uint16_t sound_boot_block_destination(Snes *s, uint16_t at) { return m_read16(s, 0xC7001Cu + at); }

/* The length of boot block 2, which goes to SPC700 address $4700: the blocks
 * the songs load are placed after it. Read where the boot reads it, from the
 * block's own length prefix. */
enum { SOUND_BOOT_BLOCK_2_DESTINATION = 0x4700 };
static inline uint16_t sound_boot_block_2_length(Snes *s) { return m_read16(s, 0xC740B4u); }

/* ---- Commands ---------------------------------------------------------------------- */

/* Commands $30-$3F stand for four bytes each, a command and its three
 * parameters, from the table at $C7:0A98 (in this ROM: command $10, song
 * $00-$0F, $FF, $FF). */
static inline uint16_t sound_shortcut_command_word(Snes *s, uint8_t n, uint8_t half)
{
    return m_read16(s, 0xC70A98u + 4u * n + 2u * half);
}

/* The songs whose command gets bit 2 set ($C7:0AD8, ended by a negative byte;
 * empty in this ROM). */
static inline uint8_t sound_marked_song(Snes *s, uint16_t i) { return m_read8(s, 0xC70AD8u + i); }

/* ---- Songs -------------------------------------------------------------------------- */

/* How many songs there are (the byte at $C7:0AE9: 83). */
static inline uint8_t sound_song_count(Snes *s) { return m_read8(s, 0xC70AE9u); }

/* song_ptrs ($C7:0D18, 83 entries): where song `song`'s sequence is, a length
 * and the data, which goes to SPC700 address $2000. */
static inline uint32_t sound_song(Snes *s, uint8_t song)
{
    uint32_t at = 0xC70D18u + 3u * song;
    return m_read16(s, at) | (uint32_t)m_read8(s, at + 2) << 16;
}

/* The 16 block ids song `song` uses ($C7:0E11, 16 words per song; a 0 ends the
 * list). The index is the original's: word `i` past the song's first, which a
 * caller may take outside the song's 16 (the refused-move path starts one song
 * on and counts down). */
static inline uint16_t sound_song_block_id(Snes *s, uint8_t song, int16_t i)
{
    return m_read16(s, 0xC70E11u + 32u * song + (uint32_t)(int32_t)(2 * i));
}

/* Two bytes per song ($C7:241D, indexed by the song's low 7 bits): the low
 * nibble of the first is how much SPC700 memory the song keeps for itself at
 * the top (sound_driver.c, set_reserve), the second a parameter sent with the
 * song's command. */
static inline uint8_t sound_song_reserve(Snes *s, uint8_t song) { return m_read8(s, 0xC7241Du + 2u * (song & 0x7F)); }
static inline uint8_t sound_song_parameter(Snes *s, uint8_t song)
{
    return m_read8(s, 0xC7241Eu + 2u * (song & 0x7F));
}

/* Song `song`'s percussion: 12 three-byte entries (an instrument, a note and
 * a pan) for SPC700 address $F120, 36 bytes a song from $C7:1871. A track in
 * percussion mode plays its notes' pitch as an index into them
 * (game/sound/sequencer.c). */
enum { SOUND_PERCUSSION_ENTRIES = 12, SOUND_PERCUSSION_ENTRY_BYTES = 3 };
static inline uint32_t sound_song_percussion_table(void) { return 0xC71871u; }

/* ---- Blocks ------------------------------------------------------------------------- */

/* sound_block_ptrs ($C7:0AEA, 62 entries): where block `id` (1-62) is, a length
 * and the data, which goes into one of the SPC700 memory slots. The index is
 * the original's 16-bit (id - 1) * 3, so an id of 0 reads before the table as
 * the original does. */
static inline uint32_t sound_block(Snes *s, uint16_t id)
{
    uint32_t at = 0xC70AEAu + (uint16_t)((id - 1) * 3);
    return m_read16(s, at) | (uint32_t)m_read8(s, at + 2) << 16;
}

/* One word per block id, from three tables that follow sound_block_ptrs: an
 * offset into the block that the driver adds to its slot's address for the
 * second word of its $1E80 entry, where the sample loops from ($C7:0BA4); and
 * the instrument the block's sample is, its tuning ($C7:0C20, to the SPC700's
 * table at $1F40) and its ADSR envelope ($C7:0C9C, to $1FC0). */
static inline uint16_t sound_block_address_offset(Snes *s, uint16_t id)
{
    return m_read16(s, 0xC70BA4u + (uint16_t)((id - 1) * 2));
}
static inline uint16_t sound_block_tuning(Snes *s, uint16_t id)
{
    return m_read16(s, 0xC70C20u + (uint16_t)((id - 1) * 2));
}
static inline uint16_t sound_block_envelope(Snes *s, uint16_t id)
{
    return m_read16(s, 0xC70C9Cu + (uint16_t)((id - 1) * 2));
}

/* ---- The $2F00 sets ----------------------------------------------------------------- */

/* The two data sets for SPC700 address $2F00, each a length and the data: set 0
 * at $C7:4309 (also boot block 1), set 1 at $C7:5B0B. They are the sound
 * effects (game/sound/): a table of the effects' tracks, then the tracks. */
static inline uint32_t sound_2f00_set(uint8_t set) { return set ? 0xC75B0Bu : 0xC74309u; }

/* ---- What the sound driver itself loads (game/sound/) ------------------------------
 *
 * The C sound driver loads the same resources the CPU half uploads, by what
 * they are rather than where they go: a song's sequence and percussion, the
 * instruments' tunings and envelopes, a set of sound effects, and the tables
 * that are part of the driver's program (boot block 0, read by their SPC700
 * address in it). These take the game's state as the handle to the cartridge
 * (rom.h). */

/* A byte of the driver's program, boot block 0, by its SPC700 address. */
static inline uint8_t sound_program_byte(const GameState *g, uint16_t spc)
{
    uint32_t block = 0xC70000u | rom_read16(g, 0xC70010u);
    uint16_t destination = rom_read16(g, 0xC7001Cu);
    return rom_read8(g, block + 2 + (uint16_t)(spc - destination));
}
static inline uint16_t sound_program_word(const GameState *g, uint16_t spc)
{
    return (uint16_t)(sound_program_byte(g, spc) | sound_program_byte(g, (uint16_t)(spc + 1)) << 8);
}

/* The pitch of each of the twelve semitones of the octave notes are counted
 * from, in 4096ths of a sample's own rate ($1D6D). */
enum { SOUND_SEMITONES = 12 };
static inline uint16_t sound_semitone_pitch(const GameState *g, uint8_t semitone)
{
    return sound_program_word(g, (uint16_t)(0x1D6D + 2 * semitone));
}

/* The fourteen lengths a note can have, in ticks of the song's tempo ($1DB3). */
enum { SOUND_NOTE_LENGTHS = 14 };
static inline uint8_t sound_note_length(const GameState *g, uint8_t i) { return sound_program_byte(g, (uint16_t)(0x1DB3 + i)); }

/* How deep a vibrato is, in 256ths of the note's pitch ($1D87): the first for
 * music, one chosen by the effect for sound effects. An effect chooses among
 * twelve, and the twelfth is the first byte of the filter presets below, 0,
 * which the driver takes as the whole pitch. */
enum { SOUND_VIBRATO_SCALES = 12 };
static inline uint8_t sound_vibrato_scale(const GameState *g, uint8_t i) { return sound_program_byte(g, (uint16_t)(0x1D87 + i)); }

/* The four echo filters a song can choose, eight taps each ($1D93). */
enum { SOUND_ECHO_FILTERS = 4, SOUND_ECHO_TAPS = 8 };
static inline int8_t sound_echo_filter_tap(const GameState *g, uint8_t filter, uint8_t tap)
{
    return (int8_t)sound_program_byte(g, (uint16_t)(0x1D93 + SOUND_ECHO_TAPS * filter + tap));
}

/* The driver's own sample: five BRR blocks that loop from the second, which
 * the driver rewrites as it plays ($1DC2). */
enum { SOUND_OWN_SAMPLE_BYTES = 45, SOUND_OWN_SAMPLE_LOOP = 9 };
static inline uint8_t sound_own_sample_byte(const GameState *g, uint8_t i)
{
    return sound_program_byte(g, (uint16_t)(0x1DC2 + i));
}

/* The instruments the boot uploads, their tunings (block 5, to $1F00) and
 * envelopes (block 4, to $1F80): a word each. */
static inline uint32_t sound_boot_block_data(const GameState *g, uint8_t block)
{
    return (0xC70000u | rom_read16(g, (uint32_t)(0xC70010u + 2 * block))) + 2;
}
static inline uint8_t sound_boot_instruments(const GameState *g)
{
    return (uint8_t)(rom_read16(g, 0xC70000u | rom_read16(g, 0xC7001Au)) / 2);
}
static inline uint16_t sound_boot_instrument_tuning(const GameState *g, uint8_t n)
{
    return rom_read16(g, sound_boot_block_data(g, 5) + 2u * n);
}
static inline uint16_t sound_boot_instrument_envelope(const GameState *g, uint8_t n)
{
    return rom_read16(g, sound_boot_block_data(g, 4) + 2u * n);
}

/* song_ptrs: a song's sequence, as it goes to SPC700 $2000. */
static inline uint32_t sound_song_at(const GameState *g, uint8_t song)
{
    uint32_t at = 0xC70D18u + 3u * song;
    return rom_read16(g, at) | (uint32_t)rom_read8(g, at + 2) << 16;
}
static inline uint16_t sound_song_length(const GameState *g, uint8_t song) { return rom_read16(g, sound_song_at(g, song)); }
static inline uint8_t sound_song_byte(const GameState *g, uint8_t song, uint16_t i)
{
    uint32_t at = sound_song_at(g, song) + 2;
    return rom_read8(g, (at & 0xFF0000u) + (at & 0xFFFFu) + i);
}

/* The song's instruments $20 on, the blocks its samples are (a 0 ends them;
 * the driver reads 16 all the same). */
enum { SOUND_SONG_INSTRUMENTS = 16, SOUND_FIRST_SONG_INSTRUMENT = 0x20 };
static inline uint16_t sound_song_instrument_block(const GameState *g, uint8_t song, uint8_t i)
{
    return rom_read16(g, 0xC70E11u + 32u * song + 2u * i);
}
/* A block's instrument: its tuning and envelope. The index is the original's,
 * so block 0 (no block) reads the word before each table, as the CPU half
 * sends it. */
static inline uint16_t sound_instrument_tuning(const GameState *g, uint16_t block)
{
    return rom_read16(g, 0xC70C20u + (uint16_t)((block - 1) * 2));
}
static inline uint16_t sound_instrument_envelope(const GameState *g, uint16_t block)
{
    return rom_read16(g, 0xC70C9Cu + (uint16_t)((block - 1) * 2));
}

static inline uint8_t sound_song_percussion_byte(const GameState *g, uint8_t song, uint8_t i)
{
    return rom_read8(g, sound_song_percussion_table() + SOUND_PERCUSSION_ENTRIES * SOUND_PERCUSSION_ENTRY_BYTES * song + i);
}

/* ---- What the CPU half's commands read (game/sound/loads.c) ------------------------ */

/* How many songs there are; and song `song`'s entry: the echo delay it plays
 * with, which is also how much sound memory it keeps free for the echo at the
 * top (the low nibble of the first byte), and the volume it starts at. */
static inline uint8_t sound_songs(const GameState *g) { return rom_read8(g, 0xC70AE9u); }
static inline uint8_t sound_song_echo_delay(const GameState *g, uint8_t song)
{
    return rom_read8(g, 0xC7241Du + 2u * (song & 0x7F)) & 0x0F;
}
static inline uint8_t sound_song_volume(const GameState *g, uint8_t song) { return rom_read8(g, 0xC7241Eu + 2u * (song & 0x7F)); }

/* Commands $30-$3F stand for four bytes each, a command and its three
 * parameters (`i` 0-3); and the songs whose command saves the song playing
 * first, a list ended by a negative byte. */
static inline uint8_t sound_shortcut_byte(const GameState *g, uint8_t n, uint8_t i)
{
    return rom_read8(g, 0xC70A98u + 4u * n + i);
}
static inline uint8_t sound_song_saving_first(const GameState *g, uint16_t i) { return rom_read8(g, 0xC70AD8u + i); }

/* A sample block (1-62): its length and BRR bytes, and where in it the sample
 * loops from (sound_block_ptrs and the table after it, as above). */
static inline uint32_t sound_block_data(const GameState *g, uint16_t id)
{
    uint32_t at = 0xC70AEAu + (uint16_t)((id - 1) * 3);
    return rom_read16(g, at) | (uint32_t)rom_read8(g, at + 2) << 16;
}
static inline uint16_t sound_block_length(const GameState *g, uint16_t id) { return rom_read16(g, sound_block_data(g, id)); }
static inline uint8_t sound_block_byte(const GameState *g, uint16_t id, uint16_t i)
{
    return rom_read8(g, sound_block_data(g, id) + 2 + i);
}
static inline uint16_t sound_block_loop(const GameState *g, uint16_t id)
{
    return rom_read16(g, 0xC70BA4u + (uint16_t)((id - 1) * 2));
}

/* The boot's own samples (block 2, to $4700, a length and the bytes) and
 * their directory entries (block 3, to $1E00); the songs' samples go after
 * them. */
enum { SOUND_BOOT_SAMPLES_BLOCK = 2, SOUND_BOOT_DIRECTORY_BLOCK = 3 };
static inline uint16_t sound_boot_block_to(const GameState *g, uint8_t block)
{
    return rom_read16(g, 0xC7001Cu + 2u * block);
}
static inline uint16_t sound_boot_block_length(const GameState *g, uint8_t block)
{
    return rom_read16(g, sound_boot_block_data(g, block) - 2);
}
static inline uint8_t sound_boot_block_byte(const GameState *g, uint8_t block, uint16_t i)
{
    return rom_read8(g, sound_boot_block_data(g, block) + i);
}

/* The boot's directory: for each of its samples, where it starts in sound
 * memory and where it loops from. */
typedef struct {
    uint16_t start, loop;
} SoundDirectoryEntry;
enum { SOUND_DIRECTORY_ENTRY_BYTES = 4 };
static inline uint8_t sound_boot_directory_entries(const GameState *g)
{
    return (uint8_t)(sound_boot_block_length(g, SOUND_BOOT_DIRECTORY_BLOCK) / SOUND_DIRECTORY_ENTRY_BYTES);
}
static inline SoundDirectoryEntry sound_boot_directory_entry(const GameState *g, uint8_t sample)
{
    uint32_t entry = sound_boot_block_data(g, SOUND_BOOT_DIRECTORY_BLOCK) + SOUND_DIRECTORY_ENTRY_BYTES * sample;
    return (SoundDirectoryEntry){ rom_read16(g, entry), rom_read16(g, entry + 2) };
}

/* A set of sound effects, as it goes to SPC700 $2F00. */
enum { SOUND_EFFECT_SETS = 2 };
static inline uint16_t sound_effect_set_length(const GameState *g, uint8_t set) { return rom_read16(g, sound_2f00_set(set)); }
static inline uint8_t sound_effect_set_byte(const GameState *g, uint8_t set, uint16_t i)
{
    return rom_read8(g, sound_2f00_set(set) + 2 + i);
}

#endif
