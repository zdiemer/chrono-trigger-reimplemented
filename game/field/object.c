/* Field objects: what an object is, where it stands and how it looks, and what
 * a script sets about its moves (field/object.h).
 *
 * The event scripts set most of this -- an object's speed, what stops it, how
 * its sprite is layered, where it is put -- and the field reads it as objects
 * move, animate and draw (field/motion.c, field/animation.c,
 * field/object_update.c, field/sprites.c). Each concept's storage functions
 * come first and are the only code here that knows how the original keeps it:
 * the role's disabled flag as a byte's top bit, an unreachable object's mark
 * as its move length's, a tile crossing as a signed byte, the sprite's halves'
 * priorities as the hardware's attribute bits. */
#include "field/object.h"

#include "field/field_data.h"
#include "field/layers.h"
#include "original_effects.h"
#include "vars.h"

/* ---- Role ------------------------------------------------------------------------------ */

/* The original keeps the role in a byte with the disabled flag as its top
 * bit, beside a byte of the character, whose top bit says the object runs no
 * script. */
enum { ROLE_DISABLED_BIT = 0x80, NO_SCRIPT_BIT = 0x80 };

static void set_role(GameState *g, FieldObject obj, ObjectRole role)
{
    set_field_object_role(g, obj, (uint8_t)(role.kind | (role.disabled ? ROLE_DISABLED_BIT : 0)));
}

ObjectRole object_role(const GameState *g, FieldObject obj)
{
    uint8_t stored = field_object_role(g, obj);
    return (ObjectRole){ (stored & ROLE_DISABLED_BIT) != 0, (ObjectKind)(stored & ~ROLE_DISABLED_BIT) };
}

bool object_disabled(const GameState *g, FieldObject obj) { return object_role(g, obj).disabled; }

bool object_is_party_member(const GameState *g, FieldObject obj)
{
    ObjectRole r = object_role(g, obj);
    return !r.disabled && r.kind < ROLE_LEFT_THE_PARTY;
}

bool object_may_run(const GameState *g, FieldObject obj)
{
    ObjectRole r = object_role(g, obj);
    return !r.disabled && r.kind < ROLE_WALKS_ONLY;
}

// $C0:3A07
void object_leave_the_party(GameState *g, FieldObject obj) { set_role(g, obj, (ObjectRole){ false, ROLE_LEFT_THE_PARTY }); }

uint8_t object_character(const GameState *g, FieldObject obj) { return field_object_character(g, obj); }

bool object_runs_a_script(const GameState *g, FieldObject obj)
{
    return !(field_object_character(g, obj) & NO_SCRIPT_BIT);
}

/* An unreachable object's mark is kept as the top bit of its move length
 * (below). */
enum { UNREACHABLE_BIT = 0x80 };

bool object_unreachable(const GameState *g, FieldObject obj)
{
    return object_disabled(g, obj) || (field_object_step_length(g, obj) & UNREACHABLE_BIT);
}

// $C0:6269, $C0:6282
void object_set_unreachable(GameState *g, FieldObject obj, bool unreachable)
{
    uint8_t length = field_object_step_length(g, obj);
    set_field_object_step_length(g, obj, unreachable ? (uint8_t)(length | UNREACHABLE_BIT)
                                                     : (uint8_t)(length & ~UNREACHABLE_BIT));
}

void object_set_sprite(GameState *g, FieldObject obj, ObjectSprite sprite) { set_field_object_sprite_flag(g, obj, sprite); }

/* A sprite shown is kept as a positive byte; hidden and none are not. */
bool object_can_be_seen(const GameState *g, FieldObject obj)
{
    return !object_disabled(g, obj) && (int8_t)field_object_sprite_flag(g, obj) > 0;
}

// $C0:6254
void object_take_off_the_map(GameState *g, FieldObject obj)
{
    set_role(g, obj, (ObjectRole){ true, ROLE_LEADER });
    object_set_sprite(g, obj, SPRITE_HIDDEN);
}

/* For the original's memory and shadow mode: the original clears the
 * solidity with a word store, whose second byte is the next slot's sprite
 * records' state (and past the last slot, the first object's falling flag,
 * where its memory has that table). The game doesn't depend on it: an
 * object's records state is set again as it comes into view, before its
 * sprite is drawn (object_view.c; docs/RECOMP.md says how that was
 * checked). */
static void leave_word_clear_spill(GameState *g, FieldObject obj)
{
    if (obj + 1 < FIELD_OBJECTS)
        object_records_to_build(g, (FieldObject)(obj + 1));
    else
        set_field_object_accelerating(g, 0, 0);
}

// $C0:58DE (the map setup's openings)
void object_start_on_the_map(GameState *g, FieldObject obj)
{
    object_set_solidity(g, obj, false);
    ORIGINAL_EFFECT(leave_word_clear_spill(g, obj));
    set_field_object_accelerating(g, obj, 0);
    set_field_object_acceleration(g, obj, 0);
}

