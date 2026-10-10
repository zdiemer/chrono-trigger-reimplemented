/* Field mode: a battle's start on the field.
 *
 * A battle is fought where the party stands: it takes the field's screen as
 * it is and the enemies standing on it. When the main loop answers a battle
 * (field/requests.c), or a screen effect hands the field over to one
 * (field/screen_effects.c), the enemies on the screen are found first; with
 * none the party only comes back together, and no battle is fought.
 * Otherwise the field hands the battle what it fights on:
 * - the ground: the map's tile attributes under the screen, sixteen tiles by
 *   sixteen from the screen's corner, and the palette as loaded for the one on
 *   the screen;
 * - the party and the enemies: where each stands on the screen (a tile from
 *   the screen's corner and the pixel within it) and the frame it shows, the
 *   party's sprites in front of the map; and for each enemy the graphics,
 *   palette and kind the battle draws it with, and the object it is, which
 *   the battle's end puts back (field/battle_return.c);
 * - the screen: the layers' tile maps and scroll, the screens, color math and
 *   tint, and its mode.
 * Then a dialog under way is closed, the field's state is put aside as for a
 * menu (field/location.c), and the party's place to come back to is where the
 * leader stands. */
#include "field/battle_start.h"
#include "field/dialog_window.h"
#include "field/field.h"
#include "field/layers.h"
#include "field/main_loop.h"
#include "field/object.h"
#include "field/palettes.h"
#include "field/party.h"

#include "charge.h"
#include "cpu_io.h"
#include "event/party.h"
#include "field/bridge.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The enemies on the screen ($C0:0617) --------------------------------------------------- */

/* The battle takes in the enemies standing on the screen, inside its
 * edges: from one tile in from the left to fourteen, and from one tile down
 * from the top to thirteen; at most twelve, the last objects first. */
enum { MOST_ENEMIES_FOUND = FIELD_BATTLE_ENEMIES_COUNT - 1, ACROSS_THE_SCREEN = 0x0F, DOWN_THE_SCREEN = 0x0E };

/* The screen's top left corner in the map's tiles. */
static MapTile screen_corner(const GameState *g)
{
    return (MapTile){ (uint8_t)(field_screen_left(g) / FIELD_ENTRIES_A_MAP_TILE),
                      (uint8_t)(field_screen_top(g) / FIELD_ENTRIES_A_MAP_TILE) };
}

static bool an_enemy(const GameState *g, FieldObject obj)
{
    ObjectRole r = object_role(g, obj);
    return !r.disabled && (r.kind == ROLE_ENEMY || r.kind == ROLE_ENEMY_SET_APART);
}

/* An object without a sprite counts as there, an enemy waiting to jump out;
 * any other, where its view isn't out (a row's link over the flag counts). */
static bool there(const GameState *g, FieldObject obj)
{
    return object_has_no_sprite(g, obj) || object_view(g, obj) != OUT_OF_VIEW;
}

static bool inside(uint8_t from_the_corner, uint8_t edge) { return from_the_corner != 0 && from_the_corner < edge; }

static bool on_the_battlefield(const GameState *g, FieldObject obj, MapTile corner)
{
    MapTile at = object_tile(g, obj);
    return inside((uint8_t)(at.x - corner.x), ACROSS_THE_SCREEN) && inside((uint8_t)(at.y - corner.y), DOWN_THE_SCREEN);
}

/* For the original's code and shadow mode: the screen's corner, in two of
 * the page's scratch cells, where the original keeps it as it works; the
 * ground's copy keeps it masked there, with its row count run down to 0. */
enum { CORNER_X_CELL = 0xDB, CORNER_Y_CELL = 0xDD };
static void leave_corner(GameState *g, MapTile corner)
{
    original_cell8(g, CORNER_X_CELL, corner.x);
    original_cell8(g, CORNER_Y_CELL, corner.y);
}
static void leave_the_ground(GameState *g, uint8_t left, uint8_t top)
{
    leave_corner(g, (MapTile){ left, top });
    original_cell16(g, FIELD_LAST_WORKED_CELL, 0);
}

// $C0:0617
/* The objects the search looks at: from the object past the map's last to
 * the first after the map's own (the original's count). */
static void find_the_enemies(GameState *g)
{
    MapTile corner = screen_corner(g);
    unsigned found = 0;
    for (FieldObject obj = event_object_count(g); obj != 0 && found < MOST_ENEMIES_FOUND; obj--) {
        if (there(g, obj) && an_enemy(g, obj) && on_the_battlefield(g, obj, corner))
            set_field_battle_enemies(g, found++, field_link_byte(field_link_to(obj)));
    }
    set_field_battle_enemies(g, found, field_link_byte(field_no_link()));
    ORIGINAL_EFFECT(leave_corner(g, corner));
}

