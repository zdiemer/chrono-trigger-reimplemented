/* Field mode: building object sprite graphics when an animation frame changes.
 *
 * The frame builder (reached from the queue $C0:CA76 works through each frame)
 * assembles an object's sprite tiles, 32 bytes each, in bank $7F at the
 * offset field_sprite_tile_offset holds, from the graphics set
 * field_sprite_graphics points at (the ROM's banks $D2-$D5, or graphics built
 * in $7F). A tile is copied straight, or mirrored left to right through the
 * work RAM port, whose cursor the builder has already placed. These are the
 * builder's two tile copies and its read of an object's current frame. */
#include "field/graphics.h"

#include "cpu_io.h"
#include "field/bridge.h"
#include "field/field_data.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

enum { TILE_BYTES = 32, TILE_NUMBER_MASK = 0x07FF };
enum { BUILD_BANK = 0x7F };

/* The long pointers the builder keeps: the graphics set, and the frame
 * entry. Read low byte first, as a long pointer is: a pointer to nothing
 * reads what the bus last held, its bank byte. */
static uint32_t graphics_set(const GameState *g)
{
    uint16_t address = field_sprite_graphics_address(g);
    return (uint32_t)field_sprite_graphics_bank(g) << 16 | address;
}

static uint32_t frame_entry(const GameState *g)
{
    uint16_t address = field_frame_entry_address(g);
    return (uint32_t)field_frame_entry_bank(g) << 16 | address;
}

/* A tile's offset in its graphics set. */
static uint16_t tile_offset(uint16_t tile) { return (uint16_t)((tile & TILE_NUMBER_MASK) * TILE_BYTES); }

/* The graphics set's bank, as the copy reads it: bank $7F where the builder
 * made the graphics, else one of the ROM's $D2-$D5 (any other bank reads
 * $D5). */
static uint8_t source_bank(uint8_t bank)
{
    if (bank == BUILD_BANK || (bank >= 0xD2 && bank <= 0xD4))
        return bank;
    return 0xD5;
}

/* ---- For the original's code and shadow mode (field/scratch.h) -------------------------- */

/* The tile copies save the caller's Y at $C5 and give it back from there. */
static void leave_saved_y(GameState *g, uint16_t y) { original_cell16(g, 0xC5, y); }

/* The frame read keeps a table size, then the frame, at $D9 for its sums. */
static void leave_sum_operand(GameState *g, uint16_t v) { original_cell16(g, 0xD9, v); }

// $C0:E687
/* Copy sprite tile `tile` of the graphics set to the build offset, a word at
 * a time, then point the work RAM port's cursor past it. The tile is at the
 * set's address plus its offset, in 16 bits, in the bank the copy reads. The
 * addresses carry into the next bank past $FFFF, as the original's indexed
 * accesses do. Returns where the cursor now is. */
static uint16_t copy_sprite_tile(GameState *g, uint16_t tile)
{
    uint32_t graphics = graphics_set(g);
    uint16_t from = (uint16_t)(tile_offset(tile) + graphics);
    uint16_t to = field_sprite_tile_offset(g);
    uint32_t source = (uint32_t)source_bank((uint8_t)(graphics >> 16)) << 16;
    for (uint16_t k = 0; k < TILE_BYTES; k += 2) {
        uint32_t at = source + k + from;
        uint16_t word = (source >> 16) == BUILD_BANK ? wram_read16(g, at) : field_sprite_graphics_word(g, at);
        wram_write16(g, ((uint32_t)BUILD_BANK << 16) + k + to, word);
    }
    uint16_t cursor = (uint16_t)(to + TILE_BYTES);
    hw_port_seek_address(g, cursor);
    return cursor;
}

// $C0:E534
/* Write sprite tile `tile` mirrored left to right to the work RAM port: each
 * of its 32 bytes, from the graphics set's pointer plus the tile's offset,
 * bit-reversed. (The pointer is read for every byte: the port's cursor can
 * reach it.) */
static void copy_sprite_tile_flipped(GameState *g, uint16_t tile)
{
    uint16_t at = tile_offset(tile);
    for (uint16_t k = 0; k < TILE_BYTES; k++)
        hw_port_write(g, field_mirrored_byte(g, field_sprite_graphics_byte(g, graphics_set(g) + (uint16_t)(at + k))));
}

/* ---- The current frame -------------------------------------------------------------------- */

enum { END_OF_ANIMATION = 0xFF, FRAMES_PER_ANIMATION = 4 };
enum { ANIMATION_TIMED = 2 };   /* field_object_animation_mode_mode */

/* The frame table for the object's facing: facing 0 is the table itself,
 * facing 2 two tables on, facings 3-$81 three, the rest one. */
