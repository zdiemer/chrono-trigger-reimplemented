/* The sprites of bank C2's two modes.
 *
 * The sprite-object mode draws its objects from a list, rebuilt every frame.
 * The vblank handler empties it before the objects run; each object's sprite
 * script then shows one of its sprite groups at the object's place, which
 * puts the group on the list in drawing order; and after the objects the list
 * is drawn into the sprite table.
 *
 * Drawing order is what the sprite table's order makes of depth: sprites
 * earlier in the table are drawn over later ones. Groups of objects drawn in
 * front come first, in the order they were shown; then the rest, those lower
 * on the screen first, so that an object nearer the viewer covers one behind
 * it; except that an object can ask to be drawn after everything.
 *
 * A sprite group is a count and then four bytes a sprite: its offset across
 * and down from the object's place (signed bytes) and its tile word, to which
 * the object's first tile and look are added. A sprite off the screen is left
 * out. An object on the map is drawn where the map's scroll puts it, the map
 * being round across and down.
 *
 * A sprite script is a string of commands: show a group for a number of
 * frames (or for as long as the object holds it), wait, jump, change a byte
 * of the object's own state, queue an upload of its tiles, stop. It runs until
 * a command has shown a group or said to wait, and goes on there next frame.
 *
 * The menus proper keep no list: each of their windows' records ($1800) draws
 * a block of sprites into the table's upper half (menu_draw_record_sprites).
 * A record shows a character's sprite: loaded from its assignment, animated
 * frame by frame, each frame's tiles built and queued for VRAM (the section
 * "The characters the records show"). Both modes hide the sprites they
 * aren't using (menu_hide_all_sprites). */
#include "menu/sprites.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/bridge.h"
#include "menu/items.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/object.h"
#include "menu/objects.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* ---- The sprite table ---------------------------------------------------------------------- */

enum { SPRITES = 128, SPRITES_A_HIGH_BYTE = 4, HIDDEN_Y = 0xE0 };

// $C2:034D
/* Hide every sprite below the screen, and clear their size and ninth X bits. */
static void hide_all_sprites(GameState *g)
{
    for (unsigned i = 0; i < SPRITES; i++)
        set_oam_buffer_y(g, i, HIDDEN_Y);
    for (unsigned i = 0; i < SPRITES / SPRITES_A_HIGH_BYTE; i++)
        set_oam_high_table_byte(g, i, 0);
}

/* A sprite's two high bits: its X's ninth bit, and whether it is the other
 * size, in its place in the high table's byte. */
static uint8_t high_bits(unsigned sprite, bool x_ninth_bit, bool other_size)
{
    return (uint8_t)(((other_size ? 2 : 0) | (x_ninth_bit ? 1 : 0)) << (2 * (sprite % SPRITES_A_HIGH_BYTE)));
}

/* ---- The list ------------------------------------------------------------------------------ */

/* The list's nodes are 64 records in bank $7E; a node links to its neighbours
 * by address, and a list's own head is a pair of links where a node has its
 * two, so that a link to the head closes the list. The free nodes are a second
 * list of the same kind. */
enum { FIRST_NODE = 0xB000, NODE_BYTES = 0x10, NODES = 64 };
enum { DRAW_LIST = 0xB500, FREE_LIST = 0xB504 };
typedef uint16_t NodeLink;

static unsigned node_of(NodeLink l) { return (unsigned)(l - FIRST_NODE) / NODE_BYTES; }

/* Every link the list's code follows is a node or a head in the game, as the
 * mode's start lays them out; the guards of the list's own entries see to it.
 * A link that is neither -- which a list the game never makes could hold -- is
 * reached as the original reaches every link, as a record at that address in
 * bank $7E: its fields at the offsets a node has them. */
static bool names_a_node(NodeLink l)
{
    return l >= FIRST_NODE && l < FIRST_NODE + NODES * NODE_BYTES && (l - FIRST_NODE) % NODE_BYTES == 0;
}

static bool is_link(NodeLink l) { return names_a_node(l) || l == DRAW_LIST || l == FREE_LIST; }

enum { AT_PREV = 0, AT_NEXT = 2, AT_X = 4, AT_Y = 6, AT_LOOK = 8, AT_TILES = 9, AT_GROUP = 0x0B };

static uint32_t stray(NodeLink l, unsigned at) { return 0x7E0000u + l + at; }
static uint16_t stray_word(const GameState *g, NodeLink l, unsigned at)
{
    return (uint16_t)(wram_read8(g, stray(l, at)) | wram_read8(g, stray(l, at + 1)) << 8);
}
static void set_stray_word(GameState *g, NodeLink l, unsigned at, uint16_t v)
{
    wram_write8(g, stray(l, at), (uint8_t)v);
    wram_write8(g, stray(l, at + 1), (uint8_t)(v >> 8));
}

static NodeLink next_of(const GameState *g, NodeLink l)
{
    if (l == DRAW_LIST)
        return menu_sprite_draw_list_first(g);
    if (l == FREE_LIST)
        return menu_sprite_free_list_first(g);
    return names_a_node(l) ? menu_sprite_nodes_next(g, node_of(l)) : stray_word(g, l, AT_NEXT);
}

static NodeLink prev_of(const GameState *g, NodeLink l)
{
    if (l == DRAW_LIST)
        return menu_sprite_draw_list_last(g);
    if (l == FREE_LIST)
        return menu_sprite_free_list_last(g);
    return names_a_node(l) ? menu_sprite_nodes_prev(g, node_of(l)) : stray_word(g, l, AT_PREV);
}

static void set_next(GameState *g, NodeLink l, NodeLink to)
{
    if (l == DRAW_LIST)
        set_menu_sprite_draw_list_first(g, to);
    else if (l == FREE_LIST)
        set_menu_sprite_free_list_first(g, to);
    else if (names_a_node(l))
        set_menu_sprite_nodes_next(g, node_of(l), to);
    else
        set_stray_word(g, l, AT_NEXT, to);
}

static void set_prev(GameState *g, NodeLink l, NodeLink to)
{
    if (l == DRAW_LIST)
        set_menu_sprite_draw_list_last(g, to);
    else if (l == FREE_LIST)
        set_menu_sprite_free_list_last(g, to);
    else if (names_a_node(l))
        set_menu_sprite_nodes_prev(g, node_of(l), to);
    else
        set_stray_word(g, l, AT_PREV, to);
}

/* A node's sprite group: where it is on the map, its look and first tile, and
 * the group's address. */
typedef struct {
    uint16_t x, y;
    uint8_t look;
    uint16_t tiles;
    uint32_t group;
} NodeGroup;

static NodeGroup group_of(const GameState *g, NodeLink l)
{
    if (names_a_node(l)) {
        unsigned n = node_of(l);
        return (NodeGroup){ menu_sprite_nodes_x(g, n), menu_sprite_nodes_y(g, n), menu_sprite_nodes_look(g, n),
                            menu_sprite_nodes_tiles(g, n), menu_sprite_nodes_group(g, n) };
    }
    return (NodeGroup){ stray_word(g, l, AT_X), stray_word(g, l, AT_Y), wram_read8(g, stray(l, AT_LOOK)),
                        stray_word(g, l, AT_TILES),
                        (uint32_t)wram_read8(g, stray(l, AT_GROUP + 2)) << 16 | stray_word(g, l, AT_GROUP) };
}

static void set_group_of(GameState *g, NodeLink l, NodeGroup v)
{
    if (names_a_node(l)) {
        unsigned n = node_of(l);
        set_menu_sprite_nodes_x(g, n, v.x);
        set_menu_sprite_nodes_y(g, n, v.y);
        set_menu_sprite_nodes_group(g, n, v.group);
        set_menu_sprite_nodes_tiles(g, n, v.tiles);
        set_menu_sprite_nodes_look(g, n, v.look);
        return;
    }
    set_stray_word(g, l, AT_X, v.x);
    set_stray_word(g, l, AT_Y, v.y);
    set_stray_word(g, l, AT_GROUP, (uint16_t)v.group);
    wram_write8(g, stray(l, AT_GROUP + 2), (uint8_t)(v.group >> 16));
    set_stray_word(g, l, AT_TILES, v.tiles);
    wram_write8(g, stray(l, AT_LOOK), v.look);
}

// $C2:0C4D
/* Empty the list for a new frame: every node back on the free list, in order,
 * from the links the mode's start laid out for them. */
static void empty_the_list(GameState *g)
{
    set_menu_sprite_draw_list_last(g, DRAW_LIST);
    set_menu_sprite_draw_list_first(g, DRAW_LIST);
    set_menu_sprite_free_list_last(g, FIRST_NODE + (NODES - 1) * NODE_BYTES);
    set_menu_sprite_free_list_first(g, FIRST_NODE);
    for (unsigned n = 0; n < NODES; n++) {
        set_menu_sprite_nodes_prev(g, n, menu_sprite_node_links_prev(g, n));
        set_menu_sprite_nodes_next(g, n, menu_sprite_node_links_next(g, n));
    }
}

/* A node's place in the list and its drawing follow its look (menu/object.h)
 * and these bits of its tiles. */
enum { IN_FRONT = MENU_LOOK_IN_FRONT, ON_THE_MAP = MENU_LOOK_ON_THE_MAP };
enum { OTHER_SIZE = 0x4000, DRAWN_LAST = 0x8000, TILE_AND_ATTRIBUTES = 0x3FFF };

/* Where a node goes: before the first node that isn't in front, for one in
 * front; at the end for one drawn last; otherwise before the first node that
 * isn't in front and stands higher on the screen. */
static NodeLink place_in_the_list(const GameState *g, uint8_t look, uint16_t tiles, uint16_t y)
{
    NodeLink at = DRAW_LIST;
    if (look & IN_FRONT) {
        do
            at = next_of(g, at);
        while (at != DRAW_LIST && (group_of(g, at).look & IN_FRONT));
        return at;
    }
    if (tiles & DRAWN_LAST)
        return DRAW_LIST;
    for (;;) {
        at = next_of(g, at);
        if (at == DRAW_LIST)
            return at;
        NodeGroup there = group_of(g, at);
        if (there.look & IN_FRONT)
            continue;
        if (y > there.y)
            return at;
    }
}

// $C2:0B53
/* Show a sprite group this frame for the running object, at its place, with
 * its look: a node from the free list, put in the list where it is drawn. */
static void show_sprite_group(GameState *g, uint32_t group)
{
    NodeLink node = menu_sprite_free_list_first(g);
    set_next(g, prev_of(g, node), next_of(g, node));
    set_prev(g, next_of(g, node), prev_of(g, node));
    MenuObject o = menu_object_running(g);
    NodeGroup shown = { menu_object_slots_x_pixel(g, o), menu_object_slots_y_pixel(g, o), menu_object_slots_look(g, o),
                        menu_object_slots_tiles(g, o), group };
    set_group_of(g, node, shown);
    shown = group_of(g, node);
    NodeLink before = place_in_the_list(g, shown.look, shown.tiles, shown.y);
    NodeLink after = prev_of(g, before);
    set_next(g, node, next_of(g, after));
    set_next(g, after, node);
    set_prev(g, node, prev_of(g, next_of(g, node)));
    set_prev(g, next_of(g, node), node);
}

