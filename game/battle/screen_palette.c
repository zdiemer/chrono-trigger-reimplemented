/* Battle: palette effects in bank D1.
 *
 * The battle keeps the palette twice, as loaded (palette_base, $7E:2000) and
 * as shown (palette_working, $7E:2200); the palette animation slots ($0520,
 * core/palette.c) run on top of both. The effects here are the screen script's
 * palette commands (screen.h): moving or whitening rows of both copies, and
 * the palette animation slots that go with them; the per-frame HP gradient,
 * whose colors follow the first enemy's HP and rotate through three palette
 * rows; loading an effect's graphics and palette; and pausing palette
 * animation while the battle is paused.
 *
 * Each routine is a C function of what it does; its entry, registered in
 * game/registry.c, only moves the original's registers in and out. A color is
 * two bytes, so a byte offset into a palette is twice the color index, which
 * is how the original indexes them. */
#include "battle/screen.h"

#include "battle/screen_data.h"
#include "port/machine.h"
#include "ram.h"

enum {
    COLOR_BYTES = 2,
    ROW_COLORS = 16,
    WHITE = 0x7FFF,
    /* palette animation slots, 12 bytes each */
    SLOT_BYTES = RAM_PALETTE_ANIMATIONS_STRIDE,
    ANIMATED_SLOTS = 8,
    KIND_FADE_ROWS = 0x30,
    /* the HP gradient: 14 colors in each of three rows */
    GRADIENT_COLORS = 14,
    GRADIENT_FADE_FRAMES = 0x20,
    GRADIENT_LEVELS = 8,
    /* battle_background_flags */
    BACKGROUND_HP_GRADIENT = 0x01,
    BACKGROUND_SWAP_ROWS = 0x02,
};

bool battle_screen_page(Snes *s) { return s->cpu.d == 0x0000 && s->cpu.db == 0x7E; }

/* ---- The machine at the edges ---------------------------------------------------------
 *
 * The routines run with 16-bit index registers, and return with 8-bit A and
 * the value callers find in A (only its high byte is live after any of them,
 * but no declaration can leave out the low byte alone). */

static void set_a(Snes *s, uint16_t a)
{
    s->cpu.a = a;
    s->cpu.p |= CPU_FLAG_M;
}

/* Byte offsets into a palette run on past its end as the original's indexed
 * accesses do, into bank $7F. */
static uint16_t color(Snes *s, uint32_t palette, uint32_t offset) { return m_read16(s, palette + offset); }
static void set_color(Snes *s, uint32_t palette, uint32_t offset, uint16_t v) { m_write16(s, palette + offset, v); }

/* Set a color in both copies of the palette. */
static void set_both(Snes *s, uint32_t offset, uint16_t v)
{
    set_color(s, RAM_PALETTE_BASE, offset, v);
    set_color(s, RAM_PALETTE_WORKING, offset, v);
}

static uint32_t slot_field(int slot, uint32_t field) { return field + (uint32_t)slot * SLOT_BYTES; }

// $D1:E70A
/* Screen command $E1: swap palette rows 9-11 with rows 13-15 (the sprite
 * palettes 1-3 and 5-7) in both copies, undoing a flip of the sprites'
 * palettes (which $CD:1B78 does to their attributes); when the background
 * flags' bit 1 is set, palette animation slot 7 follows its colors (its first
 * color moves by four rows). The command has no operand. */
static void swap_palette_rows(Snes *s)
{
    m_write8(s, RAM_BATTLE_SPRITE_PALETTES_FLIPPED, 0);
    if (m_read8(s, RAM_BATTLE_BACKGROUND_FLAGS) & BACKGROUND_SWAP_ROWS) {
        uint32_t first = slot_field(7, RAM_PALETTE_ANIMATIONS_FIRST_COLOR);
        m_write8(s, first, m_read8(s, first) ^ 0x40);
    }
    for (uint16_t i = 0; i < ROW_COLORS * COLOR_BYTES; i += COLOR_BYTES) {
        static const struct { uint32_t palette; uint16_t row; } rows[6] = {
            { RAM_PALETTE_WORKING, 9 }, { RAM_PALETTE_WORKING, 10 }, { RAM_PALETTE_WORKING, 11 },
            { RAM_PALETTE_BASE, 9 },    { RAM_PALETTE_BASE, 10 },    { RAM_PALETTE_BASE, 11 },
        };
        for (int r = 0; r < 6; r++) {
            uint16_t low = (uint16_t)(rows[r].row * ROW_COLORS * COLOR_BYTES + i);
            uint16_t high = (uint16_t)(low + 4 * ROW_COLORS * COLOR_BYTES);
            uint16_t v = color(s, rows[r].palette, low);
            set_color(s, rows[r].palette, low, color(s, rows[r].palette, high));
            set_color(s, rows[r].palette, high, v);
        }
    }
    /* no operand: back one byte (a 16-bit DEC, which leaves the bank alone) */
    m_write16(s, RAM_BATTLE_SCREEN_SCRIPT, (uint16_t)(m_read16(s, RAM_BATTLE_SCREEN_SCRIPT) - 1));
}

