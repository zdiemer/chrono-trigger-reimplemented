/* Field mode: the sprite palette slots objects share.
 *
 * The field's objects draw with eight sprite palette slots. An object coming
 * into view takes one for its colors, among the slots for what it is: a free
 * slot, or one an object with the same colors already has, whose sharers it
 * joins. A slot's sharers are a list in the order they took it: each object
 * knows the sharer before it and the one after it, and the slot knows the
 * newest. An object leaving view, or an enemy a battle took off the map,
 * gives its slot up here; a slot whose last sharer leaves is free again.
 * While a script has loaded palettes itself (event $33), objects keep out of
 * the slots altogether, until the next map.
 *
 * An object's colours are twelve, from the ROM (field/cast.c says where), and
 * load into colours 1-12 of its slot's sixteen: in the palette as loaded and
 * in the one on the screen, which the palette animations work from and on. */
#include "field/palettes.h"
#include "field/field.h"

#include "charge.h"
#include "field/bridge.h"
#include "field/field_data.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

enum { PALETTE_SLOTS = FIELD_PALETTE_SLOT_NEWEST_COUNT };

/* The object's palette bits in its sprites' attributes are its slot,
 * doubled. */
static unsigned palette_slot(const GameState *g, FieldObject obj) { return field_object_palette(g, obj) >> 1; }

static FieldLink sharer_before(const GameState *g, FieldObject obj)
{
    return field_link_stored(field_object_palette_older(g, obj));
}
static FieldLink sharer_after(const GameState *g, FieldObject obj)
{
    return field_link_stored(field_object_palette_newer(g, obj));
}
static void set_sharer_before(GameState *g, FieldObject obj, FieldLink link)
{
    set_field_object_palette_older(g, obj, field_link_byte(link));
}
static void set_sharer_after(GameState *g, FieldObject obj, FieldLink link)
{
    set_field_object_palette_newer(g, obj, field_link_byte(link));
}

// $C0:734C
void object_give_up_palette(GameState *g, FieldObject obj)
{
    if (field_palettes_set_by_script(g))
        return;
    FieldLink before = sharer_before(g, obj), after = sharer_after(g, obj);
    bool first = !field_link_is_object(before);
    if (!field_link_is_object(after)) {
        /* The newest sharer: the one before it is the newest now, or no
         * object has the slot. */
        set_field_palette_slot_newest(g, palette_slot(g, obj), field_link_byte(first ? field_no_link() : before));
        if (!first) {
            set_sharer_after(g, field_linked_object(before), field_no_link());
        }
        return;
    }
    /* Otherwise the sharers either side of it close up round it. */
    set_sharer_before(g, field_linked_object(after), first ? field_no_link() : before);
    if (!first)
        set_sharer_after(g, field_linked_object(before), after);
}

/* ---- The object's colours ---------------------------------------------------------------- */

/* The sprite palettes are the second half of the 256 colours, sixteen each. */
enum { FIRST_SPRITE_COLOUR = 128, COLOURS_A_PALETTE = 16, FIRST_OBJECT_COLOUR = 1 };

unsigned object_first_colour(const GameState *g, FieldObject obj)
{
    return FIRST_SPRITE_COLOUR + palette_slot(g, obj) * COLOURS_A_PALETTE;
}

void object_load_colours(GameState *g, FieldObject obj)
{
    unsigned to = object_first_colour(g, obj) + FIRST_OBJECT_COLOUR;
    uint16_t from = field_object_colours(g, obj);
    uint32_t mark = 0;
    mark = charge_uncounted_begin(g);
    for (unsigned n = 0; n < FIELD_SPRITE_COLOURS; n++) {
        uint16_t colour = field_sprite_colour(g, from, n);
        set_palette_base(g, to + n, colour);
        set_palette_working(g, to + n, colour);
    }
    charge_uncounted_end(g, mark);
    charge_block_move(g, 2 * FIELD_SPRITE_COLOURS * 2);
    (void)mark;
}

/* Which slots an object takes its palette from, by what it is: each of the
 * party's places its own (the leader's 5, the members' 6 and 7), the enemies
 * 1-3, and everyone else the people's, 0-4. (The kinds past the objects left
 * alone, which the field treats as walking, take the people's too.) */
typedef struct {
    uint8_t first, end;
} SlotRange;

