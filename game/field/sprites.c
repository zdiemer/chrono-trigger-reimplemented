/* Field objects' sprites: the frame's sprite table.
 *
 * Every frame the field builds the OAM image (oam_buffer and oam_high_table, which the
 * vblank uploads) from the objects in view, walking the rows from the bottom
 * of the screen up (field/object_update.c) so that nearer objects come first
 * and are drawn over the ones behind them. The table is kept in three ranges
 * by an object's priority: the objects drawn in front of everything, the
 * ordinary ones, and those drawn behind. Each range fills from its start, and
 * the entries a range used last frame and not this one are hidden.
 *
 * An object's sprites are its sprite records (field_sprite_records, from the
 * object's offset): 4, 8, 12 or 24 of them by the object's layout, each a
 * sprite's place on the screen, its offset from the object's, and its tile and
 * attributes. Before it is drawn an object brings its records up to date:
 * only their screen positions where only the object moved, or rebuilt from
 * its frame's data after the frame builder drew a new frame, when the spare
 * tile areas the builder drew the frame into are free again. The 4-sprite
 * layout is placed by place_sprites, the 8-, 12- and 24-sprite ones by
 * place_larger, each with the original's own rules for which sprites near
 * the screen's edges it hides.
 *
 * An object's sprites are read into a list of Sprite, whose X is a signed
 * screen coordinate, and one function writes a list into the table
 * (write_sprites), streaming the entries through the work RAM port. The
 * table's form -- X as a byte and a ninth bit apart, Y as a byte, the line
 * that hides a sprite -- is the video service's (VID_OAM_*, vid_oam_*), which
 * turns a position into it and back. The lists are an object's, not a
 * range's: a range's would be up to 128 sprites of locals, more than a build
 * for the 65816 can keep. */
#include "field/sprites.h"
#include "field/field.h"
#include "field/object.h"
#include "platform/video.h"

#include "charge.h"
#include "core/sprite_table.h"
#include "cpu_io.h"
#include "field/bridge.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* A sprite as the table shows it. Its Y is already in the table's form (the
 * records hold it that way, table_y); its X is a screen coordinate. */
typedef struct {
    int16_t x;
    uint8_t y, tile, attributes;
    bool large;
} Sprite;

enum { MOST_SPRITES = 24 };

/* The layouts: how many sprites, and high table bytes (VID_OAM_SPRITES_A_HIGH_BYTE a byte). */
typedef struct {
    uint8_t sprites, high_bytes;
} Layout;
static const Layout layouts[4] = { [LAYOUT_4] = { 4, 1 }, [LAYOUT_8] = { 8, 2 }, [LAYOUT_12] = { 12, 3 },
                                   [LAYOUT_24] = { 24, 6 } };

static Layout layout_of(const GameState *g, FieldObject obj) { return layouts[object_sprite_layout(g, obj)]; }

/* ---- The table's storage: the only code that knows its form ------------------------------ */

/* The table is in low RAM at FIELD_SPRITE_TABLE_ADDRESS, in the video
 * service's form (VID_OAM_*): the entries, then the high table. The ranges'
 * cursors are kept as the table's addresses; here they are offsets into it. */
enum {
    OAM_ENTRY = VID_OAM_ENTRY_BYTES,
    BITS_A_SPRITE = VID_OAM_HIGH_BITS,
    SPRITES_A_HIGH_BYTE = VID_OAM_SPRITES_A_HIGH_BYTE,
};

enum { RANGES = 3 };   /* by SpriteRange, as field_sprite_range_* index them */

/* Each range's first entry and the entry after its last, and its first high
 * table byte, as offsets into the table: the front range entries 4-27, the
 * middle 28-103, the back 104-127. */
static const uint16_t range_first_entry[RANGES] = { 0x070, 0x1A0, 0x010 };
static const uint16_t range_limit[RANGES] = { 0x1A0, 0x200, 0x070 };
static const uint16_t range_first_high[RANGES] = { 0x207, 0x21A, 0x201 };

static uint16_t range_next(const GameState *g, unsigned r) { return (uint16_t)(field_sprite_range_next(g, r) - FIELD_SPRITE_TABLE_ADDRESS); }
static void set_range_next(GameState *g, unsigned r, uint16_t at) { set_field_sprite_range_next(g, r, (uint16_t)(at + FIELD_SPRITE_TABLE_ADDRESS)); }
static uint16_t range_high(const GameState *g, unsigned r) { return (uint16_t)(field_sprite_range_high(g, r) - FIELD_SPRITE_TABLE_ADDRESS); }
static void set_range_high(GameState *g, unsigned r, uint16_t at) { set_field_sprite_range_high(g, r, (uint16_t)(at + FIELD_SPRITE_TABLE_ADDRESS)); }

/* The table's entries are streamed through the work RAM port, in bank $7E. */
static void open_table_stream(GameState *g) { hw_port_seek_bank(g, 0); }

