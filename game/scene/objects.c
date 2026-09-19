/* Scenes: object routines in bank C6.
 *
 * The menu mode's sprite objects (menu/objects.c) run command scripts, and
 * command $4E calls a routine by its long address (JML [$005B] at $C2:1836):
 * the ones here are in bank C6, next to the scene tables they use. They run
 * with 8-bit A, 16-bit index registers and the menu's direct page; the object
 * running them is the $40-byte slot at the address in $4E, whose fields are
 * reached through the data bank. The Epoch's map screen also calls two of them
 * directly (from $C2:455E and $C2:4E6D).
 *
 * The script commands they return to are reached through an indirect jump,
 * which the liveness analysis doesn't follow, so for those every register and
 * flag counts as read after the return, and each entry leaves them as the
 * original does. */
#include "scene/scene.h"

#include "platform/video.h"
#include "port/machine.h"
#include "ram.h"
#include "scene/scene_data.h"

/* An object slot's fields used here (offsets in the slot). */
enum {
    OBJECT_X_FRACTION = 0x12,
    OBJECT_X = 0x14,
    OBJECT_Y_FRACTION = 0x16,
    OBJECT_Y = 0x18,
    OBJECT_VELOCITY_X = 0x1A,   /* 32-bit: fraction, then whole */
    OBJECT_VELOCITY_Y = 0x1E,
    OBJECT_PARAMETER = 0x22,
    OBJECT_PARAMETER_2 = 0x24,
};

static uint16_t object(Snes *s) { return m_read16(s, m_dp(s, RAM_MENU_OBJECT_CURRENT_DP)); }
static uint32_t field_at(Snes *s, uint16_t slot, uint8_t field) { return m_abs(s, field) + slot; }
static uint16_t field16(Snes *s, uint16_t slot, uint8_t field) { return m_read16(s, field_at(s, slot, field)); }
static void set_field16(Snes *s, uint16_t slot, uint8_t field, uint16_t v) { m_write16(s, field_at(s, slot, field), v); }

static uint8_t cell8(Snes *s, uint8_t dp) { return m_read8(s, m_dp(s, dp)); }
static void set_cell8(Snes *s, uint8_t dp, uint8_t v) { m_write8(s, m_dp(s, dp), v); }
static void set_cell16(Snes *s, uint8_t dp, uint16_t v) { m_write16(s, m_dp(s, dp), v); }

/* A 16-bit addition with the carry in, and the flags ADC leaves. */
typedef struct {
    bool carry, overflow;
} Sum;

static uint16_t add16(Sum *f, uint16_t a, uint16_t b)
{
    uint32_t sum = (uint32_t)a + b + (f->carry ? 1 : 0);
    f->carry = sum > 0xFFFF;
    f->overflow = (~(a ^ b) & (a ^ sum) & 0x8000) != 0;
    return (uint16_t)sum;
}

/* ---- Where the entries meet the original's registers -------------------------------- */

static void set_a_low(Snes *s, uint8_t v) { s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | v); }

/* A 16-bit result left in A by the last addition, with its flags, and A
 * 16-bit (the routine's REP #$20). */
static void return_sum16(Snes *s, uint16_t a, const Sum *f)
{
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    s->cpu.a = a;
    m_set_nz16(s, a);
    m_set_flag(s, CPU_FLAG_C, f->carry);
    m_set_flag(s, CPU_FLAG_V, f->overflow);
    m_rtl(s);
}

/* ---- Cells ---------------------------------------------------------------------------- */

// $C6:E237
/* $00 = the mode bits 2-5 of $1BA9 (8-bit A). */
void scene_object_read_mode(Snes *s)
{
    uint8_t v = (uint8_t)((m_read8(s, m_abs(s, RAM_ABS(RAM_MENU_SCENE_MODE_BITS))) & 0x3C) >> 2);
    set_cell8(s, 0x00, v);
    set_a_low(s, v);
    m_set_nz8(s, v);
    m_set_flag(s, CPU_FLAG_C, false);   /* bit 1 of the masked byte, shifted out */
    m_rtl(s);
}

/* TDC, then a byte stored: A is the direct page, which sets N and Z. */
static void store_page_low(Snes *s, uint32_t at)
{
    s->cpu.a = s->cpu.d;
    m_set_nz16(s, s->cpu.a);
    m_write8(s, at, (uint8_t)s->cpu.d);
    m_rtl(s);
}

