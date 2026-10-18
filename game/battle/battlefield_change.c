/* Battle: changing the battlefield, and loading an effect's rows of the
 * battlefield's tiles.
 *
 * An action's animation can move the fight onto another battlefield as it
 * starts, and an effect can have its graphics drawn from the battlefield's
 * tiles. Both load graphics into VRAM over the frames that follow, and the
 * battle goes on meanwhile: each piece is decoded into a buffer here, then
 * handed to the frame work's transfer ($CD:3ED2), which queues it for the
 * vblank handler and waits, a frame at a time, until it has gone.
 *
 * A battlefield is a palette, two screen scripts of its own and, for most of
 * them, a BG3 layer of its own. Changing to one first takes the old layer
 * away: the effects that moved it stop, its colors fade out where the battle
 * has a layer to fade, and it leaves the screen. Then the new battlefield's
 * look and layout are taken from the ROM. A battlefield that keeps BG3 still
 * has nothing more to load; any other has its layer built: four sets of 8x8
 * tiles decoded and sent (the last one's last 16x16 tile blank), and a map of
 * 16x16 tiles, every place blank until the battlefield's strips of map fill
 * those they mark. Its colors and its screen scripts come last (the scripts
 * start in screen_script.c).
 *
 * The work is C on the game's state. What reaches the machine is the bridge
 * around the transfers and the waits, which are the original's code: each is
 * made where the original makes it, with what it keeps on the stack there,
 * since shadow mode pairs a call with the original's by its site and its
 * stack. */
#include "battle/animation_data.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame.h"
#include "battle/frame_data.h"
#include "battle/screen.h"
#include "charge.h"
#include "original_effects.h"
#include "platform/video.h"
#include "vars.h"

/* ---- Which battlefield --------------------------------------------------------------------- */

/* One battlefield stands for three, which one shown by the combatant the
 * animation scripts' channels 3 and 4 are animating: the fifth enemy on the
 * fourth channel, or on the third, or neither. */
enum { BATTLEFIELD_OF_THREE = 0x88, ON_THE_FOURTH = 0x88, ON_THE_THIRD = 0x89, ON_NEITHER = 0x8A };
enum { THIRD_CHANNEL = 3, FOURTH_CHANNEL = 4, FIFTH_ENEMY = 7 };

static uint16_t battlefield_to_show(const GameState *g, uint16_t asked)
{
    if (asked != BATTLEFIELD_OF_THREE)
        return asked;
    if (battle_script_subjects(g, FOURTH_CHANNEL) == FIFTH_ENEMY)
        return ON_THE_FOURTH;
    if (battle_script_subjects(g, THIRD_CHANNEL) == FIFTH_ENEMY)
        return ON_THE_THIRD;
    return ON_NEITHER;
}

/* ---- Taking the old battlefield's layer away ------------------------------------------------ */

/* Whatever the old battlefield's effects left moving stops: the orbit's
 * growth, the count toward stopping the palettes, the layers the line scroll
 * moves, the column band and the windows' step counts. */
enum { LINE_SCROLL_LAYERS = 3, WINDOW_STEP_COUNTS = 2 };

static void stop_the_old_effects(GameState *g)
{
    set_battle_screen_orbit_growth(g, 0);
    set_battle_screen_palette_stop_count(g, 0);
    for (unsigned layer = 0; layer < LINE_SCROLL_LAYERS; layer++)
        set_battle_line_scroll_layers(g, layer, 0);
    set_battle_column_band_half_height(g, 0);
    set_battle_column_band_left(g, 0);
    for (unsigned count = 0; count < WINDOW_STEP_COUNTS; count++)
        set_battle_window_steps(g, count, 0);
}

/* The layer's colors fade through palette animation slot 10's preset, out
 * as an old battlefield leaves and back in as the battlefield is put back
 * (battlefield_restore.c): background row 1's last eight colors go back to
 * how the battle started, and unless the battle's color math halves its
 * colors, the fade for the battlefield's kind (the HP gradient's own where it
 * has one) is set up and asked for. The original keeps the halving and the
 * fade's kind in its cells $55 and $53. */
enum { KEPT_COLORS = 8, FIRST_KEPT_COLOR = 24 };
enum { HALVED_MATH = 0x40, GRADIENT = 0x01 };
enum { FADE_SLOT = 2, GRADIENT_FADES = 0x20, FADE_BACK_IN = 8, FADE_SLOT_REQUESTED = 0x80 };
enum { LEFT_FADE_KIND = 0x53, LEFT_HALVED = 0x55 };

// $CD:0E23
void battle_restore_kept_colors(GameState *g)
{
    for (unsigned i = 0; i < KEPT_COLORS; i++) {
        set_palette_base(g, FIRST_KEPT_COLOR + i, battle_kept_row_colors(g, i));
        set_palette_working(g, FIRST_KEPT_COLOR + i, battle_kept_row_colors(g, i));
    }
}