static void set_high_bits(GameState *g, uint16_t at, uint8_t bits)
{
    if (at < SPRITE_TABLE_BYTES)
        sprite_table_set_byte(g, at, bits);
}

/* A sprite's whole X, for a view wider than the original's screen: objects
 * run only within a little of the screen (object_view.c's window, two map
 * tiles past it), so a nine-bit X far left of the screen is one right of it. */
enum { FAR_LEFT_OF_THE_SCREEN = -128, NINE_BITS_ROUND = 0x200 };
static int16_t whole_x(int16_t x) { return (int16_t)(x < FAR_LEFT_OF_THE_SCREEN ? x + NINE_BITS_ROUND : x); }

/* Write an object's sprites into its range: their high table bits at the
 * range's high table cursor, and their entries from its next entry; and move
 * the range on past them. */
static void write_sprites(GameState *g, unsigned range, const Sprite *sprites, unsigned count)
{
    uint16_t high = range_high(g, range);
    for (unsigned b = 0; b < count / SPRITES_A_HIGH_BYTE; b++) {
        uint8_t bits = 0;
        for (unsigned i = 0; i < SPRITES_A_HIGH_BYTE; i++) {
            const Sprite *s = &sprites[b * SPRITES_A_HIGH_BYTE + i];
            bits = (uint8_t)(bits | vid_oam_high_bits(s->x, s->large) << (BITS_A_SPRITE * i));
        }
        set_high_bits(g, (uint16_t)(high + b), bits);
    }
    set_range_high(g, range, (uint16_t)(high + count / SPRITES_A_HIGH_BYTE));

    uint16_t entry = range_next(g, range);
    hw_port_seek_address(g, (uint16_t)(entry + FIELD_SPRITE_TABLE_ADDRESS));
    set_range_next(g, range, (uint16_t)(entry + count * OAM_ENTRY));
    for (unsigned n = 0; n < count; n++) {
        hw_port_write(g, vid_oam_x_low(sprites[n].x));
        hw_port_write(g, sprites[n].y);
        hw_port_write(g, sprites[n].tile);
        hw_port_write(g, sprites[n].attributes);
        sprite_table_place(g, entry / OAM_ENTRY + n, whole_x(sprites[n].x), sprites[n].y);
    }
    charge_accesses(g, 1); /* the completed sprite count */
}

/* Hide the entries each range used last frame beyond this frame's last, and
 * remember where this frame's ended. (The cursors are compared as the
 * addresses they are kept as.) */
static void hide_leftovers(GameState *g)
{
    for (unsigned r = 0; r < RANGES; r++) {
        uint16_t next = field_sprite_range_next(g, r);
        uint16_t limit = (uint16_t)(range_limit[r] + FIELD_SPRITE_TABLE_ADDRESS);
        for (uint16_t entry = next; entry < field_sprite_range_end(g, r); entry = (uint16_t)(entry + OAM_ENTRY)) {
            sprite_table_set_byte(g, (uint16_t)(entry - FIELD_SPRITE_TABLE_ADDRESS + VID_OAM_ENTRY_Y), VID_OAM_HIDDEN_Y);
            if ((uint16_t)(entry + OAM_ENTRY) >= limit)
                break;
        }
        set_field_sprite_range_end(g, r, next);
    }
}

// $C0:B262
/* The table's first sprites hidden, as the field comes back from a battle
 * (whose sprites the vblank sent from there). */
void sprites_hide_the_first(GameState *g, unsigned count)
{
    for (unsigned n = 0; n < count; n++)
        sprite_table_set_byte(g, n * OAM_ENTRY + VID_OAM_ENTRY_Y, VID_OAM_HIDDEN_Y);
}

/* A fresh start (the field's): each range taken as having used all its
 * entries last frame, so the first table built hides every entry it doesn't
 * fill. */
void sprites_hide_all_next_frame(GameState *g)
{
    for (unsigned r = 0; r < RANGES; r++)
        set_field_sprite_range_end(g, r, (uint16_t)(range_limit[r] + FIELD_SPRITE_TABLE_ADDRESS));
}

/* ---- The records' storage ----------------------------------------------------------------- */

/* One of an object's sprites as its records keep it (field_sprite_records, 8
 * bytes a sprite from the object's offset): its screen X's low byte, its
 * screen Y in the table's form, its X offset from the object's screen
 * position (a word), its Y offset, and its tile and attributes (its look).
 * The object's sprites' high table bits are kept apart, in three words for up
 * to 24 sprites. The frame's data a rebuild takes the records from
 * (field_sprite_frame_data) keeps the same sprite at the same place. These
 * functions are the only code that knows the layout. */
typedef struct {
    uint16_t at;   /* where the record is among the records */
} SpriteRecord;

enum {
    RECORD_BYTES = 8,
    RECORD_X = 0,
    RECORD_Y = 1,
    RECORD_X_OFFSET = 2,
    RECORD_Y_OFFSET = 4,
    RECORD_TILE = 6,
    RECORD_ATTRIBUTES = 7,
};

