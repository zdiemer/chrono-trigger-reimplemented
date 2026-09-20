/* Menu: the object scripts' commands, and starting an object.
 *
 * Every sprite object of the menus' object mode (menu/objects.c) can run a
 * command script. The interpreter at $C2:0F63 takes a command byte through the
 * script pointer, calls the handler from the table at $C2:0F91, and reads what
 * the handler hands back:
 *
 * - a length moves the script on by that many bytes and runs the next command;
 * - zero leaves the object where it is until the next frame;
 * - zero with carry set frees the object's slot;
 * - a signed offset with carry clear is a branch, moved by the same addition.
 *
 * Commands reach three places: the running object's slot, by an offset into it
 * (a "slot byte"); low RAM, by a 16-bit address (a "cell"); and their own
 * operand bytes, which follow the command byte in the script.
 *
 * tools/disasm/objscript.py parses the scripts and names the same commands.
 * Every handler runs with 16-bit A and index registers, the menus' direct page
 * and the data bank on low RAM, and hands back A, X, the zero flag and the
 * carry; Y and the sign and overflow flags are dead after it.
 *
 * Still the original's, called out to: the map's own scrolling and redrawing
 * ($C2:0568, $C2:066C, $C2:09C5), the sound queue ($C2:2ED9), the sprite script
 * interpreter ($C2:0E1D, ported in menu/objects.c), and the two jumps a script
 * makes through an address it gives ($C2:1819, $C2:1836). */
#include "menu/object_script.h"

#include "port/machine.h"
#include "port/native.h"
#include "ram.h"

/* ---- the script, the slot and low RAM ---------------------------------------------------- */

/* Every command runs on the menus' direct page with the data bank on low RAM,
 * which is where the slot and the cells it names are. */
bool menu_object_script_page_is_menu(Snes *s) { return s->cpu.d == RAM_PAGE_MENU && s->cpu.db == 0x00; }

/* The running object's slot. */
static uint16_t object(Snes *s) { return m_read16(s, m_dp(s, RAM_MENU_OBJECT_CURRENT_DP)); }

/* An address `index` past `base`, as the original's indexed and
 * indirect-indexed forms make it: the sum carries out of the bank. */
static uint32_t at(Snes *s, uint16_t base, uint16_t index)
{
    return (m_abs(s, base) + index) & 0xFFFFFFu;
}

/* A cell of low RAM a command names by its 16-bit address. */
static uint32_t cell_at(Snes *s, uint16_t addr) { return at(s, 0, addr); }

/* Byte `offset` of the command being run, through the script's long pointer. */
static uint32_t script_at(Snes *s, uint16_t offset)
{
    uint32_t p = (uint32_t)m_read16(s, m_dp(s, RAM_MENU_OBJECT_SCRIPT_DP))
               | (uint32_t)m_read8(s, m_dp(s, RAM_MENU_OBJECT_SCRIPT_DP + 2)) << 16;
    return (p + offset) & 0xFFFFFFu;
}
static uint8_t operand(Snes *s, uint16_t offset) { return m_read8(s, script_at(s, offset)); }
static uint16_t operand16(Snes *s, uint16_t offset) { return m_read16(s, script_at(s, offset)); }

/* An operand that names a slot offset or a low-RAM address. The original loads
 * it with an 8-bit accumulator and passes the whole register on, so whatever
 * the caller left in A's high byte goes with it; the interpreter always calls
 * with it zero. */
static uint16_t wide(Snes *s, uint8_t byte) { return (uint16_t)((s->cpu.a & 0xFF00u) | byte); }

/* The fields of a slot these commands use, beyond the ones game/ram.h names. */
enum {
    SAVED_RETURN = 0x05,       /* where a call command leaves the script pointer to come back to */
    SPRITE_SCRIPT = 0x0B,      /* the sprite script's long pointer, and its countdown at +$0E */
    SPRITE_SCRIPT_WAIT = 0x0E,
    SPRITE_ATTRIBUTES = 0x0F,  /* bit 0 draws behind, 1-3 palette, 4-5 priority, 6 no countdown,
                                * 7 places the sprite relative to the map's scroll */
    FRAMES_LEFT = 0x0A,        /* what a command that lasts a number of frames counts down */
    POSITION_X = 0x12,         /* 32-bit place and velocity: a fraction, then the whole part */
    POSITION_Y = 0x16,
    VELOCITY_X = 0x1A,
    VELOCITY_Y = 0x1E,
    MOVE_STARTED = 0x32,       /* a move command has set itself up */
    MOVE_TARGET = 0x34,        /* and where it is moving to */
    PALETTE_ROW = 0x32,        /* what the palette loader takes: the row, whether to hand over */
    PALETTE_HAND_OVER = 0x34,  /* to $C2:1E38 instead, and the long address of the colours */
    PALETTE_SOURCE = 0x35,
};

/* ---- what a handler hands back ------------------------------------------------------------ */

/* Move the script on by `length` bytes and run the next command. */
static void advance(Snes *s, uint16_t length)
{
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, length);
    m_rts(s);
}

/* Leave the object where it is until the next frame. */
static void wait_a_frame(Snes *s)
{
    m_rep(s, CPU_FLAG_M);
    m_tdc(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* Branch: the signed byte moves the script pointer, carry clear. A negative
 * offset is widened to a word; a positive one keeps A's high byte, and an
 * offset of zero leaves the object for this frame instead, as any zero does. */
static void branch(Snes *s, uint8_t offset)
{
    m_rep(s, CPU_FLAG_M);
    if (offset & 0x80u) {
        m_lda16(s, (uint16_t)(0xFF00u | offset));
    } else {
        s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00u) | offset);
        m_set_nz8(s, offset);
    }
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* A command that lasts a number of frames keeps the count in its slot: it
 * loads it from the operand at `rel` the first time and counts down after, so
 * a count of zero in the script never runs out. True while it is still going. */
static bool counting_down(Snes *s, uint16_t slot, uint16_t rel)
{
    uint32_t cell = at(s, FRAMES_LEFT, slot);
    uint8_t left = m_lda8(s, m_read8(s, cell));
    if (left == 0) {
        m_ldy(s, rel);
        m_write8(s, cell, m_lda8(s, operand(s, rel)));
        return true;
    }
    left--;
    m_write8(s, cell, left);
    m_set_nz8(s, left);
    return left != 0;
}

/* ---- starting objects --------------------------------------------------------------------- */

/* $048A walks the 60 slots from the fifth on; $04CD only the four at the
 * start, which a screen keeps for the objects it wants run first. */
enum { SPAWN_FIRST = 4, SPAWN_COUNT = 60, SPAWN_HIGH_FIRST = 0, SPAWN_HIGH_COUNT = 4 };
/* A new object inherits the state of the one starting it from +$05 on. */
enum { INHERITED_FROM = 0x05, INHERITED_BYTES = 0x3B };

/* $C2:0494's loop: take the first free slot of `count` from `first` for an
 * object running `handler`. Answers one slot past the one it took, or past the
 * last it looked at when every one was busy, and leaves an 8-bit accumulator
 * as both entries do. */
static uint16_t spawn(Snes *s, uint16_t handler, uint16_t first, uint16_t count)
{
    uint16_t slot = (uint16_t)(RAM_ABS(RAM_MENU_OBJECT_SLOTS) + first * RAM_MENU_OBJECT_SLOTS_STRIDE);
    uint16_t left = count;
    m_write16(s, m_dp(s, 0x08), handler);
    m_rep(s, CPU_FLAG_M);
    m_ldx(s, slot);
    m_ldy(s, left);
    while (m_lda16(s, m_read16(s, cell_at(s, slot))) != 0) {
        m_set_flag(s, CPU_FLAG_C, false);
        slot = m_ldx(s, m_adc16(s, slot, RAM_MENU_OBJECT_SLOTS_STRIDE));
        left = m_ldy(s, (uint16_t)(left - 1));
        if (left == 0) {
            m_sep(s, CPU_FLAG_M);     /* every slot is busy: nothing is started */
            return slot;
        }
    }
    m_write16(s, cell_at(s, slot), m_lda16(s, handler));
    m_write16(s, cell_at(s, (uint16_t)(slot + 3)), 0);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, cell_at(s, (uint16_t)(slot + 2)), 0);
    m_rep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, false);
    m_ldy(s, m_adc16(s, slot, INHERITED_FROM));
    m_set_flag(s, CPU_FLAG_C, false);
    m_ldx(s, m_adc16(s, object(s), INHERITED_FROM));
    uint8_t bank = s->cpu.db;
    m_lda16(s, INHERITED_BYTES - 1);
    m_mvn(s, 0x00, 0x00);
    m_tyx(s);
    m_sep(s, CPU_FLAG_M);
    s->cpu.db = bank;
    return s->cpu.x;
}

// $C2:048A
/* Start an object running the handler in X, in the first free ordinary slot. */
void menu_object_spawn(Snes *s)
{
    spawn(s, s->cpu.x, SPAWN_FIRST, SPAWN_COUNT);
    m_rts(s);
}

// $C2:04CD
/* The same among the four slots a screen runs first. */
void menu_object_spawn_high(Snes *s)
{
    spawn(s, s->cpu.x, SPAWN_HIGH_FIRST, SPAWN_HIGH_COUNT);
    m_rts(s);
}

/* $C2:04D9 and $C2:04FB: start an object on the command script at `script` in
 * bank `bank`, in whichever set of slots `high` picks. */
static void start_script(Snes *s, uint16_t script, uint8_t bank, bool high)
{
    m_write8(s, m_dp(s, 0x01), bank);
    m_write16(s, m_dp(s, 0x0A), script);
    m_ldx(s, 0x0F63);
    if (high)
        NATIVE_TIME_AS(s, 0xC204CDu, spawn(s, 0x0F63, SPAWN_HIGH_FIRST, SPAWN_HIGH_COUNT));
    else
        NATIVE_TIME_AS(s, 0xC2048Au, spawn(s, 0x0F63, SPAWN_FIRST, SPAWN_COUNT));
    m_rep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, true);
    uint16_t slot = m_sbc16(s, s->cpu.x, RAM_MENU_OBJECT_SLOTS_STRIDE);
    m_ldx(s, slot);
    m_write16(s, cell_at(s, (uint16_t)(slot + 7)), m_lda16(s, m_read16(s, m_dp(s, 0x0A))));
    m_sep(s, CPU_FLAG_M);
    m_write8(s, cell_at(s, (uint16_t)(slot + 9)), m_lda8(s, m_read8(s, m_dp(s, 0x01))));
    m_write8(s, cell_at(s, (uint16_t)(slot + FRAMES_LEFT)), 0);
}

