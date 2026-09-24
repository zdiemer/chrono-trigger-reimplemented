/* The sprite-object mode's command scripts: what each command does, and the
 * table of opcodes (menu/script.h says how the language is put together).
 *
 * A script runs one object (menu/object.h). Its commands fall into a few
 * families:
 *
 * - the object's own state: forget what it was doing, its look, where it is
 *   and how fast it moves, its sprite script;
 * - bytes: change a byte -- one of the object's own, by its offset, or a cell
 *   of low RAM, by its address -- test one and branch on it, copy one to
 *   another, count one down in a loop;
 * - the script's own way: jump, call and return, wait a frame, end;
 * - lasting a number of frames: moving and animating, animating in place,
 *   doing nothing, scrolling the map with the object's move; moving to a
 *   place; waiting until the object is old enough;
 * - other objects: start one with a handler (a fade, a mosaic, a palette
 *   loader, a tile streamer) or on a script, in the ordinary slots or the
 *   first four;
 * - the map: a tile of a plane, a rectangle of tiles copied, a tile group
 *   shown, a plane scrolled or redrawn; an upload;
 * - the game: the scene's objects' sprite scripts, a location to go to,
 *   marks in the screens' own tables, sounds and songs, a routine the script
 *   names. */
#include "menu/script.h"

#include "cpu_io.h"
#include "menu/menu_data.h"
#include "menu/sprites.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "vars.h"
#include "wram.h"

/* ---- The object's own state ------------------------------------------------------------------ */

// $C2:1037
/* Forget everything the object was doing but its script: where a call goes
 * back to, its sprite script, look, place, velocity and the rest. */
static ScriptFlow forget_everything(GameState *g, MenuObject self)
{
    enum { FROM_THE_SPRITE_SCRIPT_ON = 0x0B };
    set_menu_object_slots_return_to(g, self, 0);
    for (unsigned at = FROM_THE_SPRITE_SCRIPT_ON; at < MENU_OBJECT_SLOTS_BYTES_BYTES; at++)
        set_menu_object_slots_bytes_byte(g, self, at, 0);
    return next_command();
}
SCRIPT_OPERATION_OF_THE_OBJECT(forget_everything)

// $C2:1091, $C2:10A8
/* A part of how the object's sprites look (menu/object.h): their palette, or
 * their priority and whether they move with the map. The script's bits go
 * over the part unmasked, as the original puts them. */
static ScriptFlow set_look(GameState *g, MenuObject self, uint8_t part, uint8_t look)
{
    set_menu_object_slots_look(g, self, (uint8_t)((menu_object_slots_look(g, self) & ~part) | look));
    return next_command();
}
SCRIPT_OPERATION(set_look, part, look)

// $C2:15C3
/* Put the object on a pixel of the map. */
static ScriptFlow place(GameState *g, MenuObject self, uint16_t x, uint16_t y)
{
    menu_object_set_place(g, self, (MenuPlace){ (uint32_t)x * MENU_POSITION_UNITS, (uint32_t)y * MENU_POSITION_UNITS });
    return next_command();
}
SCRIPT_OPERATION(place, x, y)

// $C2:15E3, $C2:15F9
/* Its velocity across or down: whole pixels a frame, and a fraction of one. */
static ScriptFlow set_velocity(GameState *g, MenuObject self, MenuAxis axis, uint16_t pixels, uint16_t fraction)
{
    MenuPlace v = menu_object_velocity(g, self);
    uint32_t pixels_a_frame = (uint32_t)pixels * MENU_POSITION_UNITS + fraction;
    if (axis == MENU_ACROSS)
        v.x = pixels_a_frame;
    else
        v.y = pixels_a_frame;
    menu_object_set_velocity(g, self, v);
    return next_command();
}
SCRIPT_OPERATION(set_velocity, axis, pixels, fraction)

// $C2:160F
/* Give the object one of the game's sprite scripts, from its start. */
static ScriptFlow give_sprite_script(GameState *g, MenuObject self, uint8_t sprite_script)
{
    menu_give_sprite_script(g, self, sprite_script);
    return next_command();
}
SCRIPT_OPERATION(give_sprite_script, sprite_script)

// $C2:15DF
/* Two bytes that do nothing. */
static ScriptFlow nothing(GameState *g)
{
    (void)g;
    return next_command();
}
SCRIPT_OPERATION_TAKING_NOTHING(nothing)

/* ---- Bytes ---------------------------------------------------------------------------------- */

static uint8_t changed(ByteChange change, uint8_t was, uint8_t value)
{
    switch (change) {
    case BYTE_CLEAR: return 0;
    case BYTE_COUNT_UP: return (uint8_t)(was + 1);
    case BYTE_COUNT_DOWN: return (uint8_t)(was - 1);
    case BYTE_SET: return value;
    case BYTE_SET_BITS: return (uint8_t)(was | value);
    case BYTE_CLEAR_BITS: return (uint8_t)(was & ~value);
    case BYTE_ADD: return (uint8_t)(was + value);
    default: return (uint8_t)(was - value);
    }
}

/* The two changes that put a value there don't look at what was there. */
static bool reads_first(ByteChange change) { return change != BYTE_CLEAR && change != BYTE_SET; }

// $C2:120C-$C2:12E7, $C2:1ACA-$C2:1B15
/* Change a byte of the object's state or of the game's. */
static ScriptFlow change_byte(GameState *g, MenuObject self, ByteChange change, ByteRef byte, uint8_t value)
{
    uint8_t was = reads_first(change) ? script_byte_at(g, self, byte) : 0;
    script_set_byte_at(g, self, byte, changed(change, was, value));
    return next_command();
}
SCRIPT_OPERATION(change_byte, change, byte, value)

// $C2:1302, $C2:131C, $C2:1336, $C2:1350
/* Copy a byte of the object's state or the game's to another. */
static ScriptFlow copy_byte(GameState *g, MenuObject self, ByteRef from, ByteRef byte)
{
    script_set_byte_at(g, self, byte, script_byte_at(g, self, from));
    return next_command();
}
SCRIPT_OPERATION(copy_byte, from, byte)

