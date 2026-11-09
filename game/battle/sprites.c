/* Battle: building the objects' sprite graphics.
 *
 * An object's frame is drawn from its tile buffer in work RAM, which holds
 * the frame's tiles in the order its sprite parts use them (battle/animation.h).
 * Building a frame fills the buffer from the frame's tile list: each tile is
 * blank, a tile of the object's graphics set, or one mirrored left to right;
 * the list goes on with the offsets of the frame's sprite parts from the
 * object's position. How many tiles a frame has and how they are laid out in
 * the buffer (rows of tiles at offsets) is the object's graphics kind's.
 *
 * Beside that: the objects' palette animations, which cycle a few sprite
 * colors through color sets; dividing an effect's animation records into its
 * animations; and building the sprites of bitmap shapes, a sprite for each
 * set bit of a small bitmap. Drawing the objects into the OAM buffer is
 * battle/draw_objects.c. */
#include "battle/animation.h"

#include "battle/animation_data.h"
#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/wait.h"
#include "battle/frame.h"
#include "battle/entries.h"
#include "charge.h"
#include "vars.h"
#include "wram.h"

/* ---- An object's frame graphics ----------------------------------------------------------- */

/* A tile descriptor: the tile's number in the graphics set (0 a blank tile),
 * and whether it is mirrored left to right. */
enum { TILE_NUMBER = 0x07FF, TILE_MIRRORED = 0x4000, BLANK_TILE = 0 };
enum { TILE_BYTES = 32, TILE_DESCRIPTOR_BYTES = 2 };

/* The graphics kind whose frames other code builds, and the kind whose
 * layout depends on the object. */
enum { KIND_BUILT_ELSEWHERE = 3, KIND_WIDE = 2 };

bool battle_object_builds_graphics(const GameState *g, unsigned object)
{
    return battle_object_kind(g, object) != KIND_BUILT_ELSEWHERE;
}

/* The object is a word the upload and the builders read whole; the
 * animations set its low byte. */
void battle_show_object_frame(GameState *g, unsigned object, uint8_t frame)
{
    set_battle_graphics_object(g, (uint16_t)((battle_graphics_object(g) & 0xFF00) | (uint8_t)object));
    set_battle_graphics_frame(g, frame);
}

/* The layouts: one per kind, and one more for the wide kind's objects other
 * than the first enemy, whose rows sit in another order. */
enum { FIRST_ENEMY_OBJECT = BATTLE_FIRST_ENEMY, WIDE_LAYOUT_ELSEWHERE = 5 };

static uint8_t layout_of(uint8_t kind, unsigned object)
{
    return kind == KIND_WIDE && object != FIRST_ENEMY_OBJECT ? (uint8_t)WIDE_LAYOUT_ELSEWHERE : kind;
}

/* Where a tile goes: the object's tile buffer is in bank $7E, and a row of
 * tiles runs on in 16 bits from its start, carrying into the next bank only
 * within a tile, as the original's indexed writes do. */
enum { TILE_BUFFER_BANK = 0x7E };
static uint32_t buffer_address(uint16_t at) { return battle_at(TILE_BUFFER_BANK, 0) + at; }

static void clear_tile(GameState *g, uint16_t to)
{
    for (unsigned k = 0; k < TILE_BYTES; k += 2)
        wram_write16(g, buffer_address(to) + k, 0);
}

static void copy_tile(GameState *g, uint32_t from, uint16_t to)
{
    for (unsigned k = 0; k < TILE_BYTES; k += 2)
        wram_write16(g, buffer_address(to) + k, battle_sprite_graphics_word(g, from + k));
}

static void copy_tile_mirrored(GameState *g, uint32_t from, uint16_t to)
{
    for (unsigned k = 0; k < TILE_BYTES; k++)
        wram_write8(g, buffer_address(to) + k, battle_mirrored_byte(g, battle_sprite_graphics_byte(g, from + k)));
}

/* What the original leaves of a tile in its cells: the descriptor, or for a
 * mirrored tile where it copied from and to. */
enum { LEFT_TILE = 0x0C };

