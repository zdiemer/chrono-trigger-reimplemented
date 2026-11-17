/* The interrupts' dispatch (system/interrupts.h). */
#include "system/interrupts.h"

#include <stdbool.h>

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG_IE REG16(0x04000200)
#define REG_IF REG16(0x04000202)
#define IRQ_VECTOR (*(void (*volatile *)(void))0x03007FFC)

enum { SOURCES = 14 };
static void (*handlers[SOURCES])(void);

/* The interrupts' own stack, in place of the BIOS's 160 bytes: a handler
 * that hands work on (gba_interrupt_nested) keeps its frames there while an
 * interrupt taken meanwhile runs below them. The nested work's own stack.
 * Both start zeroed (start.s), so their deepest use is their lowest byte
 * that isn't zero, above a canary in their lowest word that a run past
 * their room writes over (gba_interrupt_stacks_whole). How deep the
 * interrupts' goes depends on which handlers come inside which, so on the
 * frame's timing: 152 bytes at most in the movies, until faster pictures
 * took `menus-sweep` to 220, past the 192 it had. */
enum { INTERRUPTS_STACK_BYTES = 256, NESTED_STACK_BYTES = 448 };
static uint8_t interrupts_stack[INTERRUPTS_STACK_BYTES] __attribute__((section(".iwram_bss"), aligned(8)));
static uint8_t nested_stack[NESTED_STACK_BYTES] __attribute__((section(".iwram_bss"), aligned(8)));

void gba_run_nested(void (*fn)(void), uint8_t *stack_top);   /* system/nested.s */
void gba_interrupts_stack(uint8_t *top);

/* Each raised source's handler, from the lowest. In ARM, in fast RAM, as the
 * BIOS calls it. */
__attribute__((target("arm"), section(".iwram"), noinline)) static void dispatch(void)
{
    uint16_t raised = REG_IF & REG_IE;
    for (unsigned n = 0; raised; n++, raised >>= 1)
        if ((raised & 1) && handlers[n])
            handlers[n]();
}

enum { CANARY = 0x5AFEC0DEu };
static volatile uint32_t *canary_of(uint8_t *stack) { return (volatile uint32_t *)(void *)stack; }

void gba_interrupt_handler(uint16_t sources, void (*handler)(void))
{
    static bool stacked;
    if (!stacked) {
        *canary_of(interrupts_stack) = CANARY;
        *canary_of(nested_stack) = CANARY;
        gba_interrupts_stack(interrupts_stack + INTERRUPTS_STACK_BYTES);
        stacked = true;
    }
    for (unsigned n = 0; n < SOURCES; n++)
        if (sources & (1u << n))
            handlers[n] = handler;
    IRQ_VECTOR = dispatch;
}

__attribute__((target("arm"), section(".iwram"), noinline)) void gba_interrupt_nested(void (*work)(void))
{
    gba_run_nested(work, nested_stack + NESTED_STACK_BYTES);
}

bool gba_interrupt_stacks_whole(void)
{
    return *canary_of(interrupts_stack) == CANARY && *canary_of(nested_stack) == CANARY;
}

static size_t used(const uint8_t *stack, size_t bytes)
{
    size_t low = sizeof(uint32_t);
    while (low < bytes && !stack[low])
        low++;
    return bytes - low;
}

size_t gba_interrupts_stack_used(void) { return used(interrupts_stack, INTERRUPTS_STACK_BYTES); }
size_t gba_nested_stack_used(void) { return used(nested_stack, NESTED_STACK_BYTES); }
size_t gba_interrupts_stack_size(void) { return INTERRUPTS_STACK_BYTES; }
size_t gba_nested_stack_size(void) { return NESTED_STACK_BYTES; }