static bool test_holds(ByteTest test, uint8_t found, uint8_t value)
{
    switch (test) {
    case TEST_ZERO: return found == 0;
    case TEST_NONZERO: return found != 0;
    case TEST_EQUAL: return found == value;
    case TEST_NOT_EQUAL: return found != value;
    case TEST_ANY_BIT: return (found & value) != 0;
    case TEST_NO_BIT: return (found & value) == 0;
    case TEST_BELOW: return found < value;
    default: return found >= value;
    }
}

// $C2:1398-$C2:1566
/* Branch where a byte passes a test. */
static ScriptFlow branch_if(GameState *g, MenuObject self, ByteTest test, ByteRef byte, uint8_t value, int8_t by)
{
    return branch_if_holds(test_holds(test, script_byte_at(g, self, byte), value), by);
}
SCRIPT_OPERATION(branch_if, test, byte, value, by)

// $C2:1375
/* A loop: count a byte of the object's down, and go back while it hasn't
 * reached zero. */
static ScriptFlow loop(GameState *g, MenuObject self, ByteRef byte, int8_t by)
{
    uint8_t left = (uint8_t)(script_byte_at(g, self, byte) - 1);
    script_set_byte_at(g, self, byte, left);
    return branch_if_holds(left != 0, by);
}
SCRIPT_OPERATION(loop, byte, by)

/* ---- The script's own way -------------------------------------------------------------------- */

// $C2:136A
/* Jump: the script carries on a byte past the address the command gives. */
static ScriptFlow jump(GameState *g, MenuObject self, uint16_t to)
{
    (void)g;
    (void)self;
    return go_to((uint16_t)(to + 1));
}
SCRIPT_OPERATION(jump, to)

// $C2:1848
/* Call: the object keeps where the script goes on after the call, and the
 * script carries on at the address. One call deep. */
static ScriptFlow call(GameState *g, MenuObject self, uint32_t at, uint16_t to)
{
    enum { CALL_LENGTH = 3 };
    set_menu_object_slots_return_to(g, self, (uint16_t)(at + CALL_LENGTH));
    return go_to(to);
}
SCRIPT_PLACED_OPERATION(call, to)

// $C2:185F
/* Back to where the script's call left off. */
static ScriptFlow return_from_call(GameState *g, MenuObject self) { return go_to(menu_object_slots_return_to(g, self)); }
SCRIPT_OPERATION_OF_THE_OBJECT(return_from_call)

// $C2:1169, $C2:1A2B
/* Hold the script on this command until the next frame, where it goes on. */
static ScriptFlow wait_for_next_frame(GameState *g)
{
    (void)g;
    return wait_a_frame();
}
SCRIPT_OPERATION_TAKING_NOTHING(wait_for_next_frame)

// $C2:1C81
/* The object is done: its slot is free for another. */
static ScriptFlow end(GameState *g)
{
    (void)g;
    return end_object();
}
SCRIPT_OPERATION_TAKING_NOTHING(end)

/* ---- Lasting a number of frames --------------------------------------------------------------- */

/* A command that lasts a number of frames counts them in the object: the
 * count starts the first frame and runs down after, and the command is over
 * the frame it reaches zero. A count of zero is started again every frame,
 * so it never runs out. True while it goes on. */
static bool frames_go_on(GameState *g, MenuObject self, uint8_t frames)
{
    uint8_t left = menu_object_slots_command_frames(g, self);
    if (left == 0) {
        set_menu_object_slots_command_frames(g, self, frames);
        return true;
    }
    set_menu_object_slots_command_frames(g, self, --left);
    return left != 0;
}

// $C2:161D, $C2:186B, $C2:188E
/* For a number of frames: move by the object's velocity (round the map) and
 * run its sprite script, run the sprite script in place, or only wait. */
static ScriptFlow for_frames(GameState *g, MenuObject self, uint8_t frames, bool moving, bool animating)
{
    if (!frames_go_on(g, self, frames))
        return next_command();
    if (moving) {
        menu_object_move(g, self);
        menu_object_wrap_onto_map(g, self);
    }
    if (animating)
        menu_object_animate(g);
    return wait_a_frame();
}
SCRIPT_OPERATION(for_frames, frames, moving, animating)

// $C2:18B4
/* Go on while the object is no older than the age the command gives; from
 * then on it stays on this command for good. */
static ScriptFlow until_older_than(GameState *g, MenuObject self, uint16_t age)
{
    return age >= menu_object_slots_age(g, self) ? next_command() : wait_a_frame();
}
SCRIPT_OPERATION(until_older_than, age)

static uint16_t pixel_on(const GameState *g, MenuObject self, MenuAxis axis)
{
    MenuPlace p = menu_object_place(g, self);
    return menu_pixel(axis == MENU_ACROSS ? p.x : p.y);
}

/* The first frame of a move to a pixel: where to stop, the velocity turned
 * toward it, and the sprite script for going forward or back. */
static void set_off(GameState *g, MenuObject self, MenuAxis axis, uint16_t target, uint8_t back_script,
                    uint8_t forward_script)
{
    set_menu_object_slots_command_state(g, self, (uint16_t)(menu_object_slots_command_state(g, self) + 1));
    set_menu_object_slots_move_target(g, self, target);
    bool forward = target >= pixel_on(g, self, axis);
    MenuPlace v = menu_object_velocity(g, self);
    bool going_back = (int32_t)(axis == MENU_ACROSS ? v.x : v.y) < 0;
    if (forward == going_back)
        menu_object_turn_round(g, self, axis);
    menu_give_sprite_script(g, self, forward ? forward_script : back_script);
}

// $C2:1959, $C2:19C2
/* Move across or down to a pixel of the map, a velocity's worth a frame; the
 * command is over once the object is on that pixel. */
