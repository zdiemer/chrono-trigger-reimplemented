/* Field mode: loading a location, and the field put aside for a menu.
 *
 * A location is a map with its own song, graphics and scripts, which its
 * record says (field_data.h). Entering one (field_load_location), the field
 * notes where the record is, starts the location's song, and loads its tile
 * set: the tile graphics unpacked and sent to VRAM, BG3's too, and how the
 * map's tiles are made of them; then its map, its scripts and its palettes'
 * animations, which are the original's routines still.
 *
 * The menus use work RAM the field keeps its objects in. Before one the field
 * puts its state aside (field_put_aside_for_a_menu): the objects' tables and
 * the map's state with them, moved as one block to where the event script
 * lives, and the party's places, which the menus may change; a dialog under
 * way is shut, to start over after the menu. After the menu
 * (field_set_up_after_a_menu) the state comes back, the location's graphics,
 * map and scripts are loaded again (which the menus wrote over), and what the
 * party's scripts had been told is put back into the script; a gathering of
 * the party the menu cut short is finished at once. A battle puts the field
 * aside the same way (field/battle_start.c). */
#include "field/main_loop.h"
#include "field/dialog_window.h"
#include "field/field.h"
#include "field/map.h"
#include "field/field_data.h"
#include "field/frames.h"
#include "field/line_effects.h"
#include "field/object.h"
#include "field/party.h"
#include "field/sprites.h"

#include "charge.h"
#include "cpu_io.h"
#include "data/decompress.h"
#include "data/packed_graphics.h"
#include "event/party.h"
#include "event/variables.h"
#include "field/bridge.h"
#include "field/display.h"
#include "field/scratch.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* ---- The field's state, put aside ----------------------------------------------------------- */

/* The field's object tables and the map's state with them are one stretch of
 * work RAM, from the graphics sets the map's people share to the ripple's
 * count, which the field moves aside as one block, to where the event script
 * lives (the script is unpacked again after). */
enum {
    FIELD_STATE_FROM = RAM_FIELD_SHARED_GRAPHICS,
    FIELD_STATE_BYTES = RAM_FIELD_RIPPLE_FRAMES_LEFT + 1 - RAM_FIELD_SHARED_GRAPHICS,
    FIELD_STATE_ASIDE = RAM_EVENT_OBJECT_COUNT,
};

// $C0:0918
void field_put_the_state_aside(GameState *g)
{
    wram_copy(g, FIELD_STATE_FROM, FIELD_STATE_ASIDE, FIELD_STATE_BYTES);
    ORIGINAL_EFFECT(charge_block_move(g, FIELD_STATE_BYTES));
}

// $C0:0905
void field_bring_the_state_back(GameState *g)
{
    wram_copy(g, FIELD_STATE_ASIDE, FIELD_STATE_FROM, FIELD_STATE_BYTES);
    ORIGINAL_EFFECT(charge_block_move(g, FIELD_STATE_BYTES));
}

/* A party with no leader on the map (as a save's load leaves it, or the
 * attract demo's scenes, until the map's objects start) has the place of
 * the slot its marker names, the first past the tables, as the original
 * reads it; the map's start puts the party where the location says all the
 * same. */
LeaderPlace field_leaders_place(const GameState *g)
{
    FieldLink link = party_member_link(g, PARTY_LEADER);
    FieldObject leader = field_linked_object(link);
    if (field_link_names_object(link))
        return (LeaderPlace){ field_object_x_tile(g, leader), field_object_y_tile(g, leader),
                              field_object_facing(g, leader) };
    return (LeaderPlace){ field_object_x_tile_unchecked(g, leader), field_object_y_tile_unchecked(g, leader),
                          field_object_facing_unchecked(g, leader) };
}

bool field_leader_readable(const GameState *g)
{
    FieldLink leader = party_member_link(g, PARTY_LEADER);
    return field_party_objects_high(g, PARTY_LEADER) == 0 &&
           (field_link_names_object(leader) || field_same_link(leader, field_no_link()));
}

/* The party's place, to come back to: the leader's tile, and its facing as
 * the place's way to face. */
