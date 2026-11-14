/* The interrupts' dispatch (system/interrupts.h). */
#include "system/interrupts.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG_IE REG16(0x04000200)
#define REG_IF REG16(0x04000202)
#define IRQ_VECTOR (*(void (*volatile *)(void))0x03007FFC)

enum { SOURCES = 14 };
static void (*handlers[SOURCES])(void);

/* Each raised source's handler, from the lowest. In ARM, in fast RAM, as the
 * BIOS calls it. */
__attribute__((target("arm"), section(".iwram"), noinline)) static void dispatch(void)
{
    uint16_t raised = REG_IF & REG_IE;
    for (unsigned n = 0; raised; n++, raised >>= 1)
        if ((raised & 1) && handlers[n])
            handlers[n]();
}

void gba_interrupt_handler(uint16_t sources, void (*handler)(void))
{
    for (unsigned n = 0; n < SOURCES; n++)
        if (sources & (1u << n))
            handlers[n] = handler;
    IRQ_VECTOR = dispatch;
}