/* ---- Drawing the list ---------------------------------------------------------------------- */

/* The screen a sprite is drawn on, and how far off its top-left a sprite may
 * start and still show: a sprite is 16 pixels at most. */
enum { SCREEN_WIDTH = 0x100, SCREEN_HEIGHT = 0xE0, SPRITE_REACH = 0x0F };

/* A place on the map as it is on the screen: relative to the objects' plane's
 * scroll, and brought round the map to the nearer side. */
static int16_t on_screen(uint16_t at, uint16_t scroll, uint16_t span)
{
    int16_t d = (int16_t)(at - scroll);
    if (d < 0 && d < -(int16_t)(span / 2))
        return (int16_t)(d + span);
    if (d >= 0 && (uint16_t)d >= span / 2)
        return (int16_t)(d - span);
    return d;
}

/* A sprite group's bytes, wherever its node points: the bank stays the
 * group's while the offset wraps, as the original's pointer does, and a
 * sprite's four bytes are read on from there. */
typedef struct {
    uint8_t bank;
    uint16_t at;
} GroupPlace;

static uint8_t group_byte(const GameState *g, GroupPlace p, uint16_t k)
{
    return menu_sprite_group_byte(g, ((uint32_t)p.bank << 16 | p.at) + k);
}

/* The next entry of the table: the count wraps the table's 128, and past 255
 * the original's count, a word, carries into the running object's cell (a
 * stray the game never makes: no frame shows so many). */
static void count_a_sprite(GameState *g)
{
    uint8_t n = (uint8_t)(menu_object_sprite_index(g) + 1);
    set_menu_object_sprite_index(g, n);
    if (n == 0) {
        uint16_t current = menu_object_current(g);
        set_menu_object_current(g, (uint16_t)((current & 0xFF00) | (uint8_t)(current + 1)));
    }
}

/* The high table's byte for a sprite: past the 128th sprite the original's
 * index runs past the table into the bytes after it (explicitly, through
 * work RAM: the game never shows so many). */
static void or_high_bits(GameState *g, uint8_t sprite, uint8_t bits)
{
    unsigned at = sprite / SPRITES_A_HIGH_BYTE;
    if (at < SPRITES / SPRITES_A_HIGH_BYTE) {
        set_oam_high_table_byte(g, at, (uint8_t)(oam_high_table_byte(g, at) | bits));
        return;
    }
    uint32_t past_the_table = 0x7E0900u + at;
    wram_write8(g, past_the_table, (uint8_t)(wram_read8(g, past_the_table) | bits));
}

/* What the drawing's last node and sprite left, which the mode's scratch
 * cells end holding (leave_drawn): values the path through the list decides
 * and the list afterwards doesn't show. */
typedef struct {
    uint16_t node, node_x, node_y, attributes, first_tile, size;
    uint32_t group_end;
    int16_t x, x_on_screen_y;
    bool x_on_screen, shown, any_x_on_screen, any_shown;
} Drawn;

static Drawn drawn;

static void note_node(NodeLink node, int16_t x, int16_t y, uint16_t attributes, uint16_t first_tile, bool other_size,
                      uint32_t group_end)
{
    drawn.node = node;
    drawn.node_x = (uint16_t)x;
    drawn.node_y = (uint16_t)y;
    drawn.attributes = attributes;
    drawn.first_tile = first_tile;
    drawn.size = other_size ? 1 : 0;
    drawn.group_end = group_end;
}

static void note_sprite(int16_t x, int16_t y, bool x_on_screen, bool shown)
{
    drawn.x = x;
    drawn.x_on_screen = x_on_screen;
    drawn.shown = shown;
    if (x_on_screen)
        drawn.x_on_screen_y = y;
    drawn.any_x_on_screen = drawn.any_x_on_screen || x_on_screen;
    drawn.any_shown = drawn.any_shown || shown;
}

/* The original's time for a node follows its sprites: a sprite off the
 * screen across is dropped at once, one off it down a little later, and one
 * shown takes its entry and its high bits; a node on the map places itself
 * first. The port's own accesses weigh these otherwise, so it charges the
 * difference, in accesses at the drawing's rate. */
enum { DRAW_SPRITE_NODES = 0xC20CEA };
enum { NODE_ACCESSES = 2, ON_THE_MAP_ACCESSES = 9 };
enum { SPRITE_ACCESSES = 6, TESTED_DOWN_ACCESSES = 3, SHOWN_ACCESSES = 5 };

static void charge_node(GameState *g, bool on_the_map, unsigned sprites, unsigned tested_down, unsigned shown)
{
    charge_as(g, DRAW_SPRITE_NODES,
              NODE_ACCESSES + (on_the_map ? ON_THE_MAP_ACCESSES : 0) + sprites * SPRITE_ACCESSES
                  + tested_down * TESTED_DOWN_ACCESSES + shown * SHOWN_ACCESSES);
}

// $C2:0D08
/* Draw one node's group into the sprite table. */
static void draw_node(GameState *g, NodeLink node)
{
    NodeGroup drawn_group = group_of(g, node);
    uint32_t group = drawn_group.group;
    GroupPlace p = { (uint8_t)(group >> 16), (uint16_t)group };
    uint16_t left = group_byte(g, p, 0);
    uint16_t tiles = drawn_group.tiles;
    uint8_t look = drawn_group.look;
    bool other_size = (tiles & OTHER_SIZE) != 0;
    uint16_t attributes = (uint16_t)((look & MENU_LOOK_ATTRIBUTES) << 8);
    uint16_t first_tile = tiles & TILE_AND_ATTRIBUTES;
    int16_t x = (int16_t)drawn_group.x;
    int16_t y = (int16_t)drawn_group.y;
    if (look & ON_THE_MAP) {
        x = on_screen((uint16_t)x, menu_object_shadows_bg2_scroll_x(g), MENU_MAP_WIDTH);
        y = on_screen((uint16_t)y, menu_object_shadows_bg2_scroll_y(g), MENU_MAP_HEIGHT);
    }
    ORIGINAL_EFFECT(note_node(node, x, y, attributes, first_tile, other_size,
                              (group & 0xFF0000u) | (uint16_t)(group + 1 + 4 * (left ? left : 0x10000u))));
    p.at = (uint16_t)(p.at + 1);
    unsigned sprites = 0, tested_down = 0, shown_sprites = 0;
    do {
        sprites++;
        int16_t sx = (int16_t)(x + (int8_t)group_byte(g, p, 0));
        bool x_on = (uint16_t)(sx + SPRITE_REACH) < SCREEN_WIDTH + SPRITE_REACH;
        int16_t sy = 0;
        bool shown = false;
        if (x_on) {
            tested_down++;
            sy = (int16_t)(y + (int8_t)group_byte(g, p, 1));
            shown = (uint16_t)(sy + SPRITE_REACH) < SCREEN_HEIGHT + SPRITE_REACH;
        }
        if (shown) {
            uint16_t tile = (uint16_t)(group_byte(g, p, 2) | group_byte(g, p, 3) << 8);
            uint8_t i = menu_object_sprite_index(g);
            unsigned entry = i % SPRITES;
            uint16_t word = (uint16_t)((tile + first_tile) | attributes);
            set_oam_buffer_x(g, entry, (uint8_t)sx);
            set_oam_buffer_y(g, entry, (uint8_t)sy);
            set_oam_buffer_tile(g, entry, (uint8_t)word);
            set_oam_buffer_attributes(g, entry, (uint8_t)(word >> 8));
            or_high_bits(g, i, high_bits(i, ((uint16_t)sx & 0x100) != 0, other_size));
            count_a_sprite(g);
            shown_sprites++;
        }
        ORIGINAL_EFFECT(note_sprite(sx, sy, x_on, shown));
        p.at = (uint16_t)(p.at + 4);
    } while (--left != 0);
    ORIGINAL_EFFECT(charge_node(g, (look & ON_THE_MAP) != 0, sprites, tested_down, shown_sprites));
}

/* What the drawing leaves in the mode's scratch cells: the last node's place
 * on the screen, look and group pointer (run past its sprites); the last
 * sprite's X, its Y over the X's high byte where the X was on the screen, and
 * the high byte the size bit went out of where it showed. */
static void leave_drawn(GameState *g)
{
    set_menu_object_scratch_draw_count(g, 0);
    set_menu_object_scratch_draw_attributes(g, drawn.attributes);
    set_menu_object_scratch_draw_tiles(g, drawn.first_tile);
    set_menu_object_scratch_draw_node_x(g, drawn.node_x);
    set_menu_object_scratch_draw_node_y(g, drawn.node_y);
    set_menu_object_scratch_draw_node(g, drawn.node);
    set_menu_object_scratch_draw_size(g, drawn.size);
    uint16_t x = (uint16_t)drawn.x;
    set_menu_object_scratch_draw_x(g, drawn.shown ? (uint16_t)((x & 0x00FF) | (x >> 9) << 8) : x);
    uint32_t place = menu_object_scratch_draw_place(g);
    if (drawn.x_on_screen)
        place = (uint8_t)x | (uint32_t)(uint16_t)drawn.x_on_screen_y << 8;
    else if (drawn.any_x_on_screen)
        place = x | (uint32_t)((uint16_t)drawn.x_on_screen_y >> 8) << 16;
    else
        place = (place & 0xFF0000u) | x;
    set_menu_object_scratch_draw_place(g, place);
    if (drawn.any_shown)
        set_menu_object_scratch_draw_size_bit(g, 0);
    set_menu_object_sprite_group(g, drawn.group_end);
}

// $C2:0CEA
/* Draw the list, node by node, into the sprite table. */
static void draw_the_list(GameState *g)
{
    ORIGINAL_EFFECT(drawn = (Drawn){ 0 });
    for (NodeLink l = menu_sprite_draw_list_first(g); l != DRAW_LIST; l = next_of(g, l))
        draw_node(g, l);
    if (menu_sprite_draw_list_first(g) != DRAW_LIST)
        ORIGINAL_EFFECT(leave_drawn(g));
}

/* ---- Sprite scripts --------------------------------------------------------------------------- */

/* A sprite script is read a command at a time by a table of rows, an opcode
 * a row: what the command does and how long it is. The three that change a
 * byte of the object's state are one operation, their rows saying how. */
typedef enum { SPRITE_CHANGE_BYTE, SPRITE_JUMP, SPRITE_SHOW, SPRITE_WAIT, SPRITE_UPLOAD, SPRITE_STOP } SpriteOperation;
typedef enum { BYTE_CLEARED, BYTE_COUNTED_UP, BYTE_COUNTED_DOWN } SpriteByteChange;