void field_party_place_is_the_leaders(GameState *g)
{
    LeaderPlace at = field_leaders_place(g);
    set_location_x(g, at.x);
    set_location_y(g, at.y);
    set_location_flags(g, at.facing);
}

/* What the menus may change of the party, kept: each member's place on the
 * map and its sprites' priorities, and where each is on the leader's trail
 * (field/menus.c puts them back). */
static void keep_where_the_party_stands(GameState *g)
{
    for (PartyPlace place = 0; place < PARTY_PLACES; place++) {
        FieldLink link = party_member_link(g, place);
        if (!field_link_is_object(link))
            continue;
        FieldObject member = field_linked_object(link);
        FieldPosition at = object_position(g, member);
        set_field_kept_party_x(g, place, at.x);
        set_field_kept_party_y(g, place, at.y);
        SpritePriorities p = object_sprite_priorities(g, member);
        set_field_kept_party_priorities_lower(g, place, p.lower_stored);
        set_field_kept_party_priorities_upper(g, place, p.upper_stored);
    }
    for (PartyPlace place = 0; place < PARTY_PLACES; place++)
        set_field_kept_trail_positions(g, place, field_trail_position(g, place));
}

/* The party's scripts whose destinations the commands write into the event
 * script (event/party.h), whose unpacking after the menu writes over them. */
static const PartyScript scripts_kept[FIELD_KEPT_PARTY_SCRIPT_TILES_COUNT] = {
    FORMATION_SCRIPT_LEADER, FORMATION_SCRIPT_FIRST_MEMBER, FORMATION_SCRIPT_SECOND_MEMBER, PUSH_SCRIPT,
};

/* BG3's wave as the line effects' builders test it, by its low byte. */
enum { WAVE_TESTED = 0x00FF };

// $C0:011B
void field_put_aside(GameState *g)
{
    dialog_shut_to_start_over(g);
    field_put_the_state_aside(g);
    field_party_place_is_the_leaders(g);
    keep_where_the_party_stands(g);
    for (unsigned i = 0; i < FIELD_KEPT_PARTY_SCRIPT_TILES_COUNT; i++) {
        MapTile to = party_script_destination(g, scripts_kept[i]);
        set_field_kept_party_script_tiles(g, i, (uint16_t)(to.y << 8 | to.x));
    }
    set_field_kept_bg3_wave(g, (uint8_t)(field_line_wave_bg3(g) & WAVE_TESTED));
}

/* ... and put back once the script is unpacked again. */
static void put_back_what_the_party_was_told(GameState *g)
{
    for (unsigned i = 0; i < FIELD_KEPT_PARTY_SCRIPT_TILES_COUNT; i++) {
        uint16_t to = field_kept_party_script_tiles(g, i);
        party_script_go_to(g, scripts_kept[i], (MapTile){ (uint8_t)to, (uint8_t)(to >> 8) });
    }
    set_field_line_wave_bg3(g, (uint16_t)((field_line_wave_bg3(g) & ~WAVE_TESTED) | field_kept_bg3_wave(g)));
}

/* A gathering of the party (event $DA) the menu cut short is finished at
 * once: the members put where the leader stands, all counted in, and the
 * party the player's. */
static bool gathering_cut_short(const GameState *g)
{
    uint8_t count = variable_byte(g, GATHERING_COUNT);
    return count != 0 && count < PARTY_ALL_GATHERED;
}

static void finish_the_gathering(GameState *g)
{
    FieldPosition at = object_position(g, field_linked_object(party_member_link(g, PARTY_LEADER)));
    for (PartyPlace place = PARTY_FIRST_MEMBER; place < PARTY_PLACES; place++)
        object_place(g, field_linked_object(party_member_link(g, place)), at);
    hand_the_party_to_the_player(g, true);
    set_variable_byte(g, GATHERING_COUNT, PARTY_ALL_GATHERED);
}

/* ---- The location's record and its song ------------------------------------------------------ */

/* Where the location's record is, which the field keeps: the original works
 * it out on the multiplier, which only the comparison with it sees. */
enum { LOCATION_LOW_BITS = 0x00FF };
static void leave_the_record_product(GameState *g, uint16_t location)
{
    hw_set_multiplicand(g, (uint8_t)(location & LOCATION_LOW_BITS));
    (void)hw_multiply_by(g, LOCATION_RECORD_BYTES);
}