static ScriptFlow move_to(GameState *g, MenuObject self, MenuAxis axis, uint16_t target, uint8_t back_script,
                          uint8_t forward_script)
{
    if ((uint8_t)menu_object_slots_command_state(g, self) == 0)
        set_off(g, self, axis, target, back_script, forward_script);
    if (pixel_on(g, self, axis) == menu_object_slots_move_target(g, self))
        return next_command();
    MenuPlace p = menu_object_place(g, self), v = menu_object_velocity(g, self);
    if (axis == MENU_ACROSS)
        p.x += v.x;
    else
        p.y += v.y;
    menu_object_set_place(g, self, p);
    menu_object_wrap_onto_map(g, self);
    menu_object_animate(g);
    return wait_a_frame();
}
SCRIPT_OPERATION(move_to, axis, target, back_script, forward_script)

/* The layer over the map moves with it, unless one of the three story flags
 * that hold it still is set (in the map screens' copy of the story's flags). */
enum { TOP_LAYER_STAYS_FIRST = 0x10, TOP_LAYER_STAYS_SECOND = 0x20, TOP_LAYER_STAYS_THIRD = 0x02 };

static bool top_layer_stays(const GameState *g)
{
    return (menu_story_flags_top_layer_first(g) & TOP_LAYER_STAYS_FIRST)
           || (menu_story_flags_top_layer_second(g) & TOP_LAYER_STAYS_SECOND)
           || (menu_story_flags_top_layer_third(g) & TOP_LAYER_STAYS_THIRD);
}

/* A scroll moves the map by the whole pixels the object has moved since the
 * map last followed it, so the object keeps only what the map hasn't: its
 * fraction of a pixel, below zero where it was. */
static uint32_t not_yet_followed(uint32_t at)
{
    return (int32_t)at < 0 && (uint16_t)at != 0 ? (0xFFFFu << 16 | (uint16_t)at) : (uint16_t)at;
}

/* The whole pixels of a move, where it is a pixel or more either way: its
 * pixel part, as a signed byte. */
static bool whole_pixels(uint32_t moved, int8_t *step)
{
    uint32_t size = (int32_t)moved < 0 ? 0u - moved : moved;
    if (menu_pixel(size) == 0)
        return false;
    *step = (int8_t)(uint8_t)menu_pixel(moved);
    return true;
}

static void move_top_layer(GameState *g, MenuAxis axis, int8_t step)
{
    if (axis == MENU_ACROSS)
        set_menu_object_shadows_bg3_scroll_x(g, (uint16_t)(menu_object_shadows_bg3_scroll_x(g) + step));
    else
        set_menu_object_shadows_bg3_scroll_y(g, (uint16_t)(menu_object_shadows_bg3_scroll_y(g) + step));
}

/* Follow a move along one axis: the first two planes and the layer over them,
 * or the one plane. */
static bool follow(GameState *g, const ScriptServices *map, MenuAxis axis, uint8_t plane, int8_t step)
{
    if (plane != BOTH_PLANES)
        return map->scroll_map(map->ctx, axis, plane, step);
    if (!map->scroll_map(map->ctx, axis, 1, step) || !map->scroll_map(map->ctx, axis, 2, step))
        return false;
    if (!top_layer_stays(g))
        move_top_layer(g, axis, step);
    return true;
}

// $C2:164B, $C2:16FA
/* Scroll the map with the object for a number of frames: each frame the
 * object moves by its velocity from where the map last followed it, and the
 * map scrolls by the whole pixels it has moved -- the first two planes and
 * the layer over them (unless the story holds that still), or the one plane
 * the command names. */
static ScriptFlow scroll_map(GameState *g, MenuObject self, const ScriptServices *map, uint8_t frames, uint8_t plane)
{
    if (menu_object_slots_command_frames(g, self) == 0) {
        set_menu_object_slots_command_frames(g, self, frames);
        menu_object_set_place(g, self, (MenuPlace){ 0, 0 });
    } else if (!frames_go_on(g, self, frames)) {
        return next_command();
    }
    MenuPlace p = menu_object_place(g, self);
    menu_object_set_place(g, self, (MenuPlace){ not_yet_followed(p.x), not_yet_followed(p.y) });
    menu_object_move(g, self);
    for (MenuAxis axis = MENU_ACROSS; axis <= MENU_DOWN; axis++) {
        MenuPlace moved = menu_object_place(g, self);
        int8_t step;
        if (whole_pixels(axis == MENU_ACROSS ? moved.x : moved.y, &step) && !follow(g, map, axis, plane, step))
            return handed_back();
    }
    return wait_a_frame();
}
SCRIPT_SERVED_OPERATION(scroll_map, frames, plane)

/* ---- Other objects -------------------------------------------------------------------------- */

/* The handlers of the objects scripts start by name: the screen's fades and
 * mosaic, an object that streams the tiles the command describes to VRAM
 * over a few frames, and one that loads palette rows. They are the
 * original's, run by the objects' loop (menu/objects.c). */
#define HANDLER_AT(handler) ((uint16_t)(handler))
enum {
    TILE_STREAMER = HANDLER_AT(0xC21CF5), PALETTE_LOADER = HANDLER_AT(0xC21DD4), FADE_OUT = HANDLER_AT(0xC220A2),
    FADE_IN = HANDLER_AT(0xC22105), MOSAIC_OFF = HANDLER_AT(0xC22194), MOSAIC_ON = HANDLER_AT(0xC221F8),
};

// $C2:10BF, $C2:158B-$C2:15B5, $C2:1839, $C2:1A43
/* Start an object with a handler. It takes on this object's state, the
 * script's place included, which is how a tile streamer, a fade and a mosaic
 * read their steps from the command that starts them. */
static ScriptFlow start_handler(GameState *g, MenuObject self, uint16_t handler, MenuSlotKind slots)
{
    (void)self;
    MenuObject started;
    (void)menu_start_object(g, handler, slots, &started);
    return next_command();
}
SCRIPT_OPERATION(start_handler, handler, slots)