static void set_up_the_fade(GameState *g, uint8_t kind, BattlefieldFade f)
{
    set_battle_palette_slot_presets_first_color(g, FADE_SLOT, f.first_color);
    set_battle_palette_slot_presets_color_count(g, FADE_SLOT, f.color_count);
    set_battle_palette_slot_presets_step(g, FADE_SLOT, f.step);
    set_battle_palette_slot_presets_delay(g, FADE_SLOT, f.delay);
    set_battle_palette_slot_presets_period(g, FADE_SLOT, f.period);
    set_battle_palette_slot_presets_fade_mode(g, FADE_SLOT, f.fade_mode);
    set_battle_palette_slot_presets_fade_level(g, FADE_SLOT, f.fade_level);
    set_battle_palette_slot_presets_fade_target(g, FADE_SLOT, f.fade_target);
    set_battle_palette_slot_presets_kind(g, FADE_SLOT, kind);
    set_battle_palette_preset_request(g, battle_palette_preset_request(g) | FADE_SLOT_REQUESTED);
}

// $CD:0E35, with $CD:0E79 (out) and $CD:0E61 (back in)
void battle_fade_battlefield_layer(GameState *g, bool back_in)
{
    battle_restore_kept_colors(g);
    uint8_t halved = (battle_start_screens_math_layers(g) & HALVED_MATH) ? 1 : 0;
    uint8_t kind = battlefield_fade_kind(g, halved);
    screen_leave_cell(g, LEFT_HALVED, halved);
    screen_leave_cell(g, LEFT_FADE_KIND, kind);
    if (halved)
        return;
    uint8_t fade = (battle_background_flags(g) & GRADIENT) ? GRADIENT_FADES : 0;
    set_up_the_fade(g, kind, battlefield_fade(g, (uint8_t)(fade + (back_in ? FADE_BACK_IN : 0))));
}

static void fade_the_old_layer_out(GameState *g) { battle_fade_battlefield_layer(g, false); }

/* The screen's settings below the split: the layers of the main screen, and
 * which of the main and sub screens' layers the windows mask. */
enum { MAIN_SCREEN, SUB_SCREEN, MAIN_WINDOWED, SUB_WINDOWED };
enum { BG1 = 0x01, BG2 = 0x02, BG3 = 0x04, OBJ = 0x10 };
enum { FIXED_COLOR_VALUE = 5, COLOR_MATH_SETTINGS = 2 };

/* How long the old layer takes to fade: the gradient's fade, and the others'. */
enum { GRADIENT_FADE_FRAMES = 31, LAYER_FADE_FRAMES = 15 };

/* ---- The battlefield's look and layout ------------------------------------------------------ */

/* The look the battlefield brings (its screen scripts and background colors)
 * and its layer's layout; the last block of tiles the frame work sent is
 * forgotten, and the bands of light start from their dimmest. The original
 * leaves the layout's last word with channel 0's record (cleared once the
 * battlefield's scripts start) and at $CA1B, and the tile set in its cells
 * $53-$54. */
enum { NO_BLOCK = 0xFF, NO_SOURCE = 0xFFFF, BANDS_DIMMEST = 0x1F };
enum { TILE_SET_HIGH_BIT = 0x40, LEFT_TILE_SET = 0x53 };

static uint16_t layout_word(BattlefieldLayout l) { return (uint16_t)(l.kept | l.rows << 8); }

static uint16_t tile_set_of(BattlefieldLayout l)
{
    return (uint16_t)(l.tile_set | ((l.strips & TILE_SET_HIGH_BIT) ? 0x100 : 0));
}

// $CD:11B8 (and $CD:0F42-$CD:0F86)
static uint16_t take_the_look_and_layout(GameState *g, uint16_t battlefield)
{
    BattlefieldLook look = battlefield_look(g, battlefield);
    for (unsigned channel = 0; channel < SCREEN_CHANNELS; channel++)
        set_battle_battlefield_scripts(g, channel, look.scripts[channel]);
    for (unsigned which = 0; which < 2; which++)
        set_battle_screen_background_colors(g, which, look.color_sets[which]);
    set_battle_block_uploaded_row(g, NO_BLOCK);
    set_battle_block_uploaded_source(g, NO_SOURCE);
    ORIGINAL_EFFECT(set_battle_block_uploaded_pad(g, NO_BLOCK));
    BattlefieldLayout l = battlefield_layout(g, battlefield);
    set_battle_battlefield_layout_strips(g, l.strips);
    set_battle_battlefield_layout_tile_set(g, l.tile_set);
    set_battle_battlefield_layout_map(g, l.first_strip);
    set_battle_battlefield_layout_kept(g, l.kept);
    set_battle_battlefield_layout_rows(g, l.rows);
    ORIGINAL_EFFECT(set_battle_screen_channels_layout_word(g, 0, layout_word(l)));
    ORIGINAL_EFFECT(set_battle_battlefield_kept_word(g, layout_word(l)));
    screen_leave_word(g, LEFT_TILE_SET, tile_set_of(l));
    set_battle_band_fade(g, BANDS_DIMMEST);
    return tile_set_of(l);
}