typedef struct {
    SpriteOperation operation;
    SpriteByteChange change;   /* a byte change's */
    uint8_t length;            /* the command's bytes, its opcode's included */
} SpriteCommandRow;

static const SpriteCommandRow sprite_commands[] = {
    { SPRITE_CHANGE_BYTE, BYTE_CLEARED, 2 },      // $C2:0E5A
    { SPRITE_CHANGE_BYTE, BYTE_COUNTED_UP, 2 },   // $C2:0E6B
    { SPRITE_CHANGE_BYTE, BYTE_COUNTED_DOWN, 2 }, // $C2:0E7E
    { SPRITE_JUMP, BYTE_CLEARED, 2 },             // $C2:0E91
    { SPRITE_SHOW, BYTE_CLEARED, 4 },             // $C2:0EA3
    { SPRITE_WAIT, BYTE_CLEARED, 2 },             // $C2:0EDC
    { SPRITE_UPLOAD, BYTE_CLEARED, 8 },           // $C2:0F00
    { SPRITE_STOP, BYTE_CLEARED, 1 },             // $C2:0F35
};
enum { SPRITE_OPCODES = sizeof sprite_commands / sizeof sprite_commands[0] };

/* A place in a sprite script: the bank stays the script's while the offset
 * wraps in it, as the original's pointer does; operands read on from there. */
typedef struct {
    uint8_t bank;
    uint16_t at;
} SpritePlace;

static uint8_t sprite_script_byte(const GameState *g, SpritePlace p, uint16_t k)
{
    return menu_sprite_script_byte(g, ((uint32_t)p.bank << 16 | p.at) + k);
}
static uint16_t sprite_script_word(const GameState *g, SpritePlace p, uint16_t k)
{
    return (uint16_t)(sprite_script_byte(g, p, k) | sprite_script_byte(g, p, (uint16_t)(k + 1)) << 8);
}

/* A command, its operands decoded by its row's operation: the byte of the
 * object's state a change works on (by its offset, which can name a byte past
 * the slot's own 64, menu/object.h), how far a jump goes, the group a show
 * shows and in its script's bank, the frames a show or a wait lasts, and an
 * upload of the object's tiles. */
typedef struct {
    const SpriteCommandRow *row;
    uint8_t byte;
    int8_t by;
    uint32_t group;
    uint8_t frames;
    uint32_t source;
    uint16_t vram, size;
} SpriteCommand;

static SpriteCommand decode_sprite_command(const GameState *g, const SpriteCommandRow *row, SpritePlace p)
{
    SpriteCommand c = { .row = row };
    switch (row->operation) {
    case SPRITE_CHANGE_BYTE: c.byte = sprite_script_byte(g, p, 1); break;
    case SPRITE_JUMP: c.by = (int8_t)sprite_script_byte(g, p, 1); break;
    case SPRITE_SHOW:
        c.group = (uint32_t)p.bank << 16 | sprite_script_word(g, p, 1);
        c.frames = sprite_script_byte(g, p, 3);
        break;
    case SPRITE_WAIT: c.frames = sprite_script_byte(g, p, 1); break;
    case SPRITE_UPLOAD:
        c.source = (uint32_t)sprite_script_byte(g, p, 1) << 16 | sprite_script_word(g, p, 2);
        c.vram = sprite_script_word(g, p, 4);
        c.size = sprite_script_word(g, p, 6);
        break;
    case SPRITE_STOP: break;
    }
    return c;
}

/* What a command says next: on to the command `by` bytes on, or stay on this
 * one until next frame (0). */
typedef int16_t SpriteStep;
enum { SPRITE_STAY = 0 };

/* A countdown in the object's sprite frames: started from `frames` the first
 * time, counted down after. True while it runs. */
static bool counting(GameState *g, MenuObject o, uint8_t frames)
{
    uint8_t left = menu_object_slots_sprite_frames(g, o);
    if (left == 0) {
        set_menu_object_slots_sprite_frames(g, o, frames);
        return true;
    }
    set_menu_object_slots_sprite_frames(g, o, --left);
    return left != 0;
}

/* Show a group for a number of frames, or for as long as the object is held
 * on it; true while it shows, and once the frames are out the script goes
 * on instead. */
static bool show(GameState *g, MenuObject o, uint32_t group, uint8_t frames)
{
    if (!(menu_object_slots_look(g, o) & MENU_LOOK_HELD) && !counting(g, o, frames))
        return false;
    ORIGINAL_EFFECT(set_menu_object_sprite_group(g, group));
    show_sprite_group(g, group);
    return true;
}

static void change_object_byte(GameState *g, MenuObject o, SpriteByteChange change, uint8_t byte)
{
    uint8_t now = change == BYTE_CLEARED ? 0 : menu_object_byte(g, o, byte);
    if (change == BYTE_COUNTED_UP)
        now++;
    else if (change == BYTE_COUNTED_DOWN)
        now--;
    menu_set_object_byte(g, o, byte, now);
}

/* A jump goes back the bytes it gives, to repeat an animation. One forward
 * (or by nothing) holds the script on the jump: the original tests the
 * offset's sign where it means to test the step. */
static SpriteStep jump(int8_t by) { return by < 0 ? by : SPRITE_STAY; }

static SpriteStep perform_sprite_command(GameState *g, MenuObject o, const SpriteCommand *c)
{
    SpriteStep on = c->row->length;
    switch (c->row->operation) {
    case SPRITE_CHANGE_BYTE: change_object_byte(g, o, c->row->change, c->byte); return on;
    case SPRITE_JUMP: return jump(c->by);
    case SPRITE_SHOW: return show(g, o, c->group, c->frames) ? SPRITE_STAY : on;
    case SPRITE_WAIT: return counting(g, o, c->frames) ? SPRITE_STAY : on;
    case SPRITE_UPLOAD: menu_queue_object_upload(g, c->source, c->vram, c->size); return on;
    case SPRITE_STOP: break;
    }
    return SPRITE_STAY;
}

// $C2:0E1D
/* Run the running object's sprite script until it shows a group, waits or
 * stops, and keep where it got to for next frame. A command past the
 * language's eight stops the script here, where the original jumps through
 * the words after its table into code: the game's sprite scripts have none,
 * and the guards hand a script that has one to the original
 * (menu_sprite_script_is_well_formed). */
static void run_sprite_script(GameState *g)
{
    MenuObject o = menu_object_running(g);
    uint32_t script = menu_object_slots_sprite_script(g, o);
    SpritePlace p = { (uint8_t)(script >> 16), (uint16_t)script };
    for (;;) {
        uint8_t opcode = sprite_script_byte(g, p, 0);
        if (opcode >= SPRITE_OPCODES)
            break;
        SpriteCommand c = decode_sprite_command(g, &sprite_commands[opcode], p);
        SpriteStep step = perform_sprite_command(g, o, &c);
        if (step == SPRITE_STAY)
            break;
        p.at = (uint16_t)(p.at + step);
    }
    ORIGINAL_EFFECT(set_menu_object_sprite_script(g, (uint32_t)p.bank << 16 | p.at));
    set_menu_object_slots_sprite_script_at(g, o, p.at);
}

void menu_object_animate(GameState *g) { run_sprite_script(g); }

/* A command whose bytes are in the objects' slots, which the commands write:
 * no game's script is there, and one that is changes under its own reading. */
static bool in_the_slots(SpritePlace p)
{
    enum { LONGEST = 8 };
    for (uint16_t k = 0; k < LONGEST; k++)
        if (menu_objects_hold((uint32_t)p.bank << 16 | (uint16_t)(p.at + k)))
            return true;
    return false;
}

/* Where a command holds its script for good: a show or a wait of no frames,
 * and a jump forward. What follows it is the next script's, or data. */
static bool holds_for_good(const SpriteCommand *c)
{
    switch (c->row->operation) {
    case SPRITE_SHOW: case SPRITE_WAIT: return c->frames == 0;
    case SPRITE_JUMP: return c->by >= 0;
    default: return false;
    }
}

/* Whether an object's sprite script is one the language reads: every command
 * it can reach from where it is is one of the eight, and outside the objects'
 * slots, before it stops, holds for good or goes round. The game's scripts
 * all are; the original jumps through the words after its table on any
 * other. A show or a wait of some frames goes on to the next command once
 * they are out, so the walk goes on past each. */
static bool reads_as_the_language(const GameState *g, uint32_t script)
{
    enum { WALK = 64 };
    SpritePlace p = { (uint8_t)(script >> 16), (uint16_t)script };
    for (unsigned step = 0; step < WALK; step++) {
        uint8_t opcode = sprite_script_byte(g, p, 0);
        if (opcode >= SPRITE_OPCODES || in_the_slots(p))
            return false;
        SpriteCommand c = decode_sprite_command(g, &sprite_commands[opcode], p);
        if (c.row->operation == SPRITE_STOP || holds_for_good(&c))
            return true;
        p.at = (uint16_t)(p.at + (c.row->operation == SPRITE_JUMP ? c.by : c.row->length));
    }
    return true;   /* round and round within the walk's reach */
}

bool menu_sprite_script_is_well_formed(const GameState *g, MenuObject o)
{
    return reads_as_the_language(g, menu_object_slots_sprite_script(g, o));
}

bool menu_given_sprite_script_is_well_formed(const GameState *g, uint8_t script)
{
    return reads_as_the_language(g, (uint32_t)MENU_SPRITE_SCRIPT_BANK << 16 | menu_sprite_script_start(g, script));
}

// $C2:1CDA
/* Give an object one of the game's sprite scripts, from its start, its first
 * command's frames not yet begun. */
void menu_give_sprite_script(GameState *g, MenuObject o, uint8_t script)
{
    set_menu_object_slots_sprite_script(g, o, (uint32_t)MENU_SPRITE_SCRIPT_BANK << 16 | menu_sprite_script_start(g, script));
    set_menu_object_slots_sprite_frames(g, o, 0);
}

/* ---- The menus' window records ------------------------------------------------------------------ */

/* A record draws its sprites into the table's upper half, four table entries
 * a record, in order from the record's place ($1800 + $40 a record), and
 * their ninth X bits into the high table's byte for those four. Its sprite
 * count's low nibble counts fours: 1 draws four sprites (and 0 still draws
 * one). Each sprite is 16 pixels, so its tiles go two on, and past a row of
 * eight, on past the row between that holds their lower halves. */
enum { RECORD_FIRST_SPRITE = 64, RECORD_SPRITES = 4 };

static unsigned record_sprites(const GameState *g, unsigned record)
{
    unsigned sprites = (menu_record_sprite_count(g, record) & 0x0F) * RECORD_SPRITES;
    return sprites != 0 ? sprites : 1;
}

/* The next sprite's first tile: two on along the upper row of tiles, and
 * past the row between at a row's end, which holds their lower halves. */
