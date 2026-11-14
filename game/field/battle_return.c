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
 * defeated its members hold their frames.
 *
 * Before that, the field's state comes back from where the battle's start put
 * it (field/battle_start.c), and the game goes on as the battle ended: on the
 * field, taken up again a frame at a time; or with the game over, the leader
 * walking until a button is pressed, then the title; or, for a result a
 * script sets, back at the place put aside. */
#include "field/battle_return.h"
#include "field/field.h"
#include "event/chests.h"
#include "event/engine.h"
#include "event/party.h"
#include "field/dialog_window.h"
#include "field/map.h"
#include "field/frames.h"
#include "field/object.h"
#include "field/palettes.h"
#include "field/party.h"

#include "battle/battle.h"
#include "charge.h"
#include "core/display.h"
#include "core/program.h"
#include "core/input.h"
#include "core/palette.h"
#include "field/battle_start.h"
#include "field/main_loop.h"
#include "field/sprites.h"
#include "field/walk.h"
#include "field/bridge.h"
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
enum {
    LEAVE_ROW = 0xC0A98A,
    JOIN_ROW = 0xC0A9CD,
    PUT_FRAME_BACK = 0xC0CB0A,
    GIVE_UP_PALETTE = FIELD_GIVE_UP_PALETTE_ENTRY,
};

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