static SpriteRecord sprite_record(const GameState *g, FieldObject obj, unsigned n)
{
    return (SpriteRecord){ (uint16_t)(object_sprite_records(g, obj) + n * RECORD_BYTES) };
}

static uint8_t record_byte(const GameState *g, SpriteRecord r, unsigned field)
{
    return field_sprite_records_byte(g, (unsigned)r.at + field);
}
static void set_record_byte(GameState *g, SpriteRecord r, unsigned field, uint8_t v)
{
    set_field_sprite_records_byte(g, (unsigned)r.at + field, v);
}
static uint16_t record_word(const GameState *g, SpriteRecord r, unsigned field)
{
    uint8_t low = record_byte(g, r, field);
    return (uint16_t)(record_byte(g, r, field + 1) << 8 | low);
}
static void set_record_word(GameState *g, SpriteRecord r, unsigned field, uint16_t v)
{
    set_record_byte(g, r, field, (uint8_t)v);
    set_record_byte(g, r, field + 1, (uint8_t)(v >> 8));
}

static uint8_t record_x(const GameState *g, SpriteRecord r) { return record_byte(g, r, RECORD_X); }
static void set_record_x(GameState *g, SpriteRecord r, uint8_t x) { set_record_byte(g, r, RECORD_X, x); }
static uint8_t record_y(const GameState *g, SpriteRecord r) { return record_byte(g, r, RECORD_Y); }
static void set_record_y(GameState *g, SpriteRecord r, uint8_t y) { set_record_byte(g, r, RECORD_Y, y); }
static uint16_t record_x_offset(const GameState *g, SpriteRecord r) { return record_word(g, r, RECORD_X_OFFSET); }
static void set_record_x_offset(GameState *g, SpriteRecord r, uint16_t v) { set_record_word(g, r, RECORD_X_OFFSET, v); }
static uint8_t record_y_offset(const GameState *g, SpriteRecord r) { return record_byte(g, r, RECORD_Y_OFFSET); }
static void set_record_y_offset(GameState *g, SpriteRecord r, uint8_t v) { set_record_byte(g, r, RECORD_Y_OFFSET, v); }
static uint8_t record_tile(const GameState *g, SpriteRecord r) { return record_byte(g, r, RECORD_TILE); }
static uint8_t record_attributes(const GameState *g, SpriteRecord r) { return record_byte(g, r, RECORD_ATTRIBUTES); }
static void set_record_look(GameState *g, SpriteRecord r, uint16_t tile_and_attributes)
{
    set_record_word(g, r, RECORD_TILE, tile_and_attributes);
}

/* The same sprite in the frame's data. */
static uint16_t frame_word(const GameState *g, SpriteRecord r, unsigned field)
{
    uint8_t low = field_sprite_frame_data_byte(g, (unsigned)r.at + field);
    return (uint16_t)(field_sprite_frame_data_byte(g, (unsigned)r.at + field + 1) << 8 | low);
}
static uint16_t frame_x_offset(const GameState *g, SpriteRecord r) { return frame_word(g, r, RECORD_X_OFFSET); }
static uint8_t frame_y_offset(const GameState *g, SpriteRecord r)
{
    return field_sprite_frame_data_byte(g, (unsigned)r.at + RECORD_Y_OFFSET);
}
static uint16_t frame_look(const GameState *g, SpriteRecord r) { return frame_word(g, r, RECORD_TILE); }

/* A new frame's sprite, which the frame builder puts in the frame's data
 * (field/frames.c): its offsets (the X kept as a word), its tile and its
 * attributes. */
void sprites_set_frame_sprite(GameState *g, FieldObject obj, unsigned n, FrameSprite sprite)
{
    unsigned at = sprite_record(g, obj, n).at;
    set_field_sprite_frame_data_byte(g, at + RECORD_X_OFFSET, (uint8_t)sprite.x);
    set_field_sprite_frame_data_byte(g, at + RECORD_X_OFFSET + 1, sprite.x < 0 ? 0xFF : 0x00);
    set_field_sprite_frame_data_byte(g, at + RECORD_Y_OFFSET, (uint8_t)sprite.y);
    set_field_sprite_frame_data_byte(g, at + RECORD_TILE, sprite.tile);
    set_field_sprite_frame_data_byte(g, at + RECORD_ATTRIBUTES, sprite.attributes);
}

/* The same sprite put straight on the object's records (their offsets and
 * look; the screen position is the next placing's). */
void sprites_set_record_sprite(GameState *g, FieldObject obj, unsigned n, FrameSprite sprite)
{
    SpriteRecord r = sprite_record(g, obj, n);
    set_record_x_offset(g, r, (uint16_t)(int16_t)sprite.x);
    set_record_y_offset(g, r, (uint8_t)sprite.y);
    set_record_look(g, r, (uint16_t)(sprite.attributes << 8 | sprite.tile));
}