enum { SPRITE_ALONG = 2, LOWER_TILE_ROW = 0x10 };

static uint8_t next_record_tile(uint8_t tile)
{
    uint8_t along = (uint8_t)(tile + SPRITE_ALONG);
    return along & LOWER_TILE_ROW ? (uint8_t)(along + LOWER_TILE_ROW) : along;
}

/* The record's X on the screen: its place, 9 bits, from the origin. */
static uint16_t record_x(const GameState *g, unsigned record)
{
    return (uint16_t)(menu_record_x(g, record) + menu_record_origin_x(g));
}

// $C2:F871
static void draw_record_sprites(GameState *g, unsigned record)
{
    unsigned sprites = record_sprites(g, record);
    unsigned first = RECORD_FIRST_SPRITE + record * RECORD_SPRITES;
    unsigned high = first / SPRITES_A_HIGH_BYTE;
    uint16_t x = record_x(g, record);
    uint8_t tile = 0;
    for (unsigned i = 0; i < sprites; i++) {
        uint16_t sx = (uint16_t)(x + (int8_t)menu_record_sprite_offsets_byte(g, record, 2 * i));
        set_oam_buffer_x(g, first + i, (uint8_t)sx);
        if ((sx & 0x100) && i < RECORD_SPRITES)
            set_oam_high_table_byte(g, high, (uint8_t)(oam_high_table_byte(g, high) | 1 << (2 * i)));
        set_oam_buffer_y(g, first + i, (uint8_t)(menu_record_y(g, record) + menu_record_sprite_offsets_byte(g, record, 2 * i + 1)
                                                 + menu_record_origin_y(g)));
        uint16_t word = (uint16_t)(tile + menu_record_tile(g, record));
        set_oam_buffer_tile(g, first + i, (uint8_t)word);
        set_oam_buffer_attributes(g, first + i, (uint8_t)(word >> 8));
        tile = next_record_tile(tile);
    }
}

/* What the record's drawing leaves in the menus' first cells: its high bit
 * shifted out past the sprites drawn, its high table byte, its count, its X
 * (the high byte rotated as it tests the ninth bit) and its last tile. */
static void leave_record_drawn(GameState *g, unsigned record)
{
    unsigned sprites = record_sprites(g, record);
    uint16_t x = record_x(g, record);
    uint8_t high = (uint8_t)(x >> 8);
    uint8_t tile = 0;
    for (unsigned i = 0; i < sprites; i++)
        tile = next_record_tile(tile);
    set_menu_scratch_record_high_bit(g, sprites < RECORD_SPRITES ? (uint8_t)(1u << (2 * sprites)) : 0);
    set_menu_scratch_record_high_byte(g, (uint16_t)record);
    set_menu_scratch_record_sprites_end(g, (uint16_t)((menu_record_sprite_count(g, record) & 0x0F) * 8));
    set_menu_scratch_record_x(g, (uint16_t)((uint8_t)x | (uint8_t)((high & 1) << 7 | high >> 2) << 8));
    set_menu_scratch_record_tile(g, tile);
}

/* ---- The characters the records show ------------------------------------------------------ */

/* Each record shows a sprite: a character walking on the load screen, the
 * party on the menus' screens. Its load looks up the sprite's assignment
 * (menu_data.h), which says where its graphics and its frames are, which
 * colours and animations it has and how large it is; puts its colours in the
 * record's sprite palette; and unpacks its graphics, for all but the playable
 * characters, whose graphics the ROM keeps as they are. Its animation is a
 * list of frames for each facing, each frame shown for a time; each change of
 * frame builds the frame's tiles into the frame buffer and queues them for
 * VRAM, a block (32 by 32 pixels) at a time, and the records' task shares a
 * frame's uploads out among the records (menu_record_block_limit). */
enum { NO_SPRITE = 0xFF, NO_FRAME = 0xFF, NO_ANIMATION = 0xFF, ANIMATION_BITS = 0x7F, LOOPS = 0x80 };
enum { SPRITE_TABLES_BANK = 0xE4, OTHER_COLOURS = 0x10, OTHER_COLOURS_OFFSET = 0x200 };
enum { FRAMES_AN_ANIMATION = 4, FACINGS = 4 };
/* The standing animation, which holds its frame. */
enum { STANDING = 0 };

typedef struct {
    uint8_t graphics, frames, colours, animations, size;
} SpriteAssignment;

/* A 24-bit address stepped on within its bank. */
static uint32_t in_bank(uint32_t at, uint16_t by) { return (at & 0xFF0000u) | (uint16_t)(at + by); }

/* The three sets of sprites: the playable characters' first, whose five-byte
 * assignments name graphics the ROM keeps as they are; the others' graphics
 * are packed, and the last set's assignments are ten bytes. */
enum { PLAYABLE_CHARACTERS = 0, LONG_ASSIGNMENTS = 2, SPRITE_SETS = 3, SHORT_ASSIGNMENT = 5, LONG_ASSIGNMENT = 10 };

static uint8_t assignment_bytes(uint8_t set) { return set >= LONG_ASSIGNMENTS ? LONG_ASSIGNMENT : SHORT_ASSIGNMENT; }

/* The sprite's assignment: its set's place, the sprite's number times the
 * set's assignment size on (the original multiplies with the hardware). */
static uint16_t assignment_place(const GameState *g, uint8_t set, uint8_t sprite)
{
    return (uint16_t)(menu_sprite_set_place(g, (uint8_t)(set << 1)) + sprite * assignment_bytes(set));
}

static void leave_multiplied(GameState *g, uint8_t a, uint8_t b)
{
    hw_set_multiplicand(g, a);
    (void)hw_multiply_by(g, b);
}

static SpriteAssignment read_assignment(const GameState *g, uint16_t at)
{
    uint8_t b[MENU_SPRITE_ASSIGNMENT_BYTES];
    for (unsigned i = 0; i < MENU_SPRITE_ASSIGNMENT_BYTES; i++)
        b[i] = menu_sprite_assignment_byte(g, (uint16_t)(at + i));
    return (SpriteAssignment){ b[0], b[1], b[2], b[3], b[4] };
}

/* The load keeps the assignment where the frames' tile lists go later, its
 * size byte down to the colour set's bit, having looked it up with the
 * multiplier; and leaves its animations' number doubled in the menus' first
 * cells. */
static void leave_assignment(GameState *g, uint8_t set, uint8_t sprite, SpriteAssignment a)
{
    leave_multiplied(g, sprite, assignment_bytes(set));
    set_menu_record_frame_tile_list(g, 0, (uint16_t)(a.graphics | a.frames << 8));
    set_menu_record_frame_tile_list(g, 1, (uint16_t)(a.colours | a.animations << 8));
    set_menu_record_frame_tile_list(g, 2, (uint16_t)(a.size & OTHER_COLOURS));
    charge_block_move(g, MENU_SPRITE_ASSIGNMENT_BYTES);
    if (a.graphics != NO_SPRITE)
        set_menu_scratch_sprite_animations_entry(g, (uint16_t)(a.animations * 2));
}

/* The record's sprite palette: twelve colours past its first, the palette
 * its tile's attributes name. */
enum { SPRITE_PALETTES = 0x80, PALETTE_COLOURS = 16, SPRITE_COLOURS = 12, PALETTE_BITS = 0x0E00, PALETTE_SHIFT = 9 };

static void put_colours(GameState *g, unsigned record, uint16_t from)
{
    unsigned first = SPRITE_PALETTES + ((menu_record_tile(g, record) & PALETTE_BITS) >> PALETTE_SHIFT) * PALETTE_COLOURS + 1;
    for (unsigned i = 0; i < SPRITE_COLOURS; i++) {
        uint16_t at = (uint16_t)(from + 2 * i);
        set_menu_palette(g, first + i, (uint16_t)(menu_sprite_colour_byte(g, at) | menu_sprite_colour_byte(g, (uint16_t)(at + 1)) << 8));
    }
    ORIGINAL_EFFECT(charge_block_move(g, 2 * SPRITE_COLOURS));
}

/* A character's sprite colours ($FF:F628): the twelve its sprites have,
 * in the character's own sprite palette (the second on, a character each),
 * at half their brightness where the screen dims the characters it hasn't
 * chosen. The character's sprite is its colours' first entry. */
enum { FIRST_CHARACTER_PALETTE = 1, CHARACTER_BITS = 7, HALF_EACH_CHANNEL = 0x3DEF };

static uint16_t sprite_colour(const GameState *g, uint16_t at)
{
    return (uint16_t)(menu_sprite_colour_byte(g, at) | menu_sprite_colour_byte(g, (uint16_t)(at + 1)) << 8);
}

static uint16_t dimmed(uint16_t colour) { return (uint16_t)((colour >> 1) & HALF_EACH_CHANNEL); }

static unsigned first_colour_of(uint8_t character)
{
    return SPRITE_PALETTES + (FIRST_CHARACTER_PALETTE + character) * PALETTE_COLOURS + 1;
}

/* A character's colours, the weapon's three after the sprite's twelve. */
enum { CHARACTER_COLOURS = SPRITE_COLOURS + MENU_WEAPON_COLOURS };

// $C2:CCB3
void menu_dim_character_colours(GameState *g, uint8_t character)
{
    unsigned first = first_colour_of(character);
    for (unsigned i = 0; i < CHARACTER_COLOURS; i++)
        set_menu_palette(g, first + i, dimmed(menu_palette(g, first + i)));
}

void menu_leave_colours_dimmed_past_the_palette(GameState *g, uint8_t emptied)
{
    unsigned first = first_colour_of(emptied);
    for (unsigned i = 0; i < CHARACTER_COLOURS; i++)
        set_menu_palette_unchecked(g, first + i, dimmed(menu_palette_unchecked(g, first + i)));
}

/* The original dims the colours one by one, which leaves its count of them
 * run out in its cell, and moves them in undimmed with a block move. */
static void leave_colours_put(GameState *g, bool dim)
{
    if (dim)
        set_menu_scratch_dimmed_colours_left(g, 0);
    else
        charge_block_move(g, 2 * SPRITE_COLOURS);
}

// $FF:F628
void menu_put_character_colours(GameState *g, uint8_t character, bool dim)
{
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    uint8_t which = (uint8_t)(character & CHARACTER_BITS);
    unsigned first = first_colour_of(which);
    uint16_t from = menu_sprite_table_word(g, (uint16_t)(which * 2 + menu_sprite_colours_table(g)));
    for (unsigned i = 0; i < SPRITE_COLOURS; i++) {
        uint16_t colour = sprite_colour(g, (uint16_t)(from + 2 * i));
        set_menu_palette(g, first + i, dim ? dimmed(colour) : colour);
    }
    ORIGINAL_EFFECT(leave_colours_put(g, dim));
}

/* Where a record's packed graphics are unpacked to: $800 bytes a record. */
enum { UNPACKED_GRAPHICS = 0xB000, UNPACKED_BYTES = 0x800, WORK_RAM_BANK = 0x7E0000 };

