/* Who a map's objects are: the cast its scripts give them (field/cast.h).
 *
 * Three things make up casting an object:
 *
 * - its role and the character it is (field/object.h): a member of the party
 *   in a place of it, a playable character set aside or apart from the
 *   party, one of the people and creatures, or an enemy;
 * - its sprite: the sprite record that says how it looks put on it -- the
 *   assembly its frames are drawn from, the colours it loads, its animations
 *   and its layout -- and its graphics set: a playable character's straight
 *   from the ROM, the others' unpacked into bank $7F after those already
 *   there, or shared with an object of the map that has the same set
 *   unpacked. A new sprite starts standing, facing down, in front of the
 *   map's middle layers;
 * - its block of sprite tiles in VRAM, where its frames are sent: the
 *   party's three places have the first three blocks, and an enemy the block
 *   its script names. The others take a block as they come into view.
 *
 * The record tables are where field_sprite_data_tables points them, which the
 * field's start sets from the ROM (field_data.h reads them). */
#include "field/cast.h"

#include "charge.h"
#include "cpu_io.h"
#include "field/field_data.h"
#include "field/frames.h"
#include "field/graphics.h"
#include "field/party.h"
#include "field/scratch.h"
#include "original_effects.h"
#include "vars.h"

/* ---- The record tables ---------------------------------------------------------------------- */

/* The four tables a record's fields pick from (field_data.h), where
 * field_sprite_data_tables points them. The game points them all into bank
 * $E4; the guards leave any other place to the original. */
enum { SPRITE_TABLES_BANK = 0xE4 };

static uint32_t sprite_table(const GameState *g, FieldSpriteTable table) { return field_sprite_data_tables(g, table); }

bool cast_tables_in_the_rom(const GameState *g)
{
    for (FieldSpriteTable t = FIELD_SPRITE_GRAPHICS_SETS; t < FIELD_SPRITE_TABLES; t++)
        if (sprite_table(g, t) >> 16 != SPRITE_TABLES_BANK)
            return false;
    return true;
}

static ObjectGraphics long_entry(const GameState *g, FieldSpriteTable table, uint8_t index)
{
    uint32_t at = field_sprite_long_entry(g, sprite_table(g, table), index);
    return (ObjectGraphics){ (uint8_t)(at >> 16), (uint16_t)at };
}

/* For the original's code and shadow mode: what reading a record leaves --
 * its place in its table, the multiplier as working out its colours' place
 * left it, and the last of its fields worked out, in the first scratch cell.
 * Only the command that read the record reads them back. */
static void leave_record_read(GameState *g, uint16_t place, uint8_t palette, uint16_t last_field)
{
    set_field_sprite_record_place(g, place);
    hw_set_multiplicand(g, palette);
    hw_multiply_by(g, FIELD_SPRITE_COLOURS * 2);
    original_cell16(g, FIELD_SCRATCH_CELLS, last_field);
}

/* ---- A sprite put on ----------------------------------------------------------------------- */

static void set_graphics(GameState *g, FieldObject obj, ObjectGraphics set)
{
    set_field_object_graphics_bank(g, obj, set.bank);
    set_field_object_graphics_address(g, obj, set.address);
}

/* The record's fields but its graphics: the frames the object draws, the
 * colours it loads, its animations (as many to a facing as its frame table
 * holds, four frames each) and their times, and its layout. */
static void wear_record(GameState *g, FieldObject obj, FieldSpriteRecord r)
{
    ObjectGraphics frames = long_entry(g, FIELD_SPRITE_ASSEMBLIES, r.assembly);
    set_field_object_frames_address(g, obj, frames.address);
    set_field_object_frames_bank(g, obj, frames.bank);
    set_field_object_colours(g, obj, field_sprite_palette_place(r.palette));
    uint32_t animations = sprite_table(g, FIELD_SPRITE_ANIMATIONS);
    uint16_t table = field_sprite_word_entry(g, animations, r.animations);
    uint16_t next = field_sprite_word_entry(g, animations, (uint16_t)(r.animations + 1));
    set_field_object_facing_table_size(g, obj, (uint16_t)((uint16_t)(next - table) / FIELD_FRAMES_A_ANIMATION));
    set_field_object_frame_table(g, obj, table);
    uint32_t frame_times = sprite_table(g, FIELD_SPRITE_FRAME_TIMES);
    set_field_object_delay_table(g, obj, field_sprite_word_entry(g, frame_times, r.animations));
    set_field_object_sprite_layout(g, obj, r.layout);
}