enum { PEOPLES_SLOTS_END = 5, KINDS_WITH_SLOTS = 12 };
static const SlotRange slots_for_kind[KINDS_WITH_SLOTS] = {
    [ROLE_LEADER] = { 5, 6 },
    [ROLE_PARTY_1] = { 6, 7 },
    [ROLE_PARTY_2] = { 7, 8 },
    [ROLE_LEFT_THE_PARTY] = { 0, PEOPLES_SLOTS_END },
    [ROLE_WALKS_ONLY] = { 0, PEOPLES_SLOTS_END },
    [ROLE_ENEMY] = { 1, 4 },
    [ROLE_ENEMY_SET_APART] = { 1, 4 },
    [ROLE_WINDOW_ONLY] = { 0, PEOPLES_SLOTS_END },
    [ROLE_LEFT_ALONE] = { 0, PEOPLES_SLOTS_END },
    [9] = { 0, PEOPLES_SLOTS_END },
    [10] = { 0, PEOPLES_SLOTS_END },
    [11] = { 0, PEOPLES_SLOTS_END },
};
static const SlotRange enemies_slots = { 1, 4 };

/* The object becomes the slot's newest sharer, after `older` (or its only
 * one), and its colours load into the slot. */
static void join_slot(GameState *g, FieldObject obj, uint8_t slot, FieldLink older)
{
    set_sharer_before(g, obj, older);
    set_sharer_after(g, obj, field_no_link());
    if (field_link_is_object(older))
        set_sharer_after(g, field_linked_object(older), field_link_to(obj));
    set_field_palette_slot_newest(g, slot, field_link_byte(field_link_to(obj)));
    set_field_object_palette(g, obj, (uint8_t)(slot << 1));
    object_load_colours(g, obj);
}

/* The object takes one of a range of slots: the first whose newest sharer
 * has the same colours, joining its sharers, or else the first free one;
 * with every one taken by other colours, none. */
static bool take_a_slot(GameState *g, FieldObject obj, SlotRange range)
{
    for (uint8_t slot = range.first; slot < range.end; slot++) {
        FieldLink newest = field_link_stored(field_palette_slot_newest(g, slot));
        if (!field_link_is_object(newest))
            continue;
        if (field_object_colours(g, field_linked_object(newest)) == field_object_colours(g, obj)) {
            join_slot(g, obj, slot, newest);
            return true;
        }
    }
    for (uint8_t slot = range.first; slot < range.end; slot++)
        if (!field_link_is_object(field_link_stored(field_palette_slot_newest(g, slot)))) {
            join_slot(g, obj, slot, field_no_link());
            return true;
        }
    return false;
}

/* A party place's object joining the sharers of its place's slot. The
 * original files it, here alone, by the number the slot's newest sharer has
 * in the object tables where the slot's number belongs ($C0:71FE-$C0:7210):
 * the object becomes the newest sharer of the slot that number would be,
 * or, past the eight slots, the owner of the block of sprite tiles it
 * reaches in the table after them, and wears the palette of the slot that
 * number names among the eight, whose colours it loads. Its place's slot
 * keeps the older sharer as its newest. Kept because the gate compares the
 * tables, and a later leaving reads them. */
enum { MISFILED_ENTRIES = PALETTE_SLOTS + FIELD_SPRITE_BLOCK_OWNER_COUNT };
static void join_party_place_sharers(GameState *g, FieldObject obj, FieldLink older)
{
    uint8_t misfiled = field_link_byte(older);
    set_sharer_before(g, obj, older);
    set_sharer_after(g, obj, field_no_link());
    set_sharer_after(g, field_linked_object(older), field_link_to(obj));
    if (misfiled < PALETTE_SLOTS)
        set_field_palette_slot_newest(g, misfiled, field_link_byte(field_link_to(obj)));
    else
        set_field_sprite_block_owner(g, misfiled - PALETTE_SLOTS, field_link_byte(field_link_to(obj)));
    set_field_object_palette(g, obj, (uint8_t)((misfiled % PALETTE_SLOTS) << 1));
    object_load_colours(g, obj);
}

/* A party place's object takes its place's own slot: joining the sharers
 * there where they have its colours, or the slot if it is free. (Where an
 * object of other colours holds it the guard turns the call away.) */
static bool take_party_place_slot(GameState *g, FieldObject obj, uint8_t slot)
{
    FieldLink newest = field_link_stored(field_palette_slot_newest(g, slot));
    if (!field_link_is_object(newest)) {
        join_slot(g, obj, slot, field_no_link());
        return true;
    }
    if (field_object_colours(g, field_linked_object(newest)) != field_object_colours(g, obj))
        return false;
    join_party_place_sharers(g, obj, newest);
    return true;
}

