/* The game's frames on the GBA (system/frames.h). */
#include "system/frames.h"

#include "platform/time_model.h"
#include "system/debug_log.h"
#include "system/drive_box.h"

enum { GBA_FRAME_CYCLES = 280896 };

/* The runner's decisions, where it put some. */
typedef struct {
    uint32_t mark, count;
    uint8_t bits[];
} Decisions;

static const Decisions *replayed;
static uint32_t taken;
static bool ran_out;

/* The GBA's own time: what the game's frames have run past their GBA
 * frame and not yet sat out, where the main thread's part of this frame
 * began, and whether it stopped in the middle of it, its own frames then
 * sat out where it stands. The SNES's picture and sound processors were its
 * own; the GBA's picture and mixing are the driver's, so only the game's part
 * of a frame counts. */
enum { MOST_OWED = 8 * GBA_FRAME_CYCLES };
static uint32_t owed, main_began;
static bool sat_out;

void frames_start(bool driven)
{
    const Decisions *d = (const Decisions *)(uintptr_t)DRIVE_DECISIONS_AT;
    replayed = driven && d->mark == DRIVE_DECISIONS_MARK ? d : NULL;
}

void frames_game_took(uint32_t cycles)
{
    if (!sat_out && cycles > GBA_FRAME_CYCLES)
        owed = owed + (cycles - GBA_FRAME_CYCLES) < MOST_OWED ? owed + (cycles - GBA_FRAME_CYCLES) : MOST_OWED;
    sat_out = false;
}

bool frames_replayed(void) { return replayed != NULL; }
uint32_t frames_decisions_taken(void) { return taken; }
bool frames_decisions_ran_out(void) { return ran_out; }

/* The next of the runner's decisions; past the last, the game goes on as if
 * nothing lagged, and the report says so. */
static bool next_replayed(void)
{
    if (taken >= replayed->count) {
        ran_out = true;
        return false;
    }
    bool yes = (replayed->bits[taken / 8] >> (taken % 8)) & 1;
    taken++;
    return yes;
}

/* The GBA's line counter as the SNES's: its 160 lines drawn as the SNES's
 * 225, its vertical blank's 68 as the SNES's 37 after them. */
#define VCOUNT (*(volatile uint16_t *)0x04000006)
enum { GBA_LINES = 160, GBA_FRAME_LINES = 228, SNES_DRAWN_LINES = TIME_VBLANK_LINE };

static uint32_t snes_line(void)
{
    uint32_t v = VCOUNT;
    if (v < GBA_LINES)
        return v * SNES_DRAWN_LINES / GBA_LINES;
    return SNES_DRAWN_LINES + (v - GBA_LINES) * (TIME_FRAME_LINES - SNES_DRAWN_LINES) / (GBA_FRAME_LINES - GBA_LINES);
}

/* The budget's window as the time model has it (timing.c): open from line
 * 240 through the frame's start to `line`, and shut once the main thread's
 * work has run into the next frame. */
static bool time_left_at(uint32_t elapsed, uint16_t line)
{
    if (elapsed >= GBA_FRAME_CYCLES)
        return false;
    uint32_t phase = snes_line();
    return phase >= TIME_BUDGET_REOPENS_LINE || phase < line;
}

bool frames_decide(void *ctx, TmHostDecision what, uint16_t line)
{
    (void)ctx;
    if (replayed)
        return next_replayed();
    uint32_t now = cycles_now();
    switch (what) {
    case TM_HOST_LAGS:
        /* a whole frame's work still owed: sat out, the handlers' alone */
        if (owed >= GBA_FRAME_CYCLES) {
            owed -= GBA_FRAME_CYCLES;
            return true;
        }
        main_began = now;
        return false;
    case TM_HOST_SITS_OUT:
        /* past its frame: the main thread goes on in the next, where it stands */
        if (now - main_began < GBA_FRAME_CYCLES)
            return false;
        sat_out = true;
        return true;
    case TM_HOST_TIME_LEFT:
        return time_left_at(now - main_began, line);
    }
    return false;
}