// $C6:E2AF
/* Clear the story flag byte $7F:0062 (with the direct page's low byte, 0). */
void scene_object_clear_7f0062(Snes *s) { store_page_low(s, RAM_EVENT_MEMORY + 0x62); }

// $C6:E63D
/* Set the story flag byte $7F:01ED to 1. */
void scene_object_set_7f01ed(Snes *s)
{
    set_a_low(s, 1);
    m_set_nz8(s, 1);
    m_write8(s, RAM_EVENT_MEMORY + 0x1ED, 1);
    m_rtl(s);
}

// $C6:E644
/* Clear $7F:01ED (with the direct page's low byte, 0). */
void scene_object_clear_7f01ed(Snes *s) { store_page_low(s, RAM_EVENT_MEMORY + 0x1ED); }

// $C6:E568
/* Move both staged per-line table pointers ($7E:8A21, $7E:8A23) on by 8. */
void scene_object_advance_line_tables(Snes *s)
{
    Sum f = { false, false };
    m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS, add16(&f, m_read16(s, RAM_MENU_OBJECT_HDMA_POINTERS), 8));
    f.carry = false;
    uint16_t second = add16(&f, m_read16(s, RAM_MENU_OBJECT_HDMA_POINTERS + 2), 8);
    m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + 2, second);
    return_sum16(s, second, &f);
}

/* ---- Motion -------------------------------------------------------------------------- */

// $C6:E583
/* Step $28 by 2, down where the object's parameter is odd and up otherwise.
 * Leaves A the parameter halved and the carry its low bit, X the object. */
void scene_object_step_28(Snes *s)
{
    uint16_t slot = object(s);
    uint8_t parameter = m_read8(s, field_at(s, slot, OBJECT_PARAMETER));
    s->cpu.x = slot;
    set_a_low(s, parameter >> 1);
    m_set_flag(s, CPU_FLAG_C, parameter & 1);
    uint8_t v = (uint8_t)(cell8(s, 0x28) + ((parameter & 1) ? -2 : 2));
    set_cell8(s, 0x28, v);
    m_set_nz8(s, v);
    m_rtl(s);
}

// $C6:E590
/* Step $28 down by 2 (the odd half of $C6:E583, entered on its own). */
void scene_object_step_28_down(Snes *s)
{
    uint8_t v = (uint8_t)(cell8(s, 0x28) - 2);
    set_cell8(s, 0x28, v);
    m_set_nz8(s, v);
    m_rtl(s);
}

/* a + b + carry, reading a and then b, as the original's LDA and ADC do. */
static uint16_t add_reads(Snes *s, Sum *f, uint32_t a_at, uint32_t b_at)
{
    uint16_t a = m_read16(s, a_at);
    uint16_t b = m_read16(s, b_at);
    return add16(f, a, b);
}

// $C6:E77F
/* The object's 32-bit value at $1E-$21 += its 32-bit velocity at $22-$25. */
void scene_object_add_velocity(Snes *s)
{
    uint16_t slot = object(s);
    Sum f = { false, false };
    set_field16(s, slot, 0x1E, add_reads(s, &f, field_at(s, slot, 0x1E), field_at(s, slot, OBJECT_PARAMETER)));
    uint16_t high = add_reads(s, &f, field_at(s, slot, 0x20), field_at(s, slot, OBJECT_PARAMETER_2));
    set_field16(s, slot, 0x20, high);
    s->cpu.x = slot;
    return_sum16(s, high, &f);
}

// $C6:E2B5
/* Drift: the object's 32-bit position moves by its velocity, the whole part
 * going into BG3's scroll (the menu's shadows) rather than the object, plus
 * the pending scroll at $1BF1/$1BF3, which is used up; then the velocity
 * grows by a fixed acceleration. */
enum { DRIFT_ACCELERATION_X = 0x0DDB, DRIFT_ACCELERATION_Y = 0x0800 };
enum { BG3_SCROLL_X = RAM_MENU_OBJECT_SHADOWS_SCROLL_X - RAM_MENU_OBJECT_SHADOWS + RAM_MENU_OBJECT_SHADOWS_DP + 4,
       BG3_SCROLL_Y = RAM_MENU_OBJECT_SHADOWS_SCROLL_Y - RAM_MENU_OBJECT_SHADOWS + RAM_MENU_OBJECT_SHADOWS_DP + 4 };

/* A 32-bit field plus a constant, the carry through. */
static uint16_t add_to_field32(Snes *s, Sum *f, uint16_t slot, uint8_t field, uint16_t low)
{
    f->carry = false;
    set_field16(s, slot, field, add16(f, field16(s, slot, field), low));
    uint16_t high = add16(f, field16(s, slot, (uint8_t)(field + 2)), 0);
    set_field16(s, slot, (uint8_t)(field + 2), high);
    return high;
}