/* A new sprite stands facing down, its halves in front of the map's middle
 * layers. */
enum { NEW_SPRITE_PRIORITY = 2 };

static void start_fresh(GameState *g, FieldObject obj)
{
    object_set_sprite_priorities(g, obj, NEW_SPRITE_PRIORITY, NEW_SPRITE_PRIORITY);
    object_start_standing(g, obj);
    object_set_facing(g, obj, FACING_DOWN);
}

// $C0:448D
/* A playable character's sprite, its graphics straight from the ROM. */
static void put_on_character_sprite(GameState *g, FieldObject obj, uint8_t character)
{
    FieldSpriteRecord r = field_sprite_record(g, character);
    wear_record(g, obj, r);
    set_graphics(g, obj, long_entry(g, FIELD_SPRITE_GRAPHICS_SETS, r.graphics));
    start_fresh(g, obj);
    ORIGINAL_EFFECT(leave_record_read(g, (uint16_t)(character * FIELD_SPRITE_RECORD_BYTES), r.palette, r.graphics));
}

/* ---- Blocks of sprite tiles ---------------------------------------------------------------- *
 *
 * VRAM's sprite tiles are given out in blocks, each four 16x16 sprites'
 * worth: eight tiles across two rows of them, two blocks to a pair of rows.
 * An object's frames are sent to its block, and its sprites are built in its
 * place among the sprite records. */

static uint16_t block_first_tile(unsigned block)
{
    enum { TILES_A_ROW = 16, TILES_ACROSS_A_BLOCK = 8 };
    return (uint16_t)(block / 2 * 2 * TILES_A_ROW + block % 2 * TILES_ACROSS_A_BLOCK);
}

static void use_block(GameState *g, FieldObject obj, unsigned block, uint16_t records)
{
    set_field_object_sprite_tiles(g, obj, block_first_tile(block));
    set_field_object_sprite_records(g, obj, records);
}

/* The party's places have the first three blocks, and their records each
 * place's share. The original keeps the owner as a word, so the block after
 * a member's goes to the first object (0) until the next member takes it: a
 * party of one keeps block 2 from the objects coming into view, which take
 * only free blocks. */
enum { PARTY_RECORDS_A_PLACE = 0x20 };

static void take_party_block(GameState *g, FieldObject obj, PartyPlace place)
{
    unsigned block = place + 1u;
    set_field_sprite_block_owner(g, block, field_link_byte(field_link_to(obj)));
    set_field_sprite_block_owner(g, block + 1, field_link_byte(field_link_to(0)));
    use_block(g, obj, block, (uint16_t)(place * PARTY_RECORDS_A_PLACE));
}

/* An enemy's records follow its block's number. */
enum { ENEMY_RECORDS_A_BLOCK = 0x20 };

static void take_enemy_block(GameState *g, FieldObject obj, unsigned block)
{
    set_field_sprite_block_owner(g, block, field_link_byte(field_link_to(obj)));
    use_block(g, obj, block, (uint16_t)(block * ENEMY_RECORDS_A_BLOCK));
}

/* ---- Playable characters ------------------------------------------------------------------- */

/* The place of a character in the party, as the party's record says. */
static bool party_place_of(const GameState *g, uint8_t character, PartyPlace *place)
{
    for (PartyPlace p = PARTY_LEADER; p < PARTY_PLACES; p++)
        if (party_members(g, p) == character) {
            *place = p;
            return true;
        }
    return false;
}

/* A location can put the party between two tiles across (the line on the
 * tile's left), and halfway up its tile rather than at its bottom; and says
 * which way it faces. */
enum { LOCATION_FACING = 0x03, LOCATION_BETWEEN_COLUMNS = 0x04, LOCATION_HALFWAY_UP = 0x08 };
enum { TILE_MIDDLE = 0x80, TILE_LEFT = 0x00, TILE_BOTTOM = 0xFF, TILE_HALFWAY = 0x7F };

