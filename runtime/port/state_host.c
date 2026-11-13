#include "port/state_host.h"
#include "port/vars_host.h"
#include "port/arena.h"

#include <stdlib.h>
#include <string.h>

#include "charge.h"
#include "core/program.h"
#include "battle/wide_window.h"
#include "core/panel_layout.h"
#include "core/sprite_table.h"
#include "field/margin_objects.h"
#include "field/streaming.h"
#include "menu/menu.h"
#include "menu/panels_across.h"
#include "menu/sprites.h"
#include "cpu_io.h"
#include "platform/time_model.h"
#include "port/time_rates.h"
#include "rom.h"
#include "wram.h"

struct GameState {
    VarsHost vars;
    ProgramHandlers handlers;
    MenuCoroutines menus;
    SpritePlaces sprite_places;
    FieldMargins field_margins;
    PanelLayout panel_layout;
    BattleWideWindow battle_wide_window;
    MenuPanels menu_panels;
    FieldObjectsPast field_objects_past;
    MenuSpritesPast menu_sprites_past;
    uint8_t sram[STATE_HOST_SRAM_BYTES];
    /* the multiplier and divider: their operands, and the result the
     * product-or-remainder register shows */
    uint8_t multiplicand;
    uint16_t dividend, quotient, product_or_remainder;
    /* the work RAM port's 17-bit address */
    uint32_t port;
    unsigned long strays;
    uint32_t stray[STATE_HOST_STRAYS_KEPT];
    bool stopped;
    bool scene_next;          /* the program goes on to a scene (program_goes_on_to_a_scene) */
    uint8_t scene;
    /* the scenes the driver starts the game in, one after another (program_starts_in_a_scene) */
    uint8_t first_scene[STATE_HOST_FIRST_SCENES], first_song[STATE_HOST_FIRST_SCENES];
    unsigned first_scenes, first_scene_taken;
    bool field_again;         /* the field starts again (program_starts_the_field_again) */
    bool afresh;              /* the game starts afresh from the title (program_starts_afresh_from_the_title) */
    bool reset;               /* the console reset (program_resets_the_console) */
    uint32_t stopped_at;
    const char *stopped_what;
    uint32_t owed;            /* the original's time charged since the timing backend last took it (master clocks) */
    uint32_t raw_bus;         /* accesses inside a retired operation, before its rate is applied */
    uint32_t rate;            /* the cost_milli the game's accesses are timed at now (charge_time_as_begin); 0, none */
    unsigned timed;           /* index in time_rates, or time_rate_count for untagged work */
    StateHostTime *time;      /* optional diagnostic totals; never used to schedule work */
    unsigned uncounted;       /* nested stretches whose accesses aren't the game's own time */
    uint64_t work_milli;      /* the game's own accesses' time since it was last taken (master clocks x 1000) */
    bool lasting;             /* in the arena, for the run's whole life (state_host_create_lasting) */
};

static GameState *start(GameState *g, const uint8_t *sram)
{
    if (!g)
        return NULL;
    g->timed = time_rate_count;
    vars_host_init(&g->vars);
    if (sram)
        memcpy(g->sram, sram, sizeof g->sram);
    return g;
}

GameState *state_host_create(const uint8_t *sram) { return start(calloc(1, sizeof(GameState)), sram); }

GameState *state_host_create_lasting(const uint8_t *sram)
{
    GameState *g = start(arena_take(sizeof(GameState)), sram);
    if (g)
        g->lasting = true;
    return g;
}

void state_host_destroy(GameState *g)
{
    if (!g)
        return;
    free(g->time);
    if (!g->lasting)
        free(g);
}

/* The whole record, but the diagnostic totals the run keeps for itself. */
size_t state_host_snapshot_bytes(void) { return sizeof(GameState); }

void state_host_snapshot(const GameState *g, uint8_t *out) { memcpy(out, g, sizeof *g); }

void state_host_restore(GameState *g, const uint8_t *in)
{
    StateHostTime *time = g->time;
    bool lasting = g->lasting;
    memcpy(g, in, sizeof *g);
    g->time = time;
    g->lasting = lasting;
}

ProgramHandlers *program_handlers(GameState *g) { return &g->handlers; }
MenuCoroutines *menu_coroutines(GameState *g) { return &g->menus; }
SpritePlaces *sprite_places(GameState *g) { return &g->sprite_places; }
FieldMargins *field_margins(GameState *g) { return &g->field_margins; }
PanelLayout *panel_layout(GameState *g) { return &g->panel_layout; }
BattleWideWindow *battle_wide_window(GameState *g) { return &g->battle_wide_window; }
MenuPanels *menu_panels(GameState *g) { return &g->menu_panels; }
FieldObjectsPast *field_objects_past(GameState *g) { return &g->field_objects_past; }
MenuSpritesPast *menu_sprites_past(GameState *g) { return &g->menu_sprites_past; }