// $C0:092B
static LocationDescription find_the_record(GameState *g)
{
    uint16_t location = location_id(g);
    set_location_record_offset(g, location_record_place(location));
    ORIGINAL_EFFECT(leave_the_record_product(g, location));
    return location_description_at(g, location_record_offset(g));
}

static LocationDescription the_record(const GameState *g) { return location_description_at(g, location_record_offset(g)); }

/* ---- The tile set ---------------------------------------------------------------------------- */

/* Where the tile set's graphics go: the first six blocks unpacked one after
 * another where the map's tables will be (the map is unpacked after them),
 * each sent from there to its own place among BG1's and BG2's tiles in VRAM
 * (the last a little short of a full block), the last two unpacked to bank
 * $7F; BG3's block unpacked to bank $7F and sent to BG3's tiles. */
enum { UNPACKED_TILES = RAM_FIELD_LAYER_MAPS, BLOCK_TILES = 0x1000, LAST_BLOCK_TILES = 0x0F80, VRAM_TILES_FROM = 0x2000,
       VRAM_BLOCK_WORDS = 0x0800, BG3_UNPACKED_TILES = 0x7F5080, BG3_VRAM_TILES = 0x5000, TRANSFER_WORDS = 1 };
static const uint32_t kept_blocks[TILE_SET_BLOCKS - TILE_SET_BLOCKS_IN_VRAM] = { 0x7F6000, 0x7F7000 };

/* How the map's tiles are made of the tiles, unpacked: BG1's and BG2's, and
 * BG3's. */
enum { TILE_LOOKS = RAM_FIELD_TILE_LOOKS, BG3_TILE_LOOKS = RAM_FIELD_BG3_TILE_LOOKS };

/* The unpacker ($C3:0002): what to unpack and where to, in its own cells. It
 * puts every register back. */
enum { UNPACKER = 0xC30002, THE_UNPACKER_IGNORES = DC_A | DC_X | DC_N | DC_Z | DC_C };

static bool unpack(Snes *s, uint32_t site, uint32_t from, uint32_t to)
{
    GameState *g = state_of(s);
    set_decompress_source(g, from);
    set_decompress_dest(g, to);
    return field_run_long(s, site, UNPACKER, THE_UNPACKER_IGNORES, decompress, on_the_field_page(s, false));
}

/* For the original's code and shadow mode: the last block, set or the like
 * worked with, in the scratch cell the original keeps it in. */
static void leave_last_worked(GameState *g, uint16_t v) { original_cell16(g, FIELD_LAST_WORKED_CELL, v); }

/* ... and, for the tile graphics, where it read the set's list, on the
 * multiplier and in the cells it read through. */
static void leave_the_list(GameState *g, uint8_t set)
{
    hw_set_multiplicand(g, set);
    (void)hw_multiply_by(g, TILE_SET_BLOCKS);
    set_field_tile_set_list_place(g, tile_set_blocks_place(set) + TILE_SET_BLOCKS - 1);
}

/* The unpacks' sites, block by block: each of the tile graphics' first six
 * from its own routine's one, the last two from theirs. */
enum { VRAM_BLOCK_UNPACK = 0xC06D5D, FIRST_KEPT_UNPACK = 0xC06D93, SECOND_KEPT_UNPACK = 0xC06DCA };

/* Where the six blocks for VRAM go, each described as a transfer whether the
 * set has the block or not. */
static FieldTransfer vram_block_transfer(unsigned n)
{
    return (FieldTransfer){ .vram = (uint16_t)(VRAM_TILES_FROM + n * VRAM_BLOCK_WORDS), .mode = TRANSFER_WORDS,
                            .source = UNPACKED_TILES,
                            .size = n == TILE_SET_BLOCKS_IN_VRAM - 1 ? LAST_BLOCK_TILES : BLOCK_TILES };
}