/* Build one tile of a frame from its descriptor. */
static void build_tile(GameState *g, uint16_t descriptor, uint32_t graphics, uint16_t to)
{
    uint16_t number = descriptor & TILE_NUMBER;
    if (number == BLANK_TILE) {
        clear_tile(g, to);
        charge_machine_accesses(g, 2);
        return;
    }
    uint16_t offset = (uint16_t)(number * TILE_BYTES + (uint16_t)graphics);
    uint32_t from = battle_at((uint8_t)(graphics >> 16), 0) + offset;
    bool mirrored = (descriptor & TILE_MIRRORED) != 0;
    if (mirrored)
        copy_tile_mirrored(g, from, to);
    else
        copy_tile(g, from, to);
    charge_machine_accesses(g, mirrored ? 4 : 2);
}

/* The offsets of the frame's sprite parts from the object's position follow
 * its tiles, a signed byte each, the parts' X and Y in turn; each object has
 * the offsets of four parts, and a larger kind's run on into the next
 * objects'. */
enum { PARTS_PER_OBJECT = 4, OFFSETS_PER_PART = 2 };

static void set_part_offset(GameState *g, unsigned object, unsigned i, int8_t offset)
{
    unsigned n = object * PARTS_PER_OBJECT * OFFSETS_PER_PART + i;
    unsigned part = n / OFFSETS_PER_PART;
    if (n % OFFSETS_PER_PART == 0)
        set_battle_part_offsets_x(g, part, (uint16_t)offset);
    else
        set_battle_part_offsets_y(g, part, (uint16_t)offset);
}

/* What the original leaves in its cells once it has built a frame: the
 * object twice over, the tile list and graphics pointers, the part offsets'
 * count and first place, and its pointer to the layout's rows in the ROM's
 * bank $CC. */
enum {
    LEFT_OBJECT = 0x08, LEFT_PART_BYTES = 0x00, LEFT_FIRST_PART = 0x02, LEFT_LAYOUT_BANK = 0x04, LEFT_ROWS = 0x05,
    LEFT_TILE_LIST = 0, LEFT_GRAPHICS = 3, LAYOUT_BANK = 0xCC,
};

// $C1:1C55
/* The original counts its rows and tiles in the battle's direct-page cells
 * as it builds, which this leaves once at the end, and keeps the data bank on
 * the stack meanwhile: a graphics set placed over those cells or the stack
 * would read other bytes here. The resource-bank contract excludes that
 * aliasing: graphics are cartridge data or the field's unpacked sets. */
void battle_build_frame_graphics(GameState *g, unsigned object, uint8_t frame)
{
    set_battle_anim_held(g, object, 0);
    if (frame == battle_object_frame_shown(g, object)) {
        charge_machine_accesses(g, 2);
        return;
    }
    set_battle_object_frame_shown(g, object, frame);
    uint8_t kind = battle_object_kind(g, object);

    uint32_t list = battle_object_tile_lists(g, object);
    uint16_t at = (uint16_t)(list + battle_multiply_long(g, frame, battle_frame_tile_list_bytes(g, kind)));
    uint32_t tiles = battle_at((uint8_t)(list >> 16), at);
    uint32_t graphics = battle_object_graphics(g, object);

    uint8_t layout_number = layout_of(kind, object);
    BattleSpriteLayout layout = battle_sprite_layout(g, battle_multiply(g, layout_number, BATTLE_LAYOUT_BYTES));
    uint16_t buffer = battle_object_tile_buffer(g, object);
    for (unsigned row = 0; row < layout.rows; row++) {
        uint16_t to = (uint16_t)(buffer + layout.row_offset[row]);
        for (unsigned t = 0; t < layout.tiles_per_row; t++) {
            build_tile(g, battle_tile_list_word(g, tiles), graphics, to);
            tiles = battle_at((uint8_t)(tiles >> 16), (uint16_t)(tiles + TILE_DESCRIPTOR_BYTES));
            to = (uint16_t)(to + TILE_BYTES);
        }
    }

    uint8_t part_bytes = battle_frame_part_offset_bytes(g, kind);
    for (unsigned i = 0; i < part_bytes; i++)
        set_part_offset(g, object, i, (int8_t)battle_tile_list_byte(g, tiles + i));
    set_battle_graphics_built(g, (uint8_t)(battle_graphics_built(g) + 1));
    charge_machine_accesses(g, 15);
}

/* ---- The objects' palette animations ------------------------------------------------------ */

/* Four palette animations run at once, each through a range of frames, a
 * frame every few battle frames; a frame puts a color set into the first
 * sprite palette. */
enum { PALETTE_ANIMATIONS = 4 };