// $C2:04D9
void menu_object_start_script(Snes *s)
{
    start_script(s, s->cpu.x, (uint8_t)s->cpu.a, false);
    m_rts(s);
}

// $C2:04FB
void menu_object_start_script_high(Snes *s)
{
    start_script(s, s->cpu.x, (uint8_t)s->cpu.a, true);
    m_rts(s);
}

/* ---- position and motion ------------------------------------------------------------------ */

/* Add the 32-bit value at `from` to the one at `to`, both in the slot. */
static void add32(Snes *s, uint16_t slot, uint16_t to, uint16_t from)
{
    m_set_flag(s, CPU_FLAG_C, false);
    m_write16(s, at(s, to, slot),
              m_lda16(s, m_adc16(s, m_read16(s, at(s, to, slot)), m_read16(s, at(s, from, slot)))));
    m_write16(s, at(s, (uint16_t)(to + 2), slot),
              m_lda16(s, m_adc16(s, m_read16(s, at(s, (uint16_t)(to + 2), slot)),
                                 m_read16(s, at(s, (uint16_t)(from + 2), slot)))));
}

/* $C2:0F38's work: move the object by its velocity, both axes. */
static void step_position(Snes *s)
{
    m_rep(s, CPU_FLAG_M);
    uint16_t slot = m_ldx(s, object(s));
    add32(s, slot, POSITION_X, VELOCITY_X);
    add32(s, slot, POSITION_Y, VELOCITY_Y);
}

// $C2:0F38
void menu_object_step_position(Snes *s)
{
    step_position(s);
    m_rts(s);
}

/* $C2:1C84 and $C2:1C9E: turn a velocity round. */
static void negate_velocity(Snes *s, uint16_t slot, uint16_t field)
{
    m_set_flag(s, CPU_FLAG_C, false);
    m_write16(s, at(s, field, slot),
              m_lda16(s, m_adc16(s, (uint16_t)~m_read16(s, at(s, field, slot)), 1)));
    m_write16(s, at(s, (uint16_t)(field + 2), slot),
              m_lda16(s, m_adc16(s, (uint16_t)~m_read16(s, at(s, (uint16_t)(field + 2), slot)), 0)));
}

/* $C2:1CB8: wrap the object back onto the map, round its width and inside its
 * height. */
static void wrap_position(Snes *s)
{
    enum { MAP_WIDTH = 0x0600, MAP_HEIGHT_MASK = 0x03FF };
    uint16_t slot = m_ldx(s, object(s));
    uint16_t x = m_lda16(s, m_read16(s, at(s, (uint16_t)(POSITION_X + 2), slot)));
    if (x & 0x8000u) {
        m_set_flag(s, CPU_FLAG_C, false);
        x = m_lda16(s, m_adc16(s, x, MAP_WIDTH));
    } else {
        m_cmp16(s, x, MAP_WIDTH);
        if (m_flag(s, CPU_FLAG_C))
            x = m_lda16(s, m_sbc16(s, x, MAP_WIDTH));
    }
    m_write16(s, at(s, (uint16_t)(POSITION_X + 2), slot), x);
    m_write16(s, at(s, (uint16_t)(POSITION_Y + 2), slot),
              m_lda16(s, m_read16(s, at(s, (uint16_t)(POSITION_Y + 2), slot)) & MAP_HEIGHT_MASK));
}

/* $C2:1759: a position just below zero counts as zero, so a move that has run
 * out doesn't scroll the map one tile too far. */
static void clamp_negative(Snes *s, uint16_t slot, uint16_t field)
{
    uint16_t whole = m_lda16(s, m_read16(s, at(s, (uint16_t)(field + 2), slot)));
    bool below = (whole & 0x8000u) != 0
                 && m_lda16(s, m_read16(s, at(s, field, slot))) != 0;
    m_write16(s, at(s, (uint16_t)(field + 2), slot), below ? m_lda16(s, 0xFFFF) : 0);
}

/* $C2:1784: how far the object has moved in whole tiles since the map last
 * followed it. True when it has moved, with the amount left in $01 as a signed
 * byte for the scroll to take. */
static bool whole_step(Snes *s, uint16_t slot, uint16_t field)
{
    uint16_t whole = m_lda16(s, m_read16(s, at(s, (uint16_t)(field + 2), slot)));
    if (whole & 0x8000u) {
        m_set_flag(s, CPU_FLAG_C, false);
        m_adc16(s, (uint16_t)~m_read16(s, at(s, field, slot)), 1);
        whole = m_lda16(s, m_adc16(s, (uint16_t)~whole, 0));
    }
    if (whole == 0) {
        m_set_flag(s, CPU_FLAG_C, false);
        return false;
    }
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_dp(s, 0x01), m_lda8(s, m_read8(s, at(s, (uint16_t)(field + 2), slot))));
    m_rep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, true);
    return true;
}

/* $C2:1CDA: give the object the sprite script the table at $C3:E000 holds for
 * `entry`, from the start. */
static void set_sprite_script(Snes *s, uint16_t entry)
{
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, entry & 0x00FFu);
    m_lda16(s, m_asl16(s, s->cpu.a));
    uint16_t index = m_tax(s);
    uint16_t script = m_lda16(s, m_read16(s, (0xC3E000u + index) & 0xFFFFFFu));
    uint16_t slot = m_ldx(s, object(s));
    m_write16(s, at(s, SPRITE_SCRIPT, slot), script);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, at(s, (uint16_t)(SPRITE_SCRIPT + 2), slot), m_lda8(s, 0xC3));
    m_write8(s, at(s, SPRITE_SCRIPT_WAIT, slot), 0);
}

/* ---- the object's own state --------------------------------------------------------------- */

// $C2:1037
/* Forget everything the object was doing: every word of its slot from +$05 on. */
void menu_object_cmd_reset_state(Snes *s)
{
    static const uint8_t cleared[] = {
        0x05, 0x0B, 0x0D, 0x0E, 0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E, 0x20, 0x22,
        0x24, 0x26, 0x28, 0x2A, 0x2C, 0x2E, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E,
    };
    uint16_t slot = m_ldx(s, object(s));
    for (unsigned i = 0; i < sizeof cleared; i++)
        m_write16(s, at(s, cleared[i], slot), 0);
    advance(s, 1);
}

/* Put `set` into the object's sprite attribute byte, keeping the bits in
 * `keep`. */
static void set_attribute_bits(Snes *s, uint8_t keep)
{
    m_sep(s, CPU_FLAG_M);
    uint16_t slot = m_ldx(s, object(s));
    m_ldy(s, 1);
    uint32_t cell = at(s, SPRITE_ATTRIBUTES, slot);
    m_lda8(s, (uint8_t)(m_read8(s, cell) & keep));
    m_write8(s, cell, m_lda8(s, (uint8_t)(s->cpu.a | operand(s, 1))));
    advance(s, 2);
}

// $C2:1091
/* The palette its sprites are drawn in. */
void menu_object_cmd_set_sprite_palette(Snes *s) { set_attribute_bits(s, 0xF1); }

// $C2:10A8
/* Their priority, and whether they are placed relative to the map's scroll. */
void menu_object_cmd_set_sprite_priority(Snes *s) { set_attribute_bits(s, 0x4F); }

// $C2:10BF
/* Start the object the nine bytes after the command describe ($C2:1CF5): it
 * shows a sprite script and moves between two places. */
void menu_object_cmd_start_sprite_object(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    m_ldx(s, 0x1CF5);
    NATIVE_TIME_AS(s, 0xC2048Au, spawn(s, 0x1CF5, SPAWN_FIRST, SPAWN_COUNT));
    advance(s, 0x0A);
}

// $C2:10CD
/* Start the object that copies palette rows into the mode's palette buffer
 * ($C2:1DD4), from the long address and the row the operands give. */
void menu_object_cmd_load_palette_row(Snes *s)
{
    uint16_t slot = m_ldx(s, object(s));
    m_ldy(s, 1);
    m_write16(s, at(s, PALETTE_SOURCE, slot), m_lda16(s, operand16(s, 1)));
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 3);
    m_write8(s, at(s, (uint16_t)(PALETTE_SOURCE + 2), slot), m_lda8(s, operand(s, 3)));
    m_ldy(s, 4);
    m_write8(s, at(s, PALETTE_ROW, slot), m_lda8(s, operand(s, 4)));
    m_write8(s, at(s, (uint16_t)(PALETTE_ROW + 1), slot), 0);
    m_ldy(s, 5);
    m_write8(s, at(s, PALETTE_HAND_OVER, slot), m_lda8(s, operand(s, 5)));
    m_ldx(s, 0x1DD4);
    NATIVE_TIME_AS(s, 0xC2048Au, spawn(s, 0x1DD4, SPAWN_FIRST, SPAWN_COUNT));
    advance(s, 6);
}

/* The four bytes of the world map's mode that go with a location, one per
 * quarter of the map row the party leaves on ($C2:2509). */
static uint8_t location_leave_mode(Snes *s, uint16_t quarter)
{
    return m_read8(s, 0xC22509u + quarter);
}

// $C2:1100
/* Leave for the location the four bytes after the command name, keeping where
 * the party is on the world map, and put the menus in mode 2 so the screen's
 * loop ends. */
