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
#include "field/dialog_window.h"
#include "field/map.h"
#include "field/frames.h"
#include "field/object.h"
#include "field/palettes.h"
#include "field/party.h"

#include "battle/battle.h"
#include "charge.h"
#include "core/console.h"
#include "core/display.h"
#include "core/input.h"
#include "field/battle_start.h"
#include "field/main_loop.h"
#include "field/sprites.h"
#include "field/walk.h"
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

/* The sprites a battle shows at the start of the table, which the field
 * hides as it comes back. */
enum { BATTLE_SPRITES_SHOWN = 4 };

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

/* ---- After the battle ($C0:0283) ----------------------------------------------------------- */

/* How the game goes on after a battle: on the field, which a battle the party
 * may lose (the event's flags' bit 5) does even when it was beaten; the game
 * over, where it may not; or, for the result only a script sets (2), back to
 * the place put aside (event $DC), unless the event's flags say otherwise.
 * That test the original makes against the byte at $0010 rather than a bit
 * (its neighbour tests bit 5 as #$20), and the port keeps it. */
typedef enum { GO_ON_ON_THE_FIELD, GAME_OVER, BACK_TO_THE_PLACE_PUT_ASIDE } AfterBattle;
enum { LOSING_GOES_ON = 0x20, RESULT_BACK_TO_THE_PLACE_PUT_ASIDE = 2 };