/* A frame's place: which quarter of the first sprite palette its colors go
 * into, from color 1 of it, and whether the set has seven colors rather than
 * three. */
enum { PLACE_QUARTER = 0x03, PLACE_SEVEN_COLORS = 0x80, COLORS_PER_QUARTER = 4 };
enum { FIRST_SPRITE_PALETTE = 128, SMALL_SET_COLORS = BATTLE_SMALL_SET_COLORS, LARGE_SET_COLORS = BATTLE_LARGE_SET_COLORS, COLOR_BYTES = 2 };

/* The frame after the one the animation shows, starting over after its
 * last. */
static uint8_t next_palette_frame(GameState *g, unsigned anim)
{
    uint8_t reached = (uint8_t)(battle_palette_anim_frame(g, anim) + 1);
    uint8_t first = battle_palette_anim_first(g, anim);
    uint8_t frame = (uint8_t)(first + reached);
    if (frame > battle_palette_anim_last(g, anim)) {
        reached = 0;
        frame = first;
    }
    set_battle_palette_anim_frame(g, anim, reached);
    return frame;
}

/* What the original leaves in its cells of the last frame it showed: the
 * set, the place, and the count of bytes copied down to 0. */
enum { LEFT_SET = 0x02, LEFT_PLACE = 0x04 };

void battle_put_color_set(GameState *g, bool seven, uint8_t set, uint8_t quarter)
{
    uint8_t colors = seven ? LARGE_SET_COLORS : SMALL_SET_COLORS;
    unsigned first = FIRST_SPRITE_PALETTE + 1 + quarter * COLORS_PER_QUARTER;
    for (unsigned c = 0; c < colors; c++)
        set_palette_working(g, first + c, battle_palette_set_color(g, seven, set, c));
}

/* The original doubles the frame's number in a byte, and finds the set's
 * colors on the multiplier, whose operands and product it leaves
 * (battle/arith.h). */
enum { FRAME_NUMBERS_IN_A_BYTE = 0x7F };

static void show_palette_frame(GameState *g, uint8_t number)
{
    BattlePaletteFrame frame = battle_palette_frame(g, battle_palette_anim_frames(g), number & FRAME_NUMBERS_IN_A_BYTE);
    bool seven = (frame.place & PLACE_SEVEN_COLORS) != 0;
    uint8_t colors = seven ? LARGE_SET_COLORS : SMALL_SET_COLORS;
    (void)battle_multiply(g, colors * COLOR_BYTES, frame.set);
    battle_put_color_set(g, seven, frame.set, frame.place & PLACE_QUARTER);
    charge_machine_accesses(g, 4);
}

enum { LEFT_ANIMATION = 0x00 };

// $CC:F1E7
/* Step the palette animations a frame: each running one whose time is up
 * shows its next frame. */
void battle_step_palette_animations(GameState *g)
{
    for (unsigned anim = 0; anim < PALETTE_ANIMATIONS; anim++) {
        if (!battle_palette_anim_on(g, anim))
            continue;
        uint8_t left = (uint8_t)(battle_palette_anim_delay(g, anim) - 1);
        set_battle_palette_anim_delay(g, anim, left);
        if (left != 0)
            continue;
        set_battle_palette_anim_delay(g, anim, battle_palette_anim_period(g, anim));
        show_palette_frame(g, next_palette_frame(g, anim));
    }
    charge_machine_accesses(g, 2);
}

/* ---- An effect's animations ------------------------------------------------------------- */

/* An effect's sixteen animations are runs of four-byte records, one after
 * another. The top bits of a record's first byte mark where they divide: the
 * last frame of an animation has LAST_FRAME, and a record with SEPARATOR
 * ends the animation, the next starting after it; EMPTY alone marks an
 * animation with no frames. The first record after a separator is taken as a
 * frame whatever its marks. An animation's count is its frames up to its last
 * marked one, and one whose frames aren't marked keeps the count it had. */
enum { RECORD_BYTES = 4, EFFECT_ANIMATIONS = 16 };
enum { RECORD_MARKS = 0xE0, SEPARATOR = 0x80, LAST_FRAME = 0x40, EMPTY = 0x20 };

enum { LEFT_COUNT = 0x00, LEFT_ANIMATION_NUMBER = 0x02 };

// $C1:48EC
/* Find where each of the effect's animations starts and how many frames it
 * has. */