void menu_object_cmd_change_location(Snes *s)
{
    enum { MENU_MODE_LEAVING = 2, MAP_WIDTH_TILES = 0xC0, MAP_HEIGHT_MASK = 0x7F,
           MAP_CENTRE = 0x10, LOCATION_ID_BITS = 0x01FF };
    uint16_t id = m_lda16(s, m_read16(s, m_abs(s, RAM_ABS(RAM_LOCATION_ID))));
    m_write16(s, m_abs(s, RAM_ABS(RAM_LOCATION_PREVIOUS_ID)), id);
    m_write16(s, m_abs(s, RAM_ABS(RAM_MENU_SAVED_LOCATION)), id);

    m_set_flag(s, CPU_FLAG_C, false);
    uint16_t x = m_lda16(s, m_adc16(s, m_read16(s, m_dp(s, RAM_MENU_MAP_ORIGIN_DP)), MAP_CENTRE));
    m_cmp16(s, x, MAP_WIDTH_TILES);
    if (m_flag(s, CPU_FLAG_C))
        x = m_lda16(s, m_sbc16(s, x, MAP_WIDTH_TILES));
    m_write16(s, m_abs(s, RAM_ABS(RAM_LOCATION_PREVIOUS) + 2), x);
    m_write16(s, m_abs(s, RAM_ABS(RAM_MENU_SAVED_LOCATION) + 2), x);

    m_set_flag(s, CPU_FLAG_C, false);
    uint16_t y = m_lda16(s, m_adc16(s, m_read16(s, m_dp(s, RAM_MENU_MAP_ORIGIN_DP
                                                        + RAM_MENU_MAP_ORIGIN_STRIDE)),
                                    MAP_CENTRE));
    m_lda16(s, y & MAP_HEIGHT_MASK);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_abs(s, RAM_ABS(RAM_LOCATION_PREVIOUS) + 3), (uint8_t)s->cpu.a);
    m_write8(s, m_abs(s, RAM_ABS(RAM_MENU_SAVED_LOCATION) + 3), (uint8_t)s->cpu.a);
    m_rep(s, CPU_FLAG_M);

    m_ldy(s, 1);
    m_write16(s, m_abs(s, RAM_ABS(RAM_LOCATION_ID)),
              m_lda16(s, operand16(s, 1) & LOCATION_ID_BITS));
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 2);
    uint8_t quarter = m_lsr8(s, m_lda8(s, operand(s, 2)));
    m_set_a8(s, quarter);
    m_write8(s, m_abs(s, RAM_ABS(RAM_LOCATION) + 4), quarter);
    m_ldy(s, 3);
    m_write8(s, m_abs(s, RAM_ABS(RAM_LOCATION) + 2), m_lda8(s, operand(s, 3)));
    m_ldy(s, 4);
    m_write8(s, m_abs(s, RAM_ABS(RAM_LOCATION) + 3), m_lda8(s, operand(s, 4)));
    m_tdc(s);
    m_lda8(s, (uint8_t)(m_read8(s, m_abs(s, RAM_ABS(RAM_LOCATION) + 4)) & 3));
    uint16_t which = m_tax(s);
    m_write8(s, m_abs(s, RAM_ABS(RAM_LOCATION_PREVIOUS) + 4),
             m_lda8(s, location_leave_mode(s, which)));
    m_write8(s, m_abs(s, RAM_ABS(RAM_MENU_MODE)), m_lda8(s, MENU_MODE_LEAVING));
    advance(s, 5);
}

// $C2:1169
/* Stop here for this frame and run the same command again next frame. */
void menu_object_cmd_wait_frame(Snes *s)
{
    m_tdc(s);
    m_rts(s);
}

// $C2:1A2B
/* The same instruction under a second number. */
void menu_object_cmd_wait_frame_again(Snes *s)
{
    m_tdc(s);
    m_rts(s);
}

// $C2:1C81
/* The object is done: free its slot. */
void menu_object_cmd_end(Snes *s)
{
    m_tdc(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* ---- the tables a screen builds ------------------------------------------------------------ */

/* Where the screen's rows live: three tables in work RAM whose addresses are
 * words in bank $C2, the same ones the map's own scrolling reads ($C2:055C).
 * `which` is the table number already doubled. */
static uint16_t screen_table(Snes *s, uint16_t which)
{
    return m_read16(s, (0xC2055Cu + which) & 0xFFFFFFu);
}

// $C2:116B
/* Put a byte in one of the screen's row tables, at a column and a row. */
void menu_object_cmd_write_table_byte(Snes *s)
{
    enum { ROW_BYTES = 0x60 };
    m_ldy(s, 1);
    m_lda16(s, (uint16_t)(operand16(s, 1) - 1));
    m_lda16(s, s->cpu.a & 0x00FFu);
    m_lda16(s, m_asl16(s, s->cpu.a));
    uint16_t which = m_tax(s);
    m_write16(s, m_dp(s, 0x10), m_lda16(s, screen_table(s, which)));
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_dp(s, 0x12), m_lda8(s, 0x7E));
    m_ldy(s, 3);
    m_write8(s, m_abs(s, 0x4202), m_lda8(s, operand(s, 3)));
    m_write8(s, m_abs(s, 0x4203), m_lda8(s, ROW_BYTES));
    m_rep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, false);
    m_ldy(s, 2);
    uint16_t offset = m_lda16(s, m_adc16(s, (uint16_t)(operand16(s, 2) & 0x00FFu),
                                         m_read16(s, m_abs(s, 0x4216))));
    m_set_flag(s, CPU_FLAG_C, false);
    uint16_t address = m_lda16(s, m_adc16(s, offset, m_read16(s, m_dp(s, 0x10))));
    m_write16(s, m_dp(s, 0x10), address);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 4);
    m_write8(s, 0x7E0000u | address, m_lda8(s, operand(s, 4)));
    advance(s, 5);
}

/* ---- party members' scripts ---------------------------------------------------------------- */

/* Which script each of the three party members and the one extra object runs
 * on a map screen: a word each, chosen by who the member is. */
enum { MEMBER_SCRIPTS = (uint16_t)RAM_ABS(RAM_MENU_MEMBER_SCRIPTS),
       EXTRA_SCRIPT = (uint16_t)RAM_ABS(RAM_MENU_MEMBER_SCRIPTS)
                      + 3 * RAM_MENU_MEMBER_SCRIPTS_STRIDE,
       EXTRA_MEMBER = 7 };

// $C2:11B2
/* Give the member the third operand names the script the first two do. */
void menu_object_cmd_set_scene_script(Snes *s)
{
    m_ldy(s, 1);
    m_write16(s, m_dp(s, 0x08), m_lda16(s, operand16(s, 1)));
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 3);
    uint8_t who = m_lda8(s, operand(s, 3));
    m_cmp8(s, who, EXTRA_MEMBER);
    if (who == EXTRA_MEMBER) {
        m_write16(s, m_abs(s, EXTRA_SCRIPT), m_ldx(s, m_read16(s, m_dp(s, 0x08))));
    } else {
        for (unsigned i = 0; i < 3; i++) {
            uint8_t member = m_read8(s, RAM_PARTY_MEMBERS + i * RAM_PARTY_MEMBERS_STRIDE);
            m_cmp8(s, who, member);
            if (who == member) {
                m_write16(s, m_abs(s, (uint16_t)(MEMBER_SCRIPTS + 2 * i)),
                          m_ldx(s, m_read16(s, m_dp(s, 0x08))));
                break;
            }
        }
    }
    advance(s, 4);
}

// $C2:11F6
/* Start an object running the script the operands name. */
void menu_object_cmd_start_object(Snes *s)
{
    m_ldy(s, 1);
    m_ldx(s, operand16(s, 1));
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 3);
    uint8_t bank = m_lda8(s, operand(s, 3));
    NATIVE_TIME_AS(s, 0xC204D9u, start_script(s, s->cpu.x, bank, false));
    advance(s, 4);
}

// $C2:1A2D
/* The same in one of the four slots a screen runs first. */
void menu_object_cmd_start_script(Snes *s)
{
    m_ldy(s, 1);
    m_ldx(s, operand16(s, 1));
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 3);
    uint8_t bank = m_lda8(s, operand(s, 3));
    NATIVE_TIME_AS(s, 0xC204FBu, start_script(s, s->cpu.x, bank, true));
    advance(s, 4);
}

// $C2:1839
/* Start an object running the handler the operand names. */
void menu_object_cmd_start_handler(Snes *s)
{
    m_ldy(s, 1);
    uint16_t handler = m_ldx(s, m_lda16(s, operand16(s, 1)));
    NATIVE_TIME_AS(s, 0xC2048Au, spawn(s, handler, SPAWN_FIRST, SPAWN_COUNT));
    advance(s, 3);
}

// $C2:1A43
/* The same in one of the four slots a screen runs first. */
void menu_object_cmd_start_handler_high(Snes *s)
{
    m_ldy(s, 1);
    uint16_t handler = m_ldx(s, m_lda16(s, operand16(s, 1)));
    NATIVE_TIME_AS(s, 0xC204CDu, spawn(s, handler, SPAWN_HIGH_FIRST, SPAWN_HIGH_COUNT));
    advance(s, 3);
}

/* ---- slot bytes and cells ------------------------------------------------------------------ */

/* What a read-modify-write command does to a byte. */
typedef enum { OP_CLEAR, OP_INCREMENT, OP_DECREMENT, OP_SET, OP_SET_BITS, OP_CLEAR_BITS,
               OP_ADD, OP_SUBTRACT } ByteOp;

static uint8_t apply(Snes *s, ByteOp op, uint8_t was, uint8_t value)
{
    switch (op) {
    case OP_CLEAR: return 0;
    case OP_INCREMENT: return (uint8_t)(was + 1);
    case OP_DECREMENT: return (uint8_t)(was - 1);
    case OP_SET: return value;
    case OP_SET_BITS: return (uint8_t)(was | value);
    case OP_CLEAR_BITS: return (uint8_t)(was & (uint8_t)~value);
    case OP_ADD: return m_adc8(s, value, was);
    default: return m_sbc8(s, was, value);
    }
}

/* The byte of the running object's slot a command names by its offset. */
static uint32_t slot_byte(Snes *s, uint16_t offset) { return at(s, object(s), offset); }