// $C2:10CD
/* Start the palette loader on colours from anywhere in memory, for a row of
 * the palette, and say whether it hands over to the next loader when done.
 * The loader (the original's) reads what it loads from the state it inherits
 * from this object, so this object holds them first. */
static ScriptFlow load_palette_row(GameState *g, MenuObject self, uint32_t colours, uint8_t row, uint8_t hand_over)
{
    set_menu_object_slots_palette_source(g, self, colours);
    set_menu_object_slots_palette_row(g, self, row);
    set_menu_object_slots_palette_hand_over(g, self, hand_over);
    return start_handler(g, self, PALETTE_LOADER, MENU_ORDINARY_SLOT);
}
SCRIPT_OPERATION(load_palette_row, colours, row, hand_over)

// $C2:11F6, $C2:1A2D
/* Start another object on a script, in the ordinary slots or the first four. */
static ScriptFlow start_script(GameState *g, MenuObject self, uint32_t script, MenuSlotKind slots)
{
    (void)self;
    menu_start_script(g, script, slots);
    return next_command();
}
SCRIPT_OPERATION(start_script, script, slots)

/* ---- The map ---------------------------------------------------------------------------------- */

/* The map's planes are 96 map tiles across, a byte each; a script names them
 * from 1. */
enum { MAP_ROW_BYTES = 0x60, MAP_BANK = 0x7E };

static uint16_t map_tile(const GameState *g, uint8_t plane, uint8_t column, uint8_t row)
{
    return (uint16_t)(menu_plane_map(g, (uint8_t)(plane - 1)) + column + row * MAP_ROW_BYTES);
}

/* What reaching a tile leaves in the multiplier: the row, and its row's
 * offset. */
static void leave_multiplied(GameState *g, uint8_t row)
{
    hw_set_multiplicand(g, row);
    (void)hw_multiply_by(g, MAP_ROW_BYTES);
}

// $C2:116B
/* Put a map tile on a plane: the tile's byte at a column and row of the
 * plane's map. */
static ScriptFlow set_map_tile(GameState *g, MenuObject self, uint8_t plane, uint8_t column, uint8_t row,
                               uint8_t value)
{
    (void)self;
    uint16_t at = map_tile(g, plane, column, row);
    ORIGINAL_EFFECT(leave_multiplied(g, row));
    ORIGINAL_EFFECT(set_menu_object_scratch_pointer(g, (uint32_t)MAP_BANK << 16 | at));
    wram_write8(g, (uint32_t)MAP_BANK << 16 | at, value);
    return next_command();
}
SCRIPT_OPERATION(set_map_tile, plane, column, row, value)

/* The copy runs through the first plane's map or the second's. */
static uint8_t first_or_second(uint8_t plane) { return plane == 1 ? 1 : 2; }

/* What the copy leaves: the tiles across in the first cell and both counts
 * run out, the two places a row past the last, the multiplier on the last
 * place's row, and the work RAM port past the last row it wrote. */
static void leave_copied(GameState *g, uint16_t from, uint16_t to, uint8_t to_row, uint8_t columns, unsigned rows)
{
    unsigned across = columns ? columns : 256;
    uint32_t port = ((uint32_t)MAP_BANK << 16) + (uint16_t)(to + (rows - 1) * MAP_ROW_BYTES) + across;
    set_menu_object_scratch_columns(g, columns);
    set_menu_object_scratch_rows_left(g, 0);
    set_menu_object_scratch_columns_left(g, 0);
    set_menu_object_scratch_pointer(g, (uint32_t)MAP_BANK << 16 | (uint16_t)(from + rows * MAP_ROW_BYTES));
    set_menu_object_scratch_second_pointer(g, (uint32_t)MAP_BANK << 16 | (uint16_t)(to + rows * MAP_ROW_BYTES));
    leave_multiplied(g, to_row);
    hw_port_seek(g, (uint16_t)port, (uint8_t)(port >> 16));
}

// $C2:1B2F
/* Copy a rectangle of map tiles, `columns` across and `rows` down, from one
 * of the first two planes' maps to a place in either, a row at a time from
 * the top and each row from the left (so a copy onto itself runs on through
 * what it has just written). A count of zero is 256. */
static ScriptFlow copy_map_tiles(GameState *g, MenuObject self, uint8_t from_plane, uint8_t from_column,
                                 uint8_t from_row, uint8_t plane, uint8_t column, uint8_t row, uint8_t columns,
                                 uint8_t rows)
{
    (void)self;
    uint16_t from = map_tile(g, first_or_second(from_plane), from_column, from_row);
    uint16_t to = map_tile(g, first_or_second(plane), column, row);
    unsigned down = rows ? rows : 256, across = columns ? columns : 256;
    for (unsigned r = 0; r < down; r++) {
        uint32_t source = ((uint32_t)MAP_BANK << 16) + (uint16_t)(from + r * MAP_ROW_BYTES);
        uint32_t dest = ((uint32_t)MAP_BANK << 16) + (uint16_t)(to + r * MAP_ROW_BYTES);
        for (unsigned i = 0; i < across; i++)
            wram_write8(g, dest + i, wram_read8(g, source + i));
    }
    ORIGINAL_EFFECT(leave_copied(g, from, to, row, columns, down));
    return next_command();
}
SCRIPT_OPERATION(copy_map_tiles, from_plane, from_column, from_row, plane, column, row, columns, rows)

/* A tile group's four tiles go to the plane's tilemap two over two, the
 * tilemap being two screens of 32 tiles across; a map tile is two tiles
 * each way. */
enum { GROUP_BYTES = 8, GROUP_ROW_BYTES = 4, TILEMAP_COLUMNS = 0x20, TILEMAP_SCREEN_WORDS = 0x400 };

// $C2:1BE0
/* Show one of a plane's tile groups on a map tile of its tilemap (the first
 * two planes): its two rows of tiles, queued for VRAM. */