static void stand_where_the_location_says(GameState *g, FieldObject obj)
{
    uint8_t flags = location_flags(g);
    uint8_t across = flags & LOCATION_BETWEEN_COLUMNS ? TILE_LEFT : TILE_MIDDLE;
    uint8_t down = flags & LOCATION_HALFWAY_UP ? TILE_HALFWAY : TILE_BOTTOM;
    FieldPosition at = { (uint16_t)(location_x(g) << 8 | across), (uint16_t)(location_y(g) << 8 | down) };
    object_place(g, obj, at);
    object_set_facing(g, obj, (Facing)(flags & LOCATION_FACING));
}

/* A member behind the leader starts on the leader, facing its way, with no
 * motion across; with nobody leading yet, where the location puts the party. */
static void stand_on_the_leader(GameState *g, FieldObject obj)
{
    FieldLink leader = party_member_link(g, PARTY_LEADER);
    if (!field_link_is_object(leader)) {
        stand_where_the_location_says(g, obj);
        return;
    }
    FieldObject on = field_linked_object(leader);
    object_place(g, obj, object_position(g, on));
    object_stop_moving_across(g, obj);
    object_face_as(g, obj, on);
}

static void join_the_party(GameState *g, FieldObject obj, uint8_t character, PartyPlace place)
{
    object_become_party_member(g, obj, place);
    object_set_blocked_by(g, obj, 0);
    object_set_sprite(g, obj, SPRITE_SHOWN);
    object_set_solidity(g, obj, true);
    object_set_character(g, obj, character);
    party_place_taken_by(g, place, obj);
    put_on_character_sprite(g, obj, character);
    if (place != PARTY_LEADER)
        object_first_frame_if_joining(g, obj);
    take_party_block(g, obj, place);
    object_move_a_step_at_a_time(g, obj);
    if (place == PARTY_LEADER) {
        stand_where_the_location_says(g, obj);
        return;
    }
    party_trail_rejoin(g);
    stand_on_the_leader(g, obj);
}

// $C0:41E4-$C0:4214, $C0:426A
void object_cast_as_character(GameState *g, FieldObject obj, uint8_t character)
{
    set_field_character_objects(g, character, field_link_byte(field_link_to(obj)));
    PartyPlace place;
    if (party_place_of(g, character, &place)) {
        join_the_party(g, obj, character, place);
        return;
    }
    object_set_aside(g, obj);
    object_set_character(g, obj, character);
}

// $C0:4476
void object_cast_as_character_apart(GameState *g, FieldObject obj, uint8_t character)
{
    object_become(g, obj, ROLE_LEFT_THE_PARTY);
    object_set_solidity(g, obj, true);
    object_set_character(g, obj, character);
    put_on_character_sprite(g, obj, character);
}

/* ---- Graphics unpacked into bank $7F --------------------------------------------------------- *
 *
 * The people's, the creatures' and most enemies' graphics sets are packed
 * in the ROM and unpacked into bank $7F, one after another from the middle of
 * the bank, as the map's scripts load them. A set already there is shared:
 * the map keeps which sets it has unpacked and which object unpacked each, a
 * list of 16 the setup clears, and an object of the same set takes that
 * one's graphics. */

enum { NO_SET = 0xFF };

static FieldLink unpacker(const GameState *g, unsigned entry)
{
    return field_link_stored(field_shared_graphics_object(g, entry));
}

typedef struct {
    bool found;
    unsigned entry;
} SharedEntry;

static SharedEntry entry_holding(const GameState *g, uint8_t set)
{
    for (unsigned e = 0; e < FIELD_SHARED_GRAPHICS_COUNT; e++)
        if (field_shared_graphics(g, e) == set)
            return (SharedEntry){ true, e };
    return (SharedEntry){ false, 0 };
}

/* The guards': an entry for the set, its unpacker an object the tables hold. */
bool cast_graphics_entry_for(const GameState *g, uint8_t set)
{
    SharedEntry held = entry_holding(g, set);
    if (held.found)
        return field_link_names_object(unpacker(g, held.entry));
    return entry_holding(g, NO_SET).found;
}