/* Whether an operation looks at the byte before writing it: the two that just
 * put a value there don't, and reading a byte the script misnames could be a
 * register. */
static bool reads_first(ByteOp op) { return op != OP_CLEAR && op != OP_SET; }

/* $0A-$0F and $46: a byte of the slot, by the offset the first operand gives. */
static void slot_byte_command(Snes *s, ByteOp op, uint16_t length)
{
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 1);
    uint16_t offset;
    uint8_t value = 0;
    if (length == 2) {
        offset = m_ldy(s, wide(s, m_lda8(s, operand(s, 1))));
    } else {
        offset = m_ldx(s, wide(s, m_lda8(s, operand(s, 1))));
        m_iny(s);
        value = m_lda8(s, operand(s, 2));
        m_txy(s);
    }
    uint32_t cell = slot_byte(s, offset);
    if (op == OP_CLEAR)
        m_tdc(s);
    uint8_t was = reads_first(op) ? m_read8(s, cell) : 0;
    if (op == OP_ADD)
        m_set_flag(s, CPU_FLAG_C, false);
    m_write8(s, cell, m_lda8(s, apply(s, op, was, value)));
    advance(s, length);
}

// $C2:120C
void menu_object_cmd_clear_slot_byte(Snes *s) { slot_byte_command(s, OP_CLEAR, 2); }
// $C2:121D
void menu_object_cmd_increment_slot_byte(Snes *s) { slot_byte_command(s, OP_INCREMENT, 2); }
// $C2:1230
void menu_object_cmd_decrement_slot_byte(Snes *s) { slot_byte_command(s, OP_DECREMENT, 2); }
// $C2:1243
void menu_object_cmd_set_slot_byte(Snes *s) { slot_byte_command(s, OP_SET, 3); }
// $C2:1257
void menu_object_cmd_set_slot_bits(Snes *s) { slot_byte_command(s, OP_SET_BITS, 3); }
// $C2:126D
void menu_object_cmd_clear_slot_bits(Snes *s) { slot_byte_command(s, OP_CLEAR_BITS, 3); }
// $C2:1ACA
void menu_object_cmd_add_to_slot_byte(Snes *s) { slot_byte_command(s, OP_ADD, 3); }

// $C2:1AE1
/* Take the second operand off the slot byte the first names. */
void menu_object_cmd_subtract_from_slot_byte(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 1);
    uint16_t offset = m_lda8(s, operand(s, 1)), was;
    m_ldx(s, wide(s, (uint8_t)offset));
    offset = m_ldy(s, wide(s, (uint8_t)offset));
    was = m_lda8(s, m_read8(s, slot_byte(s, offset)));
    m_ldy(s, 2);
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t left = m_sbc8(s, (uint8_t)was, operand(s, 2));
    m_set_a8(s, left);
    m_txy(s);
    m_write8(s, slot_byte(s, s->cpu.y), left);
    advance(s, 3);
}

/* $10-$15, $48 and $49: a byte of low RAM, by the 16-bit address the first
 * operand gives. */
static void cell_command(Snes *s, ByteOp op, uint16_t length)
{
    m_ldy(s, 1);
    uint16_t address = m_ldx(s, m_lda16(s, operand16(s, 1)));
    uint8_t value = 0;
    if (op == OP_SUBTRACT) {
        m_ldy(s, 3);
        m_sep(s, CPU_FLAG_M);
    } else {
        m_sep(s, CPU_FLAG_M);
        if (length == 4) {
            m_ldy(s, 3);
            value = m_lda8(s, operand(s, 3));
        }
    }
    uint32_t cell = cell_at(s, address);
    uint8_t was = 0;
    if (op == OP_SUBTRACT) {
        was = m_lda8(s, m_read8(s, cell));
        value = operand(s, 3);
        m_set_flag(s, CPU_FLAG_C, true);
    } else if (reads_first(op)) {
        was = m_read8(s, cell);
        if (op == OP_ADD)
            m_set_flag(s, CPU_FLAG_C, false);
    }
    m_write8(s, cell, m_lda8(s, apply(s, op, was, value)));
    advance(s, length);
}

// $C2:1285
void menu_object_cmd_clear_cell(Snes *s) { cell_command(s, OP_CLEAR, 3); }
// $C2:1296
void menu_object_cmd_increment_cell(Snes *s) { cell_command(s, OP_INCREMENT, 3); }
// $C2:12A7
void menu_object_cmd_decrement_cell(Snes *s) { cell_command(s, OP_DECREMENT, 3); }
// $C2:12B8
void menu_object_cmd_set_cell(Snes *s) { cell_command(s, OP_SET, 4); }
// $C2:12CE
void menu_object_cmd_set_cell_bits(Snes *s) { cell_command(s, OP_SET_BITS, 4); }
// $C2:12E7
void menu_object_cmd_clear_cell_bits(Snes *s) { cell_command(s, OP_CLEAR_BITS, 4); }
// $C2:1AFB
void menu_object_cmd_add_to_cell(Snes *s) { cell_command(s, OP_ADD, 4); }
// $C2:1B15
void menu_object_cmd_subtract_from_cell(Snes *s) { cell_command(s, OP_SUBTRACT, 4); }

// $C2:1302
/* Put a slot byte in a cell. */
void menu_object_cmd_store_slot_byte(Snes *s)
{
    m_ldy(s, 2);
    uint16_t address = m_ldx(s, m_lda16(s, operand16(s, 2)));
    m_ldy(s, (uint16_t)(s->cpu.y - 1));
    uint16_t offset = m_ldy(s, m_lda16(s, operand16(s, 1) & 0x00FFu));
    m_sep(s, CPU_FLAG_M);
    m_write8(s, cell_at(s, address), m_lda8(s, m_read8(s, slot_byte(s, offset))));
    advance(s, 4);
}

// $C2:131C
/* And a cell in a slot byte. */
void menu_object_cmd_load_slot_byte(Snes *s)
{
    m_ldy(s, 2);
    uint16_t address = m_ldx(s, m_lda16(s, operand16(s, 2)));
    m_ldy(s, (uint16_t)(s->cpu.y - 1));
    uint16_t offset = m_ldy(s, m_lda16(s, operand16(s, 1) & 0x00FFu));
    m_sep(s, CPU_FLAG_M);
    m_write8(s, slot_byte(s, offset), m_lda8(s, m_read8(s, cell_at(s, address))));
    advance(s, 4);
}

// $C2:1336
/* Copy one slot byte to another. */
void menu_object_cmd_copy_slot_byte(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 2);
    uint16_t from = m_ldy(s, wide(s, m_lda8(s, operand(s, 2))));
    m_ldx(s, wide(s, m_lda8(s, m_read8(s, slot_byte(s, from)))));
    m_ldy(s, 1);
    uint16_t to = m_ldy(s, wide(s, m_lda8(s, operand(s, 1))));
    m_write8(s, slot_byte(s, to), (uint8_t)m_txa(s));
    advance(s, 3);
}

// $C2:1350
/* Copy one cell to another. */
void menu_object_cmd_copy_cell(Snes *s)
{
    m_ldy(s, 3);
    uint16_t from = m_ldx(s, m_lda16(s, operand16(s, 3)));
    m_ldy(s, 1);
    uint16_t to = m_ldy(s, m_lda16(s, operand16(s, 1)));
    m_sep(s, CPU_FLAG_M);
    m_write8(s, cell_at(s, to), m_lda8(s, m_read8(s, cell_at(s, from))));
    advance(s, 5);
}

/* ---- control flow --------------------------------------------------------------------------- */

// $C2:136A
/* Carry on at the address the operand gives, one byte before it since the
 * interpreter moves the pointer on by the 1 this hands back. */
void menu_object_cmd_jump(Snes *s)
{
    m_ldy(s, 1);
    m_write16(s, m_dp(s, RAM_MENU_OBJECT_SCRIPT_DP), m_lda16(s, operand16(s, 1)));
    m_lda16(s, 1);
    m_rts(s);
}

// $C2:1848
/* Call: keep where to come back to in the slot, and carry on at the address. */
void menu_object_cmd_call(Snes *s)
{
    uint16_t slot = m_ldx(s, object(s));
    m_set_flag(s, CPU_FLAG_C, false);
    m_write16(s, at(s, SAVED_RETURN, slot),
              m_lda16(s, m_adc16(s, m_read16(s, m_dp(s, RAM_MENU_OBJECT_SCRIPT_DP)), 3)));
    m_ldy(s, 1);
    m_write16(s, m_dp(s, RAM_MENU_OBJECT_SCRIPT_DP),
              m_lda16(s, (uint16_t)(operand16(s, 1) - 1)));
    m_lda16(s, 1);
    m_rts(s);
}

// $C2:185F
/* Return: carry on where the call left off. */
void menu_object_cmd_return(Snes *s)
{
    uint16_t slot = m_ldx(s, object(s));
    m_write16(s, m_dp(s, RAM_MENU_OBJECT_SCRIPT_DP),
              m_lda16(s, (uint16_t)(m_read16(s, at(s, SAVED_RETURN, slot)) - 1)));
    m_lda16(s, 1);
    m_rts(s);
}

// $C2:1375
/* Count a slot byte down and go back while it hasn't reached zero. */
void menu_object_cmd_loop_slot_byte(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 1);
    uint16_t offset = m_ldy(s, wide(s, m_lda8(s, operand(s, 1))));
    uint32_t cell = slot_byte(s, offset);
    uint8_t left = m_lda8(s, (uint8_t)(m_read8(s, cell) - 1));
    m_write8(s, cell, left);
    if (left == 0) {
        advance(s, 3);
        return;
    }
    m_ldy(s, 2);
    branch(s, m_lda8(s, operand(s, 2)));
}

/* $1C and $1D: go back or on by the byte at 2, on whether a slot byte is zero. */
static void branch_on_slot_byte(Snes *s, bool want_zero)
{
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 1);
    uint16_t offset = m_ldy(s, wide(s, m_lda8(s, operand(s, 1))));
    bool zero = m_lda8(s, m_read8(s, slot_byte(s, offset))) == 0;
    if (zero != want_zero) {
        advance(s, 3);
        return;
    }
    m_ldy(s, 2);
    branch(s, m_lda8(s, operand(s, 2)));
}