/* The new layer goes on BG3, its map at VRAM $7C00, its tiles 16x16 in
 * mode 1 with BG3 in front, and the windows masking BG1-BG3 and the sprites
 * on both screens. */
enum { BG3_LAYER = 2, BG3_MAP_AT_7C00 = 0x7C, MODE_1_BG3_FRONT_16x16 = 0x49, WINDOWED_LAYERS = BG1 | BG2 | BG3 | OBJ };

static void set_up_the_new_layer(GameState *g)
{
    set_battle_layer_map_settings(g, BG3_LAYER, BG3_MAP_AT_7C00);
    set_battle_screen_mode(g, MODE_1_BG3_FRONT_16x16);
    set_battle_screen_settings(g, MAIN_WINDOWED, WINDOWED_LAYERS);
    set_battle_screen_settings(g, SUB_WINDOWED, WINDOWED_LAYERS);
}

/* ---- The layer's tiles ------------------------------------------------------------------------ */

/* A tile set is 64 8x8 tiles of two bit planes, decoded into the decode
 * buffer from its descriptors: each names the tile's graphics in 8-byte units,
 * and whether it is mirrored or flipped. */
enum { SET_TILES = 64, TILE_BYTES = 16, TILE_ROWS = 8, SET_BYTES = SET_TILES * TILE_BYTES };
enum { DESCRIPTORS_AT = 0x8000, SET_DESCRIPTOR_BYTES = 64, DESCRIPTOR_BYTES = 2, GRAPHICS_UNIT = 8 };
enum { TILE_GRAPHICS = 0x1FFF, MIRRORED = 0x4000, FLIPPED = 0x8000 };

/* What the original leaves of a tile set: the descriptors' pointer past the
 * set, in bank $CF, with no tiles left to count; the last row a flip moved
 * (in its cell $69) and the mirror's count of rows run out (in $61). */
enum { LEFT_DESCRIPTORS = 0x53, LEFT_DESCRIPTOR_BANK = 0x55, LEFT_TILES_LEFT = 0x67, LEFT_FLIPPED_ROW = 0x69,
       LEFT_MIRROR_ROWS = 0x61, DESCRIPTOR_BANK = 0xCF };

static uint16_t tile_row(const GameState *g, uint16_t at)
{
    return (uint16_t)(battle_decoded_tiles(g, at) | battle_decoded_tiles(g, at + 1u) << 8);
}
static void set_tile_row(GameState *g, uint16_t at, uint16_t row)
{
    set_battle_decoded_tiles(g, at, (uint8_t)row);
    set_battle_decoded_tiles(g, at + 1u, (uint8_t)(row >> 8));
}

/* Flip the tile top to bottom; the row the original swapped last is its
 * fourth, which it leaves in its cell. */
static uint16_t flip_tile(GameState *g, uint16_t at)
{
    uint16_t kept = 0;
    for (unsigned row = 0; row < TILE_ROWS / 2; row++) {
        uint16_t top = (uint16_t)(at + row * 2), bottom = (uint16_t)(at + (TILE_ROWS - 1 - row) * 2);
        kept = tile_row(g, top);
        set_tile_row(g, top, tile_row(g, bottom));
        set_tile_row(g, bottom, kept);
    }
    return kept;
}

static void mirror_tile(GameState *g, uint16_t at)
{
    for (unsigned b = 0; b < TILE_BYTES; b++)
        set_battle_decoded_tiles(g, at + b, battle_mirrored_byte(g, battle_decoded_tiles(g, at + b)));
}