/* The object's sprites as a list, from its records and its high bits. */
static unsigned object_sprites(const GameState *g, FieldObject obj, Sprite *out)
{
    Layout layout = layout_of(g, obj);
    for (unsigned n = 0; n < layout.sprites; n++) {
        SpriteRecord r = sprite_record(g, obj, n);
        uint8_t pair = (uint8_t)(object_sprite_high_bits(g, obj, n) >> (BITS_A_SPRITE * (n % SPRITES_A_HIGH_BYTE)));
        out[n] = (Sprite){ vid_oam_x(record_x(g, r), pair), record_y(g, r), record_tile(g, r), record_attributes(g, r),
                           vid_oam_large(pair) };
    }
    return layout.sprites;
}

/* ---- Drawing an object --------------------------------------------------------------------- */

// $C0:B309
/* Draw the object's sprites into its range. */
static void draw_object(GameState *g, FieldObject obj)
{
    Sprite sprites[MOST_SPRITES];
    unsigned count = object_sprites(g, obj, sprites);
    write_sprites(g, object_sprite_range(g, obj), sprites, count);
}

/* ---- Which sprites show -------------------------------------------------------------------- *
 *
 * A sprite's place down the screen is its offset from the object added to
 * the object's screen Y; the table keeps that as a byte, so a sprite whose
 * top is above the screen wraps round to the table's last lines (224-255,
 * whose lower parts show at the screen's top), and one further off wraps
 * round onto the screen itself. So each placing hides some sprites on the
 * table's hiding line, by rules that depend on where the object stands --
 * above the screen, in its lower half or in its upper half -- and that are
 * each routine's own: an object in the same place shows different sprites
 * under different layouts. The port keeps each routine's rules. */
typedef enum { ABOVE_THE_SCREEN, LOWER_HALF, UPPER_HALF, ZONES } Zone;

/* The table's hiding line is the first below the screen. */
enum {
    HIDDEN = VID_OAM_HIDDEN_Y,
    BELOW_THE_SCREEN = VID_OAM_HIDDEN_Y,
    SPRITE_LINES = 32,
    TABLE_LINES = 256,
    SCREEN_MIDDLE_LINE = 128,
};

static Zone zone_of(int16_t screen_y)
{
    /* the object's screen Y is nine bits, and the ninth makes it negative
     * (screen_coordinate_of): 256-511 read as -256 to -1, so a place of
     * 256-383, below the screen, counts as above it, as the original takes
     * the ninth bit */
    if (screen_y < 0)
        return ABOVE_THE_SCREEN;
    return screen_y >= SCREEN_MIDDLE_LINE ? LOWER_HALF : UPPER_HALF;
}

/* The rules, by what shows. A sprite "reaching up" has a negative offset
 * (it is above the object's point); its top is its place down the screen. */
typedef enum {
    SHOWN_ANYWHERE,                       /* no hiding: past the bottom they wrap round to the top */
    HIDDEN_JUST_BELOW_THE_SCREEN,         /* hide a sprite starting on the 32 lines below the screen, which would show
                                             at its top; one further down wraps round unhidden */
    HIDDEN_REACHING_UP_FROM_BELOW,        /* the same, only for sprites reaching up */
    SHOWN_IN_THE_TOP_HALF,                /* show a sprite that lands in the screen's upper half: starting there,
                                             reaching down onto it from up to 32 lines above, or starting just below
                                             the screen, which wraps round to its top; hide one starting in the lower
                                             half, or so far above that it would wrap to the bottom */
    SHOWN_BELOW_OR_IN_THE_TOP_HALF,       /* sprites below the object show wherever they land; those reaching up as in
                                             the top half */
    SHOWN_REACHING_DOWN_ONTO_THE_SCREEN,  /* show only sprites reaching up that reach down onto the screen from the 32
                                             lines above it */
    SHOWN_ONLY_WRAPPED_FROM_FAR_ABOVE,    /* hide everything but a sprite below the object that starts 129 to 256 lines
                                             above the screen, whose place wraps round onto its upper half */
} ShowRule;

static bool shown(ShowRule rule, int16_t top, bool reaches_up)
{
    bool in_the_top_half = (top >= -SPRITE_LINES && top < SCREEN_MIDDLE_LINE) || top >= BELOW_THE_SCREEN;
    switch (rule) {
    case SHOWN_ANYWHERE: return true;
    case HIDDEN_JUST_BELOW_THE_SCREEN: return top < BELOW_THE_SCREEN || top >= TABLE_LINES;
    case HIDDEN_REACHING_UP_FROM_BELOW: return !reaches_up || top < BELOW_THE_SCREEN;
    case SHOWN_IN_THE_TOP_HALF: return in_the_top_half;
    case SHOWN_BELOW_OR_IN_THE_TOP_HALF: return !reaches_up || in_the_top_half;
    case SHOWN_REACHING_DOWN_ONTO_THE_SCREEN: return reaches_up && top >= -SPRITE_LINES && top < 0;
    case SHOWN_ONLY_WRAPPED_FROM_FAR_ABOVE: return !reaches_up && top >= -TABLE_LINES && top < -SCREEN_MIDDLE_LINE;
    }
    return true;
}