// $C2:1398
void menu_object_cmd_branch_if_slot_byte_zero(Snes *s) { branch_on_slot_byte(s, true); }
// $C2:13B8
void menu_object_cmd_branch_if_slot_byte_set(Snes *s) { branch_on_slot_byte(s, false); }

/* How a command compares the operand with what it found. */
typedef enum { TEST_EQUAL, TEST_NOT_EQUAL, TEST_ANY_BIT, TEST_NO_BIT } Test;

static bool holds(Snes *s, Test test, uint8_t found, uint8_t value)
{
    switch (test) {
    case TEST_EQUAL: m_cmp8(s, value, found); return value == found;
    case TEST_NOT_EQUAL: m_cmp8(s, value, found); return value != found;
    case TEST_ANY_BIT: m_lda8(s, (uint8_t)(value & found)); return (value & found) != 0;
    default: m_lda8(s, (uint8_t)(value & found)); return (value & found) == 0;
    }
}

/* $1E-$21: compare the operand at 2 with a slot byte and go back by the byte
 * at 3 when it holds. */
static void branch_on_slot_test(Snes *s, Test test, bool clear_high)
{
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 1);
    uint16_t offset = m_ldx(s, wide(s, m_lda8(s, operand(s, 1))));
    m_iny(s);
    uint8_t value = m_lda8(s, operand(s, 2));
    m_txy(s);
    bool take = holds(s, test, m_read8(s, slot_byte(s, offset)), value);
    if (!take) {
        advance(s, 4);
        return;
    }
    if (clear_high)
        m_tdc(s);
    m_ldy(s, 3);
    branch(s, m_lda8(s, operand(s, 3)));
}

// $C2:13D8
void menu_object_cmd_branch_if_slot_byte_not(Snes *s) { branch_on_slot_test(s, TEST_NOT_EQUAL, false); }
// $C2:13FC
void menu_object_cmd_branch_if_slot_byte_is(Snes *s) { branch_on_slot_test(s, TEST_EQUAL, false); }
// $C2:1420
void menu_object_cmd_branch_if_slot_bits_set(Snes *s) { branch_on_slot_test(s, TEST_ANY_BIT, false); }
// $C2:1444
void menu_object_cmd_branch_if_slot_bits_clear(Snes *s) { branch_on_slot_test(s, TEST_NO_BIT, true); }

/* $22 and $23: go back by the byte at 3 on whether a cell is zero. */
static void branch_on_cell(Snes *s, bool want_zero)
{
    m_ldy(s, 1);
    uint16_t address = m_ldx(s, m_lda16(s, operand16(s, 1)));
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    bool zero = m_lda8(s, m_read8(s, cell_at(s, address))) == 0;
    if (zero != want_zero) {
        advance(s, 4);
        return;
    }
    m_ldy(s, 3);
    branch(s, m_lda8(s, operand(s, 3)));
}

// $C2:1469
void menu_object_cmd_branch_if_cell_zero(Snes *s) { branch_on_cell(s, true); }
// $C2:148B
void menu_object_cmd_branch_if_cell_set(Snes *s) { branch_on_cell(s, false); }

/* $24-$27: compare the operand at 3 with a cell and go back by the byte at 4
 * when it holds. */
static void branch_on_cell_test(Snes *s, Test test)
{
    m_ldy(s, 1);
    uint16_t address = m_ldx(s, m_lda16(s, operand16(s, 1)));
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 3);
    uint8_t value = m_lda8(s, operand(s, 3));
    bool take = holds(s, test, m_read8(s, cell_at(s, address)), value);
    if (!take) {
        advance(s, 5);
        return;
    }
    m_tdc(s);
    m_iny(s);
    branch(s, m_lda8(s, operand(s, 4)));
}

// $C2:14AD
void menu_object_cmd_branch_if_cell_not(Snes *s) { branch_on_cell_test(s, TEST_NOT_EQUAL); }
// $C2:14D2
void menu_object_cmd_branch_if_cell_is(Snes *s) { branch_on_cell_test(s, TEST_EQUAL); }
// $C2:14F7
void menu_object_cmd_branch_if_cell_bits_set(Snes *s) { branch_on_cell_test(s, TEST_ANY_BIT); }
// $C2:151C
void menu_object_cmd_branch_if_cell_bits_clear(Snes *s) { branch_on_cell_test(s, TEST_NO_BIT); }

/* $4C and $4D: go back by the byte at 4 on how a cell compares with the
 * operand at 3. The cell is read as a word and compared as a byte, so the byte
 * after it goes into A's high half and the branch clears it again. */
static void branch_on_cell_order(Snes *s, bool want_below)
{
    m_ldy(s, 1);
    uint16_t address = m_ldx(s, m_lda16(s, operand16(s, 1)));
    m_ldy(s, 3);
    uint8_t found = (uint8_t)m_lda16(s, m_read16(s, cell_at(s, address)));
    m_sep(s, CPU_FLAG_M);
    uint8_t value = operand(s, 3);
    m_cmp8(s, found, value);
    if ((found < value) != want_below) {
        advance(s, 5);
        return;
    }
    m_tdc(s);
    m_iny(s);
    branch(s, m_lda8(s, operand(s, 4)));
}

// $C2:1541
void menu_object_cmd_branch_if_cell_below(Snes *s) { branch_on_cell_order(s, true); }
// $C2:1566
void menu_object_cmd_branch_if_cell_at_least(Snes *s) { branch_on_cell_order(s, false); }

/* ---- the screen's fades and mosaic ---------------------------------------------------------- */

/* Objects that take the screen away and bring it back: a brightness pair and a
 * mosaic pair, each reading its own steps from the script that started it.
 * Their handlers are still the original's, so they are named by address. */
enum { FADE_OUT_OBJECT = (uint16_t)0xC220A2u, FADE_IN_OBJECT = (uint16_t)0xC22105u,
       MOSAIC_ON_OBJECT = (uint16_t)0xC221F8u, MOSAIC_OFF_OBJECT = (uint16_t)0xC22194u };

static void start_screen_object(Snes *s, uint16_t handler, uint16_t length)
{
    m_sep(s, CPU_FLAG_M);
    m_ldx(s, handler);
    NATIVE_TIME_AS(s, 0xC2048Au, spawn(s, handler, SPAWN_FIRST, SPAWN_COUNT));
    advance(s, length);
}

// $C2:158B
/* Fade the screen out to forced blank. */
void menu_object_cmd_fade_out(Snes *s) { start_screen_object(s, FADE_OUT_OBJECT, 2); }
// $C2:1599
/* Fade it back in to full brightness. */
void menu_object_cmd_fade_in(Snes *s) { start_screen_object(s, FADE_IN_OBJECT, 2); }
// $C2:15A7
/* Grow the mosaic to its coarsest. */
void menu_object_cmd_mosaic_on(Snes *s) { start_screen_object(s, MOSAIC_ON_OBJECT, 3); }
// $C2:15B5
/* And take it away again. */
void menu_object_cmd_mosaic_off(Snes *s) { start_screen_object(s, MOSAIC_OFF_OBJECT, 3); }

/* ---- where the object is and how it moves --------------------------------------------------- */

// $C2:15C3
/* Put the object at the place the operands give, on a whole tile. */
void menu_object_cmd_set_position(Snes *s)
{
    uint16_t slot = m_ldx(s, object(s));
    m_ldy(s, 1);
    m_write16(s, at(s, (uint16_t)(POSITION_X + 2), slot), m_lda16(s, operand16(s, 1)));
    m_write16(s, at(s, POSITION_X, slot), 0);
    m_ldy(s, 3);
    m_write16(s, at(s, (uint16_t)(POSITION_Y + 2), slot), m_lda16(s, operand16(s, 3)));
    m_write16(s, at(s, POSITION_Y, slot), 0);
    m_lda16(s, 5);
    m_rts(s);
}

// $C2:15DF
/* Two bytes of script that do nothing. */
void menu_object_cmd_nothing(Snes *s)
{
    m_lda16(s, 2);
    m_rts(s);
}

/* $2E and $2F: a 32-bit velocity from two words of operands. */
static void set_velocity(Snes *s, uint16_t field)
{
    uint16_t slot = m_ldx(s, object(s));
    m_ldy(s, 1);
    m_write16(s, at(s, field, slot), m_lda16(s, operand16(s, 1)));
    m_ldy(s, 3);
    m_write16(s, at(s, (uint16_t)(field + 2), slot), m_lda16(s, operand16(s, 3)));
    m_lda16(s, 5);
    m_rts(s);
}

// $C2:15E3
void menu_object_cmd_set_velocity_x(Snes *s) { set_velocity(s, VELOCITY_X); }
// $C2:15F9
void menu_object_cmd_set_velocity_y(Snes *s) { set_velocity(s, VELOCITY_Y); }

// $C2:160F
/* Show the sprite script the operand picks from the table at $C3:E000. */
void menu_object_cmd_set_sprite_script(Snes *s)
{
    m_ldy(s, 1);
    set_sprite_script(s, m_lda16(s, operand16(s, 1)));
    advance(s, 2);
}

// $C2:161D
/* Run the object's sprite script for a number of frames, moving it by its
 * velocity as it goes. */
void menu_object_cmd_animate_for(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    uint16_t slot = m_ldx(s, object(s));
    if (!counting_down(s, slot, 1)) {
        advance(s, 2);
        return;
    }
    m_rep(s, CPU_FLAG_M);
    NATIVE_TIME_AS(s, 0xC20F38u, step_position(s));
    wrap_position(s);
    m_jsr(s, 0xC21643u, 0xC20E1Du);
    wait_a_frame(s);
}

// $C2:186B
/* Do nothing for a number of frames. */
void menu_object_cmd_wait_for(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    uint16_t slot = m_ldx(s, object(s));
    if (!counting_down(s, slot, 1)) {
        advance(s, 2);
        return;
    }
    wait_a_frame(s);
}