static bool enemies_found(const GameState *g) { return field_link_is_object(field_link_stored(field_battle_enemies(g, 0))); }

/* ---- The ground ($C0:039B) ------------------------------------------------------------------- */

/* The battlefield's ground: sixteen of the map's tiles by sixteen from the
 * screen's corner (kept inside the map's size by its mask), each tile's two
 * attribute bytes. The original copies sixteen bytes a row, whatever lies
 * there: from a corner near the map's right edge a row runs on into the
 * table beside it, and near its bottom edge the rows run on past the
 * tables' last; the battle reads them as its ground all the same. */
enum { GROUND_TILES = 16, MASK_ROW_SHIFT = 8 };

// $C0:039B
static void keep_the_ground(GameState *g)
{
    MapTile corner = screen_corner(g);
    uint16_t mask = field_map_index_mask(g);
    uint8_t left = (uint8_t)(corner.x & mask), top = (uint8_t)(corner.y & mask >> MASK_ROW_SHIFT);
    for (unsigned row = 0; row < GROUND_TILES; row++)
        for (unsigned column = 0; column < GROUND_TILES; column++) {
            unsigned cell = row * GROUND_TILES + column;
            set_battle_location_upper_attributes(g, cell, field_tile_upper_attributes_unchecked(g, top + row, left + column));
            set_battle_location_lower_attributes(g, cell, field_tile_lower_attributes_unchecked(g, top + row, left + column));
        }
    for (unsigned colour = 0; colour < PALETTE_WORKING_COUNT; colour++)
        set_palette_working(g, colour, palette_base(g, colour));
    ORIGINAL_EFFECT(leave_the_ground(g, left, top));
}

/* ---- The party and the enemies ($C0:041A, $C0:04ED) ------------------------------------------- */

/* Where an object stands for the battle: its tile from the screen's corner,
 * and the pixel within the tile it is drawn at. */
enum { WITHIN_A_TILE = FIELD_MAP_TILE_PIXELS - 1 };
typedef struct {
    uint8_t tile_x, tile_y, x_offset, y_offset;
} BattlePlace;

static BattlePlace battle_place(const GameState *g, FieldObject obj, MapTile corner)
{
    MapTile at = object_tile(g, obj);
    ScreenPoint drawn = object_screen_position(g, obj);
    return (BattlePlace){ (uint8_t)(at.x - corner.x), (uint8_t)(at.y - corner.y), (uint8_t)(drawn.x & WITHIN_A_TILE),
                          (uint8_t)(drawn.y & WITHIN_A_TILE) };
}

/* In the battle the party's sprites stand in front of the map's layers: at
 * the battle's priority, raised one where the battle asks (its flags' bit 4). */
enum { PARTY_PRIORITY = 2, PRIORITY_RAISED = 0x10, BATTLE_RAISES_THE_PARTY = 0x10 };

static uint8_t party_priority_byte(const GameState *g)
{
    return (uint8_t)((battle_setup_flags(g) & BATTLE_RAISES_THE_PARTY ? PRIORITY_RAISED : 0) | PARTY_PRIORITY << 4);
}

// $C0:041A
static void keep_where_the_party_stands(GameState *g)
{
    MapTile corner = screen_corner(g);
    ORIGINAL_EFFECT(leave_corner(g, corner));
    uint8_t priority = party_priority_byte(g);
    for (PartyPlace place = 0; place < PARTY_PLACES; place++) {
        FieldLink link = party_member_link(g, place);
        if (!field_link_is_object(link))
            continue;
        FieldObject member = field_linked_object(link);
        BattlePlace p = battle_place(g, member, corner);
        set_battle_party_placement_tile_x(g, place, p.tile_x);
        set_battle_party_placement_tile_y(g, place, p.tile_y);
        set_battle_party_placement_x_offset(g, place, p.x_offset);
        set_battle_party_placement_y_offset(g, place, p.y_offset);
        set_battle_party_placement_frame(g, place, object_frame_drawn(g, member));
        object_take_sprite_priorities(g, member, (SpritePriorities){ priority, priority });
    }
}

/* An enemy's place in the battle, from where its sprite tiles are in VRAM:
 * the enemies' blocks start at tile $40, two to a place but the first, and
 * the field's own blocks past them ($C0 on) belong to no place. */
enum { ENEMY_TILES_FROM = 0x40, ENEMY_TILES_END = 0xC0, ENEMY_PLACES = FIELD_BATTLE_ENEMY_OBJECTS_COUNT,
       TILES_A_BLOCK = 8 };