// $C0:7155
/* A map's screen starts with every slot free. */
void palette_slots_all_free(GameState *g)
{
    for (unsigned slot = 0; slot < PALETTE_SLOTS; slot++)
        set_field_palette_slot_newest(g, slot, field_link_byte(field_no_link()));
}

// $C0:72B4
/* An enemy takes one of the slots the enemies use. */
bool object_take_enemy_palette_slot(GameState *g, FieldObject obj) { return take_a_slot(g, obj, enemies_slots); }

// $C0:7170 (with $C0:7199, $C0:7212 and $C0:7217, the party's places', and $C0:721C, the people's)
/* An object coming into view takes a slot for its colours, unless a script
 * has set the palettes itself or the object is disabled. True where it took
 * one, and its colours loaded. */
bool object_take_palette(GameState *g, FieldObject obj)
{
    ObjectRole role = object_role(g, obj);
    if (field_palettes_set_by_script(g) || role.disabled)
        return false;
    if (role.kind <= ROLE_PARTY_2)
        return take_party_place_slot(g, obj, slots_for_kind[role.kind].first);
    return take_a_slot(g, obj, slots_for_kind[role.kind]);
}

bool object_takes_a_palette_the_tables_hold(const GameState *g, FieldObject obj)
{
    ObjectRole role = object_role(g, obj);
    if (field_palettes_set_by_script(g) || role.disabled)
        return true;
    if ((unsigned)role.kind >= KINDS_WITH_SLOTS)
        return false;
    SlotRange range = slots_for_kind[role.kind];
    for (uint8_t slot = range.first; slot < range.end; slot++) {
        FieldLink newest = field_link_stored(field_palette_slot_newest(g, slot));
        if (field_link_is_object(newest) && !field_link_names_object(newest))
            return false;
        /* a party place's slot held by an object of other colours: the
         * original gives up with its accumulator left 16-bit, which its
         * callers then run with */
        if (role.kind <= ROLE_PARTY_2 && field_link_is_object(newest) &&
            field_object_colours(g, field_linked_object(newest)) != field_object_colours(g, obj))
            return false;
        /* one of the same colours: the original files the object by the
         * sharer's number, which reaches past the blocks' table for an
         * object past the first 19 */
        if (role.kind <= ROLE_PARTY_2 && field_link_is_object(newest) && field_link_byte(newest) >= MISFILED_ENTRIES)
            return false;
    }
    return true;
}

/* The party's members wear the sprite palettes from slot 5 on, the leader's
 * first: a member moving up a place takes the palette of the place before
 * (event $D4's closing up, event/party.c). */
enum { LEADERS_PALETTE = 5 };

void party_palette_moves_up(GameState *g, unsigned from)
{
    unsigned to = FIRST_SPRITE_COLOUR + (LEADERS_PALETTE + from - 1u) * COLOURS_A_PALETTE;
    for (unsigned c = 0; c < COLOURS_A_PALETTE; c++)
        set_palette_base(g, to + c, palette_base(g, to + COLOURS_A_PALETTE + c));
    charge_block_move(g, 2 * COLOURS_A_PALETTE);
}

// $C0:3711
void object_take_palette_slot(GameState *g, FieldObject obj, uint8_t slot)
{
    field_mode_flag(g, FIELD_PALETTES_SET_BY_SCRIPT, true);
    set_field_object_palette(g, obj, (uint8_t)(slot << 1));
    object_load_colours(g, obj);
}

bool object_palette_slot_is_one(const GameState *g, FieldObject obj)
{
    uint8_t stored = field_object_palette(g, obj);
    return !(stored & 1) && palette_slot(g, obj) < PALETTE_SLOTS;
}

bool object_palette_links_in_the_tables(const GameState *g, FieldObject obj)
{
    FieldLink before = sharer_before(g, obj), after = sharer_after(g, obj);
    return palette_slot(g, obj) < PALETTE_SLOTS && (!field_link_is_object(before) || field_link_names_object(before)) &&
           (!field_link_is_object(after) || field_link_names_object(after));
}

/* While a script has set the palettes itself the object gives nothing up and
 * nothing of its sharers is read: Reunion's parade leaves an object leaving
 * view with links its slot no longer keeps (an odd newer sharer), which are
 * never looked at. */
bool object_palette_links_safe_to_give_up(const GameState *g, FieldObject obj)
{
    return field_palettes_set_by_script(g) || object_palette_links_in_the_tables(g, obj);
}