static ScriptFlow show_tile_group(GameState *g, MenuObject self, uint8_t plane, uint8_t column, uint8_t row,
                                  uint8_t group)
{
    (void)self;
    uint8_t p = (uint8_t)(first_or_second(plane) - 1);
    uint8_t across = (uint8_t)((column << 1) & (2 * TILEMAP_COLUMNS - 1));
    uint8_t in_its_screen = across & (TILEMAP_COLUMNS - 1);
    uint16_t source = (uint16_t)(menu_plane_groups(g, p) + group * GROUP_BYTES);
    uint16_t vram = (uint16_t)(menu_plane_tilemap(g, p) + (uint8_t)((row << 1) & (TILEMAP_COLUMNS - 1)) * TILEMAP_COLUMNS
                               + in_its_screen);
    if (across & TILEMAP_COLUMNS)
        vram = (uint16_t)(vram + TILEMAP_SCREEN_WORDS);
    ORIGINAL_EFFECT(set_menu_object_scratch_group_column(g, across));
    ORIGINAL_EFFECT(set_menu_object_scratch_group_screen_column(g, in_its_screen));
    ORIGINAL_EFFECT(set_menu_object_scratch_pointer_address(g, source));
    ORIGINAL_EFFECT(set_menu_object_scratch_second_pointer_address(g, vram));
    const MenuObjectUpload rows[2] = {
        { (uint32_t)MAP_BANK << 16 | source, vram, GROUP_ROW_BYTES, MENU_PORT_WORD_BY_WORD },
        { (uint32_t)MAP_BANK << 16 | (uint16_t)(source + GROUP_ROW_BYTES), (uint16_t)(vram + TILEMAP_COLUMNS),
          GROUP_ROW_BYTES, MENU_PORT_WORD_BY_WORD },
    };
    (void)menu_queue_object_uploads(g, rows, 2);
    return next_command();
}
SCRIPT_OPERATION(show_tile_group, plane, column, row, group)

// $C2:1947
/* Redraw one of the map's planes whole. */
static ScriptFlow redraw_map(GameState *g, MenuObject self, const ScriptServices *map, uint8_t plane)
{
    (void)g;
    (void)self;
    return map->redraw_map(map->ctx, plane) ? next_command() : handed_back();
}
SCRIPT_SERVED_OPERATION(redraw_map, plane)

// $C2:17D2
/* Queue an upload to VRAM, from anywhere, a word at a time. */
static ScriptFlow queue_upload(GameState *g, MenuObject self, uint32_t source, uint16_t vram, uint16_t size)
{
    (void)self;
    menu_queue_object_upload(g, source, vram, size);
    return next_command();
}
SCRIPT_OPERATION(queue_upload, source, vram, size)

/* ---- The game --------------------------------------------------------------------------------- */

// $C2:11B2
/* Give the object of a map screen's scene that plays a character its script:
 * the party member who is that character, or the extra object where the
 * command names 7. Nothing where no member is. */
static ScriptFlow set_scene_script(GameState *g, MenuObject self, uint16_t scene_script, uint8_t member)
{
    enum { EXTRA_OBJECT = 7, EXTRA_SLOT = 3, PARTY = 3 };
    (void)self;
    ORIGINAL_EFFECT(set_menu_object_scratch_scene_script(g, scene_script));
    if (member == EXTRA_OBJECT) {
        set_menu_member_scripts(g, EXTRA_SLOT, scene_script);
        return next_command();
    }
    for (unsigned slot = 0; slot < PARTY; slot++) {
        if (party_members(g, slot) == member) {
            set_menu_member_scripts(g, slot, scene_script);
            break;
        }
    }
    return next_command();
}
SCRIPT_OPERATION(set_scene_script, scene_script, member)

// $C2:1100
/* Leave the map screen for a location: where the party is on the world map
 * is kept to come back to (sixteen tiles in from where the second plane's
 * top-left is, round the world), and the menus' mode changes, which ends the
 * screen. The location's kind is the byte that is also its number's high
 * half. */
static ScriptFlow change_location(GameState *g, MenuObject self, uint16_t location, uint8_t region, uint8_t tile_x,
                                  uint8_t tile_y)
{
    enum { LEAVING = 2, WORLD_COLUMNS = 0xC0, WORLD_ROWS = 0x80, CENTRE = 0x10, LOCATION_BITS = 0x01FF, KINDS = 3 };
    (void)self;
    uint16_t id = location_id(g);
    uint16_t x = (uint16_t)(menu_map_column(g, 1) + CENTRE);
    if (x >= WORLD_COLUMNS)
        x = (uint16_t)(x - WORLD_COLUMNS);
    uint8_t y = (uint8_t)((menu_map_row(g, 1) + CENTRE) & (WORLD_ROWS - 1));
    set_location_previous_id(g, id);
    set_location_previous_x(g, (uint8_t)x);
    set_location_previous_y(g, y);
    set_menu_saved_location_id(g, id);
    set_menu_saved_location_x(g, (uint8_t)x);
    set_menu_saved_location_y(g, y);
    set_location_id(g, location & LOCATION_BITS);
    set_location_flags(g, (uint8_t)(region >> 1));
    set_location_x(g, tile_x);
    set_location_y(g, tile_y);
    set_location_previous_flags(g, menu_return_flags(g, (uint8_t)((region >> 1) & KINDS)));
    set_menu_mode(g, LEAVING);
    return next_command();
}
SCRIPT_OPERATION(change_location, location, region, tile_x, tile_y)

/* The screens' own tables of entries whose first byte's top bit a script
 * marks: seven bytes an entry in the first, three in the others. The table's
 * number is doubled in a byte to pick it, so its top bit is lost. */