// $C0:5C90
/* The set an object of the map has unpacked already, or the first free entry
 * taken for this object's (the guards see there is one). */
static bool share_graphics(GameState *g, FieldObject obj, uint8_t set)
{
    SharedEntry held = entry_holding(g, set);
    if (held.found) {
        FieldObject other = field_linked_object(unpacker(g, held.entry));
        set_field_object_graphics_address(g, obj, field_object_graphics_address(g, other));
        return true;
    }
    SharedEntry free = entry_holding(g, NO_SET);
    set_field_shared_graphics(g, free.entry, set);
    set_field_shared_graphics_object(g, free.entry, field_link_byte(field_link_to(obj)));
    return false;
}

static uint16_t unpacked_end(const GameState *g) { return field_unpacked_graphics_end(g); }

/* Where the next set goes: after those already unpacked, in bank $7F. */
static ObjectGraphics next_unpacked_place(const GameState *g)
{
    return (ObjectGraphics){ FIELD_BUILD_BANK, unpacked_end(g) };
}

void ask_to_unpack(GameState *g, uint8_t set)
{
    ObjectGraphics from = long_entry(g, FIELD_SPRITE_GRAPHICS_SETS, set), to = next_unpacked_place(g);
    set_decompress_source(g, (uint32_t)from.bank << 16 | from.address);
    set_decompress_dest(g, (uint32_t)to.bank << 16 | to.address);
}

/* The sets fill the bank from its middle; one that would carry them past its
 * end is more than a map loads, and stops the game. */
enum { UNPACKED_FROM = 0x8000 };

bool graphics_unpacked(GameState *g)
{
    uint16_t end = (uint16_t)(unpacked_end(g) + decompress_length(g));
    set_field_unpacked_graphics_end(g, end);
    return end >= UNPACKED_FROM;
}

/* For the original's code and shadow mode: the graphics set worked out last,
 * in the scratch cell it keeps it in. */
enum { SET_CELL = FIELD_SCRATCH_CELLS + 10 };
static void leave_set(GameState *g, uint8_t set) { original_cell8(g, SET_CELL, set); }

/* ---- People, creatures and enemies -------------------------------------------------------- */

// $C0:4557
/* One of the others: enabled in its kind, solid, a new sprite with its
 * graphics after the sets unpacked so far until they are found. */
static void cast_as_another(GameState *g, FieldObject obj, ObjectKind kind, uint8_t character, FieldSpriteRecord r)
{
    object_become(g, obj, kind);
    object_set_character(g, obj, character);
    set_graphics(g, obj, next_unpacked_place(g));
    start_fresh(g, obj);
    object_set_solidity(g, obj, true);
    wear_record(g, obj, r);
    ORIGINAL_EFFECT(leave_set(g, r.graphics));
}

/* Graphics shared with an object of the map, or to unpack. */
static CastGraphics find_graphics(GameState *g, FieldObject obj, FieldSpriteRecord r)
{
    return (CastGraphics){ share_graphics(g, obj, r.graphics) ? GRAPHICS_SHARED : GRAPHICS_TO_UNPACK, r };
}

/* The first three people and creatures loaded note where their graphics are,
 * as the graphics' place of the battle's first three records; a count past
 * 128 notes in the first again, as the original's signed comparison takes it. */
enum { NOTED_NPCS = 3, SIGNED_COUNTS = 0x80 };

static void note_npc_graphics(GameState *g, uint16_t at)
{
    uint8_t count = field_npc_graphics_count(g);
    unsigned record = count == 0 || count > SIGNED_COUNTS ? 0 : count == 1 ? 1 : NOTED_NPCS - 1;
    uint32_t kept = battle_enemy_placement_graphics(g, record);
    set_battle_enemy_placement_graphics(g, record, (kept & 0xFF0000u) | at);
}

// $C0:4590
CastGraphics object_cast_as_npc(GameState *g, FieldObject obj, uint8_t npc)
{
    FieldSpriteRecord r = field_sprite_record(g, (uint16_t)(FIRST_NPC_RECORD + npc));
    cast_as_another(g, obj, ROLE_WALKS_ONLY, npc, r);
    CastGraphics found = find_graphics(g, obj, r);
    note_npc_graphics(g, unpacked_end(g));
    ORIGINAL_EFFECT(leave_record_read(g, (uint16_t)(npc * FIELD_SPRITE_RECORD_BYTES), r.palette,
                                      found.source == GRAPHICS_SHARED ? r.assembly : r.graphics));
    return found;
}

