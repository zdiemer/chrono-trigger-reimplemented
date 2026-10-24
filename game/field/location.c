/* Field mode: loading a location, and the field put aside for a menu.
 *
 * A location is a map with its own song, graphics and scripts, which its
 * record says (field_data.h). Entering one (field_load_location), the field
 * notes where the record is, starts the location's song, and loads its tile
 * set: the tile graphics unpacked and sent to VRAM, BG3's too, and how the
 * map's tiles are made of them; its palette; its map; its event script, with
 * the built-in commands after it; and its animated tiles and palette
 * animations.
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
#include "core/palette.h"
#include "core/tile_animation.h"
#include "cpu_io.h"
#include "data/decompress.h"
#include "data/packed_graphics.h"
#include "event/event.h"
#include "event/event_data.h"
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
       VRAM_BLOCK_WORDS = 0x0800, BG3_UNPACKED_TILES = RAM_FIELD_BG3_TILES_UNPACKED, BG3_VRAM_TILES = 0x5000, TRANSFER_WORDS = 1 };
static const uint32_t kept_blocks[TILE_SET_BLOCKS - TILE_SET_BLOCKS_IN_VRAM] = { 0x7F6000, 0x7F7000 };

/* How the map's tiles are made of the tiles, unpacked: BG1's and BG2's, and
 * BG3's. */
enum { TILE_LOOKS = RAM_FIELD_TILE_LOOKS, BG3_TILE_LOOKS = RAM_FIELD_BG3_TILE_LOOKS };

/* A stream unpacked (data/decompress.h), named in the unpacker's cells and
 * timed as the unpacker. */