uint8_t *state_host_sram(GameState *g) { return g->sram; }
VarsHost *state_host_vars(GameState *g) { return &g->vars; }
const VarsHost *state_host_vars_const(const GameState *g) { return &g->vars; }

StateHostWork state_host_work(const GameState *g)
{
    return (StateHostWork){ g->raw_bus, g->owed, g->work_milli };
}

void state_host_add_work(GameState *g, StateHostWork work)
{
    g->raw_bus += work.raw_bus;
    g->owed += work.owed;
    g->work_milli += work.milli;
}

unsigned long state_host_strays(const GameState *g, uint32_t *first, unsigned *kept)
{
    unsigned n = g->strays < STATE_HOST_STRAYS_KEPT ? (unsigned)g->strays : STATE_HOST_STRAYS_KEPT;
    if (first)
        memcpy(first, g->stray, n * sizeof *first);
    if (kept)
        *kept = n;
    return g->strays;
}

bool state_host_stopped(const GameState *g, uint32_t *routine, const char **what)
{
    if (routine)
        *routine = g->stopped_at;
    if (what)
        *what = g->stopped_what;
    return g->stopped;
}

/* ---- Memory at the original's addresses ------------------------------------------------------ */

/* Where an address lies: work RAM (banks $7E-$7F, and the first 8 KiB of every
 * bank that maps the system area), save RAM (HiROM's $6000-$7FFF in banks
 * $20-$3F and $A0-$BF, 8 KiB mirrored), the cartridge, or nowhere the host has. */
typedef enum { NOWHERE, IN_WRAM, IN_SRAM, IN_ROM } Place;

static Place place_of(uint32_t addr, uint32_t *offset)
{
    uint8_t bank = (uint8_t)(addr >> 16);
    uint16_t at = (uint16_t)addr;
    bool system_bank = bank < 0x40 || (bank >= 0x80 && bank < 0xC0);
    if (bank == 0x7E || bank == 0x7F) {
        *offset = addr - 0x7E0000;
        return IN_WRAM;
    }
    if (system_bank && at < 0x2000) {
        *offset = at;
        return IN_WRAM;
    }
    if (system_bank && (bank & 0x3F) >= 0x20 && at >= 0x6000 && at < 0x8000) {
        *offset = (uint32_t)(at - 0x6000) % STATE_HOST_SRAM_BYTES;
        return IN_SRAM;
    }
    if (!system_bank || at >= 0x8000)
        return IN_ROM;
    return NOWHERE;
}

bool state_host_is_cartridge(uint32_t addr)
{
    uint32_t offset;
    return place_of(addr & 0xFFFFFF, &offset) == IN_ROM;
}

static void stray(const GameState *g, uint32_t addr)
{
    GameState *m = (GameState *)g;   /* the count is the backing's own, not the game's state */
    if (m->strays < STATE_HOST_STRAYS_KEPT)
        m->stray[m->strays] = addr;
    m->strays++;
}

uint8_t state_host_read8(const GameState *g, uint32_t addr)
{
    uint32_t offset;
    switch (place_of(addr & 0xFFFFFF, &offset)) {
    case IN_WRAM: {
        uint8_t value = 0;
        vars_host_read8(&g->vars, 0x7E0000u + offset, &value);
        return value;
    }
    case IN_SRAM: return g->sram[offset];
    case IN_ROM: {
        /* the host's read of the cartridge, not the game's: no time */
        GameState *m = (GameState *)g;
        uint32_t mark = charge_uncounted_begin(m);
        uint8_t v = rom_read8(g, addr);
        charge_uncounted_end(m, mark);
        return v;
    }
    case NOWHERE: break;
    }
    stray(g, addr & 0xFFFFFF);
    return 0;
}

void state_host_write8(GameState *g, uint32_t addr, uint8_t v)
{
    uint32_t offset;
    switch (place_of(addr & 0xFFFFFF, &offset)) {
    case IN_WRAM:
        /* Bulk clears may cross retired cells and padding. They have no
         * storage; inspection reads of those addresses return zero. */
        vars_host_write8(&g->vars, 0x7E0000u + offset, v);
        return;
    case IN_SRAM: g->sram[offset] = v; return;
    case IN_ROM:
    case NOWHERE: break;
    }
    stray(g, addr & 0xFFFFFF);
}

