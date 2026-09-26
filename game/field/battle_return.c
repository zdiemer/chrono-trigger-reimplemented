/* Field mode: the field put back after a battle.
 *
 * A battle on the field keeps where each of the party and the enemies stood
 * (battle_party_placement, battle_enemy_placement: a tile on the screen, the
 * place within it, and the frame shown), and takes them over for its own
 * objects. When it ends the field puts them back: every enemy the battle
 * didn't take away, and every member of the party, goes back to its tile on
 * the map (the battle kept it from the screen's corner), into the rows the
 * sprite table walks, showing the frame it showed (its tiles in VRAM are as
 * they were, field/frames.c); an enemy the battle took away is taken off
 * the map. The sprite table is built at once, and where the party was
 * defeated its members hold their frames. */
#include "field/battle_return.h"
#include "field/field.h"
#include "field/frames.h"
#include "field/object.h"
#include "field/palettes.h"
#include "field/party.h"

#include "battle/battle.h"
#include "charge.h"
#include "field/bridge.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

enum { ENEMIES = FIELD_BATTLE_ENEMY_OBJECTS_COUNT, TILE_PIXELS = FIELD_MAP_TILE_PIXELS, SUBPIXELS = 16 };

/* The screen's top left corner in the map's tiles, which the battle kept the
 * places from. */
static MapTile screen_corner(const GameState *g)
{
    return (MapTile){ (uint8_t)(field_screen_left(g) / FIELD_ENTRIES_A_MAP_TILE),
                      (uint8_t)(field_screen_top(g) / FIELD_ENTRIES_A_MAP_TILE) };
}

/* The routines whose work this runs, each timed as its own. */
enum { LEAVE_ROW = 0xC0A98A, JOIN_ROW = 0xC0A9CD, PUT_FRAME_BACK = 0xC0CB0A, GIVE_UP_PALETTE = FIELD_GIVE_UP_PALETTE_ENTRY };

/* Where the battle kept an object: a tile from the screen's corner, the
 * pixel within it, and the frame it showed. */
typedef struct {
    uint8_t tile_x, tile_y, x_offset, y_offset, frame;
} KeptPlace;

static KeptPlace kept_party_place(const GameState *g, unsigned place)
{
    return (KeptPlace){ battle_party_placement_tile_x(g, place), battle_party_placement_tile_y(g, place),
                        battle_party_placement_x_offset(g, place), battle_party_placement_y_offset(g, place),
                        battle_party_placement_frame(g, place) };
}

static KeptPlace kept_enemy_place(const GameState *g, unsigned enemy)
{
    return (KeptPlace){ battle_enemy_placement_tile_x(g, enemy), battle_enemy_placement_tile_y(g, enemy),
                        battle_enemy_placement_x_offset(g, enemy), battle_enemy_placement_y_offset(g, enemy),
                        battle_enemy_placement_frame(g, enemy) };
}

/* For the original's code and shadow mode: the last kept place's row, in
 * pixels, as the original worked out its screen Y. */
static void leave_last_row(GameState *g, KeptPlace k)
{
    original_cell8(g, FIELD_LAST_WORKED_CELL, (uint8_t)(k.tile_y * TILE_PIXELS));
}

// $C0:07EC (the kept enemy's), $C0:08A0 (a member's)
/* An object back where the battle kept it: on its tile of the map at the
 * pixel within it, on the screen there, its animation's frame time over,
 * in the rows again, and showing its frame. */
static void put_back(GameState *g, FieldObject obj, KeptPlace k)
{
    MapTile corner = screen_corner(g);
    FieldPosition at = { (uint16_t)((uint8_t)(k.tile_x + corner.x) << 8 | (uint8_t)(k.x_offset * SUBPIXELS)),
                         (uint16_t)((uint8_t)(k.tile_y + corner.y) << 8 | (uint8_t)(k.y_offset * SUBPIXELS)) };
    object_place(g, obj, at);
    object_set_frame_drawn(g, obj, k.frame);
    object_put_on_screen_as_kept(g, obj, (uint8_t)(k.tile_x * TILE_PIXELS | k.x_offset),
                                 (uint8_t)(k.tile_y * TILE_PIXELS | k.y_offset));
    object_end_frame_time(g, obj);
    ORIGINAL_EFFECT(leave_last_row(g, k));
    object_make_current(g, obj);
    TIMED_AS(g, JOIN_ROW, object_join_row(g, obj));
    TIMED_AS(g, PUT_FRAME_BACK, object_put_drawn_frame_back(g, obj));
}

/* The kind the battle keeps for an enemy it took away (battle_enemy_placement's
 * kind: 0, no graphics). */
enum { TAKEN_AWAY_KIND = 0 };

static bool taken_away(const GameState *g, unsigned enemy)
{
    return battle_enemy_placement_kind(g, enemy) == TAKEN_AWAY_KIND;
}