/* The enemies and the party back on the field as the battle kept them. */
static void put_back_after_battle(GameState *g)
{
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

/* ---- The party and the enemies put back ($C0:0715) --------------------------------------------- */

/* The sprites a battle shows at the start of the table, which the field
 * hides as it comes back. */
enum { BATTLE_SPRITES_SHOWN = 4 };

// $C0:0715
/* The enemies and the party put back, the sprite table built at once (a
 * routine of its own, refused where the port doesn't build it), and a
 * beaten party holding its frames. */
enum { BUILD_SPRITE_TABLE = 0xC0B271 };

static bool return_from_battle(GameState *g)
{
    put_back_after_battle(g);
    bool built = false;
    TIMED_AS(g, BUILD_SPRITE_TABLE, built = field_sprite_table_built(g));
    if (!built) {
        program_breaks_a_contract(g, BUILD_SPRITE_TABLE, "a sprite table the port doesn't build");
        return false;
    }
    party_hold_frames_if_defeated(g);
    return true;
}

/* ---- After the battle ($C0:0283) ----------------------------------------------------------- */

/* How the game goes on after a battle: on the field, which a battle the party
 * may lose (the battle's terms) does even when it was beaten; the game over,
 * where it may not; or, for the result only a script sets, back to the place
 * put aside (event $DC), unless the event's flags say otherwise. That test the
 * original makes against the byte at $0010 rather than a bit (its neighbour
 * tests the defeat's term as #$20), and the port keeps it. */
typedef enum { GO_ON_ON_THE_FIELD, GAME_OVER, BACK_TO_THE_PLACE_PUT_ASIDE } AfterBattle;

static AfterBattle how_the_game_goes_on(const GameState *g)
{
    switch (battle_how_it_ended(g)) {
    case BATTLE_DEFEATED: return battle_terms(g, BATTLE_TERMS_DEFEAT_GOES_ON) ? GO_ON_ON_THE_FIELD : GAME_OVER;
    case BATTLE_BACK_TO_THE_PLACE_PUT_ASIDE:
        return battle_event_flags(g) & field_after_battle_test_byte(g) ? GO_ON_ON_THE_FIELD : BACK_TO_THE_PLACE_PUT_ASIDE;
    default: return GO_ON_ON_THE_FIELD;
    }
}

/* The original's routines it calls, and the field's own that run frames. */
enum {
    SCRIPT_UNPACKED = 0xC056D4,     /* the location's event script unpacked */
    MAP_SCRIPT_STARTS = 0xC0595C,   /* the map's own script (object 0) run to its first return */
    PALETTE_ANIMATION = 0xFDFFF7,   /* the original's jump to it */
    WALK = 0xC0881E,
    TILE_LOOKS = 0xC009DD, BG3_TILE_LOOKS = 0xC00A14,
    RETURN_FROM_BATTLE = 0xC00715,
};

/* Where the field is taken up again, the party comes back together
 * ($C0:034B), which the original goes on to. */
enum { PARTY_BACK_TOGETHER = 0xC0034B };

/* A button pressed, the game's or an assignment's. */
static bool a_button_pressed(const GameState *g)
{
    return (uint8_t)input_mapped(g, ASSIGNED_PRESSED) != 0 || (uint8_t)input_pressed(g) != 0;
}

/* The picture fading out a step a frame, the leader walking ($C0:00EB's
 * walk) and the palettes animating, and the frame's wait. */
static bool fade_out(GameState *g, Platform *pf)
{
    while (field_dims_a_step(g)) {
        bool done = false;
        TIMED_AS(g, WALK, done = field_the_leader_walks(g));
        if (!done)
            return false;
        TIMED_AS(g, PALETTE_ANIMATION, done = palette_animated(g));
        if (!done)
            return false;
        TIMED_AS(g, FIELD_FRAME_WAIT, field_finish_the_frame(g, pf));
    }
    return true;
}

/* The game over and the place put aside start the field afresh, which is the
 * program's to do: the picture blanked first. */
static void blank_and_stop(GameState *g, Platform *pf)
{
    TIMED_AS(g, DISPLAY_BLANK_AND_STOP_ENTRY, display_blank_and_stop(pf));
}

// $C0:02A0
/* The game over: the leader walks on its own until a button is pressed, the
 * picture fades out, and the game starts again from the title. */
static bool game_over(GameState *g, Platform *pf)
{
    while (!a_button_pressed(g))
        if (!field_walked_a_frame(g, pf))
            return false;
    if (!fade_out(g, pf))
        return false;
    blank_and_stop(g, pf);
    program_starts_afresh_from_the_title(g);
    return false;
}

/* The party goes back to the place put aside (event $DC's), where the field
 * starts again. */
static void party_goes_to_the_place_put_aside(GameState *g)
{
    set_location_id(g, location_stored_id(g));
    set_location_x(g, location_stored_x(g));
    set_location_y(g, location_stored_y(g));
    set_location_flags(g, location_stored_flags(g));
}

// $C0:02CD
static bool back_to_the_place_put_aside(GameState *g, Platform *pf)
{
    if (!fade_out(g, pf))
        return false;
    party_goes_to_the_place_put_aside(g);
    blank_and_stop(g, pf);
    program_starts_the_field_again(g);
    return false;
}

/* What the port puts back after a battle: the leader's place
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

static bool battle_return_fits(const GameState *g)
{
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
    return ok;
}

/* The party and the enemies put back (above), refused where the port
 * doesn't take the objects; the map unpacked; the script unpacked (refused
 * where it doesn't fit) and the map's own script run to its first return. */
static bool put_the_fight_back(GameState *g)
{
    if (!PORT_CHECK(battle_return_fits(g))) {
        program_breaks_a_contract(g, RETURN_FROM_BATTLE, "a party or enemies the port doesn't put back");
        return false;
    }
    bool done = false;
    TIMED_AS(g, RETURN_FROM_BATTLE, done = return_from_battle(g));
    return done;
}

static bool unpack_the_map(GameState *g)
{
    bool done = false;
    TIMED_AS(g, FIELD_UNPACK_THE_MAP_ENTRY, done = field_load_the_map(g));
    return done;
}

static bool load_the_script(GameState *g)
{
    bool done = false;
    TIMED_AS(g, SCRIPT_UNPACKED, done = field_load_the_script(g));
    if (!done)
        program_breaks_a_contract(g, SCRIPT_UNPACKED, "a location's script that doesn't fit");
    return done;
}

static bool map_script(GameState *g, Platform *pf)
{
    uint16_t at;
    EventTurnEnd end = TURN_OVER;
    TIMED_AS(g, MAP_SCRIPT_STARTS, end = event_run_map_script_commands(g, pf, &at));
    if (end == TURN_OVER)
        return true;
    if (end != TURN_HANDED_BACK)
        program_breaks_a_contract(g, MAP_SCRIPT_STARTS, "a map's script the event engine hands to the original");
    return false;
}

// $C0:0304
/* The field taken up again on its map, a frame at a time: the party and the
 * enemies put back, the map, the dialog window's tile maps and the tile
 * looks again, the script and its start, the chests, the text area; then the
 * party comes back together (event/party.c), refused where it is not one
 * the port brings back. */
static bool go_on_on_the_field(GameState *g, Platform *pf)
{
    sprites_hide_all_next_frame(g);
    if (!put_the_fight_back(g) || !unpack_the_map(g) || !field_walked_a_frame(g, pf))
        return false;
    TIMED_AS(g, FIELD_BUILD_WINDOW_FRAME_ENTRY, field_build_the_window_frame(g));
    field_send_screen_jobs(g, FIELD_SEND_STAGED_MAP_FIRST | FIELD_SEND_FOUR_TILES);
    TIMED_AS(g, TILE_LOOKS, field_load_tile_looks(g));
    if (!field_walked_a_frame(g, pf))
        return false;
    TIMED_AS(g, BG3_TILE_LOOKS, field_load_bg3_tile_looks(g));
    if (!load_the_script(g) || !map_script(g, pf))
        return false;
    TIMED_AS(g, CHESTS_MARK_THE_MAP_ENTRY, chests_mark_the_map(g));
    if (!field_walked_a_frame(g, pf))
        return false;
    TIMED_AS(g, FIELD_BUILD_TEXT_AREA_ENTRY, field_build_the_text_area(g));
    field_send_screen_jobs(g, FIELD_SEND_STAGED_MAP_SECOND);
    if (!PORT_CHECK(party_can_come_back_together(g))) {
        program_breaks_a_contract(g, PARTY_BACK_TOGETHER, "a party the port doesn't bring back together");
        return false;
    }
    TIMED_AS(g, PARTY_BACK_TOGETHER, party_back_together(g));
    return true;
}

// $C0:0283
/* The battle's sprites hidden, the field's state back from where the
 * battle's start put it, and the game goes on as the battle ended. */
bool field_taken_up_after_battle(GameState *g, Platform *pf)
{
    sprites_hide_the_first(g, BATTLE_SPRITES_SHOWN);
    field_bring_the_state_back(g);
    switch (how_the_game_goes_on(g)) {
    case GAME_OVER: return game_over(g, pf);
    case BACK_TO_THE_PLACE_PUT_ASIDE: return back_to_the_place_put_aside(g, pf);
    default: return go_on_on_the_field(g, pf);
    }
}