/* What a load leaves the unpacker to do: nothing, or unpack the graphics at
 * `from` to `to` in work RAM. */
typedef struct {
    bool unpack;
    uint32_t from;
    uint16_t to;
} Unpacking;

// $C2:F422
/* Load a record's sprite: shown, no animation and no frame yet, its
 * graphics, frames, animations and colours from its assignment. A sprite
 * whose assignment names no graphics keeps the rest as it was; one with no
 * animations keeps its colours. A sprite's animations' span of frames is a
 * multiple of four (four facings), in every entry of the ROM's table
 * (tests/test_menu_sprites.py). */
static Unpacking load_record_sprite(GameState *g, unsigned record)
{
    Unpacking none = { false, 0, 0 };
    uint8_t set = menu_record_sprite_set(g, record);
    uint8_t sprite = menu_record_sprite(g, record);
    set_menu_record_hidden(g, record, 0);
    set_menu_record_animation_shown(g, record, NO_ANIMATION);
    set_menu_record_frame_shown(g, record, NO_FRAME);
    set_menu_record_frame_list(g, record, (uint32_t)SPRITE_TABLES_BANK << 16 | (uint16_t)menu_record_frame_list(g, record));
    set_menu_record_frame_times(g, record, (uint32_t)SPRITE_TABLES_BANK << 16 | (uint16_t)menu_record_frame_times(g, record));
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    SpriteAssignment a = read_assignment(g, assignment_place(g, set, sprite));
    ORIGINAL_EFFECT(leave_assignment(g, set, sprite, a));
    set_menu_record_sprite_count(g, record, menu_sprite_blocks(g, a.size));
    if (a.graphics == NO_SPRITE)
        return none;

    uint32_t graphics = menu_sprite_place(g, (uint16_t)(a.graphics * 3 + menu_sprite_graphics_table(g)));
    set_menu_record_graphics(g, record, graphics);
    set_menu_record_frame_data(g, record, menu_sprite_place(g, (uint16_t)(a.frames * 3 + menu_sprite_frames_table(g))));

    uint16_t entry = (uint16_t)(a.animations * 2);
    uint16_t frames_at = (uint16_t)(entry + menu_sprite_animation_frames_table(g));
    uint16_t first = menu_sprite_table_word(g, frames_at);
    set_menu_record_animation_frames_at(g, record, first);
    uint16_t span = (uint16_t)(menu_sprite_table_word(g, (uint16_t)(frames_at + 2)) - first);
    if (span == 0)
        return none;
    set_menu_record_animation_count(g, record, (uint16_t)(span / FACINGS));
    uint16_t times_at = (uint16_t)(entry + menu_sprite_animation_times_table(g));
    set_menu_record_animation_times_at(g, record, menu_sprite_table_word(g, times_at));

    uint16_t colours = menu_sprite_table_word(g, (uint16_t)(a.colours * 2 + menu_sprite_colours_table(g)));
    if (a.size & OTHER_COLOURS)
        colours = (uint16_t)(colours + OTHER_COLOURS_OFFSET);
    put_colours(g, record, colours);

    if (set == PLAYABLE_CHARACTERS)
        return none;
    uint16_t to = (uint16_t)(UNPACKED_GRAPHICS + record * UNPACKED_BYTES);
    set_menu_record_graphics(g, record, WORK_RAM_BANK | to);
    return (Unpacking){ true, graphics, to };
}

/* ---- A record's frame ---- */

/* A frame's tiles are 16 a block, a word each: 0 a blank tile, otherwise a
 * tile of the sprite's graphics (bits 0-10), mirrored left to right where bit
 * 14 is set. They are built in the frame buffer from $7E:8000, $200 bytes a
 * block, in the order the blocks are queued in a frame, and queued for VRAM
 * from the record's first tile on. */
enum { TILE_BYTES = 32, TILE_NUMBER = 0x7FF, MIRRORED = 0x4000, BLOCK_BYTES = 0x200, BLOCK_TILE_LIST = 32 };
enum { FRAME_BUFFER = 0x8000, TILE_NUMBER_BITS = 0x1FF, SPRITE_TILES_VRAM = 0x4000, HALF_BLOCK = 0x100 };

typedef struct {
    bool blank;
    uint16_t number;
    bool mirrored;
} FrameTile;

static FrameTile decode_tile(uint16_t word) { return (FrameTile){ word == 0, (uint16_t)(word & TILE_NUMBER), (word & MIRRORED) != 0 }; }

/* The block move the original copies a frame's pieces with: its stub, with
 * the bank it copies from. */
enum { STUB_MOVE_TO_WORK_RAM = 0x7E54, STUB_RETURN = 0x6B };

/* Where a frame's blocks go in the frame buffer: past the blocks queued this
 * frame already, and half a block further where the buffer's half says (the
 * place wraps within the bank, as the original's word does). */
enum { HALF_ON = 0x80 };

static uint16_t frame_buffer_for(const GameState *g, uint8_t blocks_before)
{
    return (uint16_t)(FRAME_BUFFER + blocks_before * BLOCK_BYTES + (menu_record_buffer_half(g) & HALF_ON ? HALF_BLOCK : 0));
}

/* Tiles are copied from the sprite's graphics, in the ROM or unpacked in work
 * RAM, and charged at the block move's rate for the bytes filled or copied. */
static void put_blank_tile(GameState *g, uint16_t at)
{
    wram_fill8(g, WORK_RAM_BANK | at, TILE_BYTES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, TILE_BYTES));
}

static void put_tile(GameState *g, uint32_t graphics, FrameTile tile, uint16_t at)
{
    uint32_t from = in_bank(graphics, (uint16_t)(tile.number * TILE_BYTES));
    if (!tile.mirrored) {
        wram_copy(g, from, WORK_RAM_BANK | at, TILE_BYTES);
        ORIGINAL_EFFECT(charge_block_move(g, TILE_BYTES));
        return;
    }
    uint8_t pixels[TILE_BYTES];
    for (unsigned i = 0; i < TILE_BYTES; i++)
        pixels[i] = menu_mirrored_pixels(g, menu_sprite_byte(g, in_bank(from, (uint16_t)i)));
    for (unsigned i = 0; i < TILE_BYTES; i++)
        wram_write8(g, WORK_RAM_BANK | (uint16_t)(at + i), pixels[i]);
}

/* The place in VRAM of a frame's next block: 8 tiles along the upper row of
 * tiles the last started on, and round to the next pair of rows at a row's
 * end (a row of tiles is $100 words; a block's lower row the next one). */
enum { BLOCK_ALONG = 0x80, TILE_ROW = 0x100 };

static uint16_t next_block_place(uint16_t upper_row)
{
    uint16_t along = (uint16_t)(upper_row + BLOCK_ALONG);
    return along & TILE_ROW ? (uint16_t)(along + TILE_ROW) : along;
}

/* The upload of a frame's blocks: one run of them where they are an even
 * number (their tiles laid out as VRAM has them), and otherwise each block's
 * two rows of tiles on their own, its upper row where the block starts and
 * its lower a row of tiles on. */
static void queue_frame(GameState *g, uint16_t vram, uint32_t source, uint8_t blocks, uint8_t spare)
{
    if (!(blocks & 1)) {
        MenuUpload u = { vram, source, (uint16_t)(blocks * BLOCK_BYTES), spare };
        TIMED_AS(g, 0xC2838E, menu_queue_upload(g, u));
        return;
    }
    for (uint8_t block = 0; block < blocks; block++) {
        MenuUpload upper = { vram, source, HALF_BLOCK, spare };
        MenuUpload lower = { (uint16_t)(vram + TILE_ROW), in_bank(source, HALF_BLOCK), HALF_BLOCK, spare };
        TIMED_AS(g, 0xC2838E, menu_queue_upload(g, upper));
        TIMED_AS(g, 0xC2838E, menu_queue_upload(g, lower));
        vram = next_block_place(vram);
        source = in_bank(source, BLOCK_BYTES);
    }
}

/* A frame's tiles: 16 a block, as many as the record's sprite has blocks (a
 * record of none builds one), three blocks at most (the guards'). */
enum { TILES_A_BLOCK = 16, MOST_BLOCKS = 3, MOST_FRAME_TILES = MOST_BLOCKS * TILES_A_BLOCK };
typedef struct {
    uint16_t tile[MOST_FRAME_TILES];
    unsigned count;
} FrameTiles;

static unsigned tiles_of(uint8_t blocks)
{
    unsigned n = (blocks & 0x0F) * TILES_A_BLOCK;
    return n != 0 ? n : 1;
}

static FrameTiles frame_tiles_at(const GameState *g, uint32_t list, uint8_t blocks)
{
    FrameTiles t = { { 0 }, tiles_of(blocks) };
    for (unsigned i = 0; i < t.count; i++)
        t.tile[i] = (uint16_t)(menu_sprite_byte(g, in_bank(list, (uint16_t)(2 * i)))
                               | menu_sprite_byte(g, in_bank(list, (uint16_t)(2 * i + 1))) << 8);
    return t;
}

// $C2:F75C
/* Build the frame's tiles into the frame buffer and queue them. */
static void show_frame_tiles(GameState *g, unsigned record, const FrameTiles *t)
{
    uint32_t graphics = menu_record_graphics(g, record);
    uint8_t blocks = menu_record_sprite_count(g, record);
    uint16_t buffer = frame_buffer_for(g, menu_record_blocks_sent(g));
    for (unsigned i = 0; i < t->count; i++) {
        FrameTile tile = decode_tile(t->tile[i]);
        uint16_t at = (uint16_t)(buffer + i * TILE_BYTES);
        if (tile.blank)
            put_blank_tile(g, at);
        else
            put_tile(g, graphics, tile, at);
    }
    uint16_t vram = (uint16_t)(((menu_record_tile(g, record) & TILE_NUMBER_BITS) << 4) | SPRITE_TILES_VRAM);
    set_menu_record_blocks_sent(g, (uint8_t)(menu_record_blocks_sent(g) + blocks));
    queue_frame(g, vram, WORK_RAM_BANK | buffer, blocks, menu_record_upload_spare(g));
}

/* What the original's frame building leaves: the block move's stub on the
 * graphics' bank; in the menus' first cells, the tile list's end, the
 * mirrored tile's byte count run out where it mirrored one, and its count of
 * the blocks left to queue (out where it queued them a block at a time); and
 * the last upload it queued, in the record it built them in. */
static bool any_mirrored(const FrameTiles *t)
{
    for (unsigned i = 0; i < t->count; i++) {
        FrameTile tile = decode_tile(t->tile[i]);
        if (!tile.blank && tile.mirrored)
            return true;
    }
    return false;
}

