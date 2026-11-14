/* What the C library and the runtime need of the GBA (targets/gba/main.c):
 * the arena (runtime/port/arena.h) and the library's heap over the end of
 * work RAM, text out to mGBA's debug log, and a stop.
 *
 * The arena and the heap share one run of memory from the end of the zeroed
 * state (gba.ld's __arena_start) to the end of work RAM, each take after the
 * last; nothing is given back. */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "port/arena.h"
#include "debug_log.h"

extern uint8_t __arena_start[], __ewram_end[];

enum { ARENA_ALIGN = 8 };
static size_t used;

static void *take(size_t bytes)
{
    size_t at = (used + ARENA_ALIGN - 1) & ~(size_t)(ARENA_ALIGN - 1);
    if (bytes > (size_t)(__ewram_end - __arena_start) - at)
        return NULL;
    used = at + bytes;
    return __arena_start + at;
}

void *arena_take(size_t bytes)
{
    void *at = take(bytes);
    if (at)
        memset(at, 0, bytes);
    return at;
}

bool arena_fixed(void) { return true; }
size_t arena_used(void) { return used; }

void *_sbrk(ptrdiff_t incr);
void *_sbrk(ptrdiff_t incr)
{
    void *at = incr >= 0 ? take((size_t)incr) : NULL;
    if (!at) {
        errno = ENOMEM;
        return (void *)-1;
    }
    return at;
}

/* ---- mGBA's debug log ------------------------------------------------------------------------ */

#define DEBUG_ENABLE (*(volatile uint16_t *)0x04FFF780)
#define DEBUG_FLAGS (*(volatile uint16_t *)0x04FFF700)
#define DEBUG_STRING ((volatile char *)0x04FFF600)
enum { DEBUG_LINE = 255, DEBUG_SEND = 0x100, DEBUG_INFO = 3 };

static char line[DEBUG_LINE + 1];
static unsigned line_length;

void debug_log_open(void) { DEBUG_ENABLE = 0xC0DE; }

static void send_line(void)
{
    for (unsigned i = 0; i < line_length; i++)
        DEBUG_STRING[i] = line[i];
    DEBUG_STRING[line_length] = 0;
    DEBUG_FLAGS = DEBUG_SEND | DEBUG_INFO;
    line_length = 0;
}

int _write(int fd, const char *bytes, int count);
int _write(int fd, const char *bytes, int count)
{
    (void)fd;
    for (int i = 0; i < count; i++) {
        if (bytes[i] == '\n' || line_length == DEBUG_LINE) {
            send_line();
            if (bytes[i] == '\n')
                continue;
        }
        line[line_length++] = bytes[i];
    }
    return count;
}

/* ---- Cycles ---------------------------------------------------------------------------------- */

#define TIMER2_COUNT (*(volatile uint16_t *)0x04000108)
#define TIMER2_CONTROL (*(volatile uint16_t *)0x0400010A)
#define TIMER3_COUNT (*(volatile uint16_t *)0x0400010C)
#define TIMER3_CONTROL (*(volatile uint16_t *)0x0400010E)
enum { TIMER_ON = 0x80, TIMER_CHAINED = 0x04 };

void cycles_start(void)
{
    TIMER2_CONTROL = 0;
    TIMER3_CONTROL = 0;
    TIMER2_COUNT = 0;   /* the reload value, taken as the timer starts */
    TIMER3_COUNT = 0;
    TIMER3_CONTROL = TIMER_ON | TIMER_CHAINED;
    TIMER2_CONTROL = TIMER_ON;
}

/* The high half again where the low one wrapped between the two reads. */
uint32_t cycles_now(void)
{
    uint16_t high = TIMER3_COUNT, low = TIMER2_COUNT, again = TIMER3_COUNT;
    if (again != high)
        low = TIMER2_COUNT;
    return (uint32_t)again << 16 | low;
}

/* ---- The rest of what the library asks for ------------------------------------------------- */

void _exit(int status);
void _exit(int status)
{
    (void)status;
    _write(2, "ct_gba: stopped\n", 16);
    for (;;)
        ;
}

/* No files: a movie's or a save's bytes come in the cartridge (pack.py). */
int _open(const char *path, int flags, ...);
int _open(const char *path, int flags, ...) { (void)path, (void)flags; errno = ENOENT; return -1; }
int _close(int fd);
int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, struct stat *st);
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd);
int _isatty(int fd) { (void)fd; return 1; }
int _lseek(int fd, int at, int whence);
int _lseek(int fd, int at, int whence) { (void)fd, (void)at, (void)whence; return 0; }
int _read(int fd, char *bytes, int count);
int _read(int fd, char *bytes, int count) { (void)fd, (void)bytes, (void)count; return 0; }
int _kill(int pid, int sig);
int _kill(int pid, int sig) { (void)pid, (void)sig; return -1; }
int _getpid(void);
int _getpid(void) { return 1; }