// $C2:188E
/* Run the sprite script for a number of frames without moving. */
void menu_object_cmd_animate_wait_for(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    uint16_t slot = m_ldx(s, object(s));
    if (!counting_down(s, slot, 1)) {
        advance(s, 2);
        return;
    }
    m_jsr(s, 0xC218ACu, 0xC20E1Du);
    wait_a_frame(s);
}

// $C2:18B4
/* Move on while the object is no older than the operand, and stop there once
 * it is older. */
void menu_object_cmd_stop_after_age(Snes *s)
{
    uint16_t slot = m_ldx(s, object(s));
    m_ldy(s, 1);
    uint16_t limit = m_lda16(s, operand16(s, 1));
    m_cmp16(s, limit, m_read16(s, at(s, 0x03, slot)));
    if (m_flag(s, CPU_FLAG_C)) {
        m_lda16(s, 3);
        m_rts(s);
        return;
    }
    m_tdc(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* $3F and $40: move along one axis until the object reaches the place the
 * operand names, turning the velocity round when it points the wrong way and
 * showing one of two sprite scripts for the direction it takes. */
static void move_to(Snes *s, uint16_t position, uint16_t velocity)
{
    m_sep(s, CPU_FLAG_M);
    uint16_t slot = m_ldx(s, object(s));
    uint32_t started = at(s, MOVE_STARTED, slot);
    if (m_lda8(s, m_read8(s, started)) == 0) {
        m_write8(s, started, (uint8_t)(m_read8(s, started) + 1));
        m_rep(s, CPU_FLAG_M);
        m_ldy(s, 1);
        uint16_t target = m_lda16(s, operand16(s, 1));
        m_write16(s, at(s, MOVE_TARGET, slot), target);
        m_cmp16(s, target, m_read16(s, at(s, (uint16_t)(position + 2), slot)));
        bool forward = m_flag(s, CPU_FLAG_C);
        uint16_t high = m_lda16(s, m_read16(s, at(s, (uint16_t)(velocity + 2), slot)));
        if (forward ? (high & 0x8000u) != 0 : (high & 0x8000u) == 0)
            negate_velocity(s, slot, velocity);
        m_ldy(s, forward ? 4 : 3);
        set_sprite_script(s, m_lda16(s, operand16(s, forward ? 4 : 3)));
    }
    m_rep(s, CPU_FLAG_M);
    slot = s->cpu.x;
    uint16_t now = m_lda16(s, m_read16(s, at(s, (uint16_t)(position + 2), slot)));
    uint16_t target = m_read16(s, at(s, MOVE_TARGET, slot));
    m_cmp16(s, now, target);
    if (now == target) {
        m_lda16(s, 5);
        m_rts(s);
        return;
    }
    add32(s, slot, position, velocity);
    wrap_position(s);
    m_jsr(s, position == POSITION_X ? 0xC219B6u : 0xC21A1Fu, 0xC20E1Du);
    wait_a_frame(s);
}

// $C2:1959
void menu_object_cmd_move_x_to(Snes *s) { move_to(s, POSITION_X, VELOCITY_X); }
// $C2:19C2
void menu_object_cmd_move_y_to(Snes *s) { move_to(s, POSITION_Y, VELOCITY_Y); }

/* ---- scrolling and redrawing the map -------------------------------------------------------- */

/* BG3's scroll in the mode's display shadows, which the map's own scrolling
 * accumulates into as the object moves. */
enum {
    BG3_SCROLL_X = (uint8_t)(RAM_MENU_OBJECT_SHADOWS_SCROLL_X - RAM_MENU_OBJECT_SHADOWS
                             + RAM_MENU_OBJECT_SHADOWS_DP + 4),
    BG3_SCROLL_Y = (uint8_t)(RAM_MENU_OBJECT_SHADOWS_SCROLL_Y - RAM_MENU_OBJECT_SHADOWS
                             + RAM_MENU_OBJECT_SHADOWS_DP + 4),
};

/* Three flags that stop the map scroll from following the object; what each of
 * them means isn't established, only that any one of them set leaves the
 * layer's scroll alone (docs/TODO.md, "The RAM map's leftovers"). */
enum { SCROLL_BLOCK_A = 0x1BA7, SCROLL_BLOCK_A_BIT = 0x10,
       SCROLL_BLOCK_B = 0x1BAA, SCROLL_BLOCK_B_BIT = 0x20,
       SCROLL_BLOCK_C = 0x1BAF, SCROLL_BLOCK_C_BIT = 0x02 };

static bool scroll_blocked(Snes *s)
{
    m_lda8(s, m_read8(s, m_abs(s, SCROLL_BLOCK_A)));
    m_bit_imm8(s, SCROLL_BLOCK_A_BIT);
    if (s->cpu.a & SCROLL_BLOCK_A_BIT)
        return true;
    m_lda8(s, m_read8(s, m_abs(s, SCROLL_BLOCK_B)));
    m_bit_imm8(s, SCROLL_BLOCK_B_BIT);
    if (s->cpu.a & SCROLL_BLOCK_B_BIT)
        return true;
    m_lda8(s, m_read8(s, m_abs(s, SCROLL_BLOCK_C)));
    m_bit_imm8(s, SCROLL_BLOCK_C_BIT);
    return (s->cpu.a & SCROLL_BLOCK_C_BIT) != 0;
}

/* Add the whole step the move just made, a signed byte, to a scroll shadow. */
static void scroll_layer(Snes *s, uint8_t shadow)
{
    m_tdc(s);
    uint8_t step = m_lda8(s, m_read8(s, m_dp(s, 0x01)));
    m_rep(s, CPU_FLAG_M);
    uint16_t by = (step & 0x80u) ? m_lda16(s, (uint16_t)(0xFF00u | step)) : s->cpu.a;
    m_set_flag(s, CPU_FLAG_C, false);
    m_write16(s, m_dp(s, shadow), m_lda16(s, m_adc16(s, by, m_read16(s, m_dp(s, shadow)))));
}

/* Rebuild the map rows a whole step uncovered: the routine takes which pass to
 * make in the mode's first scratch byte. */
static void follow_map(Snes *s, uint32_t site, uint32_t routine, uint8_t pass)
{
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_dp(s, 0x00), m_lda8(s, pass));
    m_jsr(s, site, routine);
}

/* The start of $32 and $51: take the frame count from the operand the first
 * time round, forgetting where the object was, and count it down after. True
 * while the command is still going. */
static bool scrolling(Snes *s, uint16_t slot, uint16_t rel)
{
    uint32_t left = at(s, FRAMES_LEFT, slot);
    if (m_lda8(s, m_read8(s, left)) == 0) {
        m_ldy(s, rel);
        m_write8(s, left, m_lda8(s, operand(s, rel)));
        m_rep(s, CPU_FLAG_M);
        m_write16(s, at(s, POSITION_X, slot), 0);
        m_write16(s, at(s, (uint16_t)(POSITION_X + 2), slot), 0);
        m_write16(s, at(s, POSITION_Y, slot), 0);
        m_write16(s, at(s, (uint16_t)(POSITION_Y + 2), slot), 0);
        return true;
    }
    uint8_t now = (uint8_t)(m_read8(s, left) - 1);
    m_write8(s, left, now);
    m_set_nz8(s, now);
    return now != 0;
}

/* The move both scroll commands make before they follow it. */
static bool scroll_step(Snes *s, uint16_t slot)
{
    m_rep(s, CPU_FLAG_M);
    clamp_negative(s, slot, POSITION_X);
    clamp_negative(s, slot, POSITION_Y);
    NATIVE_TIME_AS(s, 0xC20F38u, step_position(s));
    return whole_step(s, s->cpu.x, POSITION_X);
}

// $C2:164B
/* Scroll the whole map with the object for a number of frames. */
void menu_object_cmd_scroll_map_for(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    uint16_t slot = m_ldx(s, object(s));
    if (!scrolling(s, slot, 1)) {
        advance(s, 2);
        return;
    }
    if (scroll_step(s, slot)) {
        follow_map(s, 0xC2168Au, 0xC20568u, 1);
        follow_map(s, 0xC21691u, 0xC20568u, 2);
        if (!scroll_blocked(s))
            scroll_layer(s, BG3_SCROLL_X);
    }
    m_rep(s, CPU_FLAG_M);
    if (whole_step(s, m_ldx(s, object(s)), POSITION_Y)) {
        follow_map(s, 0xC216C7u, 0xC2066Cu, 1);
        follow_map(s, 0xC216CEu, 0xC2066Cu, 2);
        if (!scroll_blocked(s))
            scroll_layer(s, BG3_SCROLL_Y);
    }
    wait_a_frame(s);
}

// $C2:16FA
/* The same for the one map pass the first operand picks, and without the
 * layer following it. */
void menu_object_cmd_scroll_map_plane_for(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    uint16_t slot = m_ldx(s, object(s));
    if (!scrolling(s, slot, 2)) {
        advance(s, 3);
        return;
    }
    if (scroll_step(s, slot)) {
        m_sep(s, CPU_FLAG_M);
        m_ldy(s, 1);
        m_write8(s, m_dp(s, 0x00), m_lda8(s, operand(s, 1)));
        m_jsr(s, 0xC2173Cu, 0xC20568u);
    }
    m_rep(s, CPU_FLAG_M);
    if (whole_step(s, m_ldx(s, object(s)), POSITION_Y)) {
        m_sep(s, CPU_FLAG_M);
        m_ldy(s, 1);
        m_write8(s, m_dp(s, 0x00), m_lda8(s, operand(s, 1)));
        m_jsr(s, 0xC21751u, 0xC2066Cu);
    }
    wait_a_frame(s);
}

// $C2:1947
/* Redraw the map pass the operand picks. */
void menu_object_cmd_redraw_map(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 1);
    m_write8(s, m_dp(s, 0x00), m_lda8(s, operand(s, 1)));
    m_jsr(s, 0xC21950u, 0xC209C5u);
    advance(s, 2);
}

/* ---- uploads, sound and calls ---------------------------------------------------------------- */

