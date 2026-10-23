/* Scenes: starting a scene program, and copying its resources.
 *
 * The field starts a scene -- the race, an ending -- and the game comes back
 * to the title the same way: by jumping to $C3:0000 with the scene's flags in
 * A (scene.h). The boot points the interrupts at its own handlers, which keep
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
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/scene_data.h"
#include "vars.h"
#include "wram.h"

enum { LINE_IRQ_LINE = 0xD3 };   /* the line the title's line interrupt fires on: the picture's last */
enum { FULL_BRIGHTNESS = 15 };

/* Where the program runs, in the first bank of work RAM. */
enum { PROGRAM_START = 0x7E3000, PROGRAM_ADDRESS = 0x3000 };

/* The handlers the boot installs until the program installs its own. */
enum { BOOT_NMI = 0xC30548, BOOT_IRQ = 0xC30529 };

/* An interrupt stub is a jump to its handler: installing a handler writes the
 * jump and the handler's address. */
enum { JUMP_LONG = 0x5C };

static void install_nmi_handler(GameState *g, uint32_t handler)
{
    set_scene_nmi_stub_jump(g, JUMP_LONG);
    set_scene_nmi_stub_handler(g, handler);
}

static void install_irq_handler(GameState *g, uint32_t handler)
{
    set_scene_irq_stub_jump(g, JUMP_LONG);
    set_scene_irq_stub_handler(g, handler);
}

// $C3:0000
/* Point the interrupts at the boot's handlers, keep the scene's flags, start
 * the line interrupt where the scene uses it, and set the decompressor on the
 * program. True where the scene uses the line interrupt, and interrupts end up
 * enabled. */