static uint16_t facing_table(GameState *g, unsigned object)
{
    uint8_t facing = field_object_facing(g, object);
    uint16_t table = field_object_frame_table(g, object);
    if (facing == 0)
        return table;
    uint16_t size = field_object_facing_table_size(g, object);
    if (facing == 2)
        return (uint16_t)(2 * size + table);
    if ((uint8_t)(facing - 2) & 0x80)
        return (uint16_t)(size + table);
    leave_sum_operand(g, size);
    /* Three times the size, and the table: the original adds them without
     * clearing the carry between, so each sum's carry goes into the next. */
    uint32_t twice = 2u * size;
    uint32_t thrice = (uint32_t)(uint16_t)twice + size + (twice >> 16);
    return (uint16_t)((uint16_t)thrice + table + (thrice >> 16));
}

/* The frame read: the frame (or, where a timed animation ended, the loops it
 * has left), whether there is one, and where it was read: the facing's table,
 * the entry, and the entry the animation had come to where it ended and
 * started over (else the entry itself). */
typedef struct {
    uint8_t frame;
    bool has_frame;
    uint16_t table, entry, ended_at;
} AnimationFrame;

// $C0:CB3A
/* Object `object`'s current animation frame, from its facing's frame table
 * through the pointer the caller has banked (field_frame_entry): the entry
 * for the animation (the timed one while a timed animation runs) and the
 * frame number. An entry of $FF ends the animation: a timed one counts down
 * its loops (not below 1) and has no frame; any other starts over at its
 * first entry. */
static AnimationFrame read_animation_frame(GameState *g, unsigned object)
{
    AnimationFrame r = { .table = facing_table(g, object) };
    set_field_frame_entry_address(g, r.table);
    uint8_t frame = field_object_animation_frame(g, object);
    leave_sum_operand(g, frame);
    bool timed = field_object_animation_mode_mode(g, object) == ANIMATION_TIMED;
    uint8_t animation = timed ? field_object_animation_mode_timed_animation(g, object)
                              : field_object_animation_number(g, object);
    r.entry = r.ended_at = (uint16_t)(animation * FRAMES_PER_ANIMATION + frame + r.table);
    set_field_frame_entry_address(g, r.entry);
    r.frame = field_animation_frame_entry(g, frame_entry(g));
    r.has_frame = r.frame != END_OF_ANIMATION;
    if (r.has_frame)
        return r;
    if (timed) {
        r.frame = (uint8_t)(field_object_move_state_animation_count(g, object) - 1);
        if (r.frame != 0)
            set_field_object_move_state_animation_count(g, object, r.frame);
        return r;
    }
    r.entry = (uint16_t)(r.entry - frame);
    set_field_frame_entry_address(g, r.entry);
    set_field_object_animation_frame(g, object, END_OF_ANIMATION);
    r.frame = field_animation_frame_entry(g, frame_entry(g));
    r.has_frame = true;
    return r;
}

/* ---- Entries: the frame builder's calls, on the field's page ------------------------------ */

/* The overflow a 16-bit sum or difference leaves, which the callers see. */
static bool sum_overflows(uint16_t a, uint16_t b, uint16_t sum) { return (~(a ^ b) & (a ^ sum) & 0x8000) != 0; }
static bool difference_overflows(uint16_t a, uint16_t b, uint16_t d) { return ((a ^ b) & (a ^ d) & 0x8000) != 0; }

/* 16-bit A, the tile. Y comes back from the cell the copy saved it in, where
 * the original reads it. */
void field_copy_sprite_tile(Snes *s)
{
    GameState *g = state_of(s);
    leave_saved_y(g, s->cpu.y);
    uint16_t cursor = copy_sprite_tile(g, s->cpu.a);
    s->cpu.a = cursor;
    s->cpu.y = original_cell_value16(g, 0xC5);
    uint16_t to = (uint16_t)(cursor - TILE_BYTES);
    m_set_flag(s, CPU_FLAG_V, sum_overflows(to, TILE_BYTES, cursor));
    m_rts(s);
}

void field_copy_sprite_tile_flipped(Snes *s)
{
    GameState *g = state_of(s);
    leave_saved_y(g, s->cpu.y);
    copy_sprite_tile_flipped(g, s->cpu.a);
    s->cpu.y = original_cell_value16(g, 0xC5);
    m_rts(s);
}

/* 8-bit A and X, the object doubled in X. The carry says whether there is a
 * frame; A is the frame, its high byte what the entry's address left there,
 * and the overflow that of the entry's last sum (the animation's offset
 * added to the table, or the frame taken back off where it started over). */
void field_read_animation_frame(Snes *s)
{
    AnimationFrame r = read_animation_frame(state_of(s), field_object_in_x(s));
    s->cpu.a = (uint16_t)((r.entry & 0xFF00) | r.frame);
    m_set_flag(s, CPU_FLAG_C, r.has_frame);
    uint16_t offset = (uint16_t)(r.ended_at - r.table);
    bool overflow = r.entry == r.ended_at ? sum_overflows(offset, r.table, r.entry)
                                          : difference_overflows(r.ended_at, (uint16_t)(r.ended_at - r.entry), r.entry);
    m_set_flag(s, CPU_FLAG_V, overflow);
    m_rts(s);
}