/* A record of the mode's VRAM upload queue: where it goes, where it comes from
 * and how much, with the flag that says the record is ready. */
enum { UPLOAD_VRAM = 0, UPLOAD_BANK = 2, UPLOAD_SOURCE = 3, UPLOAD_SIZE = 5, UPLOAD_READY = 7,
       UPLOAD_RECORD = 8, UPLOAD_READY_FLAG = 0x80 };

/* A field of the record `base` bytes into the queue. The queue is longer than
 * a direct page, so the offset doesn't wrap in one: the original reaches the
 * first byte of each record through the data bank and the rest through the
 * page, and both run on past $FF. */
static uint32_t upload_at(Snes *s, uint16_t base, uint8_t field)
{
    return (uint16_t)(s->cpu.d + RAM_MENU_OBJECT_VRAM_QUEUE_DP + base + field);
}

/* The vblank handler leaves the queue alone while this is set. */
static void queueing(Snes *s, bool on)
{
    uint32_t busy = m_dp(s, RAM_MENU_OBJECT_VRAM_QUEUE_BUSY_DP);
    if (on)
        m_write8(s, busy, (uint8_t)(m_read8(s, busy) + 1));
    else
        m_write8(s, busy, 0);
}

// $C2:17D2
/* Queue one upload: its bank, its VRAM address, where it comes from and how
 * many bytes. */
void menu_object_cmd_queue_vram_upload(Snes *s)
{
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    uint16_t base = m_ldx(s, m_read8(s, m_dp(s, RAM_MENU_OBJECT_VRAM_QUEUE_END_DP)));
    queueing(s, true);
    m_ldy(s, 1);
    m_write8(s, upload_at(s, base, UPLOAD_BANK), m_lda8(s, operand(s, 1)));
    m_write8(s, upload_at(s, base, UPLOAD_READY), m_lda8(s, UPLOAD_READY_FLAG));
    m_rep(s, CPU_FLAG_M);
    m_ldy(s, 2);
    m_write16(s, upload_at(s, base, UPLOAD_VRAM), m_lda16(s, operand16(s, 2)));
    m_ldy(s, 4);
    m_write16(s, upload_at(s, base, UPLOAD_SOURCE), m_lda16(s, operand16(s, 4)));
    m_ldy(s, 6);
    m_write16(s, upload_at(s, base, UPLOAD_SIZE), m_lda16(s, operand16(s, 6)));
    m_sep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, false);
    m_write8(s, m_dp(s, RAM_MENU_OBJECT_VRAM_QUEUE_END_DP),
             m_lda8(s, m_adc8(s, (uint8_t)m_txa(s), UPLOAD_RECORD)));
    queueing(s, false);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_lda16(s, 8);
    m_rts(s);
}

/* The sound driver command a script sends, and the request the mode's own
 * queue at $C2:2ED9 passes on when the driver is free. */
enum { SOUND_REQUEST = (uint16_t)RAM_ABS(RAM_MENU_SOUND_REQUEST),
       SOUND_REQUEST_AGE = (uint16_t)RAM_ABS(RAM_MENU_SOUND_REQUEST_AGE),
       SOUND_SONG_SHOWN = (uint16_t)RAM_ABS(RAM_MENU_SONG_PLAYING),
       SOUND_PLAY_SONG = 0x10, SOUND_EFFECT_18 = 0x18, SOUND_EFFECT_19 = 0x19 };

/* $3B and $3C: a sound effect with one parameter byte. */
static void sound_effect(Snes *s, uint8_t command, uint32_t site)
{
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_abs(s, SOUND_REQUEST), m_lda8(s, command));
    m_ldy(s, 1);
    m_write8(s, m_abs(s, SOUND_REQUEST + 1), m_lda8(s, operand(s, 1)));
    m_iny(s);
    m_write8(s, m_abs(s, SOUND_REQUEST + 2), m_lda8(s, operand(s, 2)));
    m_write8(s, m_abs(s, SOUND_REQUEST_AGE), 0);
    m_jsr(s, site, 0xC22ED9u);
    advance(s, 3);
}

// $C2:18C7
void menu_object_cmd_sound_18(Snes *s) { sound_effect(s, SOUND_EFFECT_18, 0xC218E8u); }
// $C2:18D0
void menu_object_cmd_sound_19(Snes *s) { sound_effect(s, SOUND_EFFECT_19, 0xC218E8u); }

/* $4A's work, which $3D runs too: start the song the operand names. */
static void play_song(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_abs(s, SOUND_REQUEST), m_lda8(s, SOUND_PLAY_SONG));
    m_ldy(s, 1);
    uint8_t song = m_lda8(s, operand(s, 1));
    m_write8(s, m_abs(s, SOUND_REQUEST + 1), song);
    m_write8(s, m_abs(s, SOUND_SONG_SHOWN), song);
    m_lda8(s, 0xFF);
    m_write8(s, m_abs(s, SOUND_REQUEST + 2), 0xFF);
    m_write8(s, m_abs(s, SOUND_REQUEST + 3), 0xFF);
    m_write8(s, m_abs(s, SOUND_REQUEST_AGE), 0);
    m_jsr(s, 0xC21916u, 0xC22ED9u);
}

// $C2:18F9
void menu_object_cmd_play_song(Snes *s)
{
    play_song(s);
    advance(s, 2);
}

// $C2:18F1
/* The same, unless the story flag at $7F:01ED is already set. */
void menu_object_cmd_play_song_unless_flag(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    if (m_lda8(s, m_read8(s, 0x7F01EDu)) == 0)
        NATIVE_TIME_AS(s, 0xC218F9u, play_song(s));
    advance(s, 2);
}

// $C2:191F
/* Send the driver the four bytes the operands give. */
void menu_object_cmd_sound_command(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    for (unsigned i = 0; i < 4; i++) {
        m_ldy(s, (uint16_t)(1 + i));
        m_write8(s, m_abs(s, (uint16_t)(SOUND_REQUEST + i)),
                 m_lda8(s, operand(s, (uint16_t)(1 + i))));
    }
    m_write8(s, m_abs(s, SOUND_REQUEST_AGE), 0);
    m_jsr(s, 0xC2193Eu, 0xC22ED9u);
    advance(s, 5);
}

// $C2:1807
/* Call the routine at the address the operand gives, through $C2:1819. */
void menu_object_cmd_call_routine(Snes *s)
{
    m_ldy(s, 1);
    m_write16(s, m_dp(s, RAM_MENU_OBJECT_CALL_TARGET_DP), m_lda16(s, operand16(s, 1)));
    m_sep(s, CPU_FLAG_M);
    m_jsr(s, 0xC21810u, 0xC21819u);
    advance(s, 3);
}

// $C2:181C
/* And the one at the long address, through $C2:1836. */
void menu_object_cmd_call_routine_long(Snes *s)
{
    m_ldy(s, 1);
    m_write16(s, m_dp(s, RAM_MENU_OBJECT_CALL_TARGET_DP), m_lda16(s, operand16(s, 1)));
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 3);
    m_write8(s, m_dp(s, RAM_MENU_OBJECT_CALL_TARGET_DP + 2), m_lda8(s, operand(s, 3)));
    m_jsl(s, 0xC2182Cu, 0xC21836u);
    advance(s, 4);
}


/* ---- the screen's own tables ------------------------------------------------------------------ */

/* $44 and $45 reach one entry of three tables the screens keep in bank $7E,
 * through the three-way dispatch at $C2:1A70: seven bytes an entry in the
 * first, three in either of the others. */
enum { ENTRY_TABLES = 3 };

static uint16_t entry_address(Snes *s, uint16_t which, uint8_t index)
{
    static const uint16_t base[ENTRY_TABLES] = { 0x7E00, 0x8300, 0x8180 };
    m_lda8(s, index);
    m_rep(s, CPU_FLAG_M);
    uint16_t n = s->cpu.a;
    m_write16(s, m_dp(s, 0x00), n);
    if (which == 0) {
        m_lda16(s, m_asl16(s, m_asl16(s, m_asl16(s, n))));
        m_set_flag(s, CPU_FLAG_C, true);
        m_lda16(s, m_sbc16(s, s->cpu.a, n));
    } else {
        m_lda16(s, m_asl16(s, n));
        m_lda16(s, m_adc16(s, s->cpu.a, n));
    }
    m_set_flag(s, CPU_FLAG_C, false);
    return m_lda16(s, m_adc16(s, s->cpu.a, base[which]));
}

/* $44 and $45: set or clear bit 7 of the byte an entry starts with. */
static void mark_entry(Snes *s, bool set, uint32_t dispatch)
{
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, 1);
    m_lda8(s, operand(s, 1));
    m_set_a8(s, m_asl8(s, (uint8_t)s->cpu.a));
    uint16_t which = m_tax(s);
    if (which >= 2 * ENTRY_TABLES || (which & 1) != 0) {
        /* the dispatch would jump outside its three-word table */
        m_stop_at_own(s, dispatch, 0);
        return;
    }
    m_iny(s);
    m_write16(s, m_dp(s, 0x10), entry_address(s, (uint16_t)(which >> 1), operand(s, 2)));
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_dp(s, 0x12), m_lda8(s, 0x7E));
    uint32_t entry = ((uint32_t)m_read8(s, m_dp(s, 0x12)) << 16 | m_read16(s, m_dp(s, 0x10)))
                     & 0xFFFFFFu;
    uint8_t was = m_lda8(s, m_read8(s, entry));
    m_write8(s, entry, m_lda8(s, (uint8_t)(set ? (was | 0x80u) : (was & 0x7Fu))));
    advance(s, 3);
}

// $C2:1A52
void menu_object_cmd_mark_table_entry(Snes *s) { mark_entry(s, true, 0xC21A5Bu); }
// $C2:1AAC
void menu_object_cmd_unmark_table_entry(Snes *s) { mark_entry(s, false, 0xC21AB5u); }

/* ---- copying tilemap rows ---------------------------------------------------------------------- */

/* The work RAM data port: memory access, not video (docs/PLATFORM.md), so
 * ports reach it directly. */