// $C0:0960 (with $C0:6D2F and $C0:6D65 for the blocks)
/* The tile set's graphics. A location with no tile set loads none. */
void field_load_tile_graphics(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    uint8_t set = the_record(g).tile_set;
    if (set == NO_TILE_SET) {
        m_rts(s);
        return;
    }
    uint8_t last = NO_GRAPHICS_BLOCK;
    for (unsigned n = 0; n < TILE_SET_BLOCKS; n++) {
        uint8_t block = tile_set_block(g, set, n);
        if (n < TILE_SET_BLOCKS_IN_VRAM && block == NO_GRAPHICS_BLOCK)
            field_describe_transfer(g, vram_block_transfer(n));
        if (block == NO_GRAPHICS_BLOCK)
            continue;
        last = block;
        if (n < TILE_SET_BLOCKS_IN_VRAM) {
            if (!unpack(s, VRAM_BLOCK_UNPACK, graphics_block_address(g, block), UNPACKED_TILES))
                return;
            TIMED_AS(g, FIELD_VRAM_TRANSFER_ENTRY, field_send_transfer(g, pf, vram_block_transfer(n)));
        } else {
            uint32_t site = n == TILE_SET_BLOCKS_IN_VRAM ? FIRST_KEPT_UNPACK : SECOND_KEPT_UNPACK;
            if (!unpack(s, site, graphics_block_address(g, block), kept_blocks[n - TILE_SET_BLOCKS_IN_VRAM]))
                return;
        }
    }
    if (last != NO_GRAPHICS_BLOCK)
        ORIGINAL_EFFECT(leave_last_worked(g, last));
    ORIGINAL_EFFECT(leave_the_list(g, set));
    m_rts(s);
}

// $C0:6DCF
/* BG3's graphics: a block of its own, where the location has one; where it
 * has none the field notes so, and BG3 keeps what it had. */
void field_load_bg3_graphics(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t block = the_record(g).bg3_tile_set;
    if (block == NO_TILE_SET) {
        field_map_names_a_bg3_set(g, false);
        m_rts(s);
        return;
    }
    if (!unpack(s, 0xC06E03, graphics_block_address(g, block), BG3_UNPACKED_TILES))
        return;
    FieldTransfer t = { .vram = BG3_VRAM_TILES, .mode = TRANSFER_WORDS, .source = BG3_UNPACKED_TILES,
                        .size = BLOCK_TILES };
    TIMED_AS(g, FIELD_VRAM_TRANSFER_ENTRY, field_send_transfer(g, m_platform(s), t));
    ORIGINAL_EFFECT(leave_last_worked(g, (uint16_t)(3 * block)));
    m_rts(s);
}

// $C0:09DD
/* How BG1's and BG2's map tiles are made of the tile set's tiles. */
void field_load_tile_looks(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t set = the_record(g).tile_set;
    if (set != NO_TILE_SET) {
        ORIGINAL_EFFECT(leave_last_worked(g, set));
        if (!unpack(s, 0xC00A0F, tile_looks_address(g, set), TILE_LOOKS))
            return;
    }
    m_rts(s);
}

// $C0:0A14
/* ... and BG3's, where the location has a set for it. */
void field_load_bg3_tile_looks(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t set = the_record(g).bg3_tile_set;
    if (field_map_has_no_bg3_set(g) == 0 && set != NO_TILE_SET) {
        ORIGINAL_EFFECT(leave_last_worked(g, set));
        if (!unpack(s, 0xC00A4A, bg3_tile_looks_address(g, set), BG3_TILE_LOOKS))
            return;
    }
    m_rts(s);
}

// $C0:1B53
/* The location's song. Where a screen keeps the song that is playing
 * (menu_song_kept), the song playing is started again. Otherwise the music
 * comes back to full volume at once, and the location's song, if it has one,
 * starts and is kept as the song playing. The driver ($C7:0004, through the
 * audio service) reads nothing of the registers, and hands A's high byte back
 * as it found it, which the loading after it ANDs away. */
#define DRIVER_IGNORES (DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C)
enum { SOUND_DRIVER = 0xC70004, SONG_SITE = 0xC01B8B, SONG_RETURNS = 0xC01B8F };

static bool play(Snes *s, uint8_t song)
{
    au_play_song(m_platform(s), PF_CALL_DC(SONG_SITE, SOUND_DRIVER, DRIVER_IGNORES), song);
    return bridge_back(s, SONG_RETURNS, on_the_field_page(s, false));
}