void npc_loaded(GameState *g) { set_field_npc_graphics_count(g, (uint8_t)(field_npc_graphics_count(g) + 1)); }

/* The last few enemies keep their graphics unpacked in the ROM. */
enum { ENEMIES_IN_THE_ROM = 0xF8 };

// $C0:46DF
CastGraphics object_cast_as_enemy(GameState *g, FieldObject obj, uint8_t enemy)
{
    FieldSpriteRecord r = battle_sprite_record(g, enemy);
    cast_as_another(g, obj, ROLE_ENEMY, enemy, r);
    CastGraphics found = { GRAPHICS_IN_THE_ROM, r };
    if (enemy >= ENEMIES_IN_THE_ROM)
        set_graphics(g, obj, long_entry(g, FIELD_SPRITE_GRAPHICS_SETS, r.graphics));
    else
        found = find_graphics(g, obj, r);
    ORIGINAL_EFFECT(leave_record_read(g, (uint16_t)(enemy * BATTLE_SPRITE_RECORD_BYTES), r.palette,
                                      found.source == GRAPHICS_SHARED ? r.assembly : r.graphics));
    return found;
}

/* An enemy's script names its block by the block before it, and the top bit
 * of the byte sets the enemy apart. */
enum { SET_APART = 0x80, BLOCK_BITS = 0x7F };

static unsigned enemy_block(uint8_t slot) { return (slot & BLOCK_BITS) + 1u; }

/* For the original's code and shadow mode: the slot byte, and the block over
 * the low byte of its first tile's row pair, in the scratch cells the
 * command worked them out in. */
enum { SLOT_CELL = FIELD_SCRATCH_CELLS + 8 };
static void leave_enemy_place(GameState *g, uint8_t slot, unsigned block)
{
    original_cell8(g, SLOT_CELL, slot);
    original_cell16(g, FIELD_SCRATCH_CELLS, (uint16_t)(block_first_tile(block & ~1u) & 0xFF00) | (uint8_t)block);
}

// $C0:4799
void enemy_takes_its_place(GameState *g, Platform *pf, FieldObject obj, uint8_t slot)
{
    unsigned block = enemy_block(slot);
    take_enemy_block(g, obj, block);
    ORIGINAL_EFFECT(leave_enemy_place(g, slot, block));
    if (object_sprite_layout(g, obj) == LAYOUT_24) {
        set_field_built_sprite_object(g, field_link_byte(field_link_to(obj)));
        TIMED_AS(g, 0xC0E12A, object_draw_whole_figure(g, pf, obj));
    }
    if (slot & SET_APART)
        object_become(g, obj, ROLE_ENEMY_SET_APART);
}

/* The guard's: the block among the blocks, and a figure's drawing one the
 * port does, with the graphics the enemy will have. */
bool cast_enemy_fits(const GameState *g, uint8_t enemy, uint8_t slot)
{
    unsigned block = enemy_block(slot);
    if (block >= FIELD_SPRITE_BLOCK_OWNER_COUNT)
        return false;
    FieldSpriteRecord r = battle_sprite_record(g, enemy);
    if (!(enemy >= ENEMIES_IN_THE_ROM || cast_graphics_entry_for(g, r.graphics)))
        return false;
    if (sprite_layout_of(r.layout) != LAYOUT_24)
        return true;
    ObjectGraphics set = next_unpacked_place(g);
    if (enemy >= ENEMIES_IN_THE_ROM) {
        set = long_entry(g, FIELD_SPRITE_GRAPHICS_SETS, r.graphics);
    } else {
        SharedEntry held = entry_holding(g, r.graphics);
        if (held.found)
            set.address = field_object_graphics_address(g, field_linked_object(unpacker(g, held.entry)));
    }
    return field_figure_drawable(g, set, long_entry(g, FIELD_SPRITE_ASSEMBLIES, r.assembly),
                                 (uint16_t)(block * ENEMY_RECORDS_A_BLOCK));
}