// $CD:11E4 (and $CD:12B8, $CD:1275, $CD:1229, $CD:1221)
static void decode_tile_set(GameState *g, uint16_t set)
{
    uint16_t descriptors = (uint16_t)(DESCRIPTORS_AT + set * SET_DESCRIPTOR_BYTES);
    bool flipped_any = false, mirrored_any = false;
    uint16_t flipped_row = 0;
    for (unsigned tile = 0; tile < SET_TILES; tile++) {
        uint16_t descriptor = battlefield_tile_descriptor(g, (uint16_t)(descriptors + tile * DESCRIPTOR_BYTES));
        uint32_t graphics = (uint32_t)(descriptor & TILE_GRAPHICS) * GRAPHICS_UNIT;
        uint16_t at = (uint16_t)(tile * TILE_BYTES);
        for (unsigned b = 0; b < TILE_BYTES; b++)
            set_battle_decoded_tiles(g, at + b, battlefield_tile_graphics_byte(g, graphics + b));
        if (descriptor & FLIPPED) {
            flipped_row = flip_tile(g, at);
            flipped_any = true;
        }
        if (descriptor & MIRRORED) {
            mirror_tile(g, at);
            mirrored_any = true;
        }
    }
    screen_leave_word(g, LEFT_DESCRIPTORS, (uint16_t)(descriptors + SET_TILES * DESCRIPTOR_BYTES));
    screen_leave_cell(g, LEFT_DESCRIPTOR_BANK, DESCRIPTOR_BANK);
    ORIGINAL_EFFECT(screen_leave_word(g, LEFT_TILES_LEFT, 0));
    if (flipped_any)
        ORIGINAL_EFFECT(screen_leave_word(g, LEFT_FLIPPED_ROW, flipped_row));
    if (mirrored_any)
        screen_leave_cell(g, LEFT_MIRROR_ROWS, 0);
}

/* The map's empty places show the layer's last 16x16 tile ($EE), which the
 * last set's tiles 46, 47, 62 and 63 make: they are left blank. */
enum { LAYER_SETS = 4, LAST_SET = LAYER_SETS - 1, BLANK_TILE_TOP = 46 * TILE_BYTES, BLANK_TILE_BOTTOM = 62 * TILE_BYTES,
       BLANK_TILE_HALF_BYTES = 2 * TILE_BYTES };

static void blank_the_empty_tile(GameState *g)
{
    for (unsigned b = 0; b < BLANK_TILE_HALF_BYTES; b++) {
        set_battle_decoded_tiles(g, BLANK_TILE_TOP + b, 0);
        set_battle_decoded_tiles(g, BLANK_TILE_BOTTOM + b, 0);
    }
}

/* ---- The layer's map -------------------------------------------------------------------------- */

/* Every place of the map buffer shows the blank tile, in the layer's first
 * palette and behind; the clear runs to the end of the bank, over the waves'
 * lines too. */
enum { BLANK_ENTRY = 0x00EE };

/* The buffers are in bank $7E; the map's is at $D000. */
enum { WORK_RAM_BANK = 0x7E, MAP_BUFFER = 0xD000 };

/* The original clears it in three stretches; the last one's end is left in
 * its cells $53-$54. */
enum { LEFT_CLEARED_TO = 0x53, LAST_STRETCH_END = 0x2800 };

// $CD:1052 (and $CD:1061)
static void clear_the_map(GameState *g)
{
    for (unsigned entry = 0; entry < BATTLE_MAP_BUFFER_COUNT; entry++)
        set_battle_map_buffer(g, entry, BLANK_ENTRY);
    screen_leave_word(g, LEFT_CLEARED_TO, LAST_STRETCH_END);
}

/* The map is built from strips of 16 places a row, each strip 256 places on
 * from the last. A strip starts with how its bytes step (by its first byte,
 * or, from $80, one at a time through a cycle of 32), then two bytes a row
 * saying which places have a tile; then the tiles, a byte each: a 16x16 tile
 * of the set's sheet (its column in the low three bits, its row in the next
 * three) and its flips in the top two. A tile goes in the layer's palette 6,
 * in front, or palette 7 where the sheet says. */
enum { STRIP_PLACES = 0x100, ROW_PLACES = 16, STRIP_STEP_CYCLES = 0x80, CYCLE_BYTES = 32, CELLS_BYTES = 32 };
enum { SHEET_COLUMN = 0x07, SHEET_ROW = 0x38, FLIPS = 0xC0, IN_FRONT_PALETTE_6 = 0x38, PALETTE_7 = 0x04 };
enum { STRIPS = 0x3F };

/* A tile's first 8x8 tile in the sheet, 16 of them a row: two columns a
 * 16x16 tile, two rows of them a row of the sheet. */
static uint8_t sheet_tile(uint8_t tile) { return (uint8_t)((tile & SHEET_COLUMN) * 2 + (tile & SHEET_ROW) * 4); }

static uint8_t tile_attributes(const GameState *g, uint8_t tile, uint8_t tile_set)
{
    uint8_t first = sheet_tile(tile);
    uint8_t mask = battlefield_palette_bit_mask(g, (uint8_t)(first >> 1 & SHEET_COLUMN));
    bool palette_7 = (battlefield_tile_palettes(g, (uint8_t)((first >> 4) + tile_set)) & mask) != 0;
    return (uint8_t)((tile & FLIPS) | IN_FRONT_PALETTE_6 | (palette_7 ? PALETTE_7 : 0));
}