void field_start_location_song(Snes *s)
{
    GameState *g = state_of(s);
    if (menu_song_kept(g)) {
        if (play(s, current_song(g)))
            m_rts(s);
        return;
    }
    AudioCommand full_volume = { .command = AU_FADE_MUSIC, .p1 = 0, .p2 = AU_FULL_VOLUME, .p3 = AU_FULL_VOLUME,
                                 .count = 3 };
    au_command(m_platform(s), PF_CALL_DC(0xC01B73, SOUND_DRIVER, DRIVER_IGNORES), full_volume);
    if (!bridge_back(s, 0xC01B77, on_the_field_page(s, false)))
        return;
    uint8_t song = the_record(g).song;
    if (song >= NO_LOCATION_SONG) {
        m_rts(s);
        return;
    }
    set_current_song(g, song);
    if (play(s, song))
        m_rts(s);
}

/* ---- The routines it calls on the machine ---------------------------------------------------- */

/* The original's routines. */
enum {
    SCREENS_SET_UP = 0xC07084,    /* the location's screens: its layers, windows and color math */
    MAP_PLACED = 0xC07F7E,        /* where the map's layers start, cleared */
    SCRIPT_UNPACKED = 0xC056D4,   /* the location's event script unpacked */
    TILE_ANIMATIONS = 0xFDFFFA,   /* the location's animated tiles, through the original's jump */
    PALETTE_ANIMATIONS = 0xFDFFF4, /* the location's palette animations, through the original's jump */
    MAP_SCRIPT_STARTS = 0xC0595C, /* the map's own script (object 0) run to its first return */
};
/* The ports it runs as C, by the routines they stand for. */
enum {
    LOCATION_SONG = 0xC01B53, TILE_GRAPHICS = 0xC00960, BG3_GRAPHICS = 0xC06DCF, TILE_LOOKS_ROUTINE = 0xC009DD,
    BG3_TILE_LOOKS_ROUTINE = 0xC00A14, LINE_EFFECTS = 0xFDC2C1,
    LINE_EFFECTS_SET_UP = 0xFDC1EE, WHOLE_FIGURE = 0xC0E12A,
};
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_THE_OVERFLOW = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C,
};

static BridgeMachine page(const Snes *s) { return on_the_field_page(s, false); }

static bool call(Snes *s, uint32_t site, uint32_t routine) { return bridge_call(s, site, routine, READS_NOTHING, page(s)); }

/* The location's graphics, map and scripts, from the original's calls at
 * the sites given, in the order both loadings make them. */
static const struct {
    uint32_t tile_graphics, bg3_graphics, screens, map_placed, map, looks, bg3_looks, script;
} loading_sites[] = {
    { 0xC000FA, 0xC000FD, 0xC00100, 0xC00103, 0xC00106, 0xC00109, 0xC0010C, 0xC0010F },
    { 0xC001C3, 0xC001C6, 0xC001C9, 0xC001CC, 0xC001CF, 0xC001D2, 0xC001D5, 0xC001D8 },
};
typedef enum { ENTERING, AFTER_A_MENU } Loading;

static bool load_graphics_map_and_scripts(Snes *s, Loading loading)
{
    const BridgeMachine m = page(s);
    return field_run(s, loading_sites[loading].tile_graphics, TILE_GRAPHICS, READS_NOTHING, field_load_tile_graphics, m) &&
           field_run(s, loading_sites[loading].bg3_graphics, BG3_GRAPHICS, READS_NOTHING, field_load_bg3_graphics, m) &&
           call(s, loading_sites[loading].screens, SCREENS_SET_UP) &&
           call(s, loading_sites[loading].map_placed, MAP_PLACED) &&
           field_run(s, loading_sites[loading].map, FIELD_UNPACK_THE_MAP_ENTRY, READS_NOTHING, field_unpack_the_map, m) &&
           field_run(s, loading_sites[loading].looks, TILE_LOOKS_ROUTINE, READS_NOTHING, field_load_tile_looks, m) &&
           field_run(s, loading_sites[loading].bg3_looks, BG3_TILE_LOOKS_ROUTINE, READS_NOTHING,
                     field_load_bg3_tile_looks, m) &&
           call(s, loading_sites[loading].script, SCRIPT_UNPACKED);
}