/* ---- The multiplier, the divider and the work RAM port (cpu_io.h) ---------------------------- */

void hw_set_multiplicand(GameState *g, uint8_t a) { g->multiplicand = a; }

uint16_t hw_multiply_by(GameState *g, uint8_t b)
{
    g->product_or_remainder = (uint16_t)(g->multiplicand * b);
    return g->product_or_remainder;
}

uint8_t hw_multiplicand(const GameState *g) { return g->multiplicand; }

void hw_set_dividend(GameState *g, uint16_t dividend) { g->dividend = dividend; }

/* By 0 the quotient is all ones and the remainder the dividend, as the console's. */
void hw_divide_by(GameState *g, uint8_t divisor)
{
    g->quotient = divisor ? (uint16_t)(g->dividend / divisor) : 0xFFFF;
    g->product_or_remainder = divisor ? (uint16_t)(g->dividend % divisor) : g->dividend;
}

/* The word's high byte goes to the register after the divisor, which isn't the divider's. */
void hw_divide_by_word(GameState *g, uint16_t word) { hw_divide_by(g, (uint8_t)word); }

uint16_t hw_quotient(const GameState *g) { return g->quotient; }
uint16_t hw_remainder(const GameState *g) { return g->product_or_remainder; }
uint16_t hw_dividend(const GameState *g) { return g->dividend; }

enum { PORT_MASK = 0x1FFFF, IO_ACCESS_CLOCKS = 6 };

void hw_port_seek(GameState *g, uint16_t at, uint8_t bank)
{
    state_host_game_access(g, 3 * IO_ACCESS_CLOCKS);
    g->port = ((uint32_t)(bank & 1) << 16 | at) & PORT_MASK;
}
void hw_port_seek_address(GameState *g, uint16_t at)
{
    state_host_game_access(g, 2 * IO_ACCESS_CLOCKS);
    g->port = (g->port & 0x10000) | at;
}
void hw_port_seek_bank(GameState *g, uint8_t bank)
{
    state_host_game_access(g, IO_ACCESS_CLOCKS);
    g->port = (uint32_t)(bank & 1) << 16 | (g->port & 0xFFFF);
}

uint8_t hw_port_read(GameState *g)
{
    state_host_game_access(g, IO_ACCESS_CLOCKS);
    uint8_t v = state_host_read8(g, 0x7E0000u + g->port);
    g->port = (g->port + 1) & PORT_MASK;
    return v;
}

void hw_port_write(GameState *g, uint8_t v)
{
    state_host_game_access(g, IO_ACCESS_CLOCKS);
    state_host_write8(g, 0x7E0000u + g->port, v);
    g->port = (g->port + 1) & PORT_MASK;
}

/* The byte the original leaves below the port: no register, nothing to keep. */
void hw_port_leave_spill(GameState *g, uint8_t low) { (void)g, (void)low; }

/* ---- Work RAM the game locates at run time (wram.h) ------------------------------------------ */

static uint32_t in_bank(uint32_t at, uint32_t offset) { return (at & 0xFF0000u) | (uint16_t)(at + offset); }

static void owe(GameState *g, uint32_t clocks);

void wram_clear(GameState *g, PfOrigin o, uint32_t at, uint32_t bytes)
{
    (void)o;
    if (!bytes)
        return;
    /* Clearing a transition's buffer still occupies its transfer time.
     * Keep it in the state's account, independent of the active port's rate. */
    owe(g, time_of_transfer(bytes));
    wram_fill8(g, at, bytes, 0);
}

void wram_fill8(GameState *g, uint32_t at, uint32_t bytes, uint8_t v)
{
    for (uint32_t i = 0; i < bytes; i++)
        state_host_write8(g, in_bank(at, i), v);
}

void wram_fill16(GameState *g, uint32_t at, uint32_t words, uint16_t v)
{
    for (uint32_t i = 0; i < words; i++) {
        state_host_write8(g, in_bank(at, 2 * i), (uint8_t)v);
        state_host_write8(g, in_bank(at, 2 * i + 1), (uint8_t)(v >> 8));
    }
}

/* Work RAM's two banks as the port steps through them. */
enum { WORK_RAM = 0x7E0000, WORK_RAM_SPAN = 0x20000 };
static uint32_t through_the_port(uint32_t at, uint32_t i) { return WORK_RAM + (at + i) % WORK_RAM_SPAN; }

void wram_port_fill(GameState *g, uint32_t at, uint32_t bytes, uint8_t v)
{
    for (uint32_t i = 0; i < bytes; i++)
        state_host_write8(g, through_the_port(at, i), v);
}

