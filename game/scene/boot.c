/* Scenes: starting a scene program, and the copies it makes out of bank $FE.
 *
 * The field and the game over start a scene by jumping to $C3:0000 with the
 * scene's mode bits in A: the boot points the interrupt stubs at bank C3's
 * handlers, decompresses the program to $7E:3000 and jumps there. */
#include "scene/scene.h"

#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/scene_data.h"
#include "vars.h"
#include "wram.h"

/* scene_flags bit 7: the scene uses the line interrupt, which the boot starts
 * at LINE_IRQ_LINE, and leaves interrupts enabled while the NMI works. */
enum { SCENE_FLAG_LINE_IRQ = 0x80 };
enum { LINE_IRQ_LINE = 0xD3 };
enum { FULL_BRIGHTNESS = 15 };

/* Where the program runs, in bank $7E. */
enum { PROGRAM_START = 0x7E3000 };
enum { PROGRAM_ADDRESS = 0x3000 };

/* An interrupt stub is a JML and its 24-bit target. */
enum { JML = 0x5C };
static uint32_t jump_to(uint32_t target) { return JML | target << 8; }

/* The handlers the boot installs until the program installs its own. */
enum { BOOT_NMI = 0xC30548, BOOT_IRQ = 0xC30529 };

// $C3:0000
/* Point the interrupts at the boot's handlers, keep the scene's mode bits,
 * start the line interrupt where they ask for it, and set the decompressor
 * on the program's stream. True where the scene uses the line interrupt, and
 * interrupts end up enabled. */
static bool boot(GameState *g, Platform *pf, uint8_t flags)
{
    set_nmi_vector(g, jump_to(BOOT_NMI));
    set_irq_vector(g, jump_to(BOOT_IRQ));
    tm_enable_interrupts(pf, PF_AT(0xC30034), TM_INT_AUTO_JOYPAD);
    set_scene_flags(g, flags);
    bool line_irq = flags & SCENE_FLAG_LINE_IRQ;
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

/* Start the scene program: the mode bits in A. The decompressor is a call-out,
 * and the port stops at the program's first instruction. */
void scene_boot(Snes *s)
{
    m_set_flag(s, CPU_FLAG_I, true);   /* SEI */
    bool line_irq = boot(state_of(s), m_platform(s), (uint8_t)s->cpu.a);
    m_set_flag(s, CPU_FLAG_I, !line_irq);   /* CLI where the scene uses the line interrupt */
    m_jsl_dc(s, 0xC3006F, 0xC30557, DC_A | DC_X | DC_N | DC_Z | DC_C);
    m_stop_at(s, PROGRAM_START, DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C);
}

// $C3:099C, $C3:09A0
/* `count` bytes of bank $FE's data from `from` to `to` in `bank`, a byte at a
 * time counting up. The destination is wherever the caller says. */
static void copy_data(GameState *g, uint8_t bank, uint16_t from, uint16_t to, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        wram_write8(g, (uint32_t)bank << 16 | (uint16_t)(to + i), scene_data_byte(g, (uint16_t)(from + i)));
}

/* MVN from bank $FE: A + 1 bytes from X to `bank`:Y. Of what it leaves, the
 * callers read only Y, past the bytes. */
static void copy_entry(Snes *s, uint8_t bank)
{
    uint16_t from = s->cpu.x, to = s->cpu.y;
    uint32_t count = (uint32_t)s->cpu.a + 1;
    copy_data(state_of(s), bank, from, to, count);
    s->cpu.y = (uint16_t)(to + count);
    m_rtl(s);
}

/* Copy A + 1 bytes of bank $FE's data from X to $7E:Y. */
void scene_copy_data_to_7e(Snes *s) { copy_entry(s, 0x7E); }

/* Copy A + 1 bytes of bank $FE's data from X to $7F:Y. */
void scene_copy_data_to_7f(Snes *s) { copy_entry(s, 0x7F); }
