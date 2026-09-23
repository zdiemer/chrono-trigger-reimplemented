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
    set_field_object_state_state(g, obj, (uint8_t)(role.kind | (role.disabled ? ROLE_DISABLED_BIT : 0)));
}

ObjectRole object_role(const GameState *g, FieldObject obj)
{
    uint8_t stored = field_object_state_state(g, obj);
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

uint8_t object_character(const GameState *g, FieldObject obj) { return field_object_state_character(g, obj); }

bool object_runs_a_script(const GameState *g, FieldObject obj)
{
    return !(field_object_state_character(g, obj) & NO_SCRIPT_BIT);
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

/* For the original's memory and shadow mode: the original clears the last
 * slot's solidity with a word, whose second byte is past the objects'
 * tables, where its memory has the first object's falling flag. */
static void leave_stray_clear_past_the_last(GameState *g) { set_field_object_accelerating(g, 0, 0); }

// $C0:58DE (the map setup's openings)
/* The original clears the solidity with a word, and so the next object's
 * sprite records' state with it: nothing to draw yet. */
void object_start_on_the_map(GameState *g, FieldObject obj)
{
    set_field_object_solid(g, obj, 0);
    if (obj + 1 < FIELD_OBJECTS)
        set_field_object_sprite_state(g, (FieldObject)(obj + 1), 0);
    else
        ORIGINAL_EFFECT(leave_stray_clear_past_the_last(g));
    set_field_object_accelerating(g, obj, 0);
    set_field_object_acceleration(g, obj, 0);
}

/* ---- Facing ------------------------------------------------------------------------------ */

Facing object_facing(const GameState *g, FieldObject obj) { return (Facing)field_object_facing(g, obj); }

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
enum { PRIORITY_SHIFT = 4, PRIORITY_MASK = 0x03, UPPER_PRIORITY_SHIFT = 4 };
enum { BATTLE_PLACES_IT = 0xFF };   /* field_object_battle_info */

void object_set_sprite_priorities(GameState *g, FieldObject obj, uint8_t upper, uint8_t lower)
{
    uint8_t low = (uint8_t)(lower << PRIORITY_SHIFT), high = (uint8_t)(upper << PRIORITY_SHIFT);
    set_field_object_sprite_priority(g, obj, (uint16_t)(high << 8 | low));
}

bool object_follows_the_map(const GameState *g, FieldObject obj)
{
    return (field_object_priority_mode(g, obj) & LAYERING_FOLLOWS_THE_MAP) != 0;
}

// $C0:4BA3
void object_layer_as_the_map(GameState *g, FieldObject obj, MapTile tile)
{
    TileAttributes there = map_tile_attributes(g, (MapPlace){ tile.y, tile.x });
    object_set_sprite_priorities(g, obj, tile_priority(there.upper), tile_priority(there.lower));
}

/* The halves take the priorities the layering's bits give. */
static void take_priorities_of(GameState *g, FieldObject obj, ObjectLayering layering)
{
    object_set_sprite_priorities(g, obj, (uint8_t)((layering >> UPPER_PRIORITY_SHIFT) & PRIORITY_MASK),
                                 (uint8_t)(layering & PRIORITY_MASK));
}

void object_take_own_priorities(GameState *g, FieldObject obj) { take_priorities_of(g, obj, field_object_priority_mode(g, obj)); }

// $C0:4C74
void object_set_layering(GameState *g, FieldObject obj, ObjectLayering layering)
{
    set_field_object_priority_mode(g, obj, layering);
    if (layering & LAYERING_FOLLOWS_THE_MAP)
        object_layer_as_the_map(g, obj, object_tile(g, obj));
    else
        take_priorities_of(g, obj, layering);
    set_field_object_battle_info(g, obj, BATTLE_PLACES_IT);
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

// $C0:4867
void object_set_solidity(GameState *g, FieldObject obj, ObjectSolidity solidity) { set_field_object_solid(g, obj, solidity); }

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
