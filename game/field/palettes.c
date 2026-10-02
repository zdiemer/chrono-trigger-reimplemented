/* Field mode: the sprite palette slots objects share.
 *
 * The field's objects draw with eight sprite palette slots. An object coming
 * into view takes one for its colors ($C0:7170, still the original's): a free
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

/* For the original's code and shadow mode: the sharer the original last
 * worked with. */
static void leave_last_sharer(GameState *g, FieldLink sharer)
{
    original_cell8(g, FIELD_LAST_WORKED_CELL, field_link_byte(sharer));
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
            ORIGINAL_EFFECT(leave_last_sharer(g, before));
            set_sharer_after(g, field_linked_object(before), field_no_link());
        }
        return;
    }
    /* Otherwise the sharers either side of it close up round it. */
    ORIGINAL_EFFECT(leave_last_sharer(g, after));
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
    ORIGINAL_EFFECT(mark = charge_uncounted_begin(g));
    for (unsigned n = 0; n < FIELD_SPRITE_COLOURS; n++) {
        uint16_t colour = field_sprite_colour(g, from, n);
        set_palette_base(g, to + n, colour);
        set_palette_working(g, to + n, colour);
    }
    ORIGINAL_EFFECT(charge_uncounted_end(g, mark));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * FIELD_SPRITE_COLOURS * 2));
    (void)mark;
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

/* ---- The entry ------------------------------------------------------------------------------ */

/* The current object (8-bit X). Its callers read none of the registers it
 * leaves. */
void field_give_up_palette(Snes *s)
{
    object_give_up_palette(state_of(s), (FieldObject)field_current_object_slot(s));
    m_rts(s);
}

/* The guard: the current object on the field's page with the data bank $00,
 * its slot and its sharers ones the tables hold. */
bool field_palette_can_be_given_up(Snes *s)
{
    if (!field_current_object_bank_zero(s))
        return false;
    vars_peek(true);
    bool ok = object_palette_links_in_the_tables(state_of(s), (FieldObject)field_current_object_slot(s));
    vars_peek(false);
    return ok;
}