/* A sprite's Y in the table: its place, or the hiding line. */
static uint8_t table_y(ShowRule rule, int16_t screen_y, uint8_t offset)
{
    bool reaches_up = (int8_t)offset < 0;
    int16_t top = (int16_t)(screen_y + (int8_t)offset);
    return shown(rule, top, reaches_up) ? (uint8_t)top : HIDDEN;
}

/* Each routine's rules, by where the object stands (above, lower half,
 * upper half). */
typedef struct {
    ShowRule zone[ZONES];
} ShowRules;

/* 4 sprites, placed and rebuilt ($C0:B788, $C0:B8CA): below an object in the
 * lower half, a sprite starting just below the screen shows at its top
 * unless it reaches up. */
static const ShowRules FOUR = { { SHOWN_REACHING_DOWN_ONTO_THE_SCREEN, HIDDEN_REACHING_UP_FROM_BELOW,
                                  SHOWN_IN_THE_TOP_HALF } };
/* 8 sprites placed ($C0:BA65): unlike their rebuild, below an object in the
 * upper half they show wherever they land. */
static const ShowRules EIGHT_PLACED = { { SHOWN_REACHING_DOWN_ONTO_THE_SCREEN, HIDDEN_JUST_BELOW_THE_SCREEN,
                                          SHOWN_BELOW_OR_IN_THE_TOP_HALF } };
/* 8 sprites rebuilt ($C0:BCDC): below an object in the lower half, unlike
 * the 4-sprite layout, the lines just below the screen are hidden whichever
 * way the sprite reaches. */
static const ShowRules EIGHT_REBUILT = { { SHOWN_REACHING_DOWN_ONTO_THE_SCREEN, HIDDEN_JUST_BELOW_THE_SCREEN,
                                           SHOWN_IN_THE_TOP_HALF } };
/* 12 and 24 sprites ($C0:BFF2, $C0:C2BF, $C0:C73A): an object above the
 * screen shows almost nothing, and one in the lower half hides nothing. */
static const ShowRules LARGEST = { { SHOWN_ONLY_WRAPPED_FROM_FAR_ABOVE, SHOWN_ANYWHERE, SHOWN_IN_THE_TOP_HALF } };

static const ShowRules *rules_for(SpriteLayout layout, bool reload)
{
    switch (layout) {
    case LAYOUT_4: return &FOUR;
    case LAYOUT_8: return reload ? &EIGHT_REBUILT : &EIGHT_PLACED;
    default: return &LARGEST;
    }
}

/* ---- Placing the 4-sprite layout ------------------------------------------------------------ */

enum { SPRITES_PLACED = 4 };

/* Every field sprite is large: the large bit in each of a high byte's pairs. */
static uint8_t all_large(void)
{
    uint8_t bits = 0;
    for (unsigned i = 0; i < SPRITES_A_HIGH_BYTE; i++)
        bits = (uint8_t)(bits | vid_oam_high_bits(0, true) << (BITS_A_SPRITE * i));
    return bits;
}

// $C0:B788, $C0:B8CA
/* Place the object's four sprites at its screen position: each sprite's X
 * and Y are its offset from the object plus the object's screen position,
 * and the X's ninth bits go into the object's high bits. `reload` first takes
 * the offsets, tiles and attributes from the object's new frame. (Each pass
 * over the sprites is the original's, in its order: a frame's data past its
 * table can run on into the records.) */
static void place_sprites(GameState *g, FieldObject obj, bool reload)
{
    ScreenPoint at = object_screen_position(g, obj);
    uint8_t ninth_bits = 0;
    for (int n = SPRITES_PLACED - 1; n >= 0; n--) {
        SpriteRecord r = sprite_record(g, obj, (unsigned)n);
        if (reload)
            set_record_x_offset(g, r, frame_x_offset(g, r));
        int16_t x = (int16_t)(record_x_offset(g, r) + (uint16_t)at.x);
        set_record_x(g, r, vid_oam_x_low(x));
        ninth_bits = (uint8_t)(ninth_bits << BITS_A_SPRITE | vid_oam_high_bits(x, false));
    }
    object_set_first_sprites_high_bits(g, obj, (uint8_t)(ninth_bits | all_large()));

    for (unsigned n = 0; n < SPRITES_PLACED; n++) {
        SpriteRecord r = sprite_record(g, obj, n);
        if (reload)
            set_record_y_offset(g, r, frame_y_offset(g, r));
        set_record_y(g, r, table_y(FOUR.zone[zone_of(at.y)], at.y, record_y_offset(g, r)));
    }
    if (reload)
        for (unsigned n = 0; n < SPRITES_PLACED; n++) {
            SpriteRecord r = sprite_record(g, obj, n);
            set_record_look(g, r, frame_look(g, r));
        }
    /* Position and record reads and their temporary words, plus the two
     * high bit bytes: 14 accesses. Above the screen the original's
     * fifth-sprite write also reads the record pointer twice and its
     * offset: six more. */
    charge_accesses(g, 14 + (zone_of(at.y) == ABOVE_THE_SCREEN ? 6 : 0));
}

