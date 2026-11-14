/* The code that runs from fast RAM (IWRAM) rather than the cartridge
 * (targets/gba/toolchain/gba.ld; docs/PLATFORM.md, "The GBA build, as built"): the
 * functions with the most of the GBA's work a byte, sampled with
 * `run.py --profile` over new-game and gato-battle. Names are the
 * optimized build's (a suffix after the name is the compiler's copy of it);
 * a name the build no longer has places nothing. */
*(.text.charge_time_as_begin .text.charge_time_as_begin.*)
*(.text.charge_time_as_end .text.charge_time_as_end.*)
*(.text.charge_as .text.charge_as.*)
*(.text.state_host_read8 .text.state_host_read8.*)
*(.text.state_host_write8 .text.state_host_write8.*)
*(.text.row_of .text.row_of.*)
*(.text.note_if_unread .text.note_if_unread.*)
*(.text.event_script_byte .text.event_script_byte.*)
*(.text.event_object_count .text.event_object_count.*)
*(.text.read_game .text.read_game.*)
*(.text.pack_resource .text.pack_resource.*)
*(.text.served .text.served.*)
*(.text.rom_read8 .text.rom_read8.*)
*(.text.asset_containing .text.asset_containing.*)
*(.text.asset_read8 .text.asset_read8.*)
*(.text.cartridge_address .text.cartridge_address.*)
*libgcc.a:_muldi3.o(.text .text.*)
*libgcc.a:_udivsi3.o(.text)
*libc.a:libc_a-memcpy-stub.o(.text)
*libc.a:libc_a-memset.o(.text)
*libc.a:libc_a-memcmp.o(.text)