void wram_port_copy(GameState *g, PfData from, uint32_t at, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes; i++) {
        const uint8_t *byte = pf_data_pointer(from, i);
        state_host_write8(g, through_the_port(at, i), byte ? *byte : state_host_read8(g, pf_data_address(from, i)));
    }
}

void wram_copy(GameState *g, uint32_t from, uint32_t to, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes; i++)
        state_host_write8(g, in_bank(to, i), state_host_read8(g, in_bank(from, i)));
}

/* ---- Time: what the frame-level time model charges ------------------------------------------- *
 *
 * A build with no machine has no bus to count, so the game's own work is
 * timed here, as the machine's estimate times a port's (docs/REIMPLEMENTATION.md,
 * "The frame-level time model"): each access the game makes -- its
 * accessors, its reads of the cartridge, save RAM through the save service
 * (TmHostProgram.accessed) -- is the bus clocks it takes on the machine
 * (state_host_game_access), at the calibrated rate of the routine the work is
 * timed as (charge_time_as_begin, TIMED_AS scopes), the
 * rates the machine measured (port/time_rates.h). What a port does another
 * way is charged as the machine charges it: a charge at its routine's rate
 * (charge_as), a block move at the block move's, and a retired routine's
 * accesses at its retained rate (charge_retired_begin). A nested uncounted
 * stretch drops its accesses before the retired routine is priced. Work no
 * tag names, and a guard's look (vars_peek), take none. The timing backend
 * takes the account as the main thread waits (state_host_take_owed) and makes
 * lag frames of the whole frames it fills, and drops the handlers' at the
 * main thread's start of a frame, keeping it as the frame's own. */

static unsigned rate_index(uint32_t routine)
{
    routine &= 0xFFFFFF;
    unsigned lo = 0, hi = time_rate_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (time_rates[mid].routine < routine)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < time_rate_count && time_rates[lo].routine == routine ? lo : time_rate_count;
}

bool state_host_record_time(GameState *g)
{
    if (g->time)
        return true;
    g->time = calloc(time_rate_count + 1u, sizeof *g->time);
    if (!g->time)
        return false;
    for (unsigned i = 0; i < time_rate_count; i++)
        g->time[i].routine = time_rates[i].routine;
    return true;
}

const StateHostTime *state_host_time(const GameState *g, unsigned *count)
{
    *count = g->time ? time_rate_count + 1u : 0;
    return g->time;
}

static void record_work(GameState *g, unsigned index, uint32_t bus, uint32_t charged, uint64_t milli)
{
    if (g->time) {
        StateHostTime *t = &g->time[index];
        t->bus_clocks += bus;
        t->charged_clocks += charged;
        t->milli_clocks += milli;
    }
}

static void owe(GameState *g, uint32_t clocks)
{
    g->owed = clocks > UINT32_MAX - g->owed ? UINT32_MAX : g->owed + clocks;
}

void state_host_game_access(const GameState *g, uint32_t clocks)
{
    GameState *m = (GameState *)g;   /* the account is the backing's own, not the game's state */
    m->raw_bus += clocks;
    if (!m->uncounted) {
        uint64_t milli = (uint64_t)clocks * m->rate;
        m->work_milli += milli;
        record_work(m, m->timed, clocks, 0, milli);
    }
}

/* The block move stub's routine ($7E:9690), whose rate a byte moved is
 * charged at (a read and a write), at the retained block-copy rate. */
enum { BLOCK_MOVE = 0x7E9690, ACCESS_CLOCKS = 8, ACCESSES_PER_BYTE_MOVED = 2 };

void charge_as(GameState *g, uint32_t routine, uint32_t accesses)
{
    unsigned index = rate_index(routine);
    uint32_t rate = index < time_rate_count ? time_rates[index].cost_milli : 0;
    uint64_t milli = (uint64_t)accesses * ACCESS_CLOCKS * (rate ? rate : g->rate);
    g->work_milli += milli;
    record_work(g, rate ? index : g->timed, 0, accesses * ACCESS_CLOCKS, milli);
}

void charge_block_move(GameState *g, uint32_t bytes) { charge_as(g, BLOCK_MOVE, bytes * ACCESSES_PER_BYTE_MOVED); }

void charge_io_accesses(GameState *g, uint32_t accesses)
{
    state_host_game_access(g, accesses * IO_ACCESS_CLOCKS);
}

void charge_rom_accesses(GameState *g, uint32_t accesses)
{
    state_host_game_access(g, accesses * 6u);
}