/* ---- Placing the larger layouts --------------------------------------------------------- */

/* Four sprites' X at the object's screen X, and their ninth bits, with every
 * sprite large, as one of the object's high bytes. */
static uint8_t place_four_across(GameState *g, FieldObject obj, unsigned first, int16_t screen_x, bool reload)
{
    uint8_t bits = 0;
    for (int n = (int)first + SPRITES_PLACED - 1; n >= (int)first; n--) {
        SpriteRecord r = sprite_record(g, obj, (unsigned)n);
        if (reload)
            set_record_x_offset(g, r, frame_x_offset(g, r));
        int16_t x = (int16_t)(record_x_offset(g, r) + (uint16_t)screen_x);
        set_record_x(g, r, vid_oam_x_low(x));
        bits = (uint8_t)(bits << BITS_A_SPRITE | vid_oam_high_bits(x, false));
    }
    uint8_t high = (uint8_t)(bits | all_large());
    object_set_sprites_high_bits(g, obj, first, high);
    return high;
}

// $C0:BA65, $C0:BCDC, $C0:BFF2, $C0:C2BF, $C0:C73A (and its fours $C0:C6E7)
/* Place a larger layout's sprites at the object's screen position, taking
 * their offsets and looks from its new frame where `reload`. The 24-sprite
 * layout is only ever placed (its frame's sprites are its records). False
 * where the object isn't drawn this frame: a 24-sprite object above the
 * screen whose last sprite starts on the screen or reaches up no more than
 * 256 lines above it (the original judges it by where its last sprite
 * lands). */
static bool place_larger(GameState *g, FieldObject obj, SpriteLayout layout, bool reload)
{
    ScreenPoint at = object_screen_position(g, obj);
    unsigned sprites = layouts[layout].sprites;
    for (unsigned first = 0; first < sprites; first += SPRITES_PLACED)
        place_four_across(g, obj, first, at.x, reload);
    Zone zone = zone_of(at.y);
    ShowRule rule = rules_for(layout, reload)->zone[zone];
    uint8_t last_offset = 0;
    for (unsigned n = 0; n < sprites; n++) {
        SpriteRecord r = sprite_record(g, obj, n);
        if (reload)
            set_record_y_offset(g, r, frame_y_offset(g, r));
        last_offset = record_y_offset(g, r);
        set_record_y(g, r, table_y(rule, at.y, last_offset));
    }
    if (reload)
        for (unsigned n = 0; n < sprites; n++) {
            SpriteRecord r = sprite_record(g, obj, n);
            set_record_look(g, r, frame_look(g, r));
        }
    /* The original reads the position, records and high bits back into
     * temporaries, and writes the 24-sprite count only on or below the
     * screen. */
    charge_accesses(g, layout == LAYOUT_24 && zone == ABOVE_THE_SCREEN ? 16 : 17);
    if (layout != LAYOUT_24 || zone != ABOVE_THE_SCREEN)
        return true;
    int16_t last_top = (int16_t)(at.y + (int8_t)last_offset);
    bool last_reaches = (int8_t)last_offset < 0 ? last_top >= -TABLE_LINES : last_top >= 0;
    return !last_reaches;
}

/* ---- The frame's tile areas ---------------------------------------------------------------- */

/* The frame builder draws an object's new frame into areas in bank $7F
 * (field_frame_area_at), one for the 4-sprite layout, two side by side for
 * the 8, three for the 12, which it holds for the object until the
 * object's records are rebuilt from the frame; then they are free again. It
 * looks for the object's claim from the first area on, as far as a run of
 * that many areas can start. */
static unsigned areas_for(SpriteLayout layout) { return layout == LAYOUT_4 ? 1 : layout == LAYOUT_8 ? 2 : 3; }

/* Who holds an area: an object, or a link to none. */
static FieldLink area_owner(const GameState *g, unsigned k) { return field_link_stored(field_frame_staging_claims(g, k)); }
static void set_area_owner(GameState *g, unsigned k, FieldLink owner)
{
    set_field_frame_staging_claims(g, k, field_link_byte(owner));
}
static bool area_free(const GameState *g, unsigned k) { return !field_link_is_object(area_owner(g, k)); }

// $C0:E952 (4), $C0:E97A (8), $C0:E9AA (12)
/* The frame builder claims areas for the object's new frame: the first run
 * of free ones long enough, in whose first the frame is drawn.
 * False where none is free. */
static bool claim_frame_areas(GameState *g, FieldObject obj, SpriteLayout layout)
{
    unsigned count = areas_for(layout);
    for (unsigned k = 0; k + count <= FIELD_FRAME_AREAS; k++) {
        bool free = true;
        for (unsigned a = 0; a < count; a++)
            free = free && area_free(g, k + a);
        if (!free)
            continue;
        for (unsigned a = 0; a < count; a++)
            set_area_owner(g, k + a, field_link_to(obj));
        set_field_object_frame_staging(g, obj, field_frame_area_at(k));
        return true;
    }
    return false;
}

