/* Scenes: starting a scene program, and the copies it makes out of bank $FE.
 *
 * The field and the game over start a scene by jumping to $C3:0000 with the
 * scene's mode bits in A: the boot points the interrupt stubs at bank C3's
 * handlers, decompresses the program to $7E:3000 and jumps there. */
#include "scene/scene.h"

#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "ram.h"
#include "scene/scene_data.h"

/* RAM_SCENE_FLAGS bit 7: the scene uses the line interrupt, which the boot
 * starts at LINE_IRQ_LINE, and leaves interrupts enabled while the NMI works. */
enum { SCENE_FLAG_LINE_IRQ = 0x80 };
enum { LINE_IRQ_LINE = 0xD3 };
enum { FULL_BRIGHTNESS = 15 };

/* Where the program runs, and the decompressor's cells (the page at $0300). */
enum { PROGRAM_START = 0x7E3000 };
enum { PROGRAM_ADDRESS = 0x3000 };

/* The stubs at $0500 (NMI) and $0504 (IRQ) are a JML and its 24-bit target. */
enum { JML = 0x5C };
static void set_stub(Snes *s, uint16_t stub, uint32_t target)
{
    m_write8(s, m_abs(s, stub), JML);
    m_write16(s, m_abs(s, (uint16_t)(stub + 1)), (uint16_t)target);
    m_write8(s, m_abs(s, (uint16_t)(stub + 3)), (uint8_t)(target >> 16));
}

/* The handlers the boot installs until the program installs its own. */
enum { BOOT_NMI = 0xC30548, BOOT_IRQ = 0xC30529 };

/* True where interrupts end up enabled (the CLI). */
static bool boot(Snes *s, uint8_t flags)
{
    Platform *pf = m_platform(s);
    set_stub(s, RAM_ABS(RAM_NMI_VECTOR), BOOT_NMI);
    set_stub(s, RAM_ABS(RAM_IRQ_VECTOR), BOOT_IRQ);
    tm_enable_interrupts(pf, PF_AT(0xC30034), TM_INT_AUTO_JOYPAD);
    m_write8(s, m_abs(s, RAM_ABS(RAM_SCENE_FLAGS)), flags);
    bool line_irq = flags & SCENE_FLAG_LINE_IRQ;
    if (line_irq) {
        vid_set_display(pf, PF_AT(0xC30041), &(VidDisplay){ .brightness = FULL_BRIGHTNESS });
        vid_enable_line_effects(pf, PF_AT(0xC30044), 0);
        tm_enable_interrupts(pf, PF_AT(0xC30049), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD);
        tm_set_line(pf, PF_AT(0xC3004F), LINE_IRQ_LINE);
        tm_ack_line_irq(pf, PF_AT(0xC30052));
    }
    /* the decompressor's source (the program's stream) and destination */
    m_write16(s, m_abs(s, RAM_ABS(RAM_DECOMPRESS_SOURCE)), scene_program_stream_offset(s));
    m_write16(s, m_abs(s, RAM_ABS(RAM_DECOMPRESS_DEST)), PROGRAM_ADDRESS);
    m_write8(s, m_abs(s, RAM_ABS(RAM_DECOMPRESS_DEST) + 2), 0x00);
    m_write8(s, m_abs(s, RAM_ABS(RAM_DECOMPRESS_SOURCE) + 2), SCENE_PROGRAM_STREAM_BANK);
    return line_irq;
}

bool scene_boot_bank_reaches_io(Snes *s) { return m_io_bank(s->cpu.db); }

// $C3:0000
/* Start the scene program: the mode bits in A. The decompressor is a call-out,
 * and the port stops at the program's first instruction. */
void scene_boot(Snes *s)
{
    Cpu *c = &s->cpu;
    uint8_t flags = (uint8_t)c->a;
    c->p |= CPU_FLAG_I;   /* SEI */
    bool line_irq = boot(s, flags);
    if (line_irq)
        c->p &= (uint8_t)~CPU_FLAG_I;   /* CLI */
    /* the registers at the JSL: the last values loaded */
    c->a = (uint16_t)(PROGRAM_ADDRESS & 0xFF00) | SCENE_PROGRAM_STREAM_BANK;
    c->x = line_irq ? LINE_IRQ_LINE : (uint16_t)BOOT_NMI;
    m_jsl_dc(s, 0xC3006F, 0xC30557, DC_N | DC_Z | DC_C);
    m_stop_at(s, PROGRAM_START, DC_N | DC_Z | DC_C);
}

/* MVN from bank $FE: A + 1 bytes from $FE:X to `bank`:Y, leaving A $FFFF, X and
 * Y past the bytes and the data bank the destination's. */
static void copy_from_data_bank(Snes *s, uint8_t bank)
{
    Cpu *c = &s->cpu;
    uint32_t count = (uint32_t)c->a + 1;
    for (uint32_t i = 0; i < count; i++) {
        m_write8(s, (uint32_t)bank << 16 | c->y, scene_data_byte(s, c->x));
        c->x++;
        c->y++;
    }
    c->a = 0xFFFF;
    c->db = bank;
}

// $C3:099C
/* Copy A + 1 bytes of bank $FE's data from X to $7E:Y. */
void scene_copy_data_to_7e(Snes *s)
{
    copy_from_data_bank(s, 0x7E);
    m_rtl(s);
}

// $C3:09A0
/* Copy A + 1 bytes of bank $FE's data from X to $7F:Y. */
void scene_copy_data_to_7f(Snes *s)
{
    copy_from_data_bank(s, 0x7F);
    m_rtl(s);
}