static bool boot(GameState *g, Platform *pf, uint8_t flags)
{
    install_nmi_handler(g, BOOT_NMI);
    install_irq_handler(g, BOOT_IRQ);
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

/* The stubs, the flags and the decompressor's cells are reached through the
 * data bank. */
bool scene_boot_bank_reaches_io(Snes *s) { return m_bank_reaches_io(s); }

/* Start the scene program, the flags in A. The decompressor is a call-out, and
 * the port stops at the program's first instruction. */
void scene_boot(Snes *s)
{
    m_set_flag(s, CPU_FLAG_I, true);   /* SEI */
    bool line_irq = boot(state_of(s), m_platform(s), (uint8_t)s->cpu.a);
    m_set_flag(s, CPU_FLAG_I, !line_irq);   /* interrupts on where the scene uses the line interrupt */
    m_jsl_dc(s, 0xC3006F, 0xC30557, DC_A | DC_X | DC_N | DC_Z | DC_C);
    m_stop_at(s, PROGRAM_START, DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C);
}

/* The program unpacked into work RAM, as C (the decompressor's call from the
 * boot, $C3:006F): the program's code isn't run, but its data -- the scripts,
 * the sprite lists, the tables -- is what the C reads. */
enum { THE_DECOMPRESSOR = 0xC30557 };

bool scene_boots(GameState *g, Platform *pf, uint8_t flags)
{
    bool line_irq = boot(g, pf, flags);
    TIMED_AS(g, THE_DECOMPRESSOR, (void)unpack_stream(g));
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

void scene_boot_nmi(Snes *s)
{
    boot_vblank(m_platform(s));
    m_rti(s);
}

// $C3:0529
/* The boot's line interrupt: acknowledge it, then blank the screen at the
 * start of the next hblank. The two waits for the beam are the original's
 * loops, idle entries, and the blank the entry after them (docs/TODO.md). The
 * handler saves A's low byte, which is on the stack across the waits. */
void scene_boot_line_irq(Snes *s)
{
    m_set_flag(s, CPU_FLAG_M, true);
    m_push8(s, (uint8_t)s->cpu.a);
    /* A holds the acknowledge's status until the wait reloads it; its other
     * bits are open bus, which the original reads as the load's last operand
     * byte, 0 */
    uint8_t ack = tm_ack_line_irq(m_platform(s), PF_AT(0xC3052C));
    m_set_a8(s, ack & TM_LINE_IRQ_PENDING);
    m_stop_at(s, 0xC30530, DC_N | DC_Z);
}

/* The status the waits load into A: bits 1-5 are open bus, which the original
 * reads as the load's last operand byte, 0. The load sets N, and the test of
 * hblank Z. */
enum { STATUS_BITS = TM_SNES_IN_VBLANK | TM_SNES_IN_HBLANK | TM_SNES_JOYPAD_BUSY };
static void load_status(Snes *s, uint8_t status)
{
    m_set_a8(s, status & STATUS_BITS);
    m_set_flag(s, CPU_FLAG_N, status & TM_SNES_IN_VBLANK);
    m_set_flag(s, CPU_FLAG_Z, !(status & TM_SNES_IN_HBLANK));
}

// $C3:0530
/* One pass of the wait for the current hblank to end. Idle entry. */
void scene_boot_line_irq_wait_hblank_end_loop(Snes *s)
{
    uint8_t status = tm_snes_video_status(m_platform(s), PF_AT(0xC30530));
    load_status(s, status);
    if (!(status & TM_SNES_IN_HBLANK))
        s->cpu.pc = 0x0538;
}

// $C3:0538
/* One pass of the wait for the next hblank to start. Idle entry. */
void scene_boot_line_irq_wait_hblank_loop(Snes *s)
{
    uint8_t status = tm_snes_video_status(m_platform(s), PF_AT(0xC30538));
    load_status(s, status);
    if (status & TM_SNES_IN_HBLANK)
        s->cpu.pc = 0x0540;
}

/* The line interrupt as C: acknowledged, and the rest of the picture blank
 * from the line it came on (a host has no beam to wait for the hblank on). */
void scene_boot_line(GameState *g, Platform *pf)
{
    (void)g;
    (void)tm_ack_line_irq(pf, PF_AT(0xC3052C));
    vid_set_display(pf, PF_AT(0xC30542), &(VidDisplay){ .blank = true });
}

// $C3:0540
/* In hblank: blank the screen, A's low byte back, and return. */
void scene_boot_line_irq_blank(Snes *s)
{
    vid_set_display(m_platform(s), PF_AT(0xC30542), &(VidDisplay){ .blank = true });
    m_set_a8(s, m_pull8(s));
    m_rti(s);
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

/* A + 1 bytes from X in bank $FE to Y; of what the block move leaves, the
 * callers read only Y, past the bytes. */
static void copy_resource_entry(Snes *s, uint8_t bank)
{
    uint16_t from = s->cpu.x, to = s->cpu.y;
    uint32_t count = (uint32_t)s->cpu.a + 1;
    scene_copy_resource_bytes(state_of(s), bank, from, to, count);
    s->cpu.y = (uint16_t)(to + count);
    m_rtl(s);
}

/* A resource into the first bank of work RAM. */
void scene_copy_resource(Snes *s) { copy_resource_entry(s, SCENE_LOWER_WORK_RAM); }

/* A resource into the second bank of work RAM. */
void scene_copy_resource_to_upper_ram(Snes *s) { copy_resource_entry(s, SCENE_UPPER_WORK_RAM); }

/* ---- Resources unpacked ------------------------------------------------------------------------ */

/* The decompressor, run as C from the unpacker's call; it reads none of the
 * registers. */
enum { UNPACK_CALL = 0x7E36A4, DECOMPRESSOR = 0xC30557, THE_DECOMPRESSOR_READS_NOTHING = DC_A | DC_X | DC_N | DC_Z | DC_C };

// $7E:3691
/* One of the program's resources unpacked as C, by its place in the
 * resource table, to where the caller has set the unpacker's destination. */
void scene_unpacks(GameState *g, uint16_t resource)
{
    set_decompress_source(g, scene_resource_stream(g, resource));
    TIMED_AS(g, THE_DECOMPRESSOR, (void)unpack_stream(g));
}

/* One of the program's resources unpacked, by its place in the resource table
 * (a byte offset, in X), to where the caller has set the unpacker's
 * destination. It keeps its caller's flags on the stack around the
 * decompressor, and leaves in A what the last of its loads had there: the
 * stream's address past its first byte. */
void scene_unpack_resource(Snes *s)
{
    GameState *g = state_of(s);
    uint32_t stream = scene_resource_stream(g, s->cpu.x);
    set_decompress_source(g, stream);
    m_push8(s, s->cpu.p);
    m_set_flag(s, CPU_FLAG_M, false);
    s->cpu.a = (uint16_t)(stream >> 8);
    if (!scene_run_long(s, UNPACK_CALL, DECOMPRESSOR, THE_DECOMPRESSOR_READS_NOTHING, decompress))
        return;
    s->cpu.p = m_pull8(s);
    m_rts(s);
}

bool scene_unpack_present(Snes *s) { return m_code_matches(s, 0x7E3691, 25, 0xF55FAA2BAD9EB17Aull); }
