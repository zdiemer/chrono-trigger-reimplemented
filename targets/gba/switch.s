@ The fibers' switch (targets/gba/fiber.c): the callee-saved registers and
@ the return address pushed on the stack being left, its stack pointer kept
@ at r0, and the coming fiber's (r1) popped.
    .text
    .arm
    .align 2
    .global fiber_swap
    .type fiber_swap, %function
fiber_swap:
    push    {r4-r11, lr}
    str     sp, [r0]
    mov     sp, r1
    pop     {r4-r11, lr}
    bx      lr