void battle_find_effect_animations(GameState *g)
{
    uint16_t at = battle_effect_records(g);
    unsigned animation = 0;
    uint8_t frames = 0;
    set_battle_effect_animation_starts(g, animation, at);
    for (;;) {
        uint8_t marks = battle_effect_record_marks(g, at) & RECORD_MARKS;
        at = (uint16_t)(at + RECORD_BYTES);
        if (marks == 0) {
            frames++;
            continue;
        }
        if (!(marks & SEPARATOR)) {
            if (marks & LAST_FRAME) {
                set_battle_effect_animation_frames(g, animation, (uint8_t)(frames + 1));
                frames = 0;
                continue;
            }
            set_battle_effect_animation_frames(g, animation, 0);
        }
        if (++animation == EFFECT_ANIMATIONS)
            break;
        set_battle_effect_animation_starts(g, animation, at);
        at = (uint16_t)(at + RECORD_BYTES);
        frames = 1;
    }
    charge_machine_accesses(g, 4);
}

/* ---- Bitmap shapes ---------------------------------------------------------------------- */

/* A bitmap shape is a small bitmap of sprites, from a stream of them in the
 * decode buffer: a header byte (the shape's width in the high nibble, its rows
 * in the low one; 0 ends the stream), a byte for each row with a bit for each
 * of up to eight sprites from the left, then each sprite's tile and
 * attributes for the set bits in turn, and two bytes more. A shape is built
 * into an effect sprite set, a sprite for each set bit at its column and row;
 * up to four shapes are built at a time, into the sets from battle_bitmap_set
 * on. */
enum { SHAPES_AT_A_TIME = 4, SHAPE_ROWS = 0x0F, SHAPE_WIDTH_SHIFT = 4, SHAPE_COLUMNS = 8, LEFTMOST = 0x80 };
enum { SPRITE_STREAM_BYTES = 2, SHAPE_TAIL_BYTES = 2 };

static uint8_t stream_byte(const GameState *g, uint16_t at) { return battle_decoded_tiles(g, at); }

static void add_sprite(GameState *g, uint16_t set, uint16_t at, unsigned row, unsigned column, uint8_t tile,
                       uint8_t attributes)
{
    set_battle_effect_sprite_data(g, at + EFFECT_SPRITE_TILE, tile);
    set_battle_effect_sprite_data(g, at + EFFECT_SPRITE_ATTRIBUTES, attributes | battle_sprite_attributes(g));
    set_battle_effect_sprite_data(g, at + EFFECT_SPRITE_Y, battle_bitmap_cell_position(g, (uint16_t)row));
    set_battle_effect_sprite_data(g, at + EFFECT_SPRITE_X, battle_bitmap_cell_position(g, (uint16_t)column));
    set_battle_bitmap_counts(g, set, (uint8_t)(battle_bitmap_counts(g, set) + 1));
}

/* What the original leaves in its cells of the last shape it built: its
 * width, its rows counted down and up, the place after its last sprite, the
 * columns walked and the shapes built; and each row's bits shifted out to
 * nothing. */
enum { LEFT_WIDTH = 0x00, LEFT_ROWS_LEFT = 0x01, LEFT_ROW = 0x02, LEFT_SPRITE = 0x04, LEFT_COLUMN = 0x06 };
enum { LEFT_SHAPES = 0x0C };

/* Build a shape into the set from the stream at `at` (after its header),
 * returning where the stream goes on. */
static uint16_t build_shape(GameState *g, uint16_t set, uint8_t header, uint16_t at)
{
    uint8_t rows = header & SHAPE_ROWS;
    uint8_t bits[SHAPE_ROWS];
    for (unsigned row = 0; row < rows; row++)
        bits[row] = stream_byte(g, at++);
    uint16_t sprite = battle_sprite_set_start(g, set);
    for (unsigned row = 0; row < rows; row++) {
        for (unsigned column = 0; column < SHAPE_COLUMNS; column++) {
            if (!(bits[row] << column & LEFTMOST))
                continue;
            add_sprite(g, set, sprite, row, column, stream_byte(g, at), stream_byte(g, (uint16_t)(at + 1)));
            at = (uint16_t)(at + SPRITE_STREAM_BYTES);
            sprite = (uint16_t)(sprite + EFFECT_SPRITE_BYTES);
        }
    }
    charge_machine_accesses(g, 8 + (header & SHAPE_ROWS));
    return (uint16_t)(at + SHAPE_TAIL_BYTES);
}