void scene_object_drift(Snes *s)
{
    uint16_t slot = object(s);
    Sum f = { false, false };
    set_field16(s, slot, OBJECT_X_FRACTION,
                add_reads(s, &f, field_at(s, slot, OBJECT_X_FRACTION), field_at(s, slot, OBJECT_VELOCITY_X)));
    set_cell16(s, BG3_SCROLL_X,
               add_reads(s, &f, m_dp(s, BG3_SCROLL_X), field_at(s, slot, OBJECT_VELOCITY_X + 2)));
    f.carry = false;
    set_field16(s, slot, OBJECT_Y_FRACTION,
                add_reads(s, &f, field_at(s, slot, OBJECT_Y_FRACTION), field_at(s, slot, OBJECT_VELOCITY_Y)));
    set_cell16(s, BG3_SCROLL_Y,
               add_reads(s, &f, m_dp(s, BG3_SCROLL_Y), field_at(s, slot, OBJECT_VELOCITY_Y + 2)));
    uint32_t pending_x = m_abs(s, RAM_ABS(RAM_MENU_PENDING_SCROLL_X));
    uint32_t pending_y = m_abs(s, RAM_ABS(RAM_MENU_PENDING_SCROLL_Y));
    f.carry = false;
    set_cell16(s, BG3_SCROLL_X, add_reads(s, &f, m_dp(s, BG3_SCROLL_X), pending_x));
    f.carry = false;
    set_cell16(s, BG3_SCROLL_Y, add_reads(s, &f, m_dp(s, BG3_SCROLL_Y), pending_y));
    m_write16(s, pending_x, 0);
    m_write16(s, pending_y, 0);
    add_to_field32(s, &f, slot, OBJECT_VELOCITY_X, DRIFT_ACCELERATION_X);
    uint16_t last = add_to_field32(s, &f, slot, OBJECT_VELOCITY_Y, DRIFT_ACCELERATION_Y);
    s->cpu.x = slot;
    return_sum16(s, last, &f);
}

/* ---- The Epoch's map ---------------------------------------------------------------- */

/* A position in tiles ($00 X, $01 Y) against a rectangle ($04-$05 in X, $02-$03
 * in Y, each start inclusive and end exclusive), and the sides it is outside
 * of in $06: 1 left, 2 right, 4 above, 8 below. */
enum { TILE_X = 0x00, TILE_Y = 0x01, AREA_TOP = 0x02, AREA_BOTTOM = 0x03, AREA_LEFT = 0x04, AREA_RIGHT = 0x05,
       OUTSIDE = 0x06 };
enum { OUTSIDE_LEFT = 1, OUTSIDE_RIGHT = 2, OUTSIDE_ABOVE = 4, OUTSIDE_BELOW = 8 };

// $C6:E74E
static uint8_t outside_sides(Snes *s)
{
    uint8_t sides = 0;
    set_cell8(s, OUTSIDE, 0);
    uint8_t x = cell8(s, TILE_X);
    if (x < cell8(s, AREA_LEFT))
        sides = OUTSIDE_LEFT;
    else if (x >= cell8(s, AREA_RIGHT))
        sides = OUTSIDE_RIGHT;
    if (sides)
        set_cell8(s, OUTSIDE, sides);
    uint8_t y = cell8(s, TILE_Y);
    if (y < cell8(s, AREA_TOP))
        set_cell8(s, OUTSIDE, (uint8_t)(cell8(s, OUTSIDE) | OUTSIDE_ABOVE));
    else if (y >= cell8(s, AREA_BOTTOM))
        set_cell8(s, OUTSIDE, (uint8_t)(cell8(s, OUTSIDE) | OUTSIDE_BELOW));
    return cell8(s, OUTSIDE);
}

void scene_epoch_outside_sides(Snes *s)
{
    outside_sides(s);
    m_rtl(s);
}

/* The areas of the world maps (locations $1F0-$1F7) the Epoch's check uses,
 * as the rectangle cells take them; a map with none is never inside. */