/* Where the builder is in a strip: the next tile byte it reads (in bank $D1),
 * and the bits of the strip's rows still to come, eight at a time. What it
 * last built is kept for the original's cells. */
typedef struct {
    uint16_t at;
    uint8_t cells, cells_left, cells_read;
    uint8_t last_tile, last_place, last_palette;
    bool built_any;
} MapBuilder;

static void take_the_strip(GameState *g, MapBuilder *b, uint16_t strip, uint8_t rows)
{
    uint16_t place = battlefield_strip_place(g, strip);
    set_battle_map_strip_step(g, battlefield_strip_byte(g, BATTLEFIELD_STRIP_BANK | place));
    for (unsigned i = 0; i < CELLS_BYTES; i++)
        set_battle_map_strip_cells(g, i, battlefield_strip_byte(g, BATTLEFIELD_STRIP_BANK + place + 1u + i));
    b->at = (uint16_t)(place + rows * 2u + 1u);
    b->cells_left = 1;
    b->cells_read = 0;
}

static bool next_place_has_a_tile(GameState *g, MapBuilder *b)
{
    if (--b->cells_left == 0) {
        b->cells_left = 8;
        b->cells = battle_map_strip_cells(g, b->cells_read++);
    }
    bool has = (b->cells & 0x80) != 0;
    b->cells = (uint8_t)(b->cells << 1);
    return has;
}

/* The tile byte for the place, and the strip's bytes stepped on. */
static uint8_t next_tile(GameState *g, MapBuilder *b)
{
    uint8_t tile = battlefield_strip_byte(g, BATTLEFIELD_STRIP_BANK | b->at);
    uint16_t step = battle_map_strip_step(g);
    if (!(step & STRIP_STEP_CYCLES)) {
        b->at = (uint16_t)(b->at + step);
        return tile;
    }
    uint8_t cycled = (uint8_t)(battle_map_cycle(g) + 1);
    set_battle_map_cycle(g, cycled);
    b->at = (uint16_t)(b->at + 1);
    if (!(cycled % CYCLE_BYTES))
        b->at = (uint16_t)(b->at - CYCLE_BYTES);
    return tile;
}

/* The original builds the map in its cells $53-$75, and leaves them as the
 * last place left them: its pointers into the strip and the buffer, its
 * counts, the last tile and its place, attributes and palette. Those up to
 * $61 are where an animation script's screen effect takes the settings its
 * command doesn't give (battle_screen_effect_settings), so what this and the
 * rest of the loading leave there is the game's, and kept plainly; the rest
 * is scaffolding. */
enum { LEFT_STRIP_AT = 0x53, LEFT_STRIP_BANK = 0x55, LEFT_BUFFER_AT = 0x57, LEFT_BUFFER_BANK = 0x59,
       LEFT_CELLS_LEFT = 0x5F, LEFT_CELLS_READ = 0x61, LEFT_CELLS = 0x63, LEFT_COLUMN_AND_ROW = 0x65,
       LEFT_TILE = 0x67, LEFT_PLACE = 0x69, LEFT_SHEET_COLUMN = 0x6B, LEFT_ROWS_LEFT = 0x6D, LEFT_FLIPS = 0x6F,
       LEFT_ATTRIBUTES = 0x71, LEFT_TILE_SET_AT = 0x73, LEFT_PALETTE = 0x75 };

static void leave_the_builder_counts(GameState *g, const MapBuilder *b, uint16_t tile_set, uint8_t rows)
{
    screen_leave_cell(g, LEFT_CELLS, b->cells);
    screen_leave_word(g, LEFT_COLUMN_AND_ROW, (uint16_t)(rows << 8));
    if (b->built_any) {
        screen_leave_cell(g, LEFT_TILE, b->last_tile);
        screen_leave_word(g, LEFT_PLACE, b->last_place);
        screen_leave_cell(g, LEFT_SHEET_COLUMN, (uint8_t)((b->last_tile & SHEET_COLUMN) * 2));
        screen_leave_cell(g, LEFT_PALETTE, b->last_palette);
    }
    screen_leave_cell(g, LEFT_ROWS_LEFT, 0);
    screen_leave_word(g, LEFT_FLIPS, 0);
    screen_leave_cell(g, LEFT_ATTRIBUTES, IN_FRONT_PALETTE_6);
    screen_leave_word(g, LEFT_TILE_SET_AT, tile_set);
}