void battle_screen_swap_palette_rows(Snes *s)
{
    swap_palette_rows(s);
    set_a(s, 0);
    m_rtl(s);
}

// $D1:E899
/* Screen command $80, operation 26 ($CD:2A4A dispatches on its operand):
 * turn colors $20-$7F and $90-$BF white in both copies. */
void battle_screen_whiten_palette(Snes *s)
{
    for (uint16_t i = 0; i < 0x60 * COLOR_BYTES; i += COLOR_BYTES)
        set_both(s, (uint16_t)(0x20 * COLOR_BYTES + i), WHITE);
    for (uint16_t i = 0; i < 0x30 * COLOR_BYTES; i += COLOR_BYTES)
        set_both(s, (uint16_t)(0x90 * COLOR_BYTES + i), WHITE);
    set_a(s, 0);
    m_rtl(s);
}

// $D1:E8C1
/* Command $80, operation 28: turn colors $D0-$FF (sprite palettes 5-7)
 * white in both copies. */
void battle_screen_whiten_high_rows(Snes *s)
{
    for (uint16_t i = 0; i < 0x30 * COLOR_BYTES; i += COLOR_BYTES)
        set_both(s, (uint16_t)(0xD0 * COLOR_BYTES + i), WHITE);
    set_a(s, 0);
    m_rtl(s);
}

/* The rows the two commands below move: colors $20-$4F and $50-$7F, and the
 * palette animation slots that go with them, 0-2 and 3-5. */
enum { LOW_ROWS = 0x20 * COLOR_BYTES, HIGH_ROWS = 0x50 * COLOR_BYTES, MOVED = 0x30 * COLOR_BYTES };

// $D1:E91A
/* Command $80, operation 24: move background rows 2-4 up to rows 5-7: the loaded colors go to both
 * copies of the upper rows and the lower rows turn black, and the animations
 * of slots 3-5 move to slots 0-2 with their colors, as fades of three rows;
 * their saved kinds say the same. Clears the rows-moved flag. */
static void move_rows_up(Snes *s)
{
    for (uint16_t i = 0; i < MOVED; i += COLOR_BYTES) {
        uint16_t v = color(s, RAM_PALETTE_BASE, (uint16_t)(LOW_ROWS + i));
        set_both(s, (uint16_t)(HIGH_ROWS + i), v);
        set_both(s, (uint16_t)(LOW_ROWS + i), 0);
    }
    for (uint16_t i = 0; i < 3 * SLOT_BYTES; i++)
        m_write8(s, RAM_PALETTE_ANIMATIONS + i, m_read8(s, RAM_PALETTE_ANIMATIONS + 3 * SLOT_BYTES + i));
    for (int slot = 0; slot < 3; slot++) {
        uint32_t first = slot_field(slot, RAM_PALETTE_ANIMATIONS_FIRST_COLOR);
        m_write8(s, first, (uint8_t)(m_read8(s, first) + 0x30));
    }
    for (int slot = 0; slot < 3; slot++) {
        m_write8(s, slot_field(slot, RAM_PALETTE_ANIMATIONS_KIND), KIND_FADE_ROWS);
        m_write8(s, RAM_BATTLE_SAVED_PALETTE_TYPES + (uint32_t)slot, KIND_FADE_ROWS);
    }
    for (int slot = 3; slot < 6; slot++)
        m_write8(s, RAM_BATTLE_SAVED_PALETTE_TYPES + (uint32_t)slot, 0);
    m_write8(s, RAM_BATTLE_PALETTE_ROWS_MOVED, 0);
    m_write8(s, RAM_BATTLE_PALETTE_PAUSED, (uint8_t)(m_read8(s, RAM_BATTLE_PALETTE_PAUSED) + 1));
}