enum { WORLD_MAPS = 0x01F0, WORLD_MAP_COUNT = 8 };
static const struct {
    bool has_area;
    uint8_t top, bottom, left, right;
} world_map_areas[WORLD_MAP_COUNT] = {
    { true, 0x64, 0x7F, 0x41, 0x72 },
    { true, 0x76, 0x7F, 0x60, 0x6E },
    { false, 0, 0, 0, 0 },
    { true, 0x00, 0x34, 0x00, 0x0E },
    { true, 0x60, 0x7F, 0x00, 0x20 },
    { false, 0, 0, 0, 0 },
    { false, 0, 0, 0, 0 },
    { false, 0, 0, 0, 0 },
};

/* Guard: a location past the table would send the original's jump through
 * whatever follows it. */
bool scene_epoch_on_world_map(Snes *s)
{
    uint32_t at = m_abs(s, RAM_ABS(RAM_LOCATION_ID));
    uint16_t location = (uint16_t)(snes_bus_peek(&s->bus, at) | snes_bus_peek(&s->bus, (at + 1) & 0xFFFFFF) << 8);
    return (uint16_t)(location - WORLD_MAPS) < WORLD_MAP_COUNT;
}

// $C6:E797
/* Is the object at $4E, in tiles, inside the current world map's area? The
 * carry says so; its tile position is left in $00-$01, and the area in
 * $02-$05. Y ends as the object, or where there is an area, as the
 * rectangle's left and right bytes (the original's LDY #imm). */
void scene_epoch_in_area(Snes *s)
{
    Cpu *c = &s->cpu;
    uint16_t slot = object(s);
    set_cell16(s, TILE_X, (uint16_t)(m_read16(s, field_at(s, slot, OBJECT_X)) >> 3));
    set_cell16(s, TILE_Y, (uint16_t)(m_read16(s, field_at(s, slot, OBJECT_Y)) >> 3));
    uint16_t location = m_read16(s, m_abs(s, RAM_ABS(RAM_LOCATION_ID)));
    uint16_t index = (uint16_t)(location - WORLD_MAPS);
    /* the SBC's overflow is what the callers see */
    Sum f = { true, false };
    add16(&f, location, (uint16_t)~WORLD_MAPS);
    c->p |= CPU_FLAG_M;
    m_set_flag(s, CPU_FLAG_V, f.overflow);
    c->y = slot;
    bool inside = false;
    if (world_map_areas[index].has_area) {
        set_cell8(s, AREA_TOP, world_map_areas[index].top);
        set_cell8(s, AREA_BOTTOM, world_map_areas[index].bottom);
        set_cell8(s, AREA_LEFT, world_map_areas[index].left);
        set_cell8(s, AREA_RIGHT, world_map_areas[index].right);
        c->y = (uint16_t)(world_map_areas[index].right << 8 | world_map_areas[index].left);
        inside = outside_sides(s) == 0;
    }
    m_set_flag(s, CPU_FLAG_C, inside);
    m_rtl(s);
}

/* ---- Graphics loaders --------------------------------------------------------------- */

/* Decompress stream `offset` of `table` to $7F:dest (through the decompressor
 * at $C3:0002, a call-out); A holds the pointer's bank byte over its high
 * byte, X the offset, as the original leaves them for the call. */
static void load_stream(Snes *s, uint32_t site, uint16_t dest, uint32_t table, uint16_t offset)
{
    Cpu *c = &s->cpu;
    m_write16(s, m_abs(s, RAM_ABS(RAM_DECOMPRESS_DEST)), dest);
    m_write8(s, m_abs(s, RAM_ABS(RAM_DECOMPRESS_DEST) + 2), 0x7F);
    uint16_t word = scene_stream_word(s, table, offset);
    m_write16(s, m_abs(s, RAM_ABS(RAM_DECOMPRESS_SOURCE)), word);
    uint8_t bank = scene_stream_bank(s, table, offset);
    m_write8(s, m_abs(s, RAM_ABS(RAM_DECOMPRESS_SOURCE) + 2), bank);
    c->a = (uint16_t)((word & 0xFF00) | bank);
    c->x = offset;
    m_set_nz8(s, bank);
    m_jsl(s, site, 0xC30002);
    m_rtl(s);
}

// $C6:E028
void scene_object_load_graphics_2(Snes *s) { load_stream(s, 0xC6E048, 0x9000, SCENE_GRAPHICS_STREAMS, 6); }
// $C6:E04D
void scene_object_load_c3_stream_1(Snes *s) { load_stream(s, 0xC6E06D, 0x8C00, SCENE_C3_STREAMS, 3); }
// $C6:E64A
void scene_object_load_graphics_13(Snes *s) { load_stream(s, 0xC6E66A, 0x9000, SCENE_GRAPHICS_STREAMS, 0x27); }
// $C6:E66F
void scene_object_load_c3_stream_2(Snes *s) { load_stream(s, 0xC6E68F, 0x8C00, SCENE_C3_STREAMS, 6); }