static void leave_the_builder(GameState *g, const MapBuilder *b, uint16_t tile_set, uint8_t strips, uint8_t rows)
{
    screen_leave_word(g, LEFT_STRIP_AT, b->at);
    screen_leave_cell(g, LEFT_STRIP_BANK, (uint8_t)(BATTLEFIELD_STRIP_BANK >> 16));
    screen_leave_word(g, LEFT_BUFFER_AT, (uint16_t)(MAP_BUFFER + strips * STRIP_PLACES * 2));
    screen_leave_cell(g, LEFT_BUFFER_BANK, WORK_RAM_BANK);
    screen_leave_cell(g, LEFT_CELLS_LEFT, b->cells_left);
    screen_leave_word(g, LEFT_CELLS_READ, b->cells_read);
    ORIGINAL_EFFECT(leave_the_builder_counts(g, b, tile_set, rows));
}

/* Put a tile in its place of the map: the strip's 256 places, sixteen a row. */
static void place_tile(GameState *g, MapBuilder *b, uint8_t strip, uint8_t row, uint8_t column, uint8_t tile_set)
{
    uint8_t place = battlefield_column_place(g, column);
    uint8_t tile = next_tile(g, b);
    uint8_t attributes = tile_attributes(g, tile, tile_set);
    unsigned entry = strip * STRIP_PLACES + (unsigned)((row * ROW_PLACES * 2 + place) / 2);
    set_battle_map_buffer(g, entry, (uint16_t)(sheet_tile(tile) | attributes << 8));
    b->last_tile = tile;
    b->last_place = place;
    b->last_palette = attributes & PALETTE_7;
    b->built_any = true;
}

// $CD:107D
/* Build the layer's map from its strips, into the map buffer cleared to the
 * blank tile. The layout is used up as it goes: its strips counted down to
 * none, its first strip stepped on past the last built. */
static void build_the_map(GameState *g, uint16_t tile_set)
{
    uint8_t strips = battle_battlefield_layout_strips(g) & STRIPS;
    uint8_t rows = battle_battlefield_layout_rows(g);
    uint16_t strip = battle_battlefield_layout_map(g);
    MapBuilder b = { 0 };
    set_battle_map_cycle(g, 0);
    uint8_t built = 0;
    do {
        take_the_strip(g, &b, strip, rows);
        uint8_t row = 0;
        do {
            for (uint8_t column = 0; column < ROW_PLACES; column++)
                if (next_place_has_a_tile(g, &b))
                    place_tile(g, &b, built, row, column, (uint8_t)tile_set);
        } while (++row != rows);
        strip++;
        built++;
    } while (built != strips);
    set_battle_battlefield_layout_strips(g, 0);
    set_battle_battlefield_layout_map(g, strip);
    leave_the_builder(g, &b, tile_set, built, rows);
}

/* ---- Loading the battlefield ------------------------------------------------------------------ */

/* A buffer of work RAM sent to VRAM from the main thread, which waits for it
 * to go (the battle's transfer, $CD:3ED2, which reads none of the flags). */
enum { DECODE_BUFFER = 0x2D00, MAP_VRAM = 0x7C00, MAP_BYTES = 0x800 };
enum { SEND_READS = DC_FLAGS };

static bool send(Snes *s, uint32_t site, uint16_t buffer, uint16_t vram, uint16_t bytes)
{
    uint16_t page = m_direct_page(s);
    uint8_t bank = s->cpu.db;
    vid_send_vram_and_wait(m_platform(s), PF_CALL_DC(site, 0xCD3ED2, SEND_READS), vram,
                           PF_GAME((uint32_t)WORK_RAM_BANK << 16 | buffer), bytes);
    return battle_back(s, site + BATTLE_JSR_SIZE, page, bank);
}

/* The frame work's waits for the next frame ($CD:3E75) and for a number of
 * them ($CD:3E3B). Each carries A's high byte on to the battle's pause, which
 * reads it. */
enum { WAIT_READS = DC_AL | DC_X | DC_Y | DC_FLAGS, WAIT_FRAMES_READS = DC_X | DC_Y | DC_FLAGS };

static bool wait_for_the_next_frame(Snes *s, uint32_t site) { return battle_wait_frame(s, site, 0xCD3E75, WAIT_READS); }

static bool wait_frames(Snes *s, uint32_t site, uint8_t frames)
{
    return battle_wait_frames(s, site, 0xCD3E3B, WAIT_FRAMES_READS, frames);
}

// $CD:0EF8-$CD:0F3F
/* Take the old layer away: fade its colors out where the battle shows it,
 * take it (with the gradient's BG2, where the battlefield has one) off the
 * main screen, and then over two frames the windows' masking and the color
 * math off. */