void battle_screen_move_rows_up(Snes *s)
{
    move_rows_up(s);
    set_a(s, KIND_FADE_ROWS);
    m_rtl(s);
}

// $D1:E984
/* Command $80, operation 23, the way back: the lower rows get their starting colors ($D0:FD00) in both
 * copies, and slots 0-2 move back to slots 3-5, their first colors down three
 * rows, each stepping on its next frame. Sets the rows-moved flag and the
 * reduced frame work. */
static void move_rows_down(Snes *s)
{
    m_write8(s, RAM_BATTLE_PALETTE_ROWS_MOVED, (uint8_t)(m_read8(s, RAM_BATTLE_PALETTE_ROWS_MOVED) + 1));
    m_write8(s, RAM_BATTLE_FRAME_WORK_REDUCED, (uint8_t)(m_read8(s, RAM_BATTLE_FRAME_WORK_REDUCED) + 1));
    for (uint16_t i = 0; i < MOVED; i += COLOR_BYTES)
        set_both(s, (uint16_t)(LOW_ROWS + i), shift_palette_color(s, i));
    for (uint16_t i = 0; i < 3 * SLOT_BYTES; i++)
        m_write8(s, RAM_PALETTE_ANIMATIONS + 3 * SLOT_BYTES + i, m_read8(s, RAM_PALETTE_ANIMATIONS + i));
    for (int slot = 3; slot < 6; slot++) {
        uint32_t first = slot_field(slot, RAM_PALETTE_ANIMATIONS_FIRST_COLOR);
        m_write8(s, first, (uint8_t)(m_read8(s, first) - 0x30));
    }
    for (int slot = 3; slot < 6; slot++)
        m_write16(s, slot_field(slot, RAM_PALETTE_ANIMATIONS_DELAY), 0x0100);   /* delay 0, period 1 */
    for (int slot = 3; slot < 6; slot++) {
        m_write8(s, slot_field(slot, RAM_PALETTE_ANIMATIONS_KIND), KIND_FADE_ROWS);
        m_write8(s, RAM_BATTLE_SAVED_PALETTE_TYPES + (uint32_t)slot, KIND_FADE_ROWS);
    }
    m_write8(s, RAM_BATTLE_PALETTE_PAUSED, (uint8_t)(m_read8(s, RAM_BATTLE_PALETTE_PAUSED) + 1));
}

void battle_screen_move_rows_down(Snes *s)
{
    move_rows_down(s);
    set_a(s, KIND_FADE_ROWS);
    m_rtl(s);
}

/* ---- The HP gradient ----------------------------------------------------------------- */

// $D1:EA01
/* The gradient's phase: where the first color of level 0's set sits among the
 * 14 colors from $51 in the loaded palette (0 if it isn't there). */
static void find_gradient_phase(Snes *s)
{
    uint16_t first = gradient_color(s, 0);
    uint8_t phase = 0;
    for (uint16_t i = 0; i < GRADIENT_COLORS; i++) {
        if (first == color(s, RAM_PALETTE_BASE, (uint16_t)(0x51 * COLOR_BYTES + i * COLOR_BYTES))) {
            phase = (uint8_t)i;
            break;
        }
    }
    m_write8(s, RAM_BATTLE_GRADIENT_PHASE, phase);
}

void battle_find_gradient_phase(Snes *s)
{
    find_gradient_phase(s);
    set_a(s, m_read8(s, RAM_BATTLE_GRADIENT_PHASE));
    m_rts(s);
}

/* One 5-bit channel of a color one step toward the target's: `mask` picks the
 * channel (in the low byte, the word or the high byte, as the original does
 * it), `unit` is its lowest bit. */
static void step_red(Snes *s, uint32_t at, uint8_t target)
{
    uint8_t v = m_read8(s, at);
    uint8_t now = v & 0x1F, want = target & 0x1F;
    if (now > want)
        m_write8(s, at, (uint8_t)(v - 1));
    else if (now < want)
        m_write8(s, at, (uint8_t)(v + 1));
}

static void step_green(Snes *s, uint32_t at, uint16_t target)
{
    uint16_t v = m_read16(s, at);
    uint16_t now = v & 0x03E0, want = target & 0x03E0;
    if (now > want)
        m_write16(s, at, (uint16_t)(v - 0x20));
    else if (now < want)
        m_write16(s, at, (uint16_t)(v + 0x20));
}

