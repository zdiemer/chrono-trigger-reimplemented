/* Scenes: the curtain -- the ragged edge that sweeps across the screen to
 * open or close a scene.
 *
 * A scene that sweeps a curtain keeps a per-line table of where its edge is
 * (a byte a line, for the line effect that windows the picture) and asks for
 * the sweep: from the left, its edges start at 0 and move right; from the
 * right, they start at the right and move left. Each frame the scene engine
 * moves every line's edge on by 8 to 11 pixels, the lines in runs of 3 to 34
 * that move alike, so the edge stays ragged as it crosses; after 31 steps the
 * sweep is over. The runs' lengths and moves come from the game's table of
 * random bytes, from a place a stirred number picks each frame
 * (scene/random.c). The edges are kept $1C0 bytes past the table's start and
 * sent to the table, or to the half after it, a frame each, for the line
 * effect to read the one not being written.
 *
 * The field's, the race's and the menus' attract scene call it through the
 * scene engine's jump ($C3:000E). */
#include "scene/scene.h"

#include "cpu_io.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/scene_data.h"
#include "vars.h"
#include "wram.h"

typedef enum { NO_SWEEP = 0, FROM_THE_LEFT = 1, FROM_THE_RIGHT = 2 } CurtainSweep;

enum {
    CURTAIN_LINES = 0xE0,      /* the picture's 224 lines */
    EDGES_KEPT_AT = 0x1C0,     /* where the edges are kept, past the table's start */
    LAST_STEP = 0x20,          /* the sweep ends when its step count gets here */
    FIRST_BANK = 0x7E, SECOND_BANK = 0x7F,
    CLOSED_AT_THE_LEFT = 0x00, CLOSED_AT_THE_RIGHT = 0xFF,
    RUN_SHORTEST = 3, RUN_SPREAD = 0x1F, MOVE_LEAST = 8, MOVE_SPREAD = 0x03,
};

/* The pointer the original keeps into the random bytes: their page and bank. */
enum { RANDOM_BYTES_PAGE_AND_BANK = 0xC0FE };

static void leave_the_noise_page(GameState *g) { set_scene_curtain_noise_page(g, RANDOM_BYTES_PAGE_AND_BANK); }

/* The first step: every line's edge closed at the side the curtain starts
 * from. Only the bank's low bit says which bank the edges are in here. */
static void close_every_line(GameState *g, uint16_t edges, uint8_t bank, uint8_t fill)
{
    uint8_t in_bank = (bank & 1) ? SECOND_BANK : FIRST_BANK;
    wram_fill8(g, (uint32_t)in_bank << 16 | edges, CURTAIN_LINES, fill);
    ORIGINAL_EFFECT(set_scene_curtain_fill(g, fill));
}

/* A run of lines that move alike: how many, and how far. */
typedef struct {
    unsigned left;
    uint8_t move;
} Run;

/* The next run, from the next two random bytes on from where the last left
 * off. Once an edge has been stopped at 0, every run after it lasts past the
 * picture's bottom: the original stops the edge with its direct page's low
 * byte, which leaves A's high byte at the page's, 3, and the runs' lengths it
 * loads after that take it in. `extra` is the carry the length's add takes in
 * (below). */
enum { PAGE_IN_THE_LENGTH = 0x300 };

static Run next_run(GameState *g, bool extra, bool after_a_stop)
{
    uint8_t at = scene_random_byte(g, scene_curtain_table_bank_high(g));
    set_scene_curtain_table_bank_high(g, at);
    unsigned left = (unsigned)(at & RUN_SPREAD) + RUN_SHORTEST + (extra ? 1 : 0) + (after_a_stop ? PAGE_IN_THE_LENGTH : 0);
    at = scene_random_byte(g, at);
    set_scene_curtain_table_bank_high(g, at);
    return (Run){ left, (uint8_t)((at & MOVE_SPREAD) + MOVE_LEAST) };
}

/* An edge moved on: right, stopping at the screen's edge; or left, a pixel
 * further than the run says (the original's subtract borrows), stopping at 0
 * where it would wrap. `extra` is a pixel the original's add takes in, or its
 * subtract doesn't borrow (below). */
static uint8_t moved(uint8_t edge, uint8_t move, bool to_the_right, bool extra, bool *stopped_at_0)
{
    unsigned c = extra ? 1 : 0;
    if (to_the_right)
        return edge + move + c > 0xFF ? 0xFF : (uint8_t)(edge + move + c);
    uint8_t to = (uint8_t)(edge - move - 1 + c);
    bool wrapped = edge + c < move + 1u;
    if (wrapped && (to & 0x80)) {
        *stopped_at_0 = true;
        return 0;
    }
    return to;
}

/* The original's adds and subtracts take in the carry the instruction before
 * them left, which is a pixel or a line more only by accident: on the first
 * line its sweep's compare (from the right, or any sweep past it), and after
 * that the compare of the line just done with the end, which carries only
 * where the lines run past the end of the bank and wrap round. A run's
 * length takes it, and the lines that don't start a run move by it. */
static bool carried_in(uint16_t edges, unsigned line, CurtainSweep sweep)
{
    if (line == 0)
        return sweep >= FROM_THE_RIGHT;
    return (uint16_t)(edges + line) >= (uint16_t)(edges + CURTAIN_LINES);
}