static bool take_the_old_layer_away(Snes *s)
{
    GameState *g = state_of(s);
    if (battle_background_flags(g) & GRADIENT) {
        TIMED_AS(g, 0xCD0E79, fade_the_old_layer_out(g));
        if (!wait_frames(s, 0xCD0F09, GRADIENT_FADE_FRAMES))
            return false;
        set_battle_screen_settings(g, MAIN_SCREEN, battle_screen_settings(g, MAIN_SCREEN) & (uint8_t)~BG2);
    } else {
        if ((battle_start_screens_main(g) | battle_start_screens_sub(g)) & BG3) {
            TIMED_AS(g, 0xCD0E79, fade_the_old_layer_out(g));
            if (!wait_frames(s, 0xCD0F25, LAYER_FADE_FRAMES))
                return false;
        }
        set_battle_screen_settings(g, MAIN_SCREEN, BG1 | BG2 | OBJ);
    }
    set_battle_screen_settings(g, MAIN_WINDOWED, 0);
    set_battle_screen_settings(g, SUB_WINDOWED, 0);
    if (!wait_for_the_next_frame(s, 0xCD0F33))
        return false;
    set_battle_split_line_values(g, FIXED_COLOR_VALUE, 0);
    for (unsigned i = 0; i < COLOR_MATH_SETTINGS; i++)
        set_battle_color_math_settings(g, i, 0);
    return wait_for_the_next_frame(s, 0xCD0F3F);
}

/* The original counts the sets in a cell as it goes ($CACE), and keeps the
 * tile set and the set it is decoding on the stack around the transfers. */
static bool load_the_tile_sets(Snes *s, uint16_t tile_set)
{
    GameState *g = state_of(s);
    m_push16(s, tile_set);
    for (uint8_t set = 0; set < LAYER_SETS; set++) {
        uint16_t of_set = (uint16_t)(tile_set + set * 2);
        ORIGINAL_EFFECT(set_battle_tile_rows_next(g, set));
        m_push16(s, of_set);
        TIMED_AS(g, 0xCD11E4, decode_tile_set(g, of_set));
        if (set == LAST_SET)
            blank_the_empty_tile(g);
        if (!send(s, 0xCD0FDA, DECODE_BUFFER, battlefield_tile_set_vram(g, set), SET_BYTES))
            return false;
        set_battle_battlefield_tiles_replaced(g, (uint8_t)(battle_battlefield_tiles_replaced(g) + 1));
        m_pull16(s);
    }
    ORIGINAL_EFFECT(set_battle_tile_rows_next(g, LAYER_SETS));
    return true;
}

// $CD:0EBD
static bool load_the_battlefield(Snes *s, uint16_t asked)
{
    GameState *g = state_of(s);
    uint16_t battlefield = battlefield_to_show(g, asked);
    set_battle_background(g, battlefield);
    stop_the_old_effects(g);
    m_push16(s, battlefield);
    s->cpu.a = 0; /* what the waits are made with where no fade's wait came first */
    if (!battle_bg3_stays_still(g) && !take_the_old_layer_away(s))
        return false;
    m_pull16(s);
    uint16_t tile_set = take_the_look_and_layout(g, battlefield);
    if (battle_bg3_stays_still(g))
        return true;
    set_up_the_new_layer(g);
    if (!load_the_tile_sets(s, tile_set))
        return false;
    TIMED_AS(g, 0xCD1052, clear_the_map(g));
    if (!send(s, 0xCD0FFD, MAP_BUFFER, MAP_VRAM, MAP_BYTES))
        return false;
    m_pull16(s);
    TIMED_AS(g, 0xCD107D, build_the_map(g, tile_set));
    battle_show_background_sets(g);
    return true;
}

/* ---- Changing the battlefield ---------------------------------------------------------------- */

// $CD:0D28 (through $CD:0018)
/* Change the battlefield: its screen scripts stop, it is loaded, and its own
 * scripts start. The battlefield comes in A, a word. */
static bool change_the_battlefield(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_screen_scripts_running(g, 0);
    s->cpu.x = s->cpu.a;
    if (!battle_run(s, 0xCD0D2C, 0xCD0EBD, battle_load_battlefield))
        return false;
    TIMED_AS(g, 0xCD0D33, battle_start_battlefield_scripts(g));
    return true;
}

/* ---- An effect's rows of the battlefield's tiles ---------------------------------------------- */

/* An effect's first graphics set is four row sets of the battlefield's tiles
 * into VRAM's slots 0-3, its second two into slots 4-5: from the next frame,
 * each row set decoded and sent, the transfer waiting for the one before it.
 * The original counts the row sets and slots in cells ($CACE-$CAD0). */
enum { FIRST_SET_SLOTS = 4, SECOND_SET_FIRST_SLOT = 4, SECOND_SET_SLOTS = 2, ROW_TILES = 32, ROW_BYTES = 0x400 };

