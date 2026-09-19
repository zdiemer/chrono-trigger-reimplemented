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

/* Song `song`'s 12 three-byte entries for SPC700 address $F120, 36 bytes a
 * song from $C7:1871. What the SPC700 driver does with them isn't
 * established. */
enum { SOUND_SONG_F120_ENTRIES = 12 };
static inline uint32_t sound_song_f120_table(void) { return 0xC71871u; }

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
 * second word of its $1E80 entry ($C7:0BA4), and the words it sends to the
 * SPC700's tables at $1F40 ($C7:0C20) and $1FC0 ($C7:0C9C). What the SPC700
 * driver does with them isn't established. */
static inline uint16_t sound_block_address_offset(Snes *s, uint16_t id)
{
    return m_read16(s, 0xC70BA4u + (uint16_t)((id - 1) * 2));
}
static inline uint16_t sound_block_1f40_word(Snes *s, uint16_t id)
{
    return m_read16(s, 0xC70C20u + (uint16_t)((id - 1) * 2));
}
static inline uint16_t sound_block_1fc0_word(Snes *s, uint16_t id)
{
    return m_read16(s, 0xC70C9Cu + (uint16_t)((id - 1) * 2));
}

/* ---- The $2F00 sets ----------------------------------------------------------------- */

/* The two data sets for SPC700 address $2F00, each a length and the data: set 0
 * at $C7:4309 (also boot block 1), set 1 at $C7:5B0B. */
static inline uint32_t sound_2f00_set(uint8_t set) { return set ? 0xC75B0Bu : 0xC74309u; }

#endif