static void unpack(GameState *g, uint32_t from, uint32_t to)
{
    set_decompress_source(g, from);
    set_decompress_dest(g, to);
    TIMED_AS(g, UNPACK_STREAM_ENTRY, (void)unpack_stream(g));
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
static void load_tile_graphics(GameState *g, Platform *pf)
{
    uint8_t set = the_record(g).tile_set;
    if (set == NO_TILE_SET)
        return;
    uint8_t last = NO_GRAPHICS_BLOCK;
    for (unsigned n = 0; n < TILE_SET_BLOCKS; n++) {
        uint8_t block = tile_set_block(g, set, n);
        if (n < TILE_SET_BLOCKS_IN_VRAM && block == NO_GRAPHICS_BLOCK)
            field_describe_transfer(g, vram_block_transfer(n));
        if (block == NO_GRAPHICS_BLOCK)
            continue;
        last = block;
        if (n < TILE_SET_BLOCKS_IN_VRAM) {
            unpack(g, graphics_block_address(g, block), UNPACKED_TILES);
            TIMED_AS(g, FIELD_VRAM_TRANSFER_ENTRY, field_send_transfer(g, pf, vram_block_transfer(n)));
        } else {
            unpack(g, graphics_block_address(g, block), kept_blocks[n - TILE_SET_BLOCKS_IN_VRAM]);
        }
    }
    if (last != NO_GRAPHICS_BLOCK)
        ORIGINAL_EFFECT(leave_last_worked(g, last));
    ORIGINAL_EFFECT(leave_the_list(g, set));
}

// $C0:6DCF
/* BG3's graphics: a block of its own, where the location has one; where it
 * has none the field notes so, and BG3 keeps what it had. */
static void load_bg3_graphics(GameState *g, Platform *pf)
{
    uint8_t block = the_record(g).bg3_tile_set;
    if (block == NO_TILE_SET) {
        field_map_names_a_bg3_set(g, false);
        return;
    }
    unpack(g, graphics_block_address(g, block), BG3_UNPACKED_TILES);
    FieldTransfer t = { .vram = BG3_VRAM_TILES, .mode = TRANSFER_WORDS, .source = BG3_UNPACKED_TILES,
                        .size = BLOCK_TILES };
    TIMED_AS(g, FIELD_VRAM_TRANSFER_ENTRY, field_send_transfer(g, pf, t));
    ORIGINAL_EFFECT(leave_last_worked(g, (uint16_t)(3 * block)));
}

// $C0:09DD
/* How BG1's and BG2's map tiles are made of the tile set's tiles. */
static void load_tile_looks(GameState *g)
{
    uint8_t set = the_record(g).tile_set;
    if (set == NO_TILE_SET)
        return;
    ORIGINAL_EFFECT(leave_last_worked(g, set));
    unpack(g, tile_looks_address(g, set), TILE_LOOKS);
}

// $C0:0A14
/* ... and BG3's, where the location has a set for it. */
static void load_bg3_tile_looks(GameState *g)
{
    uint8_t set = the_record(g).bg3_tile_set;
    if (field_map_has_no_bg3_set(g) != 0 || set == NO_TILE_SET)
        return;
    ORIGINAL_EFFECT(leave_last_worked(g, set));
    unpack(g, bg3_tile_looks_address(g, set), BG3_TILE_LOOKS);
}

static const AudioCommand full_volume = { .command = AU_FADE_MUSIC, .p1 = 0, .p2 = AU_FULL_VOLUME,
                                          .p3 = AU_FULL_VOLUME, .count = 3 };

/* The driver ($C7:0004, through the audio service) reads nothing of the
 * registers, and hands A's high byte back as it found it, which the loading
 * after it ANDs away. */
#define DRIVER_IGNORES (DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C)
enum { SOUND_DRIVER = 0xC70004, SONG_SITE = 0xC01B8B, SONG_RETURNS = 0xC01B8F, FADE_SITE = 0xC01B73,
       FADE_RETURNS = 0xC01B77 };

static void play(Platform *pf, uint8_t song)
{
    au_play_song(pf, PF_CALL_DC(SONG_SITE, SOUND_DRIVER, DRIVER_IGNORES), song);
}

static void bring_the_music_to_full_volume(Platform *pf)
{
    au_command(pf, PF_CALL_DC(FADE_SITE, SOUND_DRIVER, DRIVER_IGNORES), full_volume);
}

/* The location's song, if it has one, kept as the song playing; false where
 * it has none. */
static bool keep_the_locations_song(GameState *g)
{
    uint8_t song = the_record(g).song;
    if (song >= NO_LOCATION_SONG)
        return false;
    set_current_song(g, song);
    return true;
}

/* Whether a call to the sound driver came back to the song's start, at
 * `returns`: always as C; on the machine, the driver's call is a call-out
 * whose return the bridge checks. */
typedef bool (*DriverCameBack)(void *ctx, uint32_t returns);

// $C0:1B53
/* The location's song. Where a screen keeps the song that is playing
 * (menu_song_kept), the song playing is started again. Otherwise the music
 * comes back to full volume at once, and the location's song, if it has one,
 * starts and is kept as the song playing. False where a call to the driver
 * didn't come back. */
static bool start_the_location_song(GameState *g, Platform *pf, DriverCameBack back, void *ctx)
{
    if (menu_song_kept(g)) {
        play(pf, current_song(g));
        return back(ctx, SONG_RETURNS);
    }
    bring_the_music_to_full_volume(pf);
    if (!back(ctx, FADE_RETURNS))
        return false;
    if (!keep_the_locations_song(g))
        return true;
    play(pf, current_song(g));
    return back(ctx, SONG_RETURNS);
}

static bool came_back_as_c(void *ctx, uint32_t returns)
{
    (void)ctx, (void)returns;
    return true;
}

static bool came_back_on_the_machine(void *ctx, uint32_t returns)
{
    Snes *s = ctx;
    return bridge_back(s, returns, on_the_field_page(s, false));
}

/* ---- Where the tile set's loading meets the original ------------------------------------------- */

void field_load_tile_graphics(Snes *s)
{
    load_tile_graphics(state_of(s), m_platform(s));
    m_rts(s);
}

void field_load_bg3_graphics(Snes *s)
{
    load_bg3_graphics(state_of(s), m_platform(s));
    m_rts(s);
}

void field_load_tile_looks(Snes *s)
{
    load_tile_looks(state_of(s));
    m_rts(s);
}

void field_load_bg3_tile_looks(Snes *s)
{
    load_bg3_tile_looks(state_of(s));
    m_rts(s);
}

void field_start_location_song(Snes *s)
{
    if (start_the_location_song(state_of(s), m_platform(s), came_back_on_the_machine, s))
        m_rts(s);
}

/* ---- The palette, the layers' state and the script -------------------------------------------- */

/* The location's palette fills the background palettes 1-7, each but its
 * first colour, which is the backdrop's or clear; the original works the
 * palette's place out on the multiplier, which only the comparison sees. */
enum { FIRST_ROW = 1, ROW_COLOURS = 16, FIRST_ROW_COLOUR = 1, LOCATION_PALETTE_BYTES = 210 };

static void leave_the_palette_product(GameState *g, uint8_t palette)
{
    hw_set_multiplicand(g, palette);
    (void)hw_multiply_by(g, LOCATION_PALETTE_BYTES);
}

// $C0:7084
void field_load_location_palette(GameState *g)
{
    uint8_t palette = location_palette(g, location_record_offset(g));
    ORIGINAL_EFFECT(leave_the_palette_product(g, palette));
    uint32_t mark = 0;
    ORIGINAL_EFFECT(mark = charge_uncounted_begin(g));
    for (unsigned row = 0; row < LOCATION_PALETTE_ROWS; row++)
        for (unsigned c = 0; c < LOCATION_PALETTE_ROW_COLOURS; c++)
            set_palette_base(g, (FIRST_ROW + row) * ROW_COLOURS + FIRST_ROW_COLOUR + c,
                             location_palette_colour(g, palette, row, c));
    ORIGINAL_EFFECT(charge_uncounted_end(g, mark));
    ORIGINAL_EFFECT(charge_block_move(g, LOCATION_PALETTE_BYTES));
    (void)mark;
}

// $C0:7F7E
/* The map's layers keep their state on a page of their own, which starts each
 * location clear. */
enum { LAYERS_STATE = FIELD_LAYERS_PAGE, LAYERS_STATE_BYTES = 0x0100 };

void field_clear_the_layers_state(GameState *g, Platform *pf)
{
    TIMED_AS(g, FIELD_FILL_MEMORY_ENTRY, field_clear_memory(g, pf, LAYERS_STATE, LAYERS_STATE_BYTES));
}

/* The location's event script is unpacked to the script buffer (event/script.h),
 * whose first byte is the count of the map's objects. A script that is too long
 * for the buffer's room before the built-in commands, or that is a text file
 * (it starts with a line break), is the original's error screen: no script the
 * game has is either. */
enum { SCRIPT_BUFFER = 0x7F2000, SCRIPT_ROOM = 0x1700, CR = 0x0D, LF = 0x0A };

static bool script_fits(const GameState *g)
{
    bool text = event_object_count(g) == CR && event_script_byte(g, 0) == LF;
    return !text && (int16_t)(SCRIPT_ROOM - decompress_length(g)) >= 0;
}

/* The built-in commands, copied after the script where the commands and
 * routines that use them expect them: the first run from its start, the second
 * three times, and the third alone (each of the first two copies a little of
 * the run after it too). */
static const struct {
    uint16_t from, to, bytes;
} built_in_copies[] = {
    { 0x00, 0x16FF, 0x20 }, { 0x19, 0x171F, 0x20 }, { 0x19, 0x173F, 0x20 }, { 0x19, 0x175F, 0x20 }, { 0x31, 0x177F, 6 },
};
enum { BUILT_IN_COPIES = sizeof built_in_copies / sizeof built_in_copies[0] };

static void copy_the_built_in_commands(GameState *g)
{
    uint32_t mark = 0, bytes = 0;
    ORIGINAL_EFFECT(mark = charge_uncounted_begin(g));
    for (unsigned c = 0; c < BUILT_IN_COPIES; c++) {
        for (uint16_t i = 0; i < built_in_copies[c].bytes; i++)
            set_event_script_byte(g, (uint16_t)(built_in_copies[c].to + i),
                                  built_in_command_byte(g, (uint16_t)(built_in_copies[c].from + i)));
        bytes += built_in_copies[c].bytes;
    }
    ORIGINAL_EFFECT(charge_uncounted_end(g, mark));
    ORIGINAL_EFFECT(charge_block_move(g, bytes));
    (void)mark;
    (void)bytes;
}

/* The script's place in the table of scripts is its number tripled, which the
 * original adds up from the number in its first scratch cell; its callers read
 * the last sum's overflow, which the unpacker's call keeps. */
static bool tripling_overflows(uint16_t script)
{
    uint16_t twice = (uint16_t)(script + script);
    return field_add_with_carry_overflows16(twice, script, script >> 15);
}

static uint16_t the_locations_script(const GameState *g) { return location_script(g, location_record_offset(g)); }

// $C0:56D4
/* The location's event script, with the built-in commands after it; false
 * where it doesn't fit, which is the original's error screen. */
static bool load_the_script(GameState *g)
{
    uint16_t script = the_locations_script(g);
    ORIGINAL_EFFECT(leave_last_worked(g, script));
    unpack(g, field_packed_script(g, script), SCRIPT_BUFFER);
    if (!script_fits(g))
        return false;
    copy_the_built_in_commands(g);
    return true;
}

/* ---- The location loaded ---------------------------------------------------------------------- */

/* The routines its steps stand for, which time them. */
enum {
    LOCATION_SONG = 0xC01B53, TILE_GRAPHICS = 0xC00960, BG3_GRAPHICS = 0xC06DCF, TILE_LOOKS_ROUTINE = 0xC009DD,
    BG3_TILE_LOOKS_ROUTINE = 0xC00A14, PALETTE_LOADED = 0xC07084, LAYERS_STATE_CLEARED = 0xC07F7E,
    SCRIPT_UNPACKED = 0xC056D4, TILE_ANIMATIONS = 0xFDDE98, PALETTE_ANIMATIONS = 0xFDE292,
};

/* The tile set's graphics, the palette and the layers' state, which come
 * before the map; and the tiles' looks, which come after it. */
static void load_the_graphics(GameState *g, Platform *pf)
{
    TIMED_AS(g, TILE_GRAPHICS, load_tile_graphics(g, pf));
    TIMED_AS(g, BG3_GRAPHICS, load_bg3_graphics(g, pf));
    TIMED_AS(g, PALETTE_LOADED, field_load_location_palette(g));
    TIMED_AS(g, LAYERS_STATE_CLEARED, field_clear_the_layers_state(g, pf));
}

static void load_the_tile_looks(GameState *g)
{
    TIMED_AS(g, TILE_LOOKS_ROUTINE, load_tile_looks(g));
    TIMED_AS(g, BG3_TILE_LOOKS_ROUTINE, load_bg3_tile_looks(g));
}

static void load_the_animations(GameState *g)
{
    TIMED_AS(g, TILE_ANIMATIONS, tile_animations_load(g));
    TIMED_AS(g, PALETTE_ANIMATIONS, palette_load_location_animations(g));
}

/* The location's loading, entering it and after a menu, whose steps that
 * are entries of their own -- the song, the map and the script -- run as C
 * or on the machine (below). Each is false where it went no further: as C, a
 * map or a script the port doesn't take; on the machine, a call that didn't
 * come back as the field runs. */
typedef enum { ENTERING, AFTER_A_MENU } Loading;
typedef struct {
    bool (*song)(void *ctx);
    bool (*map)(void *ctx, Loading loading);
    bool (*script)(void *ctx, Loading loading);
} LoadSteps;

/* The location's graphics, map and scripts, in the order both loadings make
 * them. */
static bool load_graphics_map_and_scripts(GameState *g, Platform *pf, const LoadSteps *steps, void *ctx,
                                          Loading loading)
{
    load_the_graphics(g, pf);
    if (!steps->map(ctx, loading))
        return false;
    load_the_tile_looks(g);
    return steps->script(ctx, loading);
}

// $C0:00F4
static bool load_location(GameState *g, Platform *pf, const LoadSteps *steps, void *ctx)
{
    find_the_record(g);
    if (!steps->song(ctx) || !load_graphics_map_and_scripts(g, pf, steps, ctx, ENTERING))
        return false;
    load_the_animations(g);
    return true;
}

/* ---- As C ------------------------------------------------------------------------------------- */

typedef struct {
    GameState *game;
    Platform *pf;
} LoadAsC;

static bool song_as_c(void *ctx)
{
    LoadAsC *c = ctx;
    bool back = false;
    TIMED_AS(c->game, LOCATION_SONG, back = start_the_location_song(c->game, c->pf, came_back_as_c, NULL));
    return back;
}

static bool map_as_c(void *ctx, Loading loading)
{
    (void)loading;
    LoadAsC *c = ctx;
    bool loaded = false;
    TIMED_AS(c->game, FIELD_UNPACK_THE_MAP_ENTRY, loaded = field_load_the_map(c->game));
    return loaded;
}

static bool script_as_c(void *ctx, Loading loading)
{
    (void)loading;
    LoadAsC *c = ctx;
    bool loaded = false;
    TIMED_AS(c->game, SCRIPT_UNPACKED, loaded = load_the_script(c->game));
    return loaded;
}

bool field_load_the_location(GameState *g, Platform *pf)
{
    static const LoadSteps as_c = { song_as_c, map_as_c, script_as_c };
    LoadAsC c = { g, pf };
    return load_location(g, pf, &as_c, &c);
}

/* ---- The routines it calls on the machine ---------------------------------------------------- */

/* The ports the setting up after a menu runs on the machine, by the routines
 * they stand for. */
enum { LINE_EFFECTS = 0xFDC2C1, LINE_EFFECTS_SET_UP = 0xFDC1EE, WHOLE_FIGURE = 0xC0E12A, MAP_SCRIPT_STARTS = 0xC0595C };
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_THE_OVERFLOW = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C,
};