/* ---- Per-line effects --------------------------------------------------------------- */

/* The menu's indirect per-line tables, one per slot from slot 1 at $7E:8A35:
 * two entries of 112 lines each, whose data pointers the vblank handler fills
 * in from the staged ones, and the end. */
enum { LINE_TABLES = RAM_MENU_OBJECT_WAVE_TABLE, LINE_TABLE_SIZE = 13, REPEAT_112_LINES = 0xF0 };

static uint32_t line_table(uint8_t slot) { return LINE_TABLES + (uint32_t)(slot - 1) * LINE_TABLE_SIZE; }

/* The table's line counts and its end: the end byte is the direct page's low
 * byte (TDC), 0. */
static void init_line_table(Snes *s, uint8_t slot)
{
    uint32_t table = line_table(slot);
    m_write8(s, table, REPEAT_112_LINES);
    m_write8(s, table + RAM_MENU_OBJECT_WAVE_TABLE_STRIDE, REPEAT_112_LINES);
    m_write8(s, table + 2 * RAM_MENU_OBJECT_WAVE_TABLE_STRIDE, (uint8_t)s->cpu.d);
}

/* Slot `slot` reads its table indirectly (the data in bank $7E) and drives
 * `target`; the menu's vblank handler enables it from the next frame (the
 * enables' shadow, $4A). STY $43x0 : STY $43x2 : STA $43x4 : STA $43x7. */
static const uint8_t line_effect_steps[] = {
    VID_STEP_CONTROL,     VID_STEP_DEST,          VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
    VID_STEP_SOURCE_BANK, VID_STEP_INDIRECT_BANK, VID_STEP_END,
};

static void start_line_effect(Snes *s, uint32_t site, uint8_t slot, VidLineTarget target)
{
    init_line_table(s, slot);
    uint32_t table = line_table(slot);
    const VidLineEffect effect = { .target = target, .table = PF_GAME(table), .indirect = true,
                                   .indirect_bank = (uint8_t)(table >> 16) };
    vid_set_line_effect(m_platform(s), PF_AT(site), slot, &effect, line_effect_steps);
    uint8_t bit = (uint8_t)(1 << slot);
    uint32_t enables = m_dp(s, RAM_MENU_OBJECT_LINE_EFFECTS_DP);
    uint8_t before = m_read8(s, enables);
    m_write8(s, enables, (uint8_t)(before | bit));
    /* A is the enable bit over the direct page's high byte, Y the table; N is
     * clear from the load, Z the TSB's test */
    s->cpu.a = (uint16_t)((s->cpu.d & 0xFF00) | bit);
    s->cpu.y = (uint16_t)table;
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, (before & bit) == 0);
}

/* The copies of these routines each program the channel at the same offset
 * from their start (the STY $43x0). */
enum { CHANNEL_WRITE_OFFSET = 0x12 };
static uint32_t channel_write_site(Snes *s)
{
    return ((uint32_t)s->cpu.pb << 16 | s->cpu.pc) + CHANNEL_WRITE_OFFSET;
}

/* Guard: they address the channel through the data bank, which has to reach
 * the registers. */
bool scene_object_bank_reaches_io(Snes *s) { return m_io_bank(s->cpu.db); }

// $C6:E000, $C6:E318, $C6:E448
/* Window 1's edges per line, on slot 1. */
void scene_object_window1_lines(Snes *s)
{
    start_line_effect(s, channel_write_site(s), 1, VID_LINE_WINDOW1);
    m_rtl(s);
}

// $C6:E340, $C6:E470
/* Window 2's edges per line, on slot 2. */
void scene_object_window2_lines(Snes *s)
{
    start_line_effect(s, channel_write_site(s), 2, VID_LINE_WINDOW2);
    m_rtl(s);
}

// $C6:E595
/* BG1's vertical scroll per line, on slot 3, and the object's parameter
 * cleared. X ends as the object. */
void scene_object_bg1_scroll_lines(Snes *s)
{
    start_line_effect(s, channel_write_site(s), 3, VID_LINE_SCROLL_BG1_V);
    uint16_t slot = object(s);
    m_write8(s, field_at(s, slot, OBJECT_PARAMETER), 0);
    m_write8(s, field_at(s, slot, OBJECT_PARAMETER + 1), 0);
    s->cpu.x = slot;
    m_set_nz16(s, slot);
    m_rtl(s);
}