static uint16_t table_entry(uint8_t table, uint8_t entry)
{
    static const uint16_t first[3] = { 0x7E00, 0x8300, 0x8180 };
    static const uint8_t bytes[3] = { 7, 3, 3 };
    unsigned which = (uint8_t)(table << 1) >> 1;
    return (uint16_t)(first[which] + entry * bytes[which]);
}

// $C2:1A52, $C2:1AAC
/* Mark an entry of one of the screen's tables, or clear its mark. */
static ScriptFlow mark_entry(GameState *g, MenuObject self, uint8_t table, uint8_t entry, bool mark)
{
    enum { MARK = 0x80 };
    (void)self;
    uint32_t at = (uint32_t)MAP_BANK << 16 | table_entry(table, entry);
    ORIGINAL_EFFECT(set_menu_object_scratch_entry(g, entry));
    ORIGINAL_EFFECT(set_menu_object_scratch_pointer(g, at));
    uint8_t was = wram_read8(g, at);
    wram_write8(g, at, mark ? (uint8_t)(was | MARK) : (uint8_t)(was & ~MARK));
    return next_command();
}
SCRIPT_OPERATION(mark_entry, table, entry, mark)

/* What the scripts ask of the sound driver (game/sound/driver.c): a song, a
 * sound effect on one of the two pairs of voices kept for effects
 * (SoundEffectVoices), or any command with its three parameters. A song
 * fills the rest of its request with $FF. */
enum { NO_PARAMETER = 0xFF };

static ScriptFlow pass_on(const ScriptServices *sound)
{
    return sound->pass_on_sound(sound->ctx) ? next_command() : handed_back();
}

// $C2:18C7, $C2:18D0
/* Play a sound effect, panned, on one of the pairs of voices. */
static ScriptFlow play_effect(GameState *g, MenuObject self, const ScriptServices *sound, SoundEffectVoices voices,
                              uint8_t effect, uint8_t pan)
{
    (void)self;
    set_menu_sound_request_command(g, (uint8_t)voices);
    set_menu_sound_request_first(g, effect);
    set_menu_sound_request_second(g, pan);
    set_menu_sound_request_age(g, 0);
    return pass_on(sound);
}
SCRIPT_SERVED_OPERATION(play_effect, voices, effect, pan)

// $C2:191F
/* Send the sound driver any command, with its three parameters. */
static ScriptFlow send_sound_command(GameState *g, MenuObject self, const ScriptServices *sound, uint8_t command,
                                    uint8_t first, uint8_t second, uint8_t third)
{
    (void)self;
    set_menu_sound_request_command(g, command);
    set_menu_sound_request_first(g, first);
    set_menu_sound_request_second(g, second);
    set_menu_sound_request_third(g, third);
    set_menu_sound_request_age(g, 0);
    return pass_on(sound);
}
SCRIPT_SERVED_OPERATION(send_sound_command, command, first, second, third)

// $C2:18F9, $C2:18F1
/* Start a song, and note it as the one playing; or start the screen's song
 * unless the screen keeps the one already playing. */
static ScriptFlow play_song(GameState *g, MenuObject self, const ScriptServices *sound, uint8_t song, bool unless_kept)
{
    (void)self;
    if (unless_kept && menu_song_kept(g) != 0)
        return next_command();
    set_menu_sound_request_command(g, SOUND_PLAY_SONG);
    set_menu_sound_request_first(g, song);
    set_menu_song_playing(g, song);
    set_menu_sound_request_second(g, NO_PARAMETER);
    set_menu_sound_request_third(g, NO_PARAMETER);
    set_menu_sound_request_age(g, 0);
    return pass_on(sound);
}
SCRIPT_SERVED_OPERATION(play_song, song, unless_kept)

// $C2:1807, $C2:181C
/* Run a routine the script names: in bank C2, or anywhere. */
static ScriptFlow call_routine(GameState *g, MenuObject self, const ScriptServices *code, uint32_t routine, bool far)
{
    (void)g;
    (void)self;
    return code->call_routine(code->ctx, routine, far) ? next_command() : handed_back();
}
SCRIPT_SERVED_OPERATION(call_routine, routine, far)

/* ---- The table ---------------------------------------------------------------------------------- */

#define ORDINARY CONSTANT(MENU_ORDINARY_SLOT)
#define FIRST CONSTANT(MENU_FIRST_SLOT)
#define ON CONSTANT(true)
#define OFF CONSTANT(false)