static AfterBattle how_the_game_goes_on(const GameState *g)
{
    uint8_t flags = battle_event_flags(g);
    switch (battle_result(g)) {
    case BATTLE_DEFEATED: return flags & LOSING_GOES_ON ? GO_ON_ON_THE_FIELD : GAME_OVER;
    case RESULT_BACK_TO_THE_PLACE_PUT_ASIDE:
        return flags & field_after_battle_test_byte(g) ? GO_ON_ON_THE_FIELD : BACK_TO_THE_PLACE_PUT_ASIDE;
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
};
enum { READS_THE_OVERFLOW = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

static bool call(Snes *s, uint32_t site, uint32_t routine)
{
    return bridge_call(s, site, routine, READS_NOTHING, on_the_field_page(s, false));
}
static bool walk_a_frame(Snes *s, uint32_t site) { return call(s, site, FIELD_WALK_A_FRAME_ENTRY); }

/* A button pressed, the game's or an assignment's. */
static bool a_button_pressed(const GameState *g)
{
    return (uint8_t)input_mapped(g, ASSIGNED_PRESSED) != 0 || (uint8_t)input_pressed(g) != 0;
}

/* The picture fading out a step a frame, the leader walking and the palettes
 * animating, from each way's own calls. */
typedef enum { GAME_OVER_FADE, PLACE_PUT_ASIDE_FADE } FadeOut;
static const struct {
    uint32_t walk, palettes, wait;
} fade_sites[] = {
    [GAME_OVER_FADE] = { 0xC002B7, 0xC002BA, 0xC002BE },
    [PLACE_PUT_ASIDE_FADE] = { 0xC002DE, 0xC002E1, 0xC002E5 },
};

static bool fade_out(Snes *s, FadeOut way)
{
    GameState *g = state_of(s);
    while (field_dims_a_step(g))
        if (!field_run(s, fade_sites[way].walk, WALK, READS_NOTHING, field_walk_the_leader, on_the_field_page(s, false)) ||
            !bridge_call_long(s, fade_sites[way].palettes, PALETTE_ANIMATION, READS_NOTHING, on_the_field_page(s, false)) ||
            !call(s, fade_sites[way].wait, FIELD_FRAME_WAIT))
            return false;
    return true;
}

/* Where the game starts again: the field's first start (the title) after a
 * game over, the field's entry for the place put aside; each on the game's
 * fresh stack. What each reads of the registers first it sets. */
enum { THE_TITLE = 0xC0000E, ENTER = 0xC0005D, STARTS_AFRESH = DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C | DC_D };

static void start_afresh(Snes *s, uint32_t at)
{
    display_go_dark(s);
    s->cpu.s = CONSOLE_STACK_TOP;
    m_stop_at_own(s, at, STARTS_AFRESH);
}

// $C0:02A0
/* The game over: the leader walks on its own until a button is pressed, the
 * picture fades out, and the game starts again from the title. */
static void game_over(Snes *s)
{
    while (!a_button_pressed(state_of(s)))
        if (!walk_a_frame(s, 0xC002AA))
            return;
    if (fade_out(s, GAME_OVER_FADE))
        start_afresh(s, THE_TITLE);
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
static void back_to_the_place_put_aside(Snes *s)
{
    if (!fade_out(s, PLACE_PUT_ASIDE_FADE))
        return;
    party_goes_to_the_place_put_aside(state_of(s));
    start_afresh(s, ENTER);
}

/* The field taken up again on its map, a frame at a time: the party and the
 * enemies put back (field_return_from_battle), the map, the dialog window's
 * tile maps and the tile looks again, the script and its start, the layers;
 * then the party comes back together ($C0:034B, event/party.c), which the
 * original goes on to. */
enum { PARTY_BACK_TOGETHER = 0xC0034B, BACK_TOGETHER_READS = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

// $C0:0304
static void go_on_on_the_field(Snes *s)
{
    GameState *g = state_of(s);
    sprites_hide_all_next_frame(g);
    if (!field_run(s, 0xC00313, 0xC00715, READS_NOTHING, field_return_from_battle, on_the_field_page(s, false)) ||
        !field_run(s, 0xC00316, FIELD_UNPACK_THE_MAP_ENTRY, READS_NOTHING, field_unpack_the_map, on_the_field_page(s, false)) ||
        !walk_a_frame(s, 0xC00319) ||
        !field_run_long(s, 0xC0031C, FIELD_BUILD_WINDOW_FRAME_ENTRY, READS_NOTHING, field_build_the_window_frame_entry,
                        on_the_field_page(s, false)))
        return;
    field_send_screen_jobs(g, FIELD_SEND_STAGED_MAP_FIRST | FIELD_SEND_FOUR_TILES);
    if (!field_run(s, 0xC00324, TILE_LOOKS, READS_NOTHING, field_load_tile_looks, on_the_field_page(s, false)) ||
        !walk_a_frame(s, 0xC00327) ||
        !field_run(s, 0xC0032A, BG3_TILE_LOOKS, READS_NOTHING, field_load_bg3_tile_looks, on_the_field_page(s, false)) ||
        !call(s, 0xC0032D, SCRIPT_UNPACKED) ||
        !bridge_call(s, 0xC00330, MAP_SCRIPT_STARTS, READS_THE_OVERFLOW, on_the_field_page(s, false)) ||
        !field_run_on_page(s, 0xC0033C, CHESTS_MARK_THE_MAP_ENTRY, READS_NOTHING, field_mark_the_chests,
                           FIELD_LAYERS_PAGE) ||
        !walk_a_frame(s, 0xC00340) ||
        !field_run_long(s, 0xC00343, FIELD_BUILD_TEXT_AREA_ENTRY, READS_NOTHING, field_build_the_text_area_entry,
                        on_the_field_page(s, false)))
        return;
    field_send_screen_jobs(g, FIELD_SEND_STAGED_MAP_SECOND);
    m_stop_at(s, PARTY_BACK_TOGETHER, BACK_TOGETHER_READS);
}

/* For shadow mode: the original moves the state back with a block move,
 * which leaves Y past the block's end; the field's first start after a game
 * over hands Y to the menus' routine, which stores it. */
enum { STATE_BLOCK_END = (RAM_FIELD_RIPPLE_FRAMES_LEFT + 1) & 0xFFFF };
static void leave_y_past_the_state(Snes *s) { s->cpu.y = STATE_BLOCK_END; }

// $C0:0283
/* The field's page, 8-bit A and 16-bit index registers: the battle's sprites
 * hidden, the field's state back from where the battle's start put it, and
 * the game goes on as the battle ended. */
void field_after_battle(Snes *s)
{
    GameState *g = state_of(s);
    sprites_hide_the_first(g, BATTLE_SPRITES_SHOWN);
    field_bring_the_state_back(g);
    ORIGINAL_EFFECT(leave_y_past_the_state(s));
    switch (how_the_game_goes_on(g)) {
    case GAME_OVER: game_over(s); break;
    case BACK_TO_THE_PLACE_PUT_ASIDE: back_to_the_place_put_aside(s); break;
    default: go_on_on_the_field(s); break;
    }
}

/* The guard: on the field's page with the data bank $00. */
bool field_can_take_up_after_battle(Snes *s) { return field_on_page_bank_zero(s); }

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