static BridgeMachine page(const Snes *s) { return on_the_field_page(s, false); }

/* The song, the map and the script from the original's calls at the sites
 * given, by loading: the map and the script are entries of their own, where
 * the original's code takes over a map or a script the port doesn't. */
enum { SONG_SITE_ENTERING = 0xC000F7 };
static const struct {
    uint32_t map, script;
} loading_sites[] = {
    [ENTERING] = { 0xC00106, 0xC0010F },
    [AFTER_A_MENU] = { 0xC001CF, 0xC001D8 },
};

/* For shadow mode: the original works the record's place out in A, and its
 * high byte stays there for the song's calls to the driver. */
static void leave_the_place_in_a(Snes *s)
{
    s->cpu.a = (uint16_t)((location_record_offset(state_of(s)) & 0xFF00) | (s->cpu.a & 0x00FF));
}

static bool song_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    ORIGINAL_EFFECT(leave_the_place_in_a(s));
    return field_run(s, SONG_SITE_ENTERING, LOCATION_SONG, READS_A_HIGH, field_start_location_song, page(s));
}

static bool map_on_the_machine(void *ctx, Loading loading)
{
    Snes *s = ctx;
    return field_run(s, loading_sites[loading].map, FIELD_UNPACK_THE_MAP_ENTRY, READS_NOTHING, field_unpack_the_map,
                     page(s));
}