static void leave_frame_built(GameState *g, unsigned record, const FrameTiles *t)
{
    uint8_t blocks = menu_record_sprite_count(g, record);
    uint8_t bank = (uint8_t)(menu_record_graphics(g, record) >> 16);
    set_menu_block_move_stub(g, (uint32_t)STUB_RETURN << 24 | (uint32_t)bank << 16 | STUB_MOVE_TO_WORK_RAM);
    set_menu_scratch_frame_tiles_end(g, (uint16_t)((blocks & 0x0F) * BLOCK_TILE_LIST));
    if (any_mirrored(t))
        set_menu_scratch_frame_mirror_bytes(g, 0);
    set_menu_scratch_frame_blocks_left(g, (blocks & 1) ? 0 : blocks);
    unsigned last = (uint8_t)(menu_vram_queue_bytes(g) - MENU_UPLOAD_BYTES) / MENU_UPLOAD_BYTES;
    set_menu_record_upload_vram(g, menu_vram_queue_vram(g, last));
    set_menu_record_upload_source(g, (uint32_t)menu_vram_queue_bank(g, last) << 16 | menu_vram_queue_source(g, last));
    set_menu_record_upload_size(g, menu_vram_queue_size(g, last));
}

/* The original copies the frame's tile list over the save summary, where its
 * frame's building reads it back, having found the frame with the
 * multiplier. */
static void leave_frame_found(GameState *g, uint8_t frame, uint8_t frame_bytes, const FrameTiles *t)
{
    leave_multiplied(g, frame, frame_bytes);
    for (unsigned i = 0; i < t->count; i++)
        set_menu_record_frame_tile_list(g, i, t->tile[i]);
    charge_block_move(g, 2 * t->count);
}

/* Show a frame of the record's sprite: its sprites' offsets from the frame's
 * data, and its tiles built and queued. */
static void show_frame(GameState *g, unsigned record, uint8_t frame)
{
    uint8_t blocks = menu_record_sprite_count(g, record);
    uint8_t bytes = menu_sprite_frame_bytes(g, blocks);
    uint32_t list = in_bank(menu_record_frame_data(g, record), (uint16_t)(frame * bytes));
    uint16_t list_bytes = (uint16_t)(blocks * BLOCK_TILE_LIST), offset_bytes = (uint16_t)(blocks * 8);
    uint32_t offsets = in_bank(list, list_bytes);
    for (uint16_t i = 0; i < offset_bytes; i++)
        set_menu_record_sprite_offsets_byte(g, record, i, menu_sprite_byte(g, in_bank(offsets, i)));
    ORIGINAL_EFFECT(charge_block_move(g, offset_bytes));
    FrameTiles tiles = frame_tiles_at(g, list, blocks);
    ORIGINAL_EFFECT(leave_frame_found(g, frame, bytes, &tiles));
    TIMED_AS(g, 0xC2F75C, show_frame_tiles(g, record, &tiles));
    ORIGINAL_EFFECT(leave_frame_built(g, record, &tiles));
}

/* ---- A record's animation ---- */

/* An animation's frames for a facing: the sprite's animations one after
 * another, four frames each, the facings' lists one after another, each
 * `animation_count` frames long. */

/* Start the animation the record is asked for from its first frame: where
 * its frames and their times are for the record's facing. */
static void start_animation(GameState *g, unsigned record, uint8_t animation)
{
    set_menu_record_frame(g, record, 0);
    uint16_t first = (uint16_t)((animation & ANIMATION_BITS) * FRAMES_AN_ANIMATION);
    uint16_t facing = (uint16_t)(menu_record_facing(g, record) * menu_record_animation_count(g, record));
    uint16_t frames = (uint16_t)(menu_record_animation_frames_at(g, record) + first + facing);
    uint16_t times = (uint16_t)(menu_record_animation_times_at(g, record) + first);
    set_menu_record_frame_list(g, record, (menu_record_frame_list(g, record) & 0xFF0000u) | frames);
    set_menu_record_frame_times(g, record, (menu_record_frame_times(g, record) & 0xFF0000u) | times);
}

static uint8_t frame_time(const GameState *g, unsigned record, uint8_t frame)
{
    return menu_sprite_byte(g, menu_record_frame_times(g, record) + frame);
}

/* The frame the animation is at, with its time: past the last (a time of 0)
 * a looping animation goes back to its loop frame and another to its start;
 * an animation with no frames at all is left past its end, its frame before
 * its first. False then. */
enum { BEFORE_THE_FIRST_FRAME = 0xFF };

static bool frame_to_show(GameState *g, unsigned record, uint8_t *frame, uint8_t *time)
{
    uint8_t at = menu_record_frame(g, record);
    for (;;) {
        uint8_t t = frame_time(g, record, at);
        if (t != 0) {
            *frame = at;
            *time = t;
            return true;
        }
        if (menu_record_animation(g, record) & LOOPS)
            at = menu_record_loop_frame(g, record);
        else if (at != 0)
            at = 0;
        else {
            set_menu_record_frame(g, record, BEFORE_THE_FIRST_FRAME);
            return false;
        }
        set_menu_record_frame(g, record, at);
    }
}

// $C2:F69D
/* Carry on the record's animation, starting the one it is asked for where
 * that has changed: show the frame it is at, where it isn't showing already,
 * and count a frame of its time. */
static void animate_record(GameState *g, unsigned record)
{
    uint8_t animation = menu_record_animation(g, record);
    bool changed = animation != menu_record_animation_shown(g, record);
    set_menu_record_animation_shown(g, record, animation);
    if (changed)
        start_animation(g, record, animation);
    uint8_t at, time;
    if (!frame_to_show(g, record, &at, &time))
        return;
    uint8_t frame = menu_sprite_byte(g, menu_record_frame_list(g, record) + at);
    if (frame != menu_record_frame_shown(g, record)) {
        set_menu_record_frame_shown(g, record, frame);
        show_frame(g, record, frame);
    }
    set_menu_record_frame_time(g, record, (uint8_t)(time - 1));
}

// $C2:F69B
/* On to the record's next frame, and on with its animation. */
static void step_animation(GameState *g, unsigned record)
{
    set_menu_record_frame(g, record, (uint8_t)(menu_record_frame(g, record) + 1));
    animate_record(g, record);
}

/* ---- The records' task ---- */

/* The records' task: a pass over the nine records every frame, then the
 * frame's end. A record that shows a sprite and isn't put away carries on
 * its animation where its frame's time has run out (or it is asked for
 * another), unless the frame's uploads have reached their share; and one
 * that isn't hidden is drawn. The standing animation (0) holds its frame. */
enum { TASK_RECORDS = 9, RUN_OUT = 0x80 };

static bool shows_a_sprite(const GameState *g, unsigned record)
{
    return !(menu_record_sprite(g, record) & RUN_OUT) && !(menu_record_hidden(g, record) & RUN_OUT);
}

/* Whether the record's animation moves on this frame: a new one asked for,
 * or a frame of the shown one whose time runs out now. */
static bool animation_moves_on(GameState *g, unsigned record)
{
    uint8_t animation = menu_record_animation(g, record);
    if (animation == menu_record_animation_shown(g, record)) {
        if (animation == STANDING)
            return false;
        uint8_t left = (uint8_t)(menu_record_frame_time(g, record) - 1);
        set_menu_record_frame_time(g, record, left);
        if (!(left & RUN_OUT))
            return false;
    }
    return menu_record_blocks_sent(g) < menu_record_block_limit(g);
}

// $C2:F657
static void run_the_records(GameState *g)
{
    set_menu_record_blocks_sent(g, 0);
    set_menu_record_blocks_sent_high(g, 0);
    for (unsigned record = 0; record < TASK_RECORDS; record++) {
        if (!shows_a_sprite(g, record))
            continue;
        if (animation_moves_on(g, record))
            TIMED_AS(g, 0xC2F69B, step_animation(g, record));
        if (menu_record_hidden(g, record) == 0) {
            TIMED_AS(g, 0xC2F871, draw_record_sprites(g, record));
            ORIGINAL_EFFECT(leave_record_drawn(g, record));
        }
    }
}

/* ---- The records as the menus open ---- */

// $C2:F3CA
/* The opening's records: the first seven show the characters, one each, in
 * their standing animation facing the viewer; the eighth and ninth show
 * nothing. The loop counts the records in the time played's cell, which the
 * save screen's header takes for a time it has already drawn. */
enum { FACING_THE_VIEWER = 1, OPENING_RECORDS_SHOWING = MENU_OPENING_RECORDS, LAST_RECORD = 8 };

static void set_up_opening_record(GameState *g, unsigned record, uint16_t tile)
{
    set_menu_record_sprite(g, record, (uint8_t)record);
    set_menu_record_tile(g, record, tile);
    set_menu_record_animation(g, record, STANDING);
    set_menu_record_facing(g, record, FACING_THE_VIEWER);
}

static void count_record_loaded(GameState *g, uint8_t records) { set_menu_opening_records_loaded(g, records); }

static void empty_record(GameState *g, unsigned record)
{
    set_menu_record_sprite(g, record, NO_SPRITE);
    set_menu_record_animation(g, record, STANDING);
    set_menu_record_facing(g, record, FACING_THE_VIEWER);
}

static void empty_the_last_records(GameState *g)
{
    set_menu_record_tile(g, OPENING_RECORDS_SHOWING, menu_last_record_tile(g));
    empty_record(g, OPENING_RECORDS_SHOWING);
    empty_record(g, LAST_RECORD);
}

/* ---- The entries ------------------------------------------------------------------------------- */

/* The sprite-object mode runs on the menus' page with a data bank on low RAM
 * and its objects in their slots; the list is as its start laid it out, a node
 * or a head at every link it follows, with a node free where one is taken. */
static bool on_the_mode_page(Snes *s) { return m_direct_page(s) == 0x0000 && m_bank_reaches_low_ram(s); }

static bool running_object_is_one(const GameState *g) { return menu_object_address_names_one(menu_object_current(g)); }

static bool list_is_sound(const GameState *g)
{
    unsigned steps = 0;
    for (NodeLink l = DRAW_LIST;;) {
        l = next_of(g, l);
        if (l == DRAW_LIST)
            return true;
        if (!names_a_node(l) || !is_link(prev_of(g, l)) || ++steps > NODES)
            return false;
    }
}

static bool free_node_there(const GameState *g)
{
    NodeLink l = menu_sprite_free_list_first(g);
    return names_a_node(l) && is_link(prev_of(g, l)) && is_link(next_of(g, l));
}

static bool peek(Snes *s, bool (*check)(const GameState *))
{
    vars_peek(true);
    bool ok = check(state_of(s));
    vars_peek(false);
    return ok;
}

static bool can_show(const GameState *g) { return running_object_is_one(g) && list_is_sound(g) && free_node_there(g); }

bool menu_sprite_list_is_sound(const GameState *g) { return list_is_sound(g) && free_node_there(g); }

static bool links_are_laid_out(const GameState *g)
{
    for (unsigned n = 0; n < NODES; n++)
        if (!is_link(menu_sprite_node_links_prev(g, n)) || !is_link(menu_sprite_node_links_next(g, n)))
            return false;
    return true;
}