static void step_blue(Snes *s, uint32_t at, uint8_t target)
{
    uint8_t v = m_read8(s, at);
    uint8_t now = v & 0x7C, want = target & 0x7C;
    if (now > want)
        m_write8(s, at, (uint8_t)(v - 4));
    else if (now < want)
        m_write8(s, at, (uint8_t)(v + 4));
}

/* Fade the gradient's 14 colors one step toward the set of the level shown.
 * The level is looked up with A's high byte as the index's (TAX after an 8-bit
 * ASL). */
static void fade_gradient(Snes *s, uint8_t index_high)
{
    uint16_t level_x2 = (uint16_t)(index_high << 8 | (uint8_t)(m_read8(s, RAM_BATTLE_GRADIENT_LEVEL) << 1));
    uint16_t target = gradient_set_offset(s, level_x2);
    for (uint16_t i = 0; i < GRADIENT_COLORS * COLOR_BYTES; i += COLOR_BYTES, target += COLOR_BYTES) {
        uint32_t at = RAM_BATTLE_GRADIENT_COLORS + i;
        uint8_t want = gradient_color_byte(s, target) & 0x1F;
        m_write8(s, RAM_BATTLE_SCRATCH, want);
        step_red(s, at, want);
        uint16_t want16 = gradient_color(s, target) & 0x03E0;
        m_write16(s, RAM_BATTLE_SCRATCH, want16);
        step_green(s, at, want16);
        want = gradient_color_byte(s, (uint16_t)(target + 1)) & 0x7C;
        m_write8(s, RAM_BATTLE_SCRATCH, want);
        step_blue(s, at + 1, want);
    }
    uint8_t left = (uint8_t)(m_read8(s, RAM_BATTLE_GRADIENT_FADE_FRAMES) - 1);
    m_write8(s, RAM_BATTLE_GRADIENT_FADE_FRAMES, left);
    if (left == 0)
        m_write8(s, RAM_BATTLE_GRADIENT_FADING, 0);
}

// $D1:EBD0
/* How far a row is rotated: its table entry, or 14 less it when the gradient
 * runs the other way. */
static uint8_t row_rotation(Snes *s, uint8_t rotation)
{
    return m_read8(s, RAM_BATTLE_GRADIENT_REVERSED) ? rotation : (uint8_t)(GRADIENT_COLORS - rotation);
}

// $D1:EBDF
/* Copy the 14 colors starting `rotation` colors into the gradient (and on into
 * its copy after it) into both palettes at byte offset `to` from color $51. */
static void write_gradient_row(Snes *s, uint16_t rotation, uint16_t to)
{
    uint16_t from = (uint16_t)(rotation << 1);
    for (int n = 0; n < GRADIENT_COLORS; n++, from += COLOR_BYTES, to += COLOR_BYTES)
        set_both(s, 0x51 * COLOR_BYTES + (uint32_t)to, m_read16(s, RAM_BATTLE_GRADIENT_COLORS + from));
    m_write16(s, RAM_BATTLE_SCRATCH, 0);   /* the count it ran down */
}

/* The HP level: 1 plus the eighths of the first enemy's maximum HP it has
 * (at most 8). The thresholds are left in the scratch cells. */
static uint8_t hp_level(Snes *s)
{
    uint16_t hp = m_read16(s, RAM_BATTLE_STATS_HP + 3 * RAM_BATTLE_STATS_STRIDE);
    uint16_t eighth = (uint16_t)(m_read16(s, RAM_BATTLE_STATS_MAX_HP + 3 * RAM_BATTLE_STATS_STRIDE) >> 3);
    uint16_t threshold = eighth;
    m_write16(s, RAM_BATTLE_SCRATCH, threshold);
    m_write16(s, RAM_BATTLE_SCRATCH + 2, eighth);
    uint8_t level = 1;
    while (hp >= threshold) {
        threshold = (uint16_t)(threshold + eighth);
        m_write16(s, RAM_BATTLE_SCRATCH, threshold);
        if (++level == GRADIENT_LEVELS)
            break;
    }
    return level;
}