// $C3:01E4
/* A frame's step of the curtain. `salt` is what the stir takes from its
 * caller's X (the scene engine's call hands it on). */
void scene_curtain_step(GameState *g, Platform *pf, uint16_t salt)
{
    CurtainSweep sweep = (CurtainSweep)scene_curtain_sweep(g);
    if (sweep == NO_SWEEP) {
        set_scene_curtain_steps(g, 0);   /* no sweep starts its count over */
        return;
    }
    uint16_t table = scene_curtain_table(g);
    uint8_t bank = scene_curtain_table_bank(g);
    uint16_t edges = (uint16_t)(table + EDGES_KEPT_AT);
    uint8_t steps = scene_curtain_steps(g);
    if (steps == 0) {
        close_every_line(g, edges, bank, sweep == FROM_THE_LEFT ? CLOSED_AT_THE_LEFT : CLOSED_AT_THE_RIGHT);
        salt = (uint16_t)(edges + CURTAIN_LINES - 1);   /* where the fill's block move left X, at its last byte */
    }
    steps = (uint8_t)(steps + 1);
    set_scene_curtain_steps(g, steps);
    if ((int8_t)(uint8_t)(steps - LAST_STEP) >= 0) {   /* compared by the sign of the difference, as the original does */
        set_scene_curtain_steps(g, 0);
        set_scene_curtain_sweep(g, NO_SWEEP);
        return;
    }

    ORIGINAL_EFFECT(leave_the_noise_page(g));
    SceneDraw d = scene_draw_random(g, pf, PF_AT(0xC3024A), scene_draw_from(RANDOM_BYTES_PAGE_AND_BANK & 0xFF, steps >= LAST_STEP,
                                                                           TM_ENTROPY_BYTES),
                                    salt, TM_ENTROPY_BYTES);
    set_scene_curtain_table_bank_high(g, (uint8_t)d);
    ORIGINAL_EFFECT(set_scene_curtain_end(g, (uint16_t)(edges + CURTAIN_LINES)));

    /* Odd steps send the edges to the table, even ones to the half after it. */
    uint16_t sent_to = (steps & 1) ? table : (uint16_t)(table + CURTAIN_LINES);
    hw_port_seek(g, sent_to, bank);
    bool to_the_right = sweep != FROM_THE_RIGHT;
    Run run = { 1, 0 };
    bool stopped_at_0 = false;
    for (unsigned line = 0; line < CURTAIN_LINES; line++) {
        bool extra = carried_in(edges, line, sweep);
        if (--run.left == 0) {
            run = next_run(g, extra, stopped_at_0);
            extra = false;
        }
        uint32_t at = (uint32_t)bank << 16 | (uint16_t)(edges + line);
        uint8_t edge = moved(wram_read8(g, at), run.move, to_the_right, extra, &stopped_at_0);
        hw_port_write(g, edge);
        wram_write8(g, at, edge);
    }
    ORIGINAL_EFFECT(set_scene_curtain_move(g, run.move));
}

/* The step saves every register and its flags and puts them back; the data
 * bank, put back last, leaves its N and Z. */
void scene_sweep_the_curtain(Snes *s)
{
    scene_curtain_step(state_of(s), m_platform(s), s->cpu.x);
    m_set_nz8(s, s->cpu.db);
    m_rtl(s);
}

/* A sweep started, its edges in `table` $1C0 bytes on, in `bank`; the step
 * that follows closes every line at the side it starts from. */
void scene_curtain_start(GameState *g, uint8_t sweep, uint16_t table, uint8_t bank)
{
    set_scene_curtain_sweep(g, sweep);
    set_scene_curtain_steps(g, 0);
    set_scene_curtain_table(g, table);
    set_scene_curtain_table_bank(g, bank);
}

// $C3:000E
/* The scene engine's jump to the step: on to it, which is a port of its own,
 * so that a comparison of the step is the step's (the stir waits there for the
 * beam the original latches). */
enum { CURTAIN_STEP = 0xC301E4 };

void scene_curtain_jump(Snes *s) { m_stop_at(s, CURTAIN_STEP, 0); }

/* The edges in either work RAM bank, clear of low RAM's first $2000 bytes
 * (the stack and the curtain's own cells, which the original's saved
 * registers and the step would write over); in the second bank, the halves
 * the steps send to short of its end, where the work RAM port would run on
 * into low RAM; and the stir's cells reached through a data bank on low RAM. */
enum { LOW_RAM_END = 0x2000, TABLE_REACH = EDGES_KEPT_AT + CURTAIN_LINES, HALVES_REACH = 2 * CURTAIN_LINES };

bool scene_curtain_table_fits(uint8_t bank, uint16_t table)
{
    if (bank == SECOND_BANK)
        return (uint32_t)table + HALVES_REACH <= 0x10000u;
    return bank == FIRST_BANK && table >= LOW_RAM_END && (uint32_t)table + TABLE_REACH <= 0x10000u;
}

bool scene_sweep_the_curtain_present(Snes *s)
{
    if (!m_bank_reaches_low_ram(s))
        return false;
    vars_peek(true);
    bool fits = scene_curtain_table_fits(scene_curtain_table_bank(state_of(s)), scene_curtain_table(state_of(s)));
    vars_peek(false);
    return fits;
}