static bool has_a_battle_place(uint8_t tiles) { return tiles < ENEMY_TILES_END; }
static unsigned battle_place_of_tiles(uint8_t tiles)
{
    unsigned block = (unsigned)(tiles - ENEMY_TILES_FROM) / TILES_A_BLOCK;
    return (block + 1) / 2;
}

/* How the battle draws an enemy (battle_enemy_placement's kind): its sprite
 * layout, the set-apart enemy marked, and one without a sprite hidden until
 * it shows itself. Only an enemy drawn with one of the three smaller layouts
 * starts on the frame it shows. */
enum { KIND_SET_APART = 0x40, KIND_HIDDEN = 0x80 };

static uint8_t battle_kind(const GameState *g, FieldObject obj)
{
    uint8_t kind = (uint8_t)object_sprite_layout(g, obj);
    if (object_role(g, obj).kind == ROLE_ENEMY_SET_APART)
        kind |= KIND_SET_APART;
    return (uint8_t)(kind | (object_has_no_sprite(g, obj) ? KIND_HIDDEN : 0));
}

/* For the original's code and shadow mode: what it worked out last for an
 * enemy, in the cells it keeps it in, and its place on the multiplier. */
enum { PLACE_BYTES = 12, KIND_CELL = 0xEE };
static void leave_enemy(GameState *g, FieldObject obj, unsigned place)
{
    hw_set_multiplicand(g, (uint8_t)place);
    (void)hw_multiply_by(g, PLACE_BYTES);
    original_cell8(g, FIELD_LAST_WORKED_CELL, (uint8_t)object_sprite_layout(g, obj));
    set_field_direction(g, (uint16_t)((field_direction(g) & 0xFF00) | (uint8_t)(battle_kind(g, obj) & ~KIND_HIDDEN)));
}

// $C0:0557
static void keep_the_enemy(GameState *g, FieldObject obj, MapTile corner)
{
    uint8_t tiles = (uint8_t)field_object_sprite_tiles(g, obj);
    if (!has_a_battle_place(tiles))
        return;
    unsigned place = battle_place_of_tiles(tiles);
    set_field_battle_enemy_objects(g, place, field_link_byte(field_link_to(obj)));
    BattlePlace p = battle_place(g, obj, corner);
    set_battle_enemy_placement_tile_x(g, place, p.tile_x);
    set_battle_enemy_placement_tile_y(g, place, p.tile_y);
    set_battle_enemy_placement_x_offset(g, place, p.x_offset);
    set_battle_enemy_placement_y_offset(g, place, p.y_offset);
    set_battle_enemy_placement_enemy(g, place, object_character(g, obj));
    set_battle_enemy_placement_kind(g, place, battle_kind(g, obj));
    set_battle_enemy_placement_frame(g, place,
                                     object_sprite_layout(g, obj) != LAYOUT_24 ? object_frame_drawn(g, obj) : 0);
    if (object_has_no_sprite(g, obj)) {
        object_make_current(g, obj);
        TIMED_AS(g, FIELD_TAKE_ENEMY_PALETTE_ENTRY, (void)object_take_enemy_palette_slot(g, obj));
    }
    set_battle_enemy_placement_palette(g, place, (uint8_t)(field_object_palette(g, obj) >> 1));
    ObjectGraphics graphics = object_graphics(g, obj);
    set_battle_enemy_placement_graphics(g, place, (uint32_t)graphics.bank << 16 | graphics.address);
    ORIGINAL_EFFECT(leave_enemy(g, obj, place));
}

/* The first six enemies found are the battle's (the list stops at the first
 * place with none). A place without one has every bit of its enemy and its
 * object set. */
enum { BATTLE_ENEMIES = 6, NO_ENEMY = 0xFFFF, NO_ENEMY_OBJECT = 0xFF };

// $C0:04ED
static void keep_the_enemies(GameState *g)
{
    MapTile corner = screen_corner(g);
    for (unsigned place = 0; place < ENEMY_PLACES; place++) {
        set_battle_enemy_placement_enemy(g, place, NO_ENEMY);
        set_field_battle_enemy_objects(g, place, NO_ENEMY_OBJECT);
    }
    ORIGINAL_EFFECT(set_field_own_tile_x(g, corner.x));
    ORIGINAL_EFFECT(set_field_own_tile_y(g, corner.y));
    for (unsigned i = 0; i < BATTLE_ENEMIES; i++) {
        FieldLink link = field_link_stored(field_battle_enemies(g, i));
        if (!field_link_is_object(link))
            break;
        keep_the_enemy(g, field_linked_object(link), corner);
    }
}