bool menu_sprites_on_page(Snes *s) { return on_the_mode_page(s); }
bool menu_sprite_list_takes_a_group(Snes *s) { return on_the_mode_page(s) && peek(s, can_show); }
bool menu_sprite_list_is_drawable(Snes *s) { return on_the_mode_page(s) && peek(s, list_is_sound); }
bool menu_sprite_list_is_resettable(Snes *s) { return on_the_mode_page(s) && peek(s, links_are_laid_out); }
static bool running_script_is_well_formed(const GameState *g)
{
    return menu_sprite_script_is_well_formed(g, menu_object_running(g));
}

bool menu_sprite_script_is_runnable(Snes *s)
{
    return on_the_mode_page(s) && peek(s, can_show) && peek(s, menu_object_queue_is_whole)
           && peek(s, running_script_is_well_formed);
}

bool menu_record_sprites_are_drawable(Snes *s)
{
    unsigned record;
    if (!menu_record_page_of(m_direct_page(s), &record) || !m_bank_reaches_low_ram(s))
        return false;
    vars_peek(true);
    bool ok = (menu_record_sprite_count(state_of(s), record) & 0x0F) <= 1;
    vars_peek(false);
    return ok;
}

/* The widths each leaves: 8-bit A and 16-bit index registers, or 16-bit A. */
static void a8_x16(Snes *s)
{
    m_rep(s, CPU_FLAG_X);
    m_sep(s, CPU_FLAG_M);
}

// $C2:034D
/* It narrows Y to a byte on the way (with 8-bit index registers), and its
 * loop's last subtraction borrows without overflowing: what the objects' run
 * that follows it in the vblank handler starts with. */
void menu_hide_all_sprites(Snes *s)
{
    hide_all_sprites(state_of(s));
    s->cpu.y &= 0xFF;
    s->cpu.p = (uint8_t)(s->cpu.p & ~(CPU_FLAG_C | CPU_FLAG_V));
    a8_x16(s);
    m_rts(s);
}