/* The frame builder's claim, timed as the routine that makes it. */
bool sprites_claim_frame_areas(GameState *g, FieldObject obj, SpriteLayout layout)
{
    enum { CLAIM_4 = 0xC0E952, CLAIM_8 = 0xC0E97A, CLAIM_12 = 0xC0E9AA };
    bool claimed;
    TIMED_AS(g, layout == LAYOUT_4 ? CLAIM_4 : layout == LAYOUT_8 ? CLAIM_8 : CLAIM_12,
             claimed = claim_frame_areas(g, obj, layout));
    return claimed;
}

/* Whether an object's records, and so its frame's data, lie inside the tables
 * for a layout. */
static bool records_in_the_tables(const GameState *g, FieldObject obj, SpriteLayout layout);
bool sprites_records_in_the_tables(const GameState *g, FieldObject obj, SpriteLayout layout)
{
    return records_in_the_tables(g, obj, layout);
}

// $C0:E935
/* A map's start frees every area. */
void sprites_free_every_frame_area(GameState *g)
{
    for (unsigned k = 0; k < FIELD_FRAME_STAGING_CLAIMS_COUNT; k++)
        set_area_owner(g, k, field_no_link());
}

// $C0:EA42
/* An object leaving view lets go of the areas it holds, whatever its
 * layout: the run of them from the first it holds. */
void sprites_release_frame_areas(GameState *g, FieldObject obj)
{
    unsigned k = 0;
    while (k < FIELD_FRAME_AREAS && !field_same_link(area_owner(g, k), field_link_to(obj)))
        k++;
    for (; k < FIELD_FRAME_AREAS && field_same_link(area_owner(g, k), field_link_to(obj)); k++)
        set_area_owner(g, k, field_no_link());
}

// $C0:B192
/* As a map's objects start, each one's sprites are to be built afresh from
 * its next frame, and every frame area is free. (A map of no objects has
 * the original run its loop round the bytes, which no map makes.) */
void sprites_all_to_build_afresh(GameState *g)
{
    unsigned objects = event_object_count(g);
    for (FieldObject obj = 0; obj < objects; obj++)
        object_records_to_build(g, obj);
    sprites_free_every_frame_area(g);
}

// $C0:E9E2 (4), $C0:E9FF (8), $C0:EA1F (12)
static void free_frame_areas(GameState *g, FieldObject obj, SpriteLayout layout)
{
    unsigned count = areas_for(layout);
    for (unsigned k = 0; k + count <= FIELD_FRAME_AREAS; k++) {
        if (!field_same_link(area_owner(g, k), field_link_to(obj)))
            continue;
        for (unsigned a = 0; a < count; a++)
            set_area_owner(g, k + a, field_no_link());
        return;
    }
}

/* ---- What an object's records need ------------------------------------------------------- */

typedef enum { RECORDS_NOTHING, RECORDS_PLACE, RECORDS_REBUILD } RecordsWork;

/* What an object's records need: nothing to draw, only placing, or a
 * rebuild. The 4-sprite layout places when nothing changed, rebuilds
 * otherwise; the 8- and 12-sprite ones rebuild once at least 2 or 3 of their
 * sprites changed, and place with fewer (or where not built yet, draw
 * nothing); the 24-sprite one is always placed. */
static uint8_t rebuild_after(unsigned layout) { return layout == LAYOUT_4 ? 1 : layout == LAYOUT_8 ? 2 : 3; }

static RecordsWork records_work(const GameState *g, FieldObject obj)
{
    if (object_sprite_layout(g, obj) == LAYOUT_24)
        return RECORDS_PLACE;
    RecordsState st = object_records_state(g, obj);
    uint8_t at_least = rebuild_after(object_sprite_layout(g, obj));
    if (!st.built && st.changed == 0)
        return RECORDS_NOTHING;
    if (st.built)
        return st.changed < at_least ? RECORDS_PLACE : RECORDS_REBUILD;
    return st.changed < at_least ? RECORDS_NOTHING : RECORDS_REBUILD;
}

/* What bringing the records up to date came to. */
typedef enum { RECORDS_NONE_TO_DRAW, RECORDS_READY } RecordsOutcome;

/* The layouts' placing and rebuilding, which are entries of their own, each
 * timed as its routine: the 4-sprite layout's with the fifth sprite's stray Y
 * (place_sprites), the larger ones' with their own rules (place_larger).
 * False where the object isn't drawn this frame. */
enum {
    PLACE_4 = 0xC0B788,
    REBUILD_4 = 0xC0B8CA,
    PLACE_8 = 0xC0BA65,
    REBUILD_8 = 0xC0BCDC,
    PLACE_12 = 0xC0BFF2,
    REBUILD_12 = 0xC0C2BF,
};