/* ---- The entries ------------------------------------------------------------------------------- */

/* For shadow mode: the original works the record's place out in A, and its
 * high byte stays there for the song's calls to the driver. */
static void leave_the_place_in_a(Snes *s)
{
    s->cpu.a = (uint16_t)((location_record_offset(state_of(s)) & 0xFF00) | (s->cpu.a & 0x00FF));
}

// $C0:00F4
void field_load_location(Snes *s)
{
    find_the_record(state_of(s));
    ORIGINAL_EFFECT(leave_the_place_in_a(s));
    if (field_run(s, 0xC000F7, LOCATION_SONG, READS_A_HIGH, field_start_location_song, page(s)) &&
        load_graphics_map_and_scripts(s, ENTERING) &&
        bridge_call_long(s, 0xC00112, TILE_ANIMATIONS, READS_NOTHING, page(s)) &&
        bridge_call_long(s, 0xC00116, PALETTE_ANIMATIONS, READS_NOTHING, page(s)))
        m_rts(s);
}

/* What it leaves for the menu: X past the party's places (the main menu takes
 * it as its argument), and the carry its last comparison set. */
enum { PAST_THE_PLACES = 2 * PARTY_PLACES };

void field_put_aside_for_a_menu(Snes *s)
{
    field_put_aside(state_of(s));
    s->cpu.x = PAST_THE_PLACES;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* The figure an event command built for an object (field_built_sprite_object),
 * built again, on the field's page with its object named in X. */
static bool build_the_figure_again(Snes *s)
{
    GameState *g = state_of(s);
    FieldLink built = field_link_stored(field_built_sprite_object(g));
    if (!field_link_is_object(built))
        return true;
    FieldObject obj = field_linked_object(built);
    object_make_current_recognised(g, obj);
    s->cpu.x = field_object_offset(obj);
    return field_run(s, 0xC00212, WHOLE_FIGURE, DC_A | DC_N | DC_Z | DC_C, field_draw_whole_figure, page(s));
}

// $C0:01A5
void field_set_up_after_a_menu(Snes *s)
{
    GameState *g = state_of(s);
    field_bring_the_state_back(g);
    m_sep(s, CPU_FLAG_X);
    if (!field_run_long(s, 0xC001AA, LINE_EFFECTS, READS_NOTHING, field_build_line_effect_tables,
                        on_the_field_page(s, true)))
        return;
    m_rep(s, CPU_FLAG_X);
    if (!field_run_long(s, 0xC001B0, LINE_EFFECTS_SET_UP, READS_NOTHING, field_setup_line_effects, page(s)))
        return;
    sprites_hide_all_next_frame(g);
    if (!load_graphics_map_and_scripts(s, AFTER_A_MENU))
        return;
    put_back_what_the_party_was_told(g);
    if (!bridge_call(s, 0xC00206, MAP_SCRIPT_STARTS, READS_THE_OVERFLOW, page(s)) || !build_the_figure_again(s))
        return;
    if (gathering_cut_short(g))
        finish_the_gathering(g);
    m_rts(s);
}

/* The guards: on the field's page with the data bank $00, the party's places
 * objects or none (the leader's an object or none, its word's high byte clear); and,
 * after a menu, a gathering cut short with both members there, their words'
 * high bytes clear too (the original moves them through the words). */
static bool party_places_readable(const GameState *g)
{
    if (!field_leader_readable(g))
        return false;
    for (PartyPlace place = PARTY_FIRST_MEMBER; place < PARTY_PLACES; place++) {
        FieldLink link = party_member_link(g, place);
        if (field_link_is_object(link) && !field_link_names_object(link))
            return false;
    }
    return true;
}

bool field_party_can_be_put_aside(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    bool ok = party_places_readable(state_of(s));
    vars_peek(false);
    return ok;
}

bool field_can_be_set_up_again(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = true;
    if (gathering_cut_short(g)) {
        ok = party_places_readable(g);
        for (PartyPlace place = PARTY_FIRST_MEMBER; ok && place < PARTY_PLACES; place++)
            ok = field_link_is_object(party_member_link(g, place)) && field_party_objects_high(g, place) == 0;
    }
    vars_peek(false);
    return ok;
}