/* ---- The screen ($C0:066D) -------------------------------------------------------------------- */

/* The battle starts on the field's screen: each layer's tile map, BG4 none;
 * the main and sub screens with no window, the sub screen added (color
 * math's control) to the layers the field adds it to; the field's tint; mode
 * 1 with BG3 in front and no mosaic; and the layers' scroll, which the
 * battle also starts its own from. */
enum { NO_TILE_MAP = 0, NO_WINDOW = 0, NO_MOSAIC = 0, UNUSED = 0 };

static void hand_over_the_screen(GameState *g)
{
    for (FieldLayer layer = FIELD_BG1; layer < FIELD_LAYERS; layer++)
        set_battle_start_tile_maps(g, layer, field_layer_map_setting(g, layer));
    set_battle_start_tile_maps(g, FIELD_LAYERS, NO_TILE_MAP);
    set_battle_start_screens_main(g, field_main_screen(g));
    set_battle_start_screens_sub(g, field_sub_screen(g));
    set_battle_start_screens_main_window_mask(g, NO_WINDOW);
    set_battle_start_screens_sub_window_mask(g, NO_WINDOW);
    set_battle_start_screens_math_control(g, field_starting_color_math());
    set_battle_start_screens_math_layers(g, field_color_math_layers(g));
    set_battle_start_screens_fixed_color(g, field_fixed_color(g));
    set_battle_start_screens_unused(g, UNUSED);
    set_battle_start_screens_mode(g, vid_mode_encode(&(VidMode){ .mode = 1, .bg3_priority = true }));
    set_battle_start_screens_mosaic(g, NO_MOSAIC);
    for (FieldLayer layer = FIELD_BG1; layer < FIELD_LAYERS; layer++) {
        uint16_t x = field_layer_scroll_h(g, layer), y = field_layer_scroll_v(g, layer);
        set_battle_start_scroll_x(g, layer, x);
        set_battle_start_scroll_y(g, layer, y);
        set_battle_bg_scroll_x(g, layer, x);
        set_battle_bg_scroll_y(g, layer, y);
    }
}

// $C0:038F
void field_hand_over_to_a_battle(GameState *g)
{
    keep_the_ground(g);
    keep_where_the_party_stands(g);
    keep_the_enemies(g);
    hand_over_the_screen(g);
}

/* ---- The entry ------------------------------------------------------------------------------- */

/* With no enemy on the screen the party comes back together, the original's
 * routine (event/party.c's port) running a frame of the leader's walk, which
 * only a call can run; nothing the routine leaves in the registers is read. */
enum { PARTY_BACK_TOGETHER = 0xC0034B, READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

// $C0:024C
/* The field's page, 8-bit A and 16-bit index registers. The carry says
 * whether no battle is fought; nothing else it leaves is read. */
void field_battle_begins(Snes *s)
{
    GameState *g = state_of(s);
    find_the_enemies(g);
    if (!enemies_found(g)) {
        if (!bridge_call(s, 0xC0027E, PARTY_BACK_TOGETHER, READS_NOTHING, on_the_field_page(s, false)))
            return;
        m_set_flag(s, CPU_FLAG_C, true);
        m_rts(s);
        return;
    }
    dialog_shut(g);
    field_hand_over_to_a_battle(g);
    field_put_the_state_aside(g);
    field_party_place_is_the_leaders(g);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* The guard: on the field's page with the data bank $00; the party's places
 * objects or none, the leader's an object whose word is clear above its link
 * (the original reads its place through the word); the map's object count
 * inside the tables (the search starts past its last object); and every
 * enemy of the map whose sprite tiles have a place in the battle with one of
 * the eight (the original works the place out of where the tiles are, and
 * would reach past the places for others). */
static bool tiles_have_a_place(uint8_t tiles)
{
    return !has_a_battle_place(tiles) || (tiles >= ENEMY_TILES_FROM && battle_place_of_tiles(tiles) < ENEMY_PLACES);
}

bool field_battle_can_begin(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = field_party_objects_high(g, PARTY_LEADER) == 0 &&
              field_link_names_object(party_member_link(g, PARTY_LEADER)) && event_object_count(g) < FIELD_OBJECTS;
    for (PartyPlace place = PARTY_FIRST_MEMBER; ok && place < PARTY_PLACES; place++) {
        FieldLink link = party_member_link(g, place);
        ok = !field_link_is_object(link) || field_link_names_object(link);
    }
    for (FieldObject obj = 1; ok && obj <= event_object_count(g); obj++)
        ok = !an_enemy(g, obj) || tiles_have_a_place((uint8_t)field_object_sprite_tiles(g, obj));
    vars_peek(false);
    return ok;
}