/* Returns what the original leaves in A. */
static uint16_t step_hp_gradient(Snes *s, uint16_t a)
{
    uint8_t busy = m_read8(s, RAM_BATTLE_SCREEN_SCRIPTS_RUNNING);
    for (int slot = 11; slot >= 8; slot--)
        busy |= m_read8(s, slot_field(slot, RAM_PALETTE_ANIMATIONS_KIND));
    if (busy)
        return (uint16_t)((a & 0xFF00) | busy);

    if (m_read8(s, RAM_BATTLE_GRADIENT_FADING)) {
        fade_gradient(s, (uint8_t)(a >> 8));
        for (uint16_t i = 0; i < GRADIENT_COLORS * COLOR_BYTES; i += COLOR_BYTES)
            m_write16(s, RAM_BATTLE_GRADIENT_COLORS + GRADIENT_COLORS * COLOR_BYTES + i,
                      m_read16(s, RAM_BATTLE_GRADIENT_COLORS + i));
        uint8_t phase = m_read8(s, RAM_BATTLE_GRADIENT_PHASE);
        for (int row = 0; row < 3; row++) {
            uint8_t rotation = gradient_row_rotation(s, row, phase);
            m_write8(s, RAM_BATTLE_SCRATCH, rotation);
            write_gradient_row(s, row_rotation(s, rotation), (uint16_t)(row * 0x20));
        }
        return 0;
    }

    uint8_t level = hp_level(s);
    uint8_t override = m_read8(s, RAM_BATTLE_GRADIENT_LEVEL_OVERRIDE);
    if (override & 0x80)
        level = override & 0x7F;
    m_write8(s, RAM_BATTLE_GRADIENT_LEVEL, level);
    uint8_t shown = m_read8(s, RAM_BATTLE_GRADIENT_LEVEL_SHOWN);
    if (level == shown)
        return level;

    /* start a fade from the level shown toward the new one, one level at a
     * time unless the level is overridden */
    m_write8(s, RAM_BATTLE_GRADIENT_FADING, (uint8_t)(m_read8(s, RAM_BATTLE_GRADIENT_FADING) + 1));
    m_write8(s, RAM_BATTLE_GRADIENT_FADE_FRAMES, GRADIENT_FADE_FRAMES);
    uint16_t from = gradient_set_offset(s, (uint8_t)(shown << 1));
    if (override & 0x80) {
        m_write8(s, RAM_BATTLE_GRADIENT_LEVEL_SHOWN, level);
    } else {
        shown = level >= shown ? (uint8_t)(shown + 1) : (uint8_t)(shown - 1);
        m_write8(s, RAM_BATTLE_GRADIENT_LEVEL_SHOWN, shown);
        m_write8(s, RAM_BATTLE_GRADIENT_LEVEL, shown);
    }
    for (uint16_t i = 0; i < GRADIENT_COLORS * COLOR_BYTES; i += COLOR_BYTES, from += COLOR_BYTES)
        m_write16(s, RAM_BATTLE_GRADIENT_COLORS + i, gradient_color(s, from));
    return 0;
}

// $D1:EA4B
/* Each frame of a battle with the HP gradient (background flags bit 0), unless
 * the screen scripts or palette animation slots 8-11 are busy: while a fade
 * runs, step the gradient's colors one step toward the level's set and write
 * them into three palette rows at the rotation their phase gives; otherwise
 * start a fade when the first enemy's HP level (or the override) changed. */
void battle_step_hp_gradient(Snes *s)
{
    set_a(s, step_hp_gradient(s, s->cpu.a));
    m_rtl(s);
}

/* $D1:EBD0 and $D1:EBDF as entries of their own, for the burndown and so that
 * shadow mode compares them where the original calls them. */
void battle_gradient_row_rotation(Snes *s)
{
    uint8_t rotation = row_rotation(s, m_read8(s, RAM_BATTLE_SCRATCH));
    m_set_a8(s, rotation);
    m_rts(s);
}

void battle_write_gradient_row(Snes *s)
{
    write_gradient_row(s, s->cpu.a, s->cpu.y);
    s->cpu.y = (uint16_t)(s->cpu.y + GRADIENT_COLORS * COLOR_BYTES);
    set_a(s, 0);
    m_rts(s);
}

/* ---- Effect graphics ---------------------------------------------------------------- */

