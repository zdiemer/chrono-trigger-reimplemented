#include "core/program.h"
/* Scenes: starting a scene program, and copying its resources.
 *
 * The field starts a scene -- the race, an ending -- and the game comes back
 * to the title the same way: by starting $C3:0000 with the scene's flags
 * (scene.h). The boot points the interrupts at its own handlers, which keep
 * the screen on until the program installs its frame's work, starts the line
 * interrupt for a scene that uses it, and decompresses the program into work
 * RAM, where it runs.
 *
 * A program loads its resources from the table in bank $FE, compressed or not;
 * the uncompressed ones are copied from the bank as they are. */
#include "scene/scene.h"

#include "charge.h"
#include "data/decompress.h"

#include "platform/timing.h"
#include "platform/video.h"
#include "port/vars_ram.h"
#include "scene/scene_data.h"
#include "vars.h"
#include "wram.h"

enum { LINE_IRQ_LINE = 0xD3 };   /* the line the title's line interrupt fires on: the picture's last */
enum { FULL_BRIGHTNESS = 15 };

/* Where the program runs, in the first bank of work RAM. */
enum { PROGRAM_START = 0x7E3000, PROGRAM_ADDRESS = 0x3000 };
// $C3:0000
/* Point the interrupts at the boot's handlers, keep the scene's flags, start
 * the line interrupt where the scene uses it, and set the decompressor on the
 * program. True where the scene uses the line interrupt, and interrupts end up
 * enabled. */
static bool boot(GameState *g, Platform *pf, uint8_t flags)
{
    program_install_vblank_handler(g, scene_boot_vblank);
    charge_legacy_accesses(g, 4);
    program_install_line_handler(g, scene_boot_line);
    charge_legacy_accesses(g, 4);
    tm_enable_interrupts(pf, PF_AT(0xC30034), TM_INT_AUTO_JOYPAD);
    set_scene_flags(g, flags);
    bool line_irq = flags & SCENE_FLAG_TITLE_WITH_LINE_IRQ;
    if (line_irq) {
        vid_set_display(pf, PF_AT(0xC30041), &(VidDisplay){ .brightness = FULL_BRIGHTNESS });
        vid_enable_line_effects(pf, PF_AT(0xC30044), 0);
        tm_enable_interrupts(pf, PF_AT(0xC30049), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD);
        tm_set_line(pf, PF_AT(0xC3004F), LINE_IRQ_LINE);
        tm_ack_line_irq(pf, PF_AT(0xC30052));
    }
    set_decompress_dest(g, PROGRAM_ADDRESS);   /* with a bank byte of 0, as the original writes it */
    set_decompress_source(g, (uint32_t)SCENE_PROGRAM_STREAM_BANK << 16 | scene_program_stream_offset(g));
    return line_irq;
}

/* The program unpacked into work RAM (the decompressor's call from the boot,
 * $C3:006F): the program's code isn't run, but its data -- the scripts, the
 * sprite lists, the tables -- is what the port reads. */
bool scene_boots(GameState *g, Platform *pf, uint8_t flags)
{
    bool line_irq = boot(g, pf, flags);
    (void)unpack_stream_direct(g);
    return line_irq;
}

/* ---- The boot's interrupt handlers ---------------------------------------------------------- */

// $C3:0548
/* The boot's NMI: acknowledge it, and the screen on at full brightness. */
static void boot_vblank(Platform *pf)
{
    tm_ack_vblank(pf, PF_AT(0xC3054B));
    vid_set_display(pf, PF_AT(0xC30551), &(VidDisplay){ .brightness = FULL_BRIGHTNESS });
}

void scene_boot_vblank(GameState *g, Platform *pf)
{
    (void)g;
    boot_vblank(pf);
}

/* The line interrupt: acknowledged, and the rest of the picture blank
 * below the line it came on (a host has no beam to wait for the hblank on, so
 * the blank is the line handler's, not the whole frame's). */
void scene_boot_line(GameState *g, Platform *pf)
{
    (void)g;
    (void)tm_ack_line_irq(pf, PF_AT(0xC3052C));
    vid_raster_display(pf, PF_AT(0xC30542), VID_AT_LINE_HANDLER, &(VidDisplay){ .blank = true });
}

/* ---- Resources copied as they are ------------------------------------------------------------ */

// $C3:099C, $C3:09A0
/* `count` bytes of a resource in bank $FE from `from` to `to`, a byte at a time
 * forward. */
void scene_copy_resource_bytes(GameState *g, uint8_t bank, uint16_t from, uint16_t to, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        wram_write8(g, (uint32_t)bank << 16 | (uint16_t)(to + i), scene_data_byte(g, (uint16_t)(from + i)));
}

/* ---- Resources unpacked ------------------------------------------------------------------------ */

// $7E:3691
/* One of the program's resources unpacked, by its place in the
 * resource table, to where the caller has set the unpacker's destination. */
void scene_unpacks(GameState *g, uint16_t resource)
{
    set_decompress_source(g, scene_resource_stream(g, resource));
    (void)unpack_stream_direct(g);
}
