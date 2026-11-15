/* The code that runs from fast RAM (IWRAM) rather than the cartridge
 * (targets/gba/toolchain/gba.ld; docs/PLATFORM.md, "The GBA build, as built"): the
 * functions with the most of the GBA's work a byte, sampled with
 * `run.py --profile` over title-start, new-game, gato-battle,
 * events-jetbike-race and menus-sweep, as many as fast RAM has room for
 * beside its own sections and the stacks, with a little to spare (one GCC
 * lays the code out larger than another). Names are the optimized build's (a
 * suffix after the name is the compiler's copy of it); a name the build no
 * longer has places nothing. */
*(.text.charge_time_as_begin .text.charge_time_as_begin.*)
*(.text.charge_time_as_end .text.charge_time_as_end.*)
*(.text.state_host_read8 .text.state_host_read8.*)
*(.text.state_host_write8 .text.state_host_write8.lto_priv.*)   /* not a .constprop copy: its callers are the scenes, in the cartridge */
*(.text.price_accesses .text.price_accesses.*)
*(.text.note_if_unread .text.note_if_unread.*)
*(.text.read_game .text.read_game.*)
*(.text.place_of .text.place_of.*)
*(.text.vars_host_read_span .text.vars_host_read_span.*)
*(.text.rom_read8 .text.rom_read8.*)
*(.text.vram_take .text.vram_take.*)
*(.text.asset_read8 .text.asset_read8.*)
*(.text.served .text.served.*)
*(.text.resource_bytes .text.resource_bytes.*)
*libgcc.a:_udivsi3.o(.text)
*libc.a:libc_a-memcmp.o(.text)
/* The picture's line effects, a frame's every transfer (services/video.c,
 * compiled as ARM there) */
*(.text.next_transfer .text.next_transfer.*)
*(.text.table_unit .text.table_unit.*)
*(.text.line_bytes .text.line_bytes.*)
*(.text.slot_entry .text.slot_entry.*)
