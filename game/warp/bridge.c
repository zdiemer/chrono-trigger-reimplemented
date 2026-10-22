/* Where the warp's ports meet the original's calling convention (warp/warp.h). */
#include "warp/warp.h"

#include "port/machine.h"
#include "port/native.h"

enum { JSR_SIZE = 3 };

/* The call's return address pushed, the sibling runs as C, timed as its
 * routine; it came back if it returned to the call. */
bool warp_run_from(Snes *s, uint32_t site, uint32_t routine, void (*entry)(Snes *))
{
    uint16_t stack = s->cpu.s;
    uint32_t returns_to = site + JSR_SIZE;
    m_push16(s, (uint16_t)(returns_to - 1));
    m_jump_to(s, routine);
    NATIVE_TIME_AS(s, routine, entry(s));
    return m_came_back(s, returns_to, stack);
}