// $CD:1323 (through $CD:0015), $CD:1314 (through $CD:002A)
static bool load_effect_tile_rows(Snes *s, uint8_t first_row_set, uint8_t first_slot, uint8_t slots)
{
    GameState *g = state_of(s);
    uint8_t end = (uint8_t)(first_slot + slots);
    ORIGINAL_EFFECT(set_battle_tile_rows_next(g, first_row_set));
    ORIGINAL_EFFECT(set_battle_tile_rows_slot(g, first_slot));
    ORIGINAL_EFFECT(set_battle_tile_rows_end(g, end));
    if (!wait_for_the_next_frame(s, 0xCD132E))
        return false;
    for (uint8_t slot = first_slot; slot != end; slot++) {
        uint8_t row_set = (uint8_t)(first_row_set + (slot - first_slot));
        ORIGINAL_EFFECT(set_battle_tile_rows_next(g, row_set));
        ORIGINAL_EFFECT(set_battle_tile_rows_slot(g, slot));
        TIMED_AS(g, 0xCD1368, battle_decode_tiles(g, row_set, ROW_TILES));
        if (!send(s, 0xCD1356, DECODE_BUFFER, effect_tile_rows_vram(g, slot), ROW_BYTES))
            return false;
    }
    ORIGINAL_EFFECT(set_battle_tile_rows_next(g, (uint8_t)(first_row_set + slots)));
    ORIGINAL_EFFECT(set_battle_tile_rows_slot(g, end));
    return true;
}

/* ---- The entries ------------------------------------------------------------------------------ */

/* The battlefield's change, the old layer's fade and the effect's rows leave
 * A's high byte clear for their callers. */
void battle_change_battlefield(Snes *s)
{
    if (!change_the_battlefield(s))
        return;
    s->cpu.a = 0;
    m_rtl(s);
}

void battle_fade_old_battlefield_layer(Snes *s)
{
    fade_the_old_layer_out(state_of(s));
    s->cpu.a = 0;
    m_rts(s);
}

/* The battlefield comes in X. */
void battle_load_battlefield(Snes *s)
{
    if (load_the_battlefield(s, s->cpu.x))
        m_rts(s);
}

/* The first row set comes in A. */
void battle_load_effect_tile_rows(Snes *s)
{
    if (!load_effect_tile_rows(s, (uint8_t)s->cpu.a, 0, FIRST_SET_SLOTS))
        return;
    s->cpu.a = 0;
    m_rtl(s);
}

void battle_load_second_effect_tile_rows(Snes *s)
{
    if (!load_effect_tile_rows(s, (uint8_t)s->cpu.a, SECOND_SET_FIRST_SLOT, SECOND_SET_SLOTS))
        return;
    s->cpu.a = 0;
    m_rtl(s);
}

/* The pieces, each as the original's routine: the tile set in X, which the
 * decoding of a set and the map's building take; each leaves A clear. */
void battle_decode_battlefield_tile_set(Snes *s)
{
    decode_tile_set(state_of(s), s->cpu.x);
    s->cpu.a = 0;
    m_rts(s);
}

void battle_clear_battlefield_map(Snes *s)
{
    clear_the_map(state_of(s));
    s->cpu.a = 0;
    m_rts(s);
}

void battle_build_battlefield_map(Snes *s)
{
    build_the_map(state_of(s), s->cpu.x);
    s->cpu.a = 0;
    m_rts(s);
}

/* ---- The guards ------------------------------------------------------------------------------- */

/* A layout whose map fits the buffer: as many strips as the game's
 * battlefields have, at least one, each of one to sixteen rows (the rows'
 * bits are 32 bytes). */
enum { MOST_STRIPS = BATTLE_MAP_BUFFER_COUNT / STRIP_PLACES, MOST_ROWS = CELLS_BYTES / 2 };

static bool layout_fits(uint8_t strips, uint8_t rows)
{
    strips &= STRIPS;
    return strips != 0 && strips <= MOST_STRIPS && rows != 0 && rows <= MOST_ROWS;
}

bool battle_battlefield_map_fits(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = layout_fits(battle_battlefield_layout_strips(g), battle_battlefield_layout_rows(g));
    vars_peek(false);
    return battle_on_page(s) && fits;
}

/* A battlefield that keeps BG3 still builds no map; any other's layout, as
 * the ROM has it, fits. */
static bool battlefield_fits(Snes *s, uint16_t asked)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    uint16_t battlefield = battlefield_to_show(g, asked);
    BattlefieldLayout l = battlefield_layout(g, battlefield);
    vars_peek(false);
    return battle_on_page(s) && (battle_keeps_bg3_still(battlefield) || layout_fits(l.strips, l.rows));
}

bool battle_battlefield_fits(Snes *s) { return battlefield_fits(s, s->cpu.x); }
bool battle_changed_battlefield_fits(Snes *s) { return battlefield_fits(s, s->cpu.a); }