static bool script_on_the_machine(void *ctx, Loading loading)
{
    Snes *s = ctx;
    return field_run(s, loading_sites[loading].script, SCRIPT_UNPACKED, READS_NOTHING, field_load_the_script,
                     page(s));
}

static const LoadSteps on_the_machine = { song_on_the_machine, map_on_the_machine, script_on_the_machine };

/* ---- The entries ------------------------------------------------------------------------------- */

void field_load_location(Snes *s)
{
    if (load_location(state_of(s), m_platform(s), &on_the_machine, s))
        m_rts(s);
}

void field_load_location_palette_entry(Snes *s)
{
    field_load_location_palette(state_of(s));
    m_rts(s);
}

void field_clear_the_layers_state_entry(Snes *s)
{
    field_clear_the_layers_state(state_of(s), m_platform(s));
    m_rts(s);
}

/* Where the script doesn't fit, the original's check and its error screen,
 * from its call, which reads nothing but the overflow its callers keep. */
enum { SCRIPT_CHECKED = 0xC05703, THE_CHECK_READS_THE_OVERFLOW = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

void field_load_the_script(Snes *s)
{
    GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_V, tripling_overflows(the_locations_script(g)));
    if (!load_the_script(g)) {
        m_stop_at(s, SCRIPT_CHECKED, THE_CHECK_READS_THE_OVERFLOW);
        return;
    }
    m_rts(s);
}
/* The script's guard: on the field's page, whose cells name the location's
 * record, with the data bank reaching low RAM, where the unpacker's cells
 * are. */
bool field_script_can_be_loaded(Snes *s) { return field_on_page_low_bank(s); }

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
    if (!load_graphics_map_and_scripts(g, m_platform(s), &on_the_machine, s, AFTER_A_MENU))
        return;
    put_back_what_the_party_was_told(g);
    if (!field_run(s, 0xC00206, MAP_SCRIPT_STARTS, READS_THE_OVERFLOW, event_run_map_script, page(s)) ||
        !build_the_figure_again(s))
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