/* ---- Its sprite -------------------------------------------------------------------------- */

/* The layout is the byte's low two bits. */
enum { LAYOUT_BITS = 0x03 };
SpriteLayout object_sprite_layout(const GameState *g, FieldObject obj)
{
    return (SpriteLayout)(field_object_sprite_layout(g, obj) & LAYOUT_BITS);
}

/* The range is its layering's: 0 the middle, and of the others, the first
 * bit behind. */
SpriteRange object_sprite_range(const GameState *g, FieldObject obj)
{
    uint8_t range = layering_of_byte(field_object_priority_mode(g, obj)).range;
    return range == 0 ? RANGE_MIDDLE : (range & 1) ? RANGE_BACK : RANGE_FRONT;
}

uint16_t object_sprite_records(const GameState *g, FieldObject obj) { return field_object_sprite_records(g, obj); }

/* The high bits are kept in three words, eight sprites each. */
enum { SPRITES_A_HIGH_WORD = 8, SPRITES_A_HIGH_BYTE = 4 };

uint8_t object_sprite_high_bits(const GameState *g, FieldObject obj, unsigned n)
{
    unsigned w = n / SPRITES_A_HIGH_WORD;
    uint16_t word = w == 0   ? field_object_sprite_high(g, obj)
                    : w == 1 ? field_object_sprite_high_more(g, obj)
                             : field_object_sprite_high_last(g, obj);
    return (uint8_t)(word >> (8 * ((n / SPRITES_A_HIGH_BYTE) & 1)));
}

void object_set_first_sprites_high_bits(GameState *g, FieldObject obj, uint8_t bits)
{
    set_field_object_sprite_high(g, obj, (uint16_t)((field_object_sprite_high(g, obj) & 0xFF00) | bits));
}

/* The records' state is kept as a byte: the count, and the top bit set once
 * built (0 is nothing built and nothing changed). */
enum { RECORDS_BUILT_BIT = 0x80 };

RecordsState object_records_state(const GameState *g, FieldObject obj)
{
    uint8_t stored = field_object_sprite_state(g, obj);
    return (RecordsState){ (stored & RECORDS_BUILT_BIT) != 0, (uint8_t)(stored & ~RECORDS_BUILT_BIT) };
}

void object_mark_records_built(GameState *g, FieldObject obj) { set_field_object_sprite_state(g, obj, RECORDS_BUILT_BIT); }
void object_records_to_build(GameState *g, FieldObject obj) { set_field_object_sprite_state(g, obj, 0); }

/* The battle's placement byte: $FF leaves it to the battle. */
enum { THE_BATTLE_PLACES_IT = 0xFF };
void object_let_the_battle_place(GameState *g, FieldObject obj) { set_field_object_battle_info(g, obj, THE_BATTLE_PLACES_IT); }

/* ---- Facing ------------------------------------------------------------------------------ */

/* The facing is kept as a byte, which the game only ever sets to one of the
 * four; the guards leave any other to the original (object.h). */
enum { FACINGS = 4 };
Facing object_facing(const GameState *g, FieldObject obj) { return (Facing)(field_object_facing(g, obj) % FACINGS); }
bool object_facing_is_one(const GameState *g, FieldObject obj) { return field_object_facing(g, obj) < FACINGS; }

void object_set_facing(GameState *g, FieldObject obj, Facing facing) { set_field_object_facing(g, obj, (uint8_t)facing); }

// $C0:554D
void object_turn(GameState *g, FieldObject obj, Facing facing)
{
    object_set_facing(g, obj, facing);
    object_end_frame_time(g, obj);
}

Facing facing_of_direction(const GameState *g, Direction direction) { return (Facing)field_facing_of_direction(g, direction); }

/* ---- Layering ---------------------------------------------------------------------------- */

/* The halves' priorities are kept as the hardware's sprite attribute bits
 * (4-5), the lower half's in the first byte. */
enum { PRIORITY_SHIFT = 4 };

/* The layering's byte (object.h). */
enum { FOLLOWS_THE_MAP_BIT = 0x80, NO_LAYERING_BIT = 0x40, UPPER_SHIFT = 4, RANGE_SHIFT = 2, TWO_BITS = 0x03 };

ObjectLayering layering_of_byte(uint8_t byte)
{
    return (ObjectLayering){ (byte & FOLLOWS_THE_MAP_BIT) != 0, (uint8_t)((byte >> UPPER_SHIFT) & TWO_BITS),
                             (uint8_t)(byte & TWO_BITS), (uint8_t)((byte >> RANGE_SHIFT) & TWO_BITS) };
}

bool layering_byte_is_one(uint8_t byte) { return !(byte & NO_LAYERING_BIT); }

static uint8_t layering_byte(ObjectLayering l)
{
    return (uint8_t)((l.follows_the_map ? FOLLOWS_THE_MAP_BIT : 0) | (l.upper & TWO_BITS) << UPPER_SHIFT |
                     (l.range & TWO_BITS) << RANGE_SHIFT | (l.lower & TWO_BITS));
}