SCRIPT_OPCODES(menu_script_opcodes,
    { 0x00, OPERATION(forget_everything), .length = 1 },
    { 0x01, OPERATION(set_look), .part = CONSTANT(MENU_LOOK_PALETTE), .look = OPERAND(1), .length = 2 },
    { 0x02, OPERATION(set_look), .part = CONSTANT(MENU_LOOK_PRIORITY | MENU_LOOK_ON_THE_MAP), .look = OPERAND(1),
      .length = 2 },
    { 0x03, OPERATION(start_handler), .handler = CONSTANT(TILE_STREAMER), .slots = ORDINARY, .length = 10 },
    { 0x04, OPERATION(load_palette_row), .colours = OPERAND_LONG(1), .row = OPERAND(4), .hand_over = OPERAND(5),
      .length = 6 },
    { 0x05, OPERATION(change_location), .location = OPERAND_WORD(1), .region = OPERAND(2), .tile_x = OPERAND(3),
      .tile_y = OPERAND(4), .length = 5 },
    { 0x06, OPERATION(wait_for_next_frame), .length = 1 },
    { 0x07, OPERATION(set_map_tile), .plane = OPERAND(1), .column = OPERAND(2), .row = OPERAND(3),
      .value = OPERAND(4), .length = 5 },
    { 0x08, OPERATION(set_scene_script), .scene_script = OPERAND_WORD(1), .member = OPERAND(3), .length = 4 },
    { 0x09, OPERATION(start_script), .script = WORD_IN_BANK(1, 3), .slots = ORDINARY, .length = 4 },
    { 0x0A, OPERATION(change_byte), .change = CONSTANT(BYTE_CLEAR), .byte = SLOT_BYTE(1), .length = 2 },
    { 0x0B, OPERATION(change_byte), .change = CONSTANT(BYTE_COUNT_UP), .byte = SLOT_BYTE(1), .length = 2 },
    { 0x0C, OPERATION(change_byte), .change = CONSTANT(BYTE_COUNT_DOWN), .byte = SLOT_BYTE(1), .length = 2 },
    { 0x0D, OPERATION(change_byte), .change = CONSTANT(BYTE_SET), .byte = SLOT_BYTE(1), .value = OPERAND(2), .length = 3 },
    { 0x0E, OPERATION(change_byte), .change = CONSTANT(BYTE_SET_BITS), .byte = SLOT_BYTE(1), .value = OPERAND(2), .length = 3 },
    { 0x0F, OPERATION(change_byte), .change = CONSTANT(BYTE_CLEAR_BITS), .byte = SLOT_BYTE(1), .value = OPERAND(2), .length = 3 },
    { 0x10, OPERATION(change_byte), .change = CONSTANT(BYTE_CLEAR), .byte = CELL(1), .length = 3 },
    { 0x11, OPERATION(change_byte), .change = CONSTANT(BYTE_COUNT_UP), .byte = CELL(1), .length = 3 },
    { 0x12, OPERATION(change_byte), .change = CONSTANT(BYTE_COUNT_DOWN), .byte = CELL(1), .length = 3 },
    { 0x13, OPERATION(change_byte), .change = CONSTANT(BYTE_SET), .byte = CELL(1), .value = OPERAND(3), .length = 4 },
    { 0x14, OPERATION(change_byte), .change = CONSTANT(BYTE_SET_BITS), .byte = CELL(1), .value = OPERAND(3), .length = 4 },
    { 0x15, OPERATION(change_byte), .change = CONSTANT(BYTE_CLEAR_BITS), .byte = CELL(1), .value = OPERAND(3), .length = 4 },
    { 0x16, OPERATION(copy_byte), .from = SLOT_BYTE(1), .byte = CELL(2), .length = 4 },
    { 0x17, OPERATION(copy_byte), .from = CELL(2), .byte = SLOT_BYTE(1), .length = 4 },
    { 0x18, OPERATION(copy_byte), .from = SLOT_BYTE(2), .byte = SLOT_BYTE(1), .length = 3 },
    { 0x19, OPERATION(copy_byte), .from = CELL(3), .byte = CELL(1), .length = 5 },
    { 0x1A, OPERATION(jump), .to = OPERAND_WORD(1), .length = 3 },
    { 0x1B, OPERATION(loop), .byte = SLOT_BYTE(1), .by = SIGNED_OPERAND(2), .length = 3 },
    { 0x1C, OPERATION(branch_if), .test = CONSTANT(TEST_ZERO), .byte = SLOT_BYTE(1), .by = SIGNED_OPERAND(2), .length = 3 },
    { 0x1D, OPERATION(branch_if), .test = CONSTANT(TEST_NONZERO), .byte = SLOT_BYTE(1), .by = SIGNED_OPERAND(2), .length = 3 },
    { 0x1E, OPERATION(branch_if), .test = CONSTANT(TEST_NOT_EQUAL), .byte = SLOT_BYTE(1), .value = OPERAND(2),
      .by = SIGNED_OPERAND(3), .length = 4 },
    { 0x1F, OPERATION(branch_if), .test = CONSTANT(TEST_EQUAL), .byte = SLOT_BYTE(1), .value = OPERAND(2),
      .by = SIGNED_OPERAND(3), .length = 4 },
    { 0x20, OPERATION(branch_if), .test = CONSTANT(TEST_ANY_BIT), .byte = SLOT_BYTE(1), .value = OPERAND(2),
      .by = SIGNED_OPERAND(3), .length = 4 },
    { 0x21, OPERATION(branch_if), .test = CONSTANT(TEST_NO_BIT), .byte = SLOT_BYTE(1), .value = OPERAND(2),
      .by = SIGNED_OPERAND(3), .length = 4 },
    { 0x22, OPERATION(branch_if), .test = CONSTANT(TEST_ZERO), .byte = CELL(1), .by = SIGNED_OPERAND(3), .length = 4 },
    { 0x23, OPERATION(branch_if), .test = CONSTANT(TEST_NONZERO), .byte = CELL(1), .by = SIGNED_OPERAND(3), .length = 4 },
    { 0x24, OPERATION(branch_if), .test = CONSTANT(TEST_NOT_EQUAL), .byte = CELL(1), .value = OPERAND(3),
      .by = SIGNED_OPERAND(4), .length = 5 },
    { 0x25, OPERATION(branch_if), .test = CONSTANT(TEST_EQUAL), .byte = CELL(1), .value = OPERAND(3),
      .by = SIGNED_OPERAND(4), .length = 5 },
    { 0x26, OPERATION(branch_if), .test = CONSTANT(TEST_ANY_BIT), .byte = CELL(1), .value = OPERAND(3),
      .by = SIGNED_OPERAND(4), .length = 5 },
    { 0x27, OPERATION(branch_if), .test = CONSTANT(TEST_NO_BIT), .byte = CELL(1), .value = OPERAND(3),
      .by = SIGNED_OPERAND(4), .length = 5 },
    { 0x28, OPERATION(start_handler), .handler = CONSTANT(FADE_OUT), .slots = ORDINARY, .length = 2 },
    { 0x29, OPERATION(start_handler), .handler = CONSTANT(FADE_IN), .slots = ORDINARY, .length = 2 },
    { 0x2A, OPERATION(start_handler), .handler = CONSTANT(MOSAIC_ON), .slots = ORDINARY, .length = 3 },
    { 0x2B, OPERATION(start_handler), .handler = CONSTANT(MOSAIC_OFF), .slots = ORDINARY, .length = 3 },
    { 0x2C, OPERATION(place), .x = OPERAND_WORD(1), .y = OPERAND_WORD(3), .length = 5 },
    { 0x2D, OPERATION(nothing), .length = 2 },
    { 0x2E, OPERATION(set_velocity), .axis = CONSTANT(MENU_ACROSS), .fraction = OPERAND_WORD(1),
      .pixels = OPERAND_WORD(3), .length = 5 },
    { 0x2F, OPERATION(set_velocity), .axis = CONSTANT(MENU_DOWN), .fraction = OPERAND_WORD(1),
      .pixels = OPERAND_WORD(3), .length = 5 },
    { 0x30, OPERATION(give_sprite_script), .sprite_script = OPERAND(1), .length = 2 },
    { 0x31, OPERATION(for_frames), .frames = OPERAND(1), .moving = ON, .animating = ON, .length = 2 },
    { 0x32, OPERATION(scroll_map), .frames = OPERAND(1), .plane = CONSTANT(BOTH_PLANES), .length = 2 },
    { 0x33, OPERATION(queue_upload), .source = WORD_IN_BANK(2, 1), .vram = OPERAND_WORD(4), .size = OPERAND_WORD(6),
      .length = 8 },
    { 0x34, OPERATION(call_routine), .routine = OPERAND_WORD(1), .far = OFF, .length = 3 },
    { 0x35, OPERATION(start_handler), .handler = OPERAND_WORD(1), .slots = ORDINARY, .length = 3 },
    { 0x36, OPERATION(call), .to = OPERAND_WORD(1), .length = 3 },
    { 0x37, OPERATION(return_from_call), .length = 1 },
    { 0x38, OPERATION(for_frames), .frames = OPERAND(1), .moving = OFF, .animating = OFF, .length = 2 },
    { 0x39, OPERATION(for_frames), .frames = OPERAND(1), .moving = OFF, .animating = ON, .length = 2 },
    { 0x3A, OPERATION(until_older_than), .age = OPERAND_WORD(1), .length = 3 },
    { 0x3B, OPERATION(play_effect), .voices = CONSTANT(SOUND_EFFECT_ON_VOICES_6_7), .effect = OPERAND(1), .pan = OPERAND(2),
      .length = 3 },
    { 0x3C, OPERATION(play_effect), .voices = CONSTANT(SOUND_EFFECT_ON_VOICES_4_5), .effect = OPERAND(1), .pan = OPERAND(2),
      .length = 3 },
    { 0x3D, OPERATION(play_song), .song = OPERAND(1), .unless_kept = ON, .length = 2 },
    { 0x3E, OPERATION(redraw_map), .plane = OPERAND(1), .length = 2 },
    { 0x3F, OPERATION(move_to), .axis = CONSTANT(MENU_ACROSS), .target = OPERAND_WORD(1), .back_script = OPERAND(3),
      .forward_script = OPERAND(4), .length = 5 },
    { 0x40, OPERATION(move_to), .axis = CONSTANT(MENU_DOWN), .target = OPERAND_WORD(1), .back_script = OPERAND(3),
      .forward_script = OPERAND(4), .length = 5 },
    { 0x41, OPERATION(wait_for_next_frame), .length = 1 },
    { 0x42, OPERATION(start_handler), .handler = OPERAND_WORD(1), .slots = FIRST, .length = 3 },
    { 0x43, OPERATION(start_script), .script = WORD_IN_BANK(1, 3), .slots = FIRST, .length = 4 },
    { 0x44, OPERATION(mark_entry), .table = OPERAND(1), .entry = OPERAND(2), .mark = ON, .length = 3 },
    { 0x45, OPERATION(mark_entry), .table = OPERAND(1), .entry = OPERAND(2), .mark = OFF, .length = 3 },
    { 0x46, OPERATION(change_byte), .change = CONSTANT(BYTE_ADD), .byte = SLOT_BYTE(1), .value = OPERAND(2), .length = 3 },
    { 0x47, OPERATION(change_byte), .change = CONSTANT(BYTE_SUBTRACT), .byte = SLOT_BYTE(1), .value = OPERAND(2), .length = 3 },
    { 0x48, OPERATION(change_byte), .change = CONSTANT(BYTE_ADD), .byte = CELL(1), .value = OPERAND(3), .length = 4 },
    { 0x49, OPERATION(change_byte), .change = CONSTANT(BYTE_SUBTRACT), .byte = CELL(1), .value = OPERAND(3), .length = 4 },
    { 0x4A, OPERATION(play_song), .song = OPERAND(1), .unless_kept = OFF, .length = 2 },
    { 0x4B, OPERATION(send_sound_command), .command = OPERAND(1), .first = OPERAND(2), .second = OPERAND(3),
      .third = OPERAND(4), .length = 5 },
    { 0x4C, OPERATION(branch_if), .test = CONSTANT(TEST_BELOW), .byte = CELL(1), .value = OPERAND(3),
      .by = SIGNED_OPERAND(4), .length = 5 },
    { 0x4D, OPERATION(branch_if), .test = CONSTANT(TEST_AT_LEAST), .byte = CELL(1), .value = OPERAND(3),
      .by = SIGNED_OPERAND(4), .length = 5 },
    { 0x4E, OPERATION(call_routine), .routine = OPERAND_LONG(1), .far = ON, .length = 4 },
    { 0x4F, OPERATION(copy_map_tiles), .from_plane = OPERAND(1), .from_column = OPERAND(2), .from_row = OPERAND(3),
      .plane = OPERAND(4), .column = OPERAND(5), .row = OPERAND(6), .columns = OPERAND(7), .rows = OPERAND(8),
      .length = 9 },
    { 0x50, OPERATION(show_tile_group), .plane = OPERAND(1), .column = OPERAND(2), .row = OPERAND(3),
      .group = OPERAND(4), .length = 5 },
    { 0x51, OPERATION(scroll_map), .frames = OPERAND(2), .plane = OPERAND(1), .length = 3 },
    { 0x52, OPERATION(end), .length = 1 });