enum { WRAM_PORT = 0x2180, WRAM_ADDRESS = 0x2181, WRAM_BANK = 0x2183 };
/* The two tilemap buffers in bank $7E a screen copies between, and how many
 * bytes a row of one holds. */
enum { TILEMAP_A = 0x4000, TILEMAP_B = 0x5800, TILEMAP_ROW = 0x60 };
/* The hardware multiplier, which ports use directly. */
enum { MULTIPLY_A = 0x4202, MULTIPLY_B = 0x4203, PRODUCT = 0x4216 };

/* Row `row`, column `column` of the buffer `which` picks. */
static uint16_t tilemap_at(Snes *s, uint8_t which, uint8_t column, uint8_t row)
{
    m_cmp8(s, which, 1);
    uint16_t base = m_ldx(s, which == 1 ? TILEMAP_A : TILEMAP_B);
    m_write8(s, m_abs(s, MULTIPLY_A), m_lda8(s, row));
    m_write8(s, m_abs(s, MULTIPLY_B), m_lda8(s, TILEMAP_ROW));
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda8(s, column);
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, m_adc16(s, s->cpu.a, m_read16(s, m_abs(s, PRODUCT))));
    m_set_flag(s, CPU_FLAG_C, false);
    return m_lda16(s, m_adc16(s, s->cpu.a, base));
}

// $C2:1B2F
/* Copy a block of tilemap rows from one buffer to the other, through the work
 * RAM port. */
void menu_object_cmd_copy_tilemap_rows(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    m_tdc(s);
    m_write8(s, m_dp(s, 0x12), m_lda8(s, 0x7E));
    m_write8(s, m_dp(s, 0x15), (uint8_t)s->cpu.a);
    m_ldy(s, 1);
    m_write16(s, m_dp(s, 0x10), tilemap_at(s, operand(s, 1), operand(s, 2), operand(s, 3)));
    m_sep(s, CPU_FLAG_M);
    m_tdc(s);
    m_ldy(s, 4);
    m_write16(s, m_dp(s, 0x13), tilemap_at(s, operand(s, 4), operand(s, 5), operand(s, 6)));
    m_sep(s, CPU_FLAG_M);
    m_tdc(s);
    m_ldy(s, 7);
    m_write8(s, m_dp(s, 0x00), m_lda8(s, operand(s, 7)));
    m_iny(s);
    m_write8(s, m_dp(s, 0x01), m_lda8(s, operand(s, 8)));
    for (;;) {
        m_write8(s, m_dp(s, 0x02), m_lda8(s, m_read8(s, m_dp(s, 0x00))));
        m_write16(s, m_abs(s, WRAM_ADDRESS), m_ldx(s, m_read16(s, m_dp(s, 0x13))));
        m_write8(s, m_abs(s, WRAM_BANK), m_lda8(s, 0x7E));
        uint32_t from = ((uint32_t)m_read8(s, m_dp(s, 0x12)) << 16) | m_read16(s, m_dp(s, 0x10));
        m_ldy(s, 0);
        for (;;) {
            m_write8(s, m_abs(s, WRAM_PORT),
                     m_lda8(s, m_read8(s, (from + s->cpu.y) & 0xFFFFFFu)));
            m_iny(s);
            uint8_t left = (uint8_t)(m_read8(s, m_dp(s, 0x02)) - 1);
            m_write8(s, m_dp(s, 0x02), left);
            m_set_nz8(s, left);
            if (left == 0)
                break;
        }
        m_rep(s, CPU_FLAG_M);
        m_set_flag(s, CPU_FLAG_C, false);
        m_write16(s, m_dp(s, 0x10),
                  m_lda16(s, m_adc16(s, m_read16(s, m_dp(s, 0x10)), TILEMAP_ROW)));
        m_set_flag(s, CPU_FLAG_C, false);
        m_write16(s, m_dp(s, 0x13),
                  m_lda16(s, m_adc16(s, m_read16(s, m_dp(s, 0x13)), TILEMAP_ROW)));
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        uint8_t rows = (uint8_t)(m_read8(s, m_dp(s, 0x01)) - 1);
        m_write8(s, m_dp(s, 0x01), rows);
        m_set_nz8(s, rows);
        if (rows == 0)
            break;
    }
    advance(s, 9);
}

// $C2:1BE0
/* Queue the two uploads that put one 8x8 block of a screen's tilemap into
 * VRAM: a block is two rows of four bytes, $20 bytes apart in the buffer and
 * four apart in VRAM. */
void menu_object_cmd_copy_tilemap_block(Snes *s)
{
    enum { VRAM_A = 0x3000, VRAM_B = 0x3800, BUFFER_A = 0x6000, BUFFER_B = 0x6800,
           BLOCK_BYTES = 4, BUFFER_ROW = 0x20, BUFFER_HALF = 0x0400,
           COLUMN_MASK = 0x3F, COLUMN_WRAP = 0x20, ROW_MASK = 0x1F };
    m_sep(s, CPU_FLAG_M);
    m_tdc(s);
    m_ldy(s, 1);
    uint8_t half = m_lda8(s, operand(s, 1));
    m_cmp8(s, half, 1);
    uint16_t vram = m_ldx(s, half == 1 ? VRAM_A : VRAM_B);
    uint16_t buffer = m_ldy(s, half == 1 ? BUFFER_A : BUFFER_B);
    m_write16(s, m_dp(s, 0x10), vram);
    m_write16(s, m_dp(s, 0x13), buffer);

    m_ldy(s, 2);
    m_set_a8(s, m_asl8(s, m_lda8(s, operand(s, 2))));
    m_set_a8(s, (uint8_t)(s->cpu.a & COLUMN_MASK));
    m_set_nz8(s, (uint8_t)s->cpu.a);
    uint8_t column = (uint8_t)s->cpu.a;
    m_write8(s, m_dp(s, 0x00), column);
    m_write8(s, m_dp(s, 0x01), 0);
    m_set_a8(s, (uint8_t)(column & ROW_MASK));
    m_set_nz8(s, (uint8_t)s->cpu.a);
    m_write8(s, m_dp(s, 0x02), (uint8_t)s->cpu.a);
    m_write8(s, m_dp(s, 0x03), 0);

    m_iny(s);
    m_set_a8(s, m_asl8(s, m_lda8(s, operand(s, 3))));
    m_set_a8(s, (uint8_t)(s->cpu.a & ROW_MASK));
    m_set_nz8(s, (uint8_t)s->cpu.a);
    m_rep(s, CPU_FLAG_M);
    uint16_t scaled = s->cpu.a;
    for (unsigned i = 0; i < 5; i++)
        scaled = m_asl16(s, scaled);
    m_lda16(s, scaled);
    m_lda16(s, m_adc16(s, s->cpu.a, m_read16(s, m_dp(s, 0x13))));
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda16(s, m_adc16(s, s->cpu.a, m_read16(s, m_dp(s, 0x02))));
    m_write16(s, m_dp(s, 0x13), s->cpu.a);
    m_lda16(s, m_read16(s, m_dp(s, 0x00)));
    if (s->cpu.a & COLUMN_WRAP) {
        m_set_flag(s, CPU_FLAG_C, false);
        m_write16(s, m_dp(s, 0x13),
                  m_lda16(s, m_adc16(s, m_read16(s, m_dp(s, 0x13)), BUFFER_HALF)));
    }
    m_iny(s);
    m_lda16(s, operand16(s, 4) & 0x00FFu);
    uint16_t offset = s->cpu.a;
    for (unsigned i = 0; i < 3; i++)
        offset = m_asl16(s, offset);
    m_lda16(s, offset);
    m_write16(s, m_dp(s, 0x10), m_lda16(s, m_adc16(s, s->cpu.a, m_read16(s, m_dp(s, 0x10)))));

    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    uint16_t base = m_ldx(s, m_read8(s, m_dp(s, RAM_MENU_OBJECT_VRAM_QUEUE_END_DP)));
    queueing(s, true);
    m_lda8(s, 0x7E);
    m_write8(s, upload_at(s, base, UPLOAD_BANK), 0x7E);
    m_write8(s, upload_at(s, (uint16_t)(base + UPLOAD_RECORD), UPLOAD_BANK), 0x7E);
    m_lda8(s, UPLOAD_READY_FLAG);
    m_write8(s, upload_at(s, base, UPLOAD_READY), UPLOAD_READY_FLAG);
    m_write8(s, upload_at(s, (uint16_t)(base + UPLOAD_RECORD), UPLOAD_READY), UPLOAD_READY_FLAG);
    m_rep(s, CPU_FLAG_M);
    m_write16(s, upload_at(s, base, UPLOAD_VRAM), m_lda16(s, m_read16(s, m_dp(s, 0x10))));
    m_set_flag(s, CPU_FLAG_C, false);
    m_write16(s, upload_at(s, (uint16_t)(base + UPLOAD_RECORD), UPLOAD_VRAM),
              m_lda16(s, m_adc16(s, s->cpu.a, BLOCK_BYTES)));
    m_write16(s, upload_at(s, base, UPLOAD_SOURCE), m_lda16(s, m_read16(s, m_dp(s, 0x13))));
    m_set_flag(s, CPU_FLAG_C, false);
    m_write16(s, upload_at(s, (uint16_t)(base + UPLOAD_RECORD), UPLOAD_SOURCE),
              m_lda16(s, m_adc16(s, s->cpu.a, BUFFER_ROW)));
    m_lda16(s, BLOCK_BYTES);
    m_write16(s, upload_at(s, base, UPLOAD_SIZE), BLOCK_BYTES);
    m_write16(s, upload_at(s, (uint16_t)(base + UPLOAD_RECORD), UPLOAD_SIZE), BLOCK_BYTES);
    m_sep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, false);
    m_write8(s, m_dp(s, RAM_MENU_OBJECT_VRAM_QUEUE_END_DP),
             m_lda8(s, m_adc8(s, (uint8_t)m_txa(s), 2 * UPLOAD_RECORD)));
    queueing(s, false);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_lda16(s, 5);
    m_rts(s);
}