enum {
    DECOMPRESS_BUFFER = 0xD000,       /* in $7E */
    EFFECT_BLOCK_VRAM = 0x2000,
    EFFECT_BLOCK_WORDS = 0x0800,
    EFFECT_BLOCK_BYTES = 0x1000,
    EXTRA_GRAPHICS_VRAM = 0x6800,
    EXTRA_GRAPHICS_BYTES = 0x0800,
    EFFECT_PALETTE_FIRST = 0x20,
};

/* JSL $C3:0002 with the source and destination in its pointers; it takes no
 * registers. */
static void decompress_to_buffer(Snes *s, uint32_t site, uint32_t source)
{
    m_write16(s, RAM_DECOMPRESS_SOURCE, (uint16_t)source);
    m_write16(s, RAM_DECOMPRESS_DEST, DECOMPRESS_BUFFER);
    m_write8(s, RAM_DECOMPRESS_SOURCE + 2, (uint8_t)(source >> 16));
    m_write8(s, RAM_DECOMPRESS_DEST + 2, 0x7E);
    m_jsl_dc(s, site, 0xC30002, DC_A | DC_X | DC_Y | DC_FLAGS);
}

/* JSL $CD:3ECE: queue a VRAM transfer of `bytes` from the buffer to `vram`
 * and wait for it. It takes the source in A:X, the address in Y, the size in
 * $53, and V and C, which it saves; the original gets them from the
 * decompressor, so the port leaves them as the call before left them. */
static void transfer_buffer(Snes *s, uint32_t site, uint16_t vram, uint16_t bytes)
{
    m_write16(s, RAM_BATTLE_SCRATCH + 0x0E, bytes);
    s->cpu.y = vram;
    s->cpu.x = DECOMPRESS_BUFFER;
    m_set_a8(s, 0x7E);
    s->cpu.p |= CPU_FLAG_M;
    m_jsl_dc(s, site, 0xCD3ECE, DC_N | DC_Z);
}

// $D1:ECF3
/* Load effect A's graphics: up to four compressed blocks into VRAM $2000,
 * $2800, $3000 and $3800 (a block id of $FF leaves its place alone), colors
 * $20-$4F of the loaded palette, and one more block into VRAM $6800. Each
 * block is decompressed to $7E:D000 and handed to the frame work's VRAM
 * transfer, which waits for it. $CD43 is set meanwhile. */