/* An enemy the battle kept goes back where it stood. One the battle took away
 * is out of reach of the scripts' calls and its sprite hidden; one without a
 * sprite first gives up its palette slot (field/palettes.c). */
static void bring_back_enemy(GameState *g, unsigned enemy, FieldObject obj)
{
    object_make_current(g, obj);
    TIMED_AS(g, LEAVE_ROW, object_leave_row(g, obj));
    if (!taken_away(g, enemy)) {
        put_back(g, obj, kept_enemy_place(g, enemy));
        return;
    }
    object_set_unreachable(g, obj, true);
    if (object_has_no_sprite(g, obj))
        TIMED_AS(g, GIVE_UP_PALETTE, object_give_up_palette(g, obj));
    object_set_sprite(g, obj, SPRITE_HIDDEN);
}

/* Each member of the party goes back where it stood. */
static void put_party_back(GameState *g)
{
    for (unsigned place = 0; place < PARTY_PLACES; place++) {
        FieldLink link = party_member_link(g, place);
        if (!field_link_is_object(link))
            continue;
        FieldObject member = field_linked_object(link);
        object_make_current(g, member);
        TIMED_AS(g, LEAVE_ROW, object_leave_row(g, member));
        put_back(g, member, kept_party_place(g, place));
    }
}

/* For the original's code and shadow mode: the screen's corner in tiles, in
 * the tile the direction routine works from (field_own_tile), where the
 * original works the places out. */
static void leave_corner(GameState *g)
{
    MapTile corner = screen_corner(g);
    set_field_own_tile_x(g, corner.x);
    set_field_own_tile_y(g, corner.y);
}

/* The enemies and the party back on the field as the battle kept them. */
static void put_back_after_battle(GameState *g)
{
    ORIGINAL_EFFECT(leave_corner(g));
    for (unsigned enemy = 0; enemy < ENEMIES; enemy++) {
        FieldLink link = field_link_stored(field_battle_enemy_objects(g, enemy));
        if (field_link_is_object(link))
            bring_back_enemy(g, enemy, field_linked_object(link));
    }
    put_party_back(g);
}

/* Where the party was defeated, each member holds the frame it shows, as it
 * lies beaten on the field. */
static void party_hold_frames_if_defeated(GameState *g)
{
    if (battle_how_it_ended(g) != BATTLE_DEFEATED)
        return;
    for (unsigned place = 0; place < PARTY_PLACES; place++) {
        FieldLink link = party_member_link(g, place);
        if (field_link_is_object(link))
            object_hold_frame(g, field_linked_object(link), object_frame_drawn(g, field_linked_object(link)));
    }
}

/* ---- The entries ---------------------------------------------------------------------------- */

enum { BUILD_SPRITE_TABLE = 0xC0B271, READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

// $C0:0715
/* The field's page, 8-bit A. Nothing the original leaves in the registers
 * is read after it. */
void field_return_from_battle(Snes *s)
{
    GameState *g = state_of(s);
    put_back_after_battle(g);
    m_rep(s, CPU_FLAG_X);
    if (!bridge_call(s, 0xC0072B, BUILD_SPRITE_TABLE, READS_NOTHING, on_the_field_page(s, false)))
        return;
    party_hold_frames_if_defeated(g);
    m_rts(s);
}

// $C0:072E
/* The return's end on its own, where the original comes back from building
 * the sprite table (when the guard below sent the return to the original):
 * the field's page, 8-bit A, 16-bit X. */
void field_hold_party_if_defeated(Snes *s)
{
    party_hold_frames_if_defeated(state_of(s));
    m_rts(s);
}

/* The guard: on the field's page with the data bank $00; the leader's place
 * an object (the original takes it without looking, so the party's loop
 * needn't); the current object's word clear above its link, which the frame
 * put-back's layouts read whole (field/frames.c); each object it brings back
 * one the rows take as the port does and whose drawn frame can go back; and
 * each enemy the battle took away without a sprite, a palette slot the
 * tables hold. */
static bool can_bring_back(const GameState *g, FieldObject obj)
{
    return field_rows_are_exact(g, obj) && object_drawn_frame_can_go_back(g, obj);
}

bool field_battle_return_can_run(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = object_making_current_recognises(g) && field_link_is_object(party_member_link(g, PARTY_LEADER));
    for (unsigned place = 0; ok && place < PARTY_PLACES; place++) {
        FieldLink link = party_member_link(g, place);
        ok = !field_link_is_object(link) || can_bring_back(g, field_linked_object(link));
    }
    for (unsigned enemy = 0; ok && enemy < ENEMIES; enemy++) {
        FieldLink link = field_link_stored(field_battle_enemy_objects(g, enemy));
        ok = !field_link_is_object(link) || can_bring_back(g, field_linked_object(link));
        if (ok && field_link_is_object(link) && taken_away(g, enemy) && object_has_no_sprite(g, field_linked_object(link)))
            ok = object_palette_links_in_the_tables(g, field_linked_object(link));
    }
    vars_peek(false);
    return ok;
}