// $C2:0C4D
void menu_reset_sprite_nodes(Snes *s)
{
    GameState *g = state_of(s);
    empty_the_list(g);
    ORIGINAL_EFFECT(set_menu_object_scratch_reset_rounds(g, 0));
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:0B53
void menu_insert_sprite_node(Snes *s)
{
    GameState *g = state_of(s);
    show_sprite_group(g, menu_object_sprite_group(g));
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:0CEA
void menu_draw_sprite_nodes(Snes *s)
{
    draw_the_list(state_of(s));
    a8_x16(s);
    m_rts(s);
}

// $C2:0E1D
/* It leaves X on the running object's slot. */
void menu_run_sprite_script(Snes *s)
{
    GameState *g = state_of(s);
    run_sprite_script(g);
    a8_x16(s);
    s->cpu.x = menu_object_current(g);
    m_rts(s);
}

// $C2:F871
/* It leaves X past the offsets and Y past the table entries it drew, as its
 * index registers count them. */
void menu_draw_record_sprites(Snes *s)
{
    GameState *g = state_of(s);
    unsigned record = 0;
    (void)menu_record_page_of(m_direct_page(s), &record);
    draw_record_sprites(g, record);
    ORIGINAL_EFFECT(leave_record_drawn(g, record));
    vars_peek(true);
    unsigned sprites = record_sprites(g, record);
    vars_peek(false);
    uint16_t x = (uint16_t)(2 * sprites);
    uint16_t y = (uint16_t)(((m_direct_page(s) >> 2) & 0xFF) + 4 * sprites);
    s->cpu.x = m_x8(s) ? (uint8_t)x : x;
    s->cpu.y = m_x8(s) ? (uint8_t)y : y;
    m_rts(s);
}

/* ---- The records' entries ---- */

static unsigned record_on_page(const Snes *s)
{
    unsigned record = 0;
    (void)menu_record_page_of(m_direct_page(s), &record);
    return record;
}

/* The unpacker, called as the original's load calls it, with 16-bit
 * registers; it reads none of them (its entry saves them all). */
enum { UNPACK_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* The load's block moves leave the data bank on work RAM. */
static void leave_data_bank_on_work_ram(Snes *s) { s->cpu.db = MENU_DATA_BANK; }

// $C2:F422
void menu_load_record_sprite(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    Unpacking u = load_record_sprite(g, record_on_page(s));
    ORIGINAL_EFFECT(leave_data_bank_on_work_ram(s));
    if (u.unpack) {
        set_decompress_source(g, u.from);
        set_decompress_dest(g, WORK_RAM_BANK | u.to);
        menu_set_widths(s, false, false);
        if (!menu_call_long(s, 0xC2F542, 0xC30002, UNPACK_IGNORES))
            return;
    }
    menu_restore_p(s);
    m_rts(s);
}

/* The frame's tiles from where the original's frame leaves them (the tile
 * list over the save summary), for the entry: only the original's code
 * would call it, and none does now that the animation is C. */
static FrameTiles frame_tiles_left(const GameState *g, uint8_t blocks)
{
    FrameTiles t = { { 0 }, tiles_of(blocks) };
    for (unsigned i = 0; i < t.count; i++)
        t.tile[i] = menu_record_frame_tile_list(g, i);
    return t;
}

// $C2:F75C
void menu_show_record_frame(Snes *s)
{
    GameState *g = state_of(s);
    unsigned record = record_on_page(s);
    menu_save_p(s);
    FrameTiles tiles = frame_tiles_left(g, menu_record_sprite_count(g, record));
    show_frame_tiles(g, record, &tiles);
    ORIGINAL_EFFECT(leave_frame_built(g, record, &tiles));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:F69D
void menu_animate_record(Snes *s)
{
    animate_record(state_of(s), record_on_page(s));
    m_rts(s);
}

/* The task stops at its yield ($C2:F696), which the original runs, with a
 * 16-bit accumulator as its loop leaves it; the yield comes back to the
 * task's start, which sets its page again. */
enum { TASK_YIELD = 0xC2F696 };
enum { YIELD_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C | DC_D };

// $C2:F657
void menu_run_record_task(Snes *s)
{
    run_the_records(state_of(s));
    m_rep(s, CPU_FLAG_M);
    m_sep(s, CPU_FLAG_X);
    m_stop_at(s, TASK_YIELD, YIELD_IGNORES);
}

// $C2:F69B
void menu_step_record_animation(Snes *s)
{
    step_animation(state_of(s), record_on_page(s));
    m_rts(s);
}

/* The records' page, as the original's opening moves its direct page along
 * them. */
enum { FIRST_RECORD_PAGE = 0x1800, RECORD_PAGE_BYTES = 0x40 };

// $C2:F3CA
/* The records as the menus open. Each record's load runs as the original's
 * call reaches it, since it may call the unpacker; the animation's start is
 * C. */
void menu_load_record_sprites(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t page = m_direct_page(s);
    m_push16(s, page);
    menu_save_p(s);
    count_record_loaded(g, 0);
    for (unsigned record = 0; record < OPENING_RECORDS_SHOWING; record++) {
        s->cpu.d = (uint16_t)(FIRST_RECORD_PAGE + record * RECORD_PAGE_BYTES);
        set_up_opening_record(g, record, menu_opening_record_tile(g, record));
        menu_set_widths(s, true, true);
        if (!menu_run(s, 0xC2F3EE, 0xC2F422, menu_load_record_sprite))
            return;
        TIMED_AS(g, 0xC2F69D, animate_record(g, record));
        count_record_loaded(g, (uint8_t)(record + 1));
    }
    s->cpu.d = (uint16_t)(FIRST_RECORD_PAGE + OPENING_RECORDS_SHOWING * RECORD_PAGE_BYTES);
    empty_the_last_records(g);
    menu_restore_p(s);
    s->cpu.d = m_pull16(s);
    m_rts(s);
}

/* ---- The records' guards ---- */

/* What a record reads is the ROM or work RAM throughout the bank its
 * pointers name (a tile, a frame or an animation's place goes anywhere in
 * it), never a bank the I/O registers or open bus take part of. */
static bool readable(uint32_t at) { return ((uint8_t)(at >> 16) & 0x7F) >= 0x40; }

/* A frame's blocks go in the frame buffer clear of the block move's stub
 * ($7E:9690), above them; its sprites' offsets fit the record (three blocks
 * at most: the menus' sprites are one); the facing is one of the four. The
 * record's tiles start on an upper row of tiles in VRAM, where a frame's
 * blocks are laid out a row pair at a time. */
enum { FRAME_BUFFER_END = 0x9690 };

static bool blocks_fit(const GameState *g, uint8_t blocks, uint8_t blocks_before)
{
    uint16_t buffer = frame_buffer_for(g, blocks_before);
    return blocks >= 1 && blocks <= MOST_BLOCKS && buffer >= FRAME_BUFFER
           && (uint32_t)buffer + (uint32_t)blocks * BLOCK_BYTES <= FRAME_BUFFER_END;
}

static bool frame_fits(const GameState *g, unsigned record)
{
    return blocks_fit(g, menu_record_sprite_count(g, record), menu_record_blocks_sent(g))
           && !(menu_record_tile(g, record) & LOWER_TILE_ROW);
}

/* A looping animation's loop frame has a time, so that its end doesn't go
 * round for ever. */
static bool animation_ends(const GameState *g, unsigned record)
{
    return !(menu_record_animation(g, record) & LOOPS)
           || frame_time(g, record, menu_record_loop_frame(g, record)) != 0;
}

/* The VRAM queue counts whole records, as the queue's own entries ask. */
static bool queue_is_whole(const GameState *g) { return menu_vram_queue_bytes(g) % MENU_UPLOAD_BYTES == 0; }

/* A facing's frames: a sprite's load makes the count a 16-bit span over the
 * four facings, under $4000, so three facings' worth added to an animation's
 * first frame stays within a word; the records' own cells are kept so. */
enum { MOST_FRAMES_A_FACING = 0x4000 };

static bool record_is_animatable(const GameState *g, unsigned record)
{
    return queue_is_whole(g) && menu_record_facing(g, record) < FACINGS
           && menu_record_animation_count(g, record) < MOST_FRAMES_A_FACING && frame_fits(g, record)
           && readable(menu_record_frame_list(g, record)) && readable(menu_record_frame_times(g, record))
           && readable(menu_record_frame_data(g, record)) && readable(menu_record_graphics(g, record))
           && animation_ends(g, record);
}

static bool on_a_record_with_work_ram(Snes *s, unsigned *record)
{
    return menu_record_page_of(m_direct_page(s), record) && s->cpu.db == MENU_DATA_BANK;
}

bool menu_record_is_animatable(Snes *s)
{
    unsigned record;
    if (!on_a_record_with_work_ram(s, &record))
        return false;
    vars_peek(true);
    bool ok = record_is_animatable(state_of(s), record);
    vars_peek(false);
    return ok;
}

/* The frame building copies with the block move's stub as the animation
 * leaves it, writing only the bank it copies from. */
static bool stub_in_place(const GameState *g)
{
    uint32_t stub = menu_block_move_stub(g);
    return (uint16_t)stub == STUB_MOVE_TO_WORK_RAM && stub >> 24 == STUB_RETURN;
}

bool menu_record_frame_is_showable(Snes *s)
{
    unsigned record;
    if (!on_a_record_with_work_ram(s, &record))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = stub_in_place(g) && queue_is_whole(g) && frame_fits(g, record) && readable(menu_record_graphics(g, record));
    vars_peek(false);
    return ok;
}

/* A record's sprite is of one of the three sets there are. */
/* A pass of the records' task: every record it draws draws one sprite's
 * worth (four sprites at most, as the menus' records do), and every record
 * whose animation may move on this frame can be animated with the blocks
 * the records before it could queue. */
static bool task_pass_is_runnable(const GameState *g)
{
    if (!queue_is_whole(g))
        return false;
    uint8_t could_be_sent = 0;
    for (unsigned record = 0; record < TASK_RECORDS; record++) {
        if (!shows_a_sprite(g, record))
            continue;
        uint8_t animation = menu_record_animation(g, record);
        bool may_move = animation != menu_record_animation_shown(g, record)
                        || (animation != STANDING && ((uint8_t)(menu_record_frame_time(g, record) - 1) & RUN_OUT));
        if (may_move) {
            uint8_t blocks = menu_record_sprite_count(g, record);
            if (!(menu_record_facing(g, record) < FACINGS && menu_record_animation_count(g, record) < MOST_FRAMES_A_FACING
                  && blocks_fit(g, blocks, could_be_sent) && !(menu_record_tile(g, record) & LOWER_TILE_ROW)
                  && readable(menu_record_frame_list(g, record)) && readable(menu_record_frame_times(g, record))
                  && readable(menu_record_frame_data(g, record)) && readable(menu_record_graphics(g, record))
                  && animation_ends(g, record)))
                return false;
            could_be_sent = (uint8_t)(could_be_sent + blocks);
        }
        if (menu_record_hidden(g, record) == 0 && (menu_record_sprite_count(g, record) & 0x0F) > 1)
            return false;
    }
    return true;
}

bool menu_record_task_is_runnable(Snes *s)
{
    if (s->cpu.db != MENU_DATA_BANK)
        return false;
    vars_peek(true);
    bool ok = task_pass_is_runnable(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_record_sprite_is_loadable(Snes *s)
{
    unsigned record;
    if (!menu_record_page_of(m_direct_page(s), &record))
        return false;
    vars_peek(true);
    bool ok = menu_record_sprite_set(state_of(s), record) < SPRITE_SETS;
    vars_peek(false);
    return ok;
}

/* The opening's records: each sprite of one of the three sets, and of a size
 * whose frames fit, the frame buffer taking them all, since the first frame
 * of each shows. */
static bool opening_records_fit(const GameState *g)
{
    if (!queue_is_whole(g))
        return false;
    uint8_t sent = menu_record_blocks_sent(g);
    for (unsigned record = 0; record < OPENING_RECORDS_SHOWING; record++) {
        uint8_t set = menu_record_sprite_set(g, record);
        if (set >= SPRITE_SETS)
            return false;
        uint8_t blocks = menu_sprite_blocks(g, read_assignment(g, assignment_place(g, set, (uint8_t)record)).size);
        if (!blocks_fit(g, blocks, sent) || (menu_opening_record_tile(g, record) & LOWER_TILE_ROW))
            return false;
        sent = (uint8_t)(sent + blocks);
    }
    return true;
}

bool menu_record_sprites_are_loadable(Snes *s)
{
    if (m_direct_page(s) != MENU_PAGE || s->cpu.db != MENU_DATA_BANK)
        return false;
    vars_peek(true);
    bool ok = opening_records_fit(state_of(s));
    vars_peek(false);
    return ok;
}

/* The character in the caller's first cell; the fourth nonzero dims it. X is
 * the caller's, kept. */
// $FF:F628
void menu_put_character_colours_entry(Snes *s)
{
    GameState *g = state_of(s);
    menu_put_character_colours(g, menu_scratch_colours_character(g), menu_scratch_colours_dimmed(g) != 0);
    m_rtl(s);
}

/* The colours go in one of the seven characters' palettes, on the menus'
 * page with the data bank on work RAM's, and the change can be counted
 * (menu/windows.c). */
bool menu_character_colours_are_puttable(Snes *s)
{
    if (!menu_on_page_work_ram(s) || !menu_palette_change_counts(s))
        return false;
    vars_peek(true);
    bool ok = (menu_scratch_colours_character(state_of(s)) & CHARACTER_BITS) < CHARACTER_BITS;
    vars_peek(false);
    return ok;
}

/* ---- The records on a screen of the party's members ------------------------------------------ */

/* A screen of members shows the characters in its records, one each:
 * record c is character c's, still, kept hidden (but animating) until the
 * screen places it. A screen that shows other sprites empties them first. */
enum { CHARACTER_RECORDS = 7, SCREEN_RECORDS = 8, NO_SPRITE_SHOWN = 0xFF, STILL = 0, KEPT_HIDDEN = 0x40, SHOWN = 0 };

// $C2:F566
void menu_reset_character_records(GameState *g)
{
    for (unsigned record = 0; record < CHARACTER_RECORDS; record++) {
        set_menu_record_sprite(g, record, (uint8_t)record);
        set_menu_record_animation(g, record, STILL);
        set_menu_record_hidden(g, record, KEPT_HIDDEN);
    }
}

static void empty_records(GameState *g, unsigned records)
{
    for (unsigned record = 0; record < records; record++) {
        set_menu_record_sprite(g, record, NO_SPRITE_SHOWN);
        set_menu_record_animation(g, record, STILL);
        set_menu_record_hidden(g, record, SHOWN);
    }
}

// $C2:F588
void menu_empty_the_records(GameState *g) { empty_records(g, SCREEN_RECORDS); }

// $C2:CE2A
void menu_empty_the_character_records(GameState *g) { empty_records(g, CHARACTER_RECORDS); }

/* The records' task animates the records and draws their sprites each
 * frame ($C2:F657). */
enum { RUN_THE_RECORDS = 0xF657 };

// $C2:F643
void menu_start_the_records(GameState *g)
{
    set_menu_record_origin_x(g, 0);
    set_menu_record_origin_y(g, 0);
    set_menu_record_origin_y_high(g, 0);
    menu_start_task(g, MENU_RECORDS_TASK, RUN_THE_RECORDS);
}

/* For the original's code and shadow mode: the weapon whose colours it
 * looked up, and the multiplier left with the record found ($50 bytes a
 * record). */
enum { CHARACTER_RECORD_BYTES = 0x50 };

static void leave_weapon_looked_up(GameState *g, uint8_t character, uint8_t weapon)
{
    set_menu_weapon_looked_up(g, weapon);
    hw_set_multiplicand(g, CHARACTER_RECORD_BYTES);
    (void)hw_multiply_by(g, character);
}

// $C2:F5ED
MenuWeaponColours menu_weapon_colours(GameState *g, uint8_t character)
{
    uint8_t weapon = character_records_record_byte(g, character, menu_category_slot_place(g, MENU_WEAPONS));
    uint8_t set = menu_weapon_colour_set(g, weapon);
    MenuWeaponColours c;
    for (unsigned i = 0; i < MENU_WEAPON_COLOURS; i++)
        c.colour[i] = menu_weapon_colour(g, set, i);
    ORIGINAL_EFFECT(leave_weapon_looked_up(g, character, weapon));
    return c;
}

// $C2:F5A7
/* Every character's sprite colours in its own sprite palette: the twelve
 * its sprites have, then its weapon's three. */
void menu_load_character_colours(GameState *g)
{
    for (uint8_t character = 0; character < CHARACTER_RECORDS; character++) {
        unsigned first = SPRITE_PALETTES + (FIRST_CHARACTER_PALETTE + character) * PALETTE_COLOURS + 1;
        uint16_t from = menu_sprite_table_word(g, (uint16_t)(character * 2 + menu_sprite_colours_table(g)));
        for (unsigned i = 0; i < SPRITE_COLOURS; i++)
            set_menu_palette(g, first + i, sprite_colour(g, (uint16_t)(from + 2 * i)));
    }
    for (uint8_t character = 0; character < CHARACTER_RECORDS; character++) {
        unsigned first = SPRITE_PALETTES + (FIRST_CHARACTER_PALETTE + character) * PALETTE_COLOURS + 1 + SPRITE_COLOURS;
        MenuWeaponColours c = menu_weapon_colours(g, character);
        for (unsigned i = 0; i < MENU_WEAPON_COLOURS; i++)
            set_menu_palette(g, first + i, c.colour[i]);
    }
    ORIGINAL_EFFECT(charge_block_move(g, 2 * CHARACTER_RECORDS * (SPRITE_COLOURS + MENU_WEAPON_COLOURS)));
    ORIGINAL_EFFECT(set_menu_scratch_colours_loaded(g, CHARACTER_RECORDS));
}

// $C2:F626
void menu_leave_record_found(GameState *g, uint8_t character)
{
    enum { RECORD_BYTES = 0x40 };
    hw_set_multiplicand(g, character);
    set_menu_record_offset(g, hw_multiply_by(g, RECORD_BYTES));
}

/* ---- The screens' records' entries ----------------------------------------------------------- */

/* The emptying runs with 8-bit index registers, which leaves X's high byte
 * clear (its caller reads X). */
static void leave_x_a_byte(Snes *s) { s->cpu.x &= 0x00FF; }

// $C2:F566
void menu_reset_character_records_entry(Snes *s)
{
    menu_reset_character_records(state_of(s));
    m_rts(s);
}

// $C2:F588
void menu_empty_the_records_entry(Snes *s)
{
    menu_empty_the_records(state_of(s));
    ORIGINAL_EFFECT(leave_x_a_byte(s));
    m_rts(s);
}

// $C2:F643
void menu_start_the_records_entry(Snes *s)
{
    menu_start_the_records(state_of(s));
    m_rts(s);
}

// $C2:F5A7
void menu_load_character_colours_entry(Snes *s)
{
    menu_load_character_colours(state_of(s));
    m_rts(s);
}

/* The colours load on the menus' page with the data bank on work RAM's,
 * for the characters' records' weapons. */
bool menu_character_colours_are_loadable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = true;
    for (uint8_t character = 0; ok && character < CHARACTER_RECORDS; character++)
        ok = character_records_record_byte(state_of(s), character, menu_category_slot_place(state_of(s), MENU_WEAPONS)) < MENU_NO_ARMOUR;
    vars_peek(false);
    return ok;
}