void battle_load_effect_graphics(Snes *s)
{
    uint8_t effect = (uint8_t)s->cpu.a;
    m_write8(s, RAM_BATTLE_EFFECT_GRAPHICS_LOADING, (uint8_t)(m_read8(s, RAM_BATTLE_EFFECT_GRAPHICS_LOADING) + 1));
    m_write8(s, RAM_BATTLE_EFFECT_GRAPHICS_ID, effect);
    /* TAX of the 8-bit id times four takes A's high byte along */
    m_write16(s, RAM_BATTLE_EFFECT_GRAPHICS_LIST, (uint16_t)((s->cpu.a & 0xFF00) | (uint8_t)(effect << 2)));
    m_write16(s, RAM_BATTLE_EFFECT_GRAPHICS_VRAM, EFFECT_BLOCK_VRAM);
    m_write8(s, RAM_BATTLE_EFFECT_GRAPHICS_BLOCK, 0);
    do {
        uint8_t id = effect_graphics_block_id(s, m_read16(s, RAM_BATTLE_EFFECT_GRAPHICS_LIST));
        if (id != 0xFF) {
            /* STA $53 (16-bit, the id's high byte from the last TDC) : ASL : ADC $53 */
            uint16_t id16 = id;
            m_write16(s, RAM_BATTLE_SCRATCH + 0x0E, id16);
            uint32_t sum = (uint32_t)(uint16_t)(id16 << 1) + id16;
            uint16_t offset = (uint16_t)sum;
            m_set_flag(s, CPU_FLAG_C, sum > 0xFFFF);
            m_set_flag(s, CPU_FLAG_V, (~((id16 << 1) ^ id16) & ((id16 << 1) ^ sum) & 0x8000) != 0);
            decompress_to_buffer(s, 0xD1ED38, graphics_block_pointer(s, offset));
            transfer_buffer(s, 0xD1ED49, m_read16(s, RAM_BATTLE_EFFECT_GRAPHICS_VRAM), EFFECT_BLOCK_BYTES);
            if (s->cpu.e || (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
                return;
        }
        m_write16(s, RAM_BATTLE_EFFECT_GRAPHICS_VRAM,
                  (uint16_t)(m_read16(s, RAM_BATTLE_EFFECT_GRAPHICS_VRAM) + EFFECT_BLOCK_WORDS));
        m_write16(s, RAM_BATTLE_EFFECT_GRAPHICS_LIST, (uint16_t)(m_read16(s, RAM_BATTLE_EFFECT_GRAPHICS_LIST) + 1));
        m_write8(s, RAM_BATTLE_EFFECT_GRAPHICS_BLOCK, (uint8_t)(m_read8(s, RAM_BATTLE_EFFECT_GRAPHICS_BLOCK) + 1));
    } while (m_read8(s, RAM_BATTLE_EFFECT_GRAPHICS_BLOCK) != 4);

    uint16_t from = effect_palette_offset(s, (uint8_t)(effect << 1));
    for (uint16_t i = 0; i < 0x30 * COLOR_BYTES; i += COLOR_BYTES, from += COLOR_BYTES)
        set_color(s, RAM_PALETTE_BASE, (uint16_t)(EFFECT_PALETTE_FIRST * COLOR_BYTES + i),
                  effect_palette_color(s, from));

    /* STA $53 (8-bit) : ASL : ADC $53, an 8-bit offset of three bytes per id */
    m_write8(s, RAM_BATTLE_SCRATCH + 0x0E, effect);
    uint16_t sum = (uint16_t)((uint8_t)(effect << 1) + effect);
    m_set_flag(s, CPU_FLAG_C, sum > 0xFF);
    m_set_flag(s, CPU_FLAG_V, (~((uint8_t)(effect << 1) ^ effect) & ((uint8_t)(effect << 1) ^ sum) & 0x80) != 0);
    decompress_to_buffer(s, 0xD1EDB4, effect_extra_graphics_pointer(s, (uint8_t)sum));
    transfer_buffer(s, 0xD1EDC5, EXTRA_GRAPHICS_VRAM, EXTRA_GRAPHICS_BYTES);
    if (s->cpu.e || (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
        return;
    m_write8(s, RAM_BATTLE_EFFECT_GRAPHICS_LOADING, 0);
    m_rtl(s);
}

/* ---- Pausing palette animation ------------------------------------------------------ */

// $D1:F411
/* Save the kinds of palette animation slots 0-7. */
static void save_palette_kinds(Snes *s)
{
    for (int slot = 0; slot < ANIMATED_SLOTS; slot++)
        m_write8(s, RAM_BATTLE_SAVED_PALETTE_TYPES + (uint32_t)slot,
                 m_read8(s, slot_field(slot, RAM_PALETTE_ANIMATIONS_KIND)));
}

void battle_save_palette_kinds(Snes *s)
{
    save_palette_kinds(s);
    s->cpu.x = ANIMATED_SLOTS * SLOT_BYTES;
    s->cpu.y = ANIMATED_SLOTS;
    m_rts(s);
}

// $D1:F426
/* While the pause request is set, pause palette animation once: save slots
 * 0-7's kinds and stop the slots of kind 1 or with bit 7 set. Once the
 * request is clear again, put the saved kinds back. */
void battle_pause_palette_animation(Snes *s)
{
    if (m_read8(s, RAM_BATTLE_PALETTE_PAUSE_REQUEST)) {
        if (!m_read8(s, RAM_BATTLE_PALETTE_PAUSED)) {
            m_write8(s, RAM_BATTLE_PALETTE_PAUSED, (uint8_t)(m_read8(s, RAM_BATTLE_PALETTE_PAUSED) + 1));
            save_palette_kinds(s);
            for (int slot = 0; slot < ANIMATED_SLOTS; slot++) {
                uint32_t kind = slot_field(slot, RAM_PALETTE_ANIMATIONS_KIND);
                uint8_t v = m_read8(s, kind);
                if ((v & 0x80) || (v & 0xF0) == 0x10)
                    m_write8(s, kind, 0);
            }
        }
    } else if (m_read8(s, RAM_BATTLE_PALETTE_PAUSED)) {
        m_write8(s, RAM_BATTLE_PALETTE_PAUSED, 0);
        for (int slot = 0; slot < ANIMATED_SLOTS; slot++)
            m_write8(s, slot_field(slot, RAM_PALETTE_ANIMATIONS_KIND),
                     m_read8(s, RAM_BATTLE_SAVED_PALETTE_TYPES + (uint32_t)slot));
    }
    m_rtl(s);
}
