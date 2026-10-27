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
#include "event/event.h"
#include "event/party.h"
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
#include "core/program.h"
#include "core/input.h"
#include "core/palette.h"
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

/* ---- The party and the enemies put back ($C0:0715) --------------------------------------------- */

/* The sprites a battle shows at the start of the table, which the field
 * hides as it comes back. */
enum { BATTLE_SPRITES_SHOWN = 4 };

// $C0:0715
/* The enemies and the party put back, the sprite table built at once (a
 * routine of its own, `table`), and a beaten party holding its frames. */
static bool return_from_battle(GameState *g, bool (*table)(void *ctx), void *ctx)
{
    put_back_after_battle(g);
    if (!table(ctx))
        return false;
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

/* The after battle's calls, each where the original makes it: a frame of the
 * leader's walk alone ($C0:00EB), the fade out's walk, palettes and frame
 * wait, the field's pieces as it is taken up again -- the party and the
 * enemies put back (above), the map unpacked, the dialog window's tile maps,
 * the tile looks, the script and its start, the chests, the text area -- the
 * party back together (event/party.c), and the starts afresh the game over
 * and the place put aside make. */
/* The fade out's two ways: the game over's and the place put aside's. */
typedef enum { GAME_OVER_FADE, PLACE_PUT_ASIDE_FADE } FadeOut;

typedef enum {
    AFTER_GAME_OVER_WALK, AFTER_FADE_WALK, AFTER_FADE_PALETTES, AFTER_FADE_WAIT,
    AFTER_PUT_BACK, AFTER_UNPACK_THE_MAP, AFTER_FIRST_WALK, AFTER_WINDOW_FRAME, AFTER_TILE_LOOKS, AFTER_SECOND_WALK,
    AFTER_BG3_TILE_LOOKS, AFTER_SCRIPT, AFTER_MAP_SCRIPT, AFTER_CHESTS, AFTER_THIRD_WALK, AFTER_TEXT_AREA,
    AFTER_BACK_TOGETHER, AFTER_THE_TITLE, AFTER_THE_FIELD_AGAIN,
} AfterCall;
typedef bool (*AfterCalls)(void *ctx, AfterCall call, FadeOut way);

/* A button pressed, the game's or an assignment's. */
static bool a_button_pressed(const GameState *g)
{
    return (uint8_t)input_mapped(g, ASSIGNED_PRESSED) != 0 || (uint8_t)input_pressed(g) != 0;
}

/* The picture fading out a step a frame, the leader walking and the palettes
 * animating. */
static bool fade_out(GameState *g, FadeOut way, AfterCalls call, void *ctx)
{
    while (field_dims_a_step(g))
        if (!call(ctx, AFTER_FADE_WALK, way) || !call(ctx, AFTER_FADE_PALETTES, way) || !call(ctx, AFTER_FADE_WAIT, way))
            return false;
    return true;
}

// $C0:02A0
/* The game over: the leader walks on its own until a button is pressed, the
 * picture fades out, and the game starts again from the title. */
static bool game_over(GameState *g, AfterCalls call, void *ctx)
{
    while (!a_button_pressed(g))
        if (!call(ctx, AFTER_GAME_OVER_WALK, GAME_OVER_FADE))
            return false;
    return fade_out(g, GAME_OVER_FADE, call, ctx) && call(ctx, AFTER_THE_TITLE, GAME_OVER_FADE);
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
static bool back_to_the_place_put_aside(GameState *g, AfterCalls call, void *ctx)
{
    if (!fade_out(g, PLACE_PUT_ASIDE_FADE, call, ctx))
        return false;
    party_goes_to_the_place_put_aside(g);
    return call(ctx, AFTER_THE_FIELD_AGAIN, PLACE_PUT_ASIDE_FADE);
}

static bool after(AfterCalls call, void *ctx, AfterCall what) { return call(ctx, what, GAME_OVER_FADE); }

// $C0:0304
/* The field taken up again on its map, a frame at a time: the party and the
 * enemies put back, the map, the dialog window's tile maps and the tile
 * looks again, the script and its start, the layers; then the party comes
 * back together. */
static bool go_on_on_the_field(GameState *g, AfterCalls call, void *ctx)
{
    sprites_hide_all_next_frame(g);
    if (!after(call, ctx, AFTER_PUT_BACK) || !after(call, ctx, AFTER_UNPACK_THE_MAP) || !after(call, ctx, AFTER_FIRST_WALK)
        || !after(call, ctx, AFTER_WINDOW_FRAME))
        return false;
    field_send_screen_jobs(g, FIELD_SEND_STAGED_MAP_FIRST | FIELD_SEND_FOUR_TILES);
    if (!after(call, ctx, AFTER_TILE_LOOKS) || !after(call, ctx, AFTER_SECOND_WALK) || !after(call, ctx, AFTER_BG3_TILE_LOOKS)
        || !after(call, ctx, AFTER_SCRIPT) || !after(call, ctx, AFTER_MAP_SCRIPT) || !after(call, ctx, AFTER_CHESTS)
        || !after(call, ctx, AFTER_THIRD_WALK) || !after(call, ctx, AFTER_TEXT_AREA))
        return false;
    field_send_screen_jobs(g, FIELD_SEND_STAGED_MAP_SECOND);
    return after(call, ctx, AFTER_BACK_TOGETHER);
}

// $C0:0283
/* The battle's sprites hidden, the field's state back from where the
 * battle's start put it, and the game goes on as the battle ended. */
static bool take_up_after_battle(GameState *g, AfterCalls call, void *ctx)
{
    sprites_hide_the_first(g, BATTLE_SPRITES_SHOWN);
    field_bring_the_state_back(g);
    switch (how_the_game_goes_on(g)) {
    case GAME_OVER: return game_over(g, call, ctx);
    case BACK_TO_THE_PLACE_PUT_ASIDE: return back_to_the_place_put_aside(g, call, ctx);
    default: return go_on_on_the_field(g, call, ctx);
    }
}

/* ---- On the machine ------------------------------------------------------------------------ */

enum { BUILD_SPRITE_TABLE = 0xC0B271, READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static bool table_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    m_rep(s, CPU_FLAG_X);
    return bridge_call(s, 0xC0072B, BUILD_SPRITE_TABLE, READS_NOTHING, on_the_field_page(s, false));
}

// $C0:0715
/* The field's page, 8-bit A. Nothing the original leaves in the registers
 * is read after it. */
void field_return_from_battle(Snes *s)
{
    if (return_from_battle(state_of(s), table_on_the_machine, s))
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

/* The original's routines it calls, and the field's own that run frames. */
enum {
    SCRIPT_UNPACKED = 0xC056D4,     /* the location's event script unpacked */
    MAP_SCRIPT_STARTS = 0xC0595C,   /* the map's own script (object 0) run to its first return */
    PALETTE_ANIMATION = 0xFDFFF7,   /* the original's jump to it */
    WALK = 0xC0881E,
    TILE_LOOKS = 0xC009DD, BG3_TILE_LOOKS = 0xC00A14,
    RETURN_FROM_BATTLE = 0xC00715,
};
enum { READS_THE_OVERFLOW = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

/* Where the game starts again: the field's first start (the title) after a
 * game over, the field's entry for the place put aside; each on the game's
 * fresh stack. What each reads of the registers first it sets. Where the
 * field is taken up again, the party comes back together ($C0:034B), which
 * the original goes on to. */
enum { THE_TITLE = 0xC0000E, ENTER = 0xC0005D, STARTS_AFRESH = DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C | DC_D };
enum { PARTY_BACK_TOGETHER = 0xC0034B, BACK_TOGETHER_READS = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static void start_afresh(Snes *s, uint32_t at)
{
    display_go_dark(s);
    s->cpu.s = CONSOLE_STACK_TOP;
    m_stop_at_own(s, at, STARTS_AFRESH);
}

/* The fade out's calls, from each way's own. */
static const uint32_t fade_walks[] = { 0xC002B7, 0xC002DE };
static const uint32_t fade_palettes[] = { 0xC002BA, 0xC002E1 };
static const uint32_t fade_waits[] = { 0xC002BE, 0xC002E5 };
/* The walks a frame of the field's taking up makes. */
static const uint32_t take_up_walks[] = { 0xC00319, 0xC00327, 0xC00340 };

static bool after_on_the_machine(void *ctx, AfterCall call, FadeOut way)
{
    Snes *s = ctx;
    BridgeMachine page = on_the_field_page(s, false);
    switch (call) {
    case AFTER_GAME_OVER_WALK: return bridge_call(s, 0xC002AA, FIELD_WALK_A_FRAME_ENTRY, READS_NOTHING, page);
    case AFTER_FADE_WALK: return field_run(s, fade_walks[way], WALK, READS_NOTHING, field_walk_the_leader, page);
    case AFTER_FADE_PALETTES: return field_run_long(s, fade_palettes[way], PALETTE_ANIMATION, READS_NOTHING, palette_animate, page);
    case AFTER_FADE_WAIT: return bridge_call(s, fade_waits[way], FIELD_FRAME_WAIT, READS_NOTHING, page);
    case AFTER_PUT_BACK: return field_run(s, 0xC00313, RETURN_FROM_BATTLE, READS_NOTHING, field_return_from_battle, page);
    case AFTER_UNPACK_THE_MAP:
        return field_run(s, 0xC00316, FIELD_UNPACK_THE_MAP_ENTRY, READS_NOTHING, field_unpack_the_map, page);
    case AFTER_FIRST_WALK:
    case AFTER_SECOND_WALK:
    case AFTER_THIRD_WALK:
        return bridge_call(s, take_up_walks[call == AFTER_FIRST_WALK ? 0 : call == AFTER_SECOND_WALK ? 1 : 2],
                           FIELD_WALK_A_FRAME_ENTRY, READS_NOTHING, page);
    case AFTER_WINDOW_FRAME:
        return field_run_long(s, 0xC0031C, FIELD_BUILD_WINDOW_FRAME_ENTRY, READS_NOTHING, field_build_the_window_frame_entry, page);
    case AFTER_TILE_LOOKS: return field_run(s, 0xC00324, TILE_LOOKS, READS_NOTHING, field_load_tile_looks_entry, page);
    case AFTER_BG3_TILE_LOOKS: return field_run(s, 0xC0032A, BG3_TILE_LOOKS, READS_NOTHING, field_load_bg3_tile_looks_entry, page);
    case AFTER_SCRIPT: return field_run(s, 0xC0032D, SCRIPT_UNPACKED, READS_NOTHING, field_load_the_script_entry, page);
    case AFTER_MAP_SCRIPT: return field_run(s, 0xC00330, MAP_SCRIPT_STARTS, READS_THE_OVERFLOW, event_run_map_script, page);
    case AFTER_CHESTS:
        return field_run_on_page(s, 0xC0033C, CHESTS_MARK_THE_MAP_ENTRY, READS_NOTHING, field_mark_the_chests, FIELD_LAYERS_PAGE);
    case AFTER_TEXT_AREA:
        return field_run_long(s, 0xC00343, FIELD_BUILD_TEXT_AREA_ENTRY, READS_NOTHING, field_build_the_text_area_entry, page);
    case AFTER_BACK_TOGETHER: m_stop_at(s, PARTY_BACK_TOGETHER, BACK_TOGETHER_READS); return false;
    case AFTER_THE_TITLE: start_afresh(s, THE_TITLE); return false;
    default: start_afresh(s, ENTER); return false;
    }
}

/* For shadow mode: the original moves the state back with a block move,
 * which leaves Y past the block's end; the field's first start after a game
 * over hands Y to the menus' routine, which stores it. */
enum { STATE_BLOCK_END = (RAM_FIELD_RIPPLE_FRAMES_LEFT + 1) & 0xFFFF };

// $C0:0283
/* The field's page, 8-bit A and 16-bit index registers. */
void field_after_battle(Snes *s)
{
    ORIGINAL_EFFECT(s->cpu.y = STATE_BLOCK_END);
    (void)take_up_after_battle(state_of(s), after_on_the_machine, s);
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

bool field_battle_return_can_run(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    bool ok = battle_return_fits(state_of(s));
    vars_peek(false);
    return ok;
}

/* ---- As C ------------------------------------------------------------------------------------ */

typedef struct {
    GameState *game;
    Platform *pf;
} AfterAsC;

static bool build_the_sprite_table(void *ctx)
{
    GameState *g = ctx;
    bool built = false;
    TIMED_AS(g, BUILD_SPRITE_TABLE, built = field_sprite_table_built(g));
    if (!built)
        program_needs_the_machine(g, BUILD_SPRITE_TABLE, "a sprite table the port doesn't build");
    return built;
}

static bool map_script(AfterAsC *c)
{
    EventAsC e = { c->game, c->pf };
    EventRunner r = event_runner(&e);
    uint16_t at;
    EventTurnEnd end = TURN_OVER;
    TIMED_AS(c->game, MAP_SCRIPT_STARTS, end = event_run_map_script_commands(c->game, &r, &at));
    if (end == TURN_OVER)
        return true;
    if (end != TURN_HANDED_BACK)
        program_needs_the_machine(c->game, MAP_SCRIPT_STARTS, "a map's script the event engine hands to the original");
    return false;
}

/* Each with its guard's state half: the putting back's, objects the rows
 * take and frames that can go back; the party's coming back together, a
 * party it brings back. The game over and the place put aside start the
 * field afresh, which is the program's to do. */
static bool after_battle_call(void *ctx, AfterCall call, FadeOut way)
{
    (void)way;
    AfterAsC *c = ctx;
    GameState *g = c->game;
    bool done = false;
    switch (call) {
    case AFTER_GAME_OVER_WALK:
    case AFTER_FIRST_WALK:
    case AFTER_SECOND_WALK:
    case AFTER_THIRD_WALK: return field_walked_a_frame(g, c->pf);
    case AFTER_FADE_WALK: TIMED_AS(g, WALK, done = field_the_leader_walks(g)); return done;
    case AFTER_FADE_PALETTES: TIMED_AS(g, PALETTE_ANIMATION, done = palette_animated(g)); return done;
    case AFTER_FADE_WAIT: TIMED_AS(g, FIELD_FRAME_WAIT, field_finish_the_frame(g, c->pf)); return true;
    case AFTER_PUT_BACK:
        if (!battle_return_fits(g)) {
            program_needs_the_machine(g, RETURN_FROM_BATTLE, "a party or enemies the port doesn't put back");
            return false;
        }
        TIMED_AS(g, RETURN_FROM_BATTLE, done = return_from_battle(g, build_the_sprite_table, g));
        return done;
    case AFTER_UNPACK_THE_MAP: TIMED_AS(g, FIELD_UNPACK_THE_MAP_ENTRY, done = field_load_the_map(g)); return done;
    case AFTER_WINDOW_FRAME: TIMED_AS(g, FIELD_BUILD_WINDOW_FRAME_ENTRY, field_build_the_window_frame(g)); return true;
    case AFTER_TILE_LOOKS: TIMED_AS(g, TILE_LOOKS, field_load_tile_looks(g)); return true;
    case AFTER_BG3_TILE_LOOKS: TIMED_AS(g, BG3_TILE_LOOKS, field_load_bg3_tile_looks(g)); return true;
    case AFTER_SCRIPT:
        TIMED_AS(g, SCRIPT_UNPACKED, done = field_load_the_script(g));
        if (!done)
            program_needs_the_machine(g, SCRIPT_UNPACKED, "a location's script that doesn't fit");
        return done;
    case AFTER_MAP_SCRIPT: return map_script(c);
    case AFTER_CHESTS: TIMED_AS(g, CHESTS_MARK_THE_MAP_ENTRY, chests_mark_the_map(g)); return true;
    case AFTER_TEXT_AREA: TIMED_AS(g, FIELD_BUILD_TEXT_AREA_ENTRY, field_build_the_text_area(g)); return true;
    case AFTER_BACK_TOGETHER:
        if (!party_can_come_back_together(g)) {
            program_needs_the_machine(g, PARTY_BACK_TOGETHER, "a party the port doesn't bring back together");
            return false;
        }
        TIMED_AS(g, PARTY_BACK_TOGETHER, party_back_together(g));
        return true;
    case AFTER_THE_TITLE:
        TIMED_AS(g, DISPLAY_BLANK_AND_STOP_ENTRY, display_blank_and_stop(c->pf));
        program_starts_afresh_from_the_title(g);
        return false;
    default:
        TIMED_AS(g, DISPLAY_BLANK_AND_STOP_ENTRY, display_blank_and_stop(c->pf));
        program_starts_the_field_again(g);
        return false;
    }
}

bool field_taken_up_after_battle(GameState *g, Platform *pf)
{
    AfterAsC c = { g, pf };
    return take_up_after_battle(g, after_battle_call, &c);
}