static bool place_layout(GameState *g, FieldObject obj, SpriteLayout layout, bool reload)
{
    bool shows = true;
    switch (layout) {
    case LAYOUT_4:
        TIMED_AS(g, reload ? REBUILD_4 : PLACE_4, place_sprites(g, obj, reload));
        break;
    case LAYOUT_8:
        TIMED_AS(g, reload ? REBUILD_8 : PLACE_8, shows = place_larger(g, obj, layout, reload));
        break;
    case LAYOUT_12:
        TIMED_AS(g, reload ? REBUILD_12 : PLACE_12, shows = place_larger(g, obj, layout, reload));
        break;
    default:
        shows = place_larger(g, obj, layout, false);
        break;
    }
    return shows;
}

// $C0:B701
/* Bring an object's records up to date for drawing: place them, or rebuild
 * them from its new frame and free the frame's tile areas, marking them
 * built. The object is the current one. */
static RecordsOutcome bring_records_up_to_date(GameState *g, FieldObject obj)
{
    SpriteLayout layout = object_sprite_layout(g, obj);
    RecordsWork work = records_work(g, obj);
    if (work == RECORDS_NOTHING)
        return RECORDS_NONE_TO_DRAW;
    bool shows = place_layout(g, obj, layout, work == RECORDS_REBUILD);
    if (work == RECORDS_REBUILD) {
        free_frame_areas(g, obj, layout);
        object_mark_records_built(g, obj);
    }
    return shows ? RECORDS_READY : RECORDS_NONE_TO_DRAW;
}

enum { UPDATE_RECORDS = 0xC0B701, DRAW = 0xC0B309 };

static bool records_in_the_tables(const GameState *g, FieldObject obj, SpriteLayout layout);

/* An object's draw: its records brought up to date, and its sprites drawn
 * if there are any. */
static RecordsOutcome update_and_draw(GameState *g, FieldObject obj)
{
    RecordsOutcome r;
    TIMED_AS(g, UPDATE_RECORDS, r = bring_records_up_to_date(g, obj));
    if (r == RECORDS_READY)
        draw_object(g, obj);
    return r;
}

/* The table is built only where every object's draw is one this file makes:
 * each row's links name objects, and each object's records lie inside the
 * tables. So the rows are walked once first; where one isn't, nothing is
 * built and the caller reports it. The ranges start at their first entries
 * every frame, and no layout this file draws can run a range's cursors out
 * of the table's memory: 64 objects of 12 sprites from the back range's
 * start end at $14A0. Rows that name more draws than there are objects (a
 * link round in a circle) are refused too. */
enum { MOST_DRAWS = FIELD_OBJECTS };

static bool draw_is_own(const GameState *g, FieldLink link)
{
    FieldObject obj = field_linked_object(link);
    if (!field_names_object(field_link_byte(link)))
        return false;
    return records_in_the_tables(g, obj, object_sprite_layout(g, obj));
}

static bool every_draw_is_own(const GameState *g)
{
    unsigned draws = 0;
    for (int row = FIELD_ROWS - 1; row >= 0; row--)
        for (FieldLink link = row_first_object(g, (unsigned)row); field_link_is_object(link);
             link = object_next_in_row(g, field_linked_object(link)))
            if (++draws > MOST_DRAWS || !draw_is_own(g, link))
                return false;
    return true;
}

/* The ranges start over each frame at their first entries. */
static void start_ranges(GameState *g)
{
    for (unsigned r = 0; r < RANGES; r++) {
        set_range_high(g, r, range_first_high[r]);
        set_field_sprite_range_spare(g, r, 0);
        set_range_next(g, r, range_first_entry[r]);
    }
}

// $C0:B271
/* Build the frame's sprite table: every object in view, the rows from the
 * bottom of the screen up, then the ranges' leftovers hidden. Built where
 * every draw is one the port makes; false otherwise, with nothing built. */
bool field_sprite_table_built(GameState *g)
{
    if (!every_draw_is_own(g))
        return false;
    start_ranges(g);
    open_table_stream(g);
    object_recognise_current(g);   /* the building clears the byte after the link */
    for (int row = FIELD_ROWS - 1; row >= 0; row--)
        for (FieldLink link = row_first_object(g, (unsigned)row); field_link_is_object(link);) {
            FieldObject obj = field_linked_object(link);
            object_make_current(g, obj);
            TIMED_AS(g, DRAW, (void)update_and_draw(g, obj));
            link = object_next_in_row(g, obj);
        }
    hide_leftovers(g);
    return true;
}

/* ---- Records the port draws from -------------------------------------------------------- */

/* An object's records inside the records, and its frame's data inside the
 * frames': an offset past them has the original read and write on into what
 * follows (the objects' high bits and the rest of the bank), which the port
 * doesn't reproduce. The game puts every object's records inside them. */
bool sprites_records_fit(uint16_t records, SpriteLayout layout)
{
    return (uint32_t)records + layouts[layout].sprites * RECORD_BYTES <= FIELD_SPRITE_RECORDS_BYTES;
}

static bool records_in_the_tables(const GameState *g, FieldObject obj, SpriteLayout layout)
{
    return sprites_records_fit(object_sprite_records(g, obj), layout);
}