static ObjectLayering own_layering(const GameState *g, FieldObject obj)
{
    return layering_of_byte(field_object_priority_mode(g, obj));
}

void object_set_sprite_priorities(GameState *g, FieldObject obj, uint8_t upper, uint8_t lower)
{
    uint8_t low = (uint8_t)(lower << PRIORITY_SHIFT), high = (uint8_t)(upper << PRIORITY_SHIFT);
    set_field_object_sprite_priority(g, obj, (uint16_t)(high << 8 | low));
}

bool object_follows_the_map(const GameState *g, FieldObject obj) { return own_layering(g, obj).follows_the_map; }

// $C0:4BA3
void object_layer_as_the_map(GameState *g, FieldObject obj, MapTile tile)
{
    TileAttributes there = map_tile_attributes(g, (MapPlace){ tile.y, tile.x });
    object_set_sprite_priorities(g, obj, tile_priority(there.upper), tile_priority(there.lower));
}

void object_take_own_priorities(GameState *g, FieldObject obj)
{
    ObjectLayering own = own_layering(g, obj);
    object_set_sprite_priorities(g, obj, own.upper, own.lower);
}

// $C0:4C74
void object_set_layering(GameState *g, FieldObject obj, ObjectLayering layering)
{
    set_field_object_priority_mode(g, obj, layering_byte(layering));
    if (layering.follows_the_map)
        object_layer_as_the_map(g, obj, object_tile(g, obj));
    else
        object_set_sprite_priorities(g, obj, layering.upper, layering.lower);
    object_let_the_battle_place(g, obj);
    object_end_frame_time(g, obj);
}

/* ---- What a script sets about its moves -------------------------------------------------- */

// $C0:4B49
void object_set_speed(GameState *g, FieldObject obj, uint8_t speed) { set_field_object_speed(g, obj, speed); }

// $C0:6295
void object_set_blocked_by(GameState *g, FieldObject obj, ObjectBlocking blocking)
{
    set_field_object_blocking(g, obj, blocking);
}

/* Solidity is kept as a byte, 1 or 0. */
enum { SOLID = 0x01 };

// $C0:4867
void object_set_solidity(GameState *g, FieldObject obj, bool solid) { set_field_object_solid(g, obj, solid ? SOLID : 0); }
bool object_solid(const GameState *g, FieldObject obj) { return (field_object_solid(g, obj) & SOLID) != 0; }

// $C0:62A4
void object_set_arrival(GameState *g, FieldObject obj, ObjectArrival arrival) { set_field_object_arrival_flags(g, obj, arrival); }

bool object_lines_up(const GameState *g, FieldObject obj, ObjectArrival how)
{
    return (field_object_arrival_flags(g, obj) & how) != 0;
}

// $C0:4876
/* The length keeps the unreachable mark; the original keeps a second copy of
 * it, which its party's walks clear. */
void object_set_move_length(GameState *g, FieldObject obj, uint8_t steps)
{
    steps = (uint8_t)(steps | (field_object_step_length(g, obj) & UNREACHABLE_BIT));
    set_field_object_step_length(g, obj, steps);
    set_field_object_step_count(g, obj, steps);
}

uint8_t object_move_length(const GameState *g, FieldObject obj) { return field_object_step_length(g, obj); }

/* The mark is kept as a byte, 1 while the move runs. */
enum { SCRIPTED_MOVE_MARK = 1 };

void object_mark_scripted_move(GameState *g, FieldObject obj) { set_field_object_counted_move(g, obj, SCRIPTED_MOVE_MARK); }
bool object_scripted_move_marked(const GameState *g, FieldObject obj) { return field_object_counted_move(g, obj) != 0; }
void object_unmark_scripted_move(GameState *g, FieldObject obj) { set_field_object_counted_move(g, obj, 0); }


/* ---- The party ------------------------------------------------------------------------- */

FieldLink party_member_link(const GameState *g, unsigned place) { return field_link_stored(field_party_objects(g, place)); }
void party_place_left_empty(GameState *g, unsigned place) { set_field_party_objects(g, place, field_link_byte(field_no_link())); }

void dialog_set_speaker(GameState *g, FieldObject speaker) { set_dialog_speaker(g, field_link_byte(field_link_to(speaker))); }

/* ---- The current object ------------------------------------------------------------------ */

/* The word's low byte is the object's link; the high byte is the original's
 * alone (the per-frame turn stores the link with 8-bit index registers, and
 * keeps it; the map's openings store it as a word, which the event engine
 * says where it does). */
FieldObject object_current(const GameState *g) { return field_linked_object(field_link_stored((uint8_t)field_current_object(g))); }
bool object_current_is_one(const GameState *g) { return field_names_object(field_current_object(g)); }

void object_make_current(GameState *g, FieldObject obj)
{
    set_field_current_object(g, (uint16_t)((field_current_object(g) & 0xFF00) | field_link_byte(field_link_to(obj))));
}