// $C1:4943
/* Build the next shapes of the stream, up to four, or to the stream's end;
 * each set is emptied first. The set is counted on in its low byte, as the
 * original counts it. */
static void build_bitmap_shapes(GameState *g)
{
    uint8_t built = 0;
    for (;;) {
        uint16_t set = battle_bitmap_set(g);
        set_battle_bitmap_counts(g, set, 0);
        uint16_t at = battle_bitmap_stream(g);
        uint8_t header = stream_byte(g, at);
        if (header == 0) {
            (charge_machine_accesses(g, 2), charge_machine_rom_accesses(g, 2));
            break;
        }
        set_battle_bitmap_stream(g, build_shape(g, set, header, (uint16_t)(at + 1)));
        set_battle_bitmap_set(g, (uint16_t)((set & 0xFF00) | (uint8_t)(set + 1)));
        if (++built == SHAPES_AT_A_TIME)
            break;
    }
    charge_machine_accesses(g, 1);
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The object and frame come in the cells the original's callers set, which
 * the upload reads too. */

/* Building leaves A's high byte clear and the carry set, as its last test
 * does; returning early leaves them as they came. */

// $C1:1C4A
/* The same, for objects whose graphics are built this way. */

/* Only an object whose graphics kind has a layout, as every object the
 * battle builds has, and whose frame's part offsets stay in the table (a
 * large kind's run on into the next objects', and the last objects are the
 * smallest); and A's high byte clear, as the game has it, since the original
 * picks the kind's sizes with the whole of A as its index. */
enum { GRAPHICS_KINDS = 5, PART_OFFSET_WORDS = BATTLE_COMBATANTS * PARTS_PER_OBJECT * OFFSETS_PER_PART };

bool battle_object_graphics_known(GameState *g, unsigned object)
{
    bool known = battle_names_combatant((uint16_t)object) && battle_object_kind(g, object) < GRAPHICS_KINDS
                 && object * PARTS_PER_OBJECT * OFFSETS_PER_PART
                            + battle_frame_part_offset_bytes(g, battle_object_kind(g, object)) <= PART_OFFSET_WORDS;
    if (!known)
        return false;
    /* The additional resource check is validation, outside the existing
     * frame-building estimate. Keep the layout check's ordinary charge. */
    uint32_t mark = charge_uncounted_begin(g);
    bool stored = battle_sprite_graphics_storage(battle_object_graphics(g, object));
    charge_uncounted_end(g, mark);
    return stored;
}

// $C1:4943
/* Up to four of the shapes a frame: the next frame waited for and the frame's
 * work let run first (`frame`, the wait and then the frame step, routines
 * of their own). */
static bool build_bitmap_shapes_over_a_frame(GameState *g, bool (*frame)(void *ctx, bool step), void *ctx)
{
    if (!frame(ctx, false) || !frame(ctx, true))
        return false;
    build_bitmap_shapes(g);
    return true;
}

static bool frame_on_the_machine(void *ctx, bool step)
{
    Snes *s = ctx;
    return step ? battle_run(s, 0xC14946, 0xC110FA, battle_frame_step)
                : battle_wait_frame(s, 0xC14943, 0xC1007E, DC_AL | DC_X | DC_Y | DC_FLAGS);
}

void battle_build_bitmap_shapes_entry(Snes *s)
{
    if (!build_bitmap_shapes_over_a_frame(state_of(s), frame_on_the_machine, s))
        return;
    s->cpu.a = 0;
    m_rts(s);
}

typedef struct {
    GameState *game;
    Platform *pf;
} ShapesContext;

static bool wait_for_the_shapes_frame(void *ctx, bool step)
{
    ShapesContext *c = ctx;
    bool done = false;
    if (step)
        TIMED_AS(c->game, 0xC110FA, done = battle_frame_stepped(c->game, c->pf));
    else
        TIMED_AS(c->game, 0xC1007E, done = battle_wait_for_frame(c->game, c->pf));
    return done;
}

bool battle_build_bitmap_shapes(GameState *g, Platform *pf)
{
    ShapesContext c = { g, pf };
    return build_bitmap_shapes_over_a_frame(g, wait_for_the_shapes_frame, &c);
}
