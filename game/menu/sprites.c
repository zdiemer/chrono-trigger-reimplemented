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
 * Both modes hide the sprites they aren't using (menu_hide_all_sprites). */
#include "menu/sprites.h"

#include "menu/menu_data.h"
#include "menu/object.h"
#include "menu/objects.h"
#include "menu/vram.h"
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

/* The look's bits a node's place in the list and its drawing follow, and the
 * tiles'. */
enum { IN_FRONT = 0x01, ON_THE_MAP = 0x80, LOOK_PALETTE_AND_PRIORITY = 0x3E };
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
    uint16_t attributes = (uint16_t)((look & LOOK_PALETTE_AND_PRIORITY) << 8);
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
    do {
        int16_t sx = (int16_t)(x + (int8_t)group_byte(g, p, 0));
        bool x_on = (uint16_t)(sx + SPRITE_REACH) < SCREEN_WIDTH + SPRITE_REACH;
        int16_t sy = 0;
        bool shown = false;
        if (x_on) {
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
        }
        ORIGINAL_EFFECT(note_sprite(sx, sy, x_on, shown));
        p.at = (uint16_t)(p.at + 4);
    } while (--left != 0);
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

/* The commands of a sprite script, and how long each is. */
enum {
    SPRITE_CLEAR_BYTE, SPRITE_COUNT_UP, SPRITE_COUNT_DOWN, SPRITE_JUMP, SPRITE_SHOW,
    SPRITE_WAIT, SPRITE_UPLOAD, SPRITE_STOP, SPRITE_COMMANDS,
};
/* The look's bit that holds an object on the frame it shows. */
enum { HELD = 0x40 };

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

// $C2:0EA3
/* Show a group for a number of frames, or for as long as the object is held
 * on it; once the frames are out, on to the next command instead. */
static SpriteStep show(GameState *g, MenuObject o, SpritePlace p)
{
    enum { LENGTH = 4 };
    if (!(menu_object_slots_look(g, o) & HELD) && !counting(g, o, sprite_script_byte(g, p, 3)))
        return LENGTH;
    uint32_t group = (uint32_t)p.bank << 16 | sprite_script_word(g, p, 1);
    ORIGINAL_EFFECT(set_menu_object_sprite_group(g, group));
    show_sprite_group(g, group);
    return SPRITE_STAY;
}

// $C2:0EDC
static SpriteStep wait(GameState *g, MenuObject o, SpritePlace p)
{
    enum { LENGTH = 2 };
    return counting(g, o, sprite_script_byte(g, p, 1)) ? SPRITE_STAY : LENGTH;
}

// $C2:0F00
/* Queue an upload of the object's tiles: bank, source, VRAM word and size. */
static SpriteStep upload(GameState *g, SpritePlace p)
{
    enum { LENGTH = 8 };
    uint32_t source = (uint32_t)sprite_script_byte(g, p, 1) << 16 | sprite_script_word(g, p, 2);
    menu_queue_object_upload(g, source, sprite_script_word(g, p, 4), sprite_script_word(g, p, 6));
    return LENGTH;
}

/* A byte of the object's state by its offset, which a byte can name past the
 * slot's own 64 into the slots after (menu/object.h). */
static SpriteStep change_byte(GameState *g, MenuObject o, SpritePlace p, uint8_t command)
{
    enum { LENGTH = 2 };
    uint8_t offset = sprite_script_byte(g, p, 1);
    uint8_t now = command == SPRITE_CLEAR_BYTE ? 0 : menu_object_byte(g, o, offset);
    if (command == SPRITE_COUNT_UP)
        now++;
    else if (command == SPRITE_COUNT_DOWN)
        now--;
    menu_set_object_byte(g, o, offset, now);
    return LENGTH;
}

typedef enum { SPRITES_GO_ON, SPRITES_STOPPED } SpriteEnd;

// $C2:0E1D
/* Run the running object's sprite script until it shows a group, waits or
 * stops, and keep where it got to for next frame. A command past the
 * language's eight stops the script here; the original jumps through the
 * words after its table, into code. The game's sprite scripts have none, and
 * only fuzzing with a sprite script pointer anywhere reaches one (divergence
 * accepted; docs/RECOMP.md, the menus' Phase 3a section). */
static void run_sprite_script(GameState *g)
{
    MenuObject o = menu_object_running(g);
    uint32_t script = menu_object_slots_sprite_script(g, o);
    SpritePlace p = { (uint8_t)(script >> 16), (uint16_t)script };
    for (;;) {
        uint8_t command = sprite_script_byte(g, p, 0);
        SpriteStep step;
        switch (command) {
        case SPRITE_CLEAR_BYTE: case SPRITE_COUNT_UP: case SPRITE_COUNT_DOWN:
            step = change_byte(g, o, p, command);
            break;
        case SPRITE_JUMP: step = (int8_t)sprite_script_byte(g, p, 1); break;
        case SPRITE_SHOW: step = show(g, o, p); break;
        case SPRITE_WAIT: step = wait(g, o, p); break;
        case SPRITE_UPLOAD: step = upload(g, p); break;
        default: step = SPRITE_STAY; break;   /* SPRITE_STOP, and past it (below) */
        }
        if (step == SPRITE_STAY)
            break;
        p.at = (uint16_t)(p.at + step);
    }
    ORIGINAL_EFFECT(set_menu_object_sprite_script(g, (uint32_t)p.bank << 16 | p.at));
    set_menu_object_slots_sprite_script_at(g, o, p.at);
}

void menu_object_animate(GameState *g) { run_sprite_script(g); }

/* Whether an object's sprite script is one the language reads: every command
 * it can reach from where it is is one of the eight, before it stops or goes
 * round. The game's scripts all are; the original jumps through the words
 * after its table on any other. A show or a wait may go on to the next
 * command, so the walk goes on past each. */
bool menu_sprite_script_is_well_formed(const GameState *g, MenuObject o)
{
    enum { WALK = 64 };
    static const uint8_t lengths[SPRITE_COMMANDS] = { 2, 2, 2, 0, 4, 2, 8, 0 };
    uint32_t script = menu_object_slots_sprite_script(g, o);
    SpritePlace p = { (uint8_t)(script >> 16), (uint16_t)script };
    for (unsigned step = 0; step < WALK; step++) {
        uint8_t command = sprite_script_byte(g, p, 0);
        if (command >= SPRITE_COMMANDS)
            return false;
        if (command == SPRITE_STOP)
            return true;
        int8_t by = command == SPRITE_JUMP ? (int8_t)sprite_script_byte(g, p, 1) : (int8_t)lengths[command];
        if (by == 0)
            return true;   /* a jump to itself: it goes round */
        p.at = (uint16_t)(p.at + by);
    }
    return true;   /* round and round within the walk's reach */
}

// $C2:1CDA
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

static uint8_t next_record_tile(uint8_t tile) { return (uint8_t)(((tile | 0x10) + 2) & ~0x10); }

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
