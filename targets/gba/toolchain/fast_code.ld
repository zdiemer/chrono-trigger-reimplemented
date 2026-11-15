/* The code that runs from fast RAM (IWRAM) rather than the cartridge
 * (targets/gba/toolchain/gba.ld; docs/PLATFORM.md, "The GBA build, as built"): the
 * functions with the most of the GBA's work a byte, sampled with
 * `run.py --profile` over new-game and gato-battle, as many as fast RAM has
 * room for beside its own sections and the stacks, with a few hundred bytes
 * to spare (one GCC lays the code out larger than another). Names are the
 * optimized build's (a suffix after the name is the compiler's copy of it);
 * a name the build no longer has places nothing. */
*(.text.charge_time_as_begin .text.charge_time_as_begin.*)
*(.text.charge_time_as_end .text.charge_time_as_end.*)
*(.text.state_host_read8 .text.state_host_read8.*)
*(.text.state_host_write8 .text.state_host_write8.*)
*(.text.row_of .text.row_of.*)
*(.text.note_if_unread .text.note_if_unread.*)
*(.text.read_game .text.read_game.*)
*(.text.rom_read8 .text.rom_read8.*)
*(.text.asset_containing .text.asset_containing.*)
*(.text.asset_read8 .text.asset_read8.*)
*(.text.cartridge_address .text.cartridge_address.*)
*libgcc.a:_udivsi3.o(.text)
*libc.a:libc_a-memcmp.o(.text)
