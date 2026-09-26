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
 * the slots altogether, until the next map. */
#include "field/palettes.h"
#include "field/field.h"

#include "field/bridge.h"
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
