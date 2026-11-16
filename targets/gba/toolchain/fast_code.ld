/* The code that runs from fast RAM (IWRAM) rather than the cartridge
 * (targets/gba/toolchain/gba.ld; docs/PLATFORM.md, "The GBA build, as built"): the
 * functions with the most of the GBA's work a byte, sampled with
 * `run.py --profile` over title-start, new-game, gato-battle,
 * events-jetbike-race and menus-sweep, as many as fast RAM has room for
 * beside its own sections and the stacks, with a little to spare (one GCC
 * lays the code out larger than another). Names are the optimized build's (a
 * suffix after the name is the compiler's copy of it); a name the build no
 * longer has places nothing. */
*(.text.state_host_read8 .text.state_host_read8.*)
*(.text.state_host_write8 .text.state_host_write8.lto_priv.*)   /* not a .constprop copy: its callers are the scenes, in the cartridge */
*(.text.note_if_unread .text.note_if_unread.*)
*(.text.read_game .text.read_game.*)
*(.text.place_of .text.place_of.*)
*(.text.vars_host_read_span .text.vars_host_read_span.*)
*(.text.rom_read8 .text.rom_read8.*)
*(.text.vram_take .text.vram_take.*)
*(.text.asset_read8 .text.asset_read8.*)
*(.text.served .text.served.*)
*(.text.resource_bytes .text.resource_bytes.*)
*libc.a:libc_a-memcmp.o(.text)
/* The picture's line effects, a frame's every transfer (services/video.c,
 * compiled as ARM there) */
*(.text.next_transfer .text.next_transfer.*)
*(.text.table_unit .text.table_unit.*)
*(.text.line_bytes .text.line_bytes.*)
*(.text.slot_entry .text.slot_entry.*)
/* With the fibers' stacks sized from the movie library's deepest use
 * (system/fiber.c): the next functions by the GBA's work a byte */
*(.text.vars_host_fill_span .text.vars_host_fill_span.*)
*(.text.sprite_table_set_byte .text.sprite_table_set_byte.*)
*(.text.vid_send_palette .text.vid_send_palette.*)
*(.text.cycles_now .text.cycles_now.*)
*(.text.w8 .text.w8.*)
*(.text.offset_row .text.offset_row.*)
*(.text.r16 .text.r16.*)
*(.text.load_sprite_list .text.load_sprite_list.*)
*(.text.wram_copy .text.wram_copy.*)
*(.text.plane_line .text.plane_line.*)
*(.text.vid_send_vram_fill .text.vid_send_vram_fill.*)
*(.text.next_of .text.next_of.*)
*(.text.event_script_byte .text.event_script_byte.*)
*(.text.r8 .text.r8.*)
/* A packed stream the cartridge keeps unpacked goes into work RAM in one
 * copy (game/data/decompress.c, put_unpacked; host/streams.py): memcpy's
 * where it can, and a byte at a time into the cold top's SRAM, where a
 * menu's load unpacks its graphics. The unpacker itself, which the streams
 * kept unpacked leave little to do, runs from the cartridge. */
*(.text.put_span .text.put_span.*)
/* A layout's work, which the picture stops for (services/video.c, ARM): a
 * scene's first picture converts its maps' thousands of entries and names
 * their tiles. */
*(.text.name_map_tiles .text.name_map_tiles.*)
*(.text.note_priorities .text.note_priorities.*)
*(.text.convert_tile .text.convert_tile.*)
*(.text.convert_map_units .text.convert_map_units.*)