void charge_accesses(GameState *g, uint32_t accesses)
{
    state_host_game_access(g, accesses * ACCESS_CLOCKS);
}

uint32_t state_host_take_owed(GameState *g)
{
    uint64_t work = g->work_milli / 1000;
    g->work_milli %= 1000;
    owe(g, work > UINT32_MAX ? UINT32_MAX : (uint32_t)work);
    uint32_t owed = g->owed;
    g->owed = 0;
    return owed;
}

uint32_t charge_retired_begin(GameState *g)
{
    g->uncounted++;
    return g->raw_bus;
}

void charge_retired_end(GameState *g, uint32_t mark, uint32_t routine, uint32_t accesses)
{
    uint32_t bus = g->raw_bus - mark;
    g->raw_bus = mark;
    g->uncounted--;
    unsigned index = rate_index(routine);
    uint32_t rate = index < time_rate_count ? time_rates[index].cost_milli : g->rate;
    uint32_t charged = accesses * ACCESS_CLOCKS;
    uint64_t milli = ((uint64_t)bus + charged) * rate;
    g->work_milli += milli;
    record_work(g, index < time_rate_count ? index : g->timed, bus, charged, milli);
    if (g->time)
        g->time[index < time_rate_count ? index : g->timed].tags++;
}

uint32_t charge_uncounted_begin(GameState *g)
{
    g->uncounted++;
    return g->raw_bus;
}

void charge_uncounted_end(GameState *g, uint32_t mark)
{
    g->raw_bus = mark;
    g->uncounted--;
}

/* A tag the rates don't name leaves the work at the rate it was at, as the
 * machine leaves an attribution to no hand port with the port's own. */
int charge_time_as_begin(GameState *g, uint32_t routine)
{
    int was = (int)g->timed;
    unsigned index = rate_index(routine);
    uint32_t rate = index < time_rate_count ? time_rates[index].cost_milli : 0;
    if (rate) {
        g->rate = rate;
        g->timed = index;
    }
    if (g->time)
        g->time[index].tags++;
    return was;
}

void charge_time_as_end(GameState *g, int was)
{
    g->timed = (unsigned)was;
    g->rate = g->timed < time_rate_count ? time_rates[g->timed].cost_milli : 0;
}

/* ---- Where the program's C ends (core/program.h) --------------------------------------------- */

bool program_went_no_further(const GameState *g) { return g->stopped; }

void program_goes_on_to_a_scene(GameState *g, uint8_t scene)
{
    g->scene_next = true;
    g->scene = scene;
}

bool program_next_scene(GameState *g, uint8_t *scene)
{
    if (!g->scene_next)
        return false;
    g->scene_next = false;
    *scene = g->scene;
    return true;
}

void program_starts_in_a_scene(GameState *g, uint8_t scene, uint8_t song)
{
    if (g->first_scenes == STATE_HOST_FIRST_SCENES)
        return;
    g->first_scene[g->first_scenes] = scene;
    g->first_song[g->first_scenes] = song;
    g->first_scenes++;
}

bool program_first_scene(GameState *g, uint8_t *scene, uint8_t *song)
{
    if (g->first_scene_taken == g->first_scenes)
        return false;
    *scene = g->first_scene[g->first_scene_taken];
    *song = g->first_song[g->first_scene_taken];
    g->first_scene_taken++;
    return true;
}

void program_starts_the_field_again(GameState *g) { g->field_again = true; }

bool program_field_starts_again(GameState *g)
{
    bool again = g->field_again;
    g->field_again = false;
    return again;
}

void program_resets_the_console(GameState *g) { g->reset = true; }

bool program_console_is_resetting(const GameState *g) { return g->reset; }

bool program_console_resets(GameState *g)
{
    bool reset = g->reset;
    g->reset = false;
    return reset;
}

void program_starts_afresh_from_the_title(GameState *g) { g->afresh = true; }

bool program_starts_afresh(GameState *g)
{
    bool afresh = g->afresh;
    g->afresh = false;
    return afresh;
}

void program_fail(GameState *g, uint32_t routine, const char *what)
{
    if (g->stopped)
        return;
    g->stopped = true;
    g->stopped_at = routine;
    g->stopped_what = what;
}

/* All game-state contract failures preserve the first diagnostic. */
void program_breaks_a_contract(GameState *g, uint32_t routine, const char *what)
{
    program_fail(g, routine, what);
}

size_t state_host_bytes(void) { return sizeof(GameState); }
size_t state_host_vars_bytes(void) { return sizeof(VarsHost); }
