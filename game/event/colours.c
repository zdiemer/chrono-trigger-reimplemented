/* Scripts and colours: an object's own palette, and effects on colours.
 *
 * A script can give its object one of the eight sprite palette slots for its
 * own, its colours loaded there ($33; field/palettes.h). It can set an effect
 * going on the object's colours or on any of the screen's ($88, $2E), through
 * the palette animation slots every mode runs (core/palette.c): its colours
 * rotating, a fade toward white or black, or colours of its own written over
 * them from the script. The object remembers a rotation running on its
 * colours, which the script can stop, its colours loaded again.
 *
 * An effect takes one of the first eight animation slots, the rest being the
 * modes' own; with all eight busy it is dropped, and the object's turn ends
 * there. The command's operands decode into the effect (ColourEffect,
 * event/script.h); the object's form names colours in its palette, the
 * screen's form among the 256. */
#include "charge.h"
#include "event/leaves.h"
#include "event/script.h"
#include "field/palettes.h"

// $C0:3711
/* The object takes a sprite palette slot of its own, its colours loaded
 * there. */
static EventFlow take_palette_slot(GameState *g, FieldObject self, uint8_t palette)
{
    object_take_palette_slot(g, self, palette);
    return next_command();
}
EVENT_OPERATION(take_palette_slot, self, palette)

/* ---- The animation slots a script's effects take ------------------------------------------- */

/* The first eight of the palette animation slots; a slot is free while it
 * runs nothing. */
enum { SCRIPT_ANIMATION_SLOTS = 8, NOTHING_RUNNING = 0 };

typedef struct {
    bool found;
    uint8_t slot;
} FreeSlot;

static FreeSlot free_animation_slot(const GameState *g)
{
    for (uint8_t slot = 0; slot < SCRIPT_ANIMATION_SLOTS; slot++)
        if (palette_animations_kind(g, slot) == NOTHING_RUNNING)
            return (FreeSlot){ true, slot };
    return (FreeSlot){ false, 0 };
}

/* The kind's low nibble: a fade's channels. */
enum { ANIMATION_TYPE = 0xF0, FADE_CHANNELS = 0x0F };

/* Every step of a rotation or a fade waits `period` frames, the first from
 * now. */
static void start_animation(GameState *g, uint8_t slot, uint8_t type, uint8_t first, uint8_t count, uint8_t period)
{
    set_palette_animations_kind(g, slot, type);
    set_palette_animations_first_color(g, slot, first);
    set_palette_animations_color_count(g, slot, count);
    set_palette_animations_period(g, slot, period);
    set_palette_animations_delay(g, slot, 0);
}

/* A fade's level and target, each given as a sixteenth of the way: the
 * nibble repeated makes the byte's fraction. It moves eight a step. */
enum { FADE_STEP = 8 };

static uint8_t sixteenths(uint8_t nibble) { return (uint8_t)(nibble << 4 | nibble); }

static void start_fade(GameState *g, uint8_t slot, const ColourEffect *e, unsigned first)
{
    start_animation(g, slot, e->animation & ANIMATION_TYPE, (uint8_t)first, e->count, e->period);
    set_palette_animations_fade_mode(g, slot, e->animation & FADE_CHANNELS);
    set_palette_animations_fade_level(g, slot, sixteenths(e->levels >> 4));
    set_palette_animations_fade_target(g, slot, sixteenths(e->levels & 0x0F));
    set_palette_animations_step(g, slot, FADE_STEP);
}

/* Colours from the script written over the palette on the screen and the one
 * as loaded, byte by byte from `first`'s: an odd count leaves the last
 * colour's high byte as it was. */
static void write_script_colours(GameState *g, unsigned first, uint16_t at, uint16_t bytes)
{
    uint32_t mark = 0;
    ORIGINAL_EFFECT(mark = charge_uncounted_begin(g));
    for (uint16_t b = 0; b < bytes; b += 2) {
        unsigned colour = first + b / 2u;
        uint8_t low = script_byte(g, (uint16_t)(at + b));
        uint16_t value = b + 1 < bytes ? (uint16_t)(low | script_byte(g, (uint16_t)(at + b + 1)) << 8)
                                       : (uint16_t)((palette_working(g, colour) & 0xFF00) | low);
        set_palette_working(g, colour, value);
        if (b + 1 >= bytes)
            value = (uint16_t)((palette_base(g, colour) & 0xFF00) | low);
        set_palette_base(g, colour, value);
    }
    ORIGINAL_EFFECT(charge_uncounted_end(g, mark));
    ORIGINAL_EFFECT(charge_block_move(g, 2u * bytes));
    (void)mark;
}

/* ---- The object's colours -------------------------------------------------------------------- */

/* The rotation the object remembers, by its slot's place among the
 * animations (the slot's number times its twelve bytes); none, a negative
 * word. */
enum { SLOT_BYTES = 12, NO_EFFECT = 0xFFFF, NO_EFFECT_BIT = 0x8000 };

static bool remembers_effect(const GameState *g, FieldObject obj)
{
    return !(field_object_colour_effect(g, obj) & NO_EFFECT_BIT);
}

bool event_object_remembers_colour_effect(const GameState *g, FieldObject obj) { return remembers_effect(g, obj); }

bool event_colour_effect_slot(const GameState *g, uint8_t *slot)
{
    FreeSlot free = free_animation_slot(g);
    *slot = free.slot;
    return free.found;
}

/* The effect stops, its slot free, and the object's colours are loaded
 * again over what it left. */
static void stop_object_effect(GameState *g, FieldObject obj)
{
    if (!remembers_effect(g, obj))
        return;
    uint8_t slot = (uint8_t)(field_object_colour_effect(g, obj) / SLOT_BYTES);
    set_field_object_colour_effect(g, obj, NO_EFFECT);
    set_palette_animations_kind(g, slot, NOTHING_RUNNING);
    object_load_colours(g, obj);
}

// $C0:4892
/* An effect on the object's colours: its rotation stopped, a rotation or a
 * fade started on some of them, or colours of the script's written over
 * them. */
static EventFlow change_object_colours(GameState *g, FieldObject self, ColourEffect effect)
{
    unsigned palette = object_first_colour(g, self);
    switch (effect.kind) {
    case COLOURS_STOP:
        stop_object_effect(g, self);
        break;
    case COLOURS_ROTATE:
    case COLOURS_FADE: {
        FreeSlot free = free_animation_slot(g);
        if (!free.found)
            return end_turn_after(effect.length);
        if (effect.kind == COLOURS_FADE) {
            start_fade(g, free.slot, &effect, palette + effect.first);
            break;
        }
        set_field_object_colour_effect(g, self, (uint16_t)(free.slot * SLOT_BYTES));
        start_animation(g, free.slot, effect.animation, (uint8_t)(palette + effect.first), effect.count,
                        effect.period);
        break;
    }
    case COLOURS_SET:
        write_script_colours(g, palette | effect.first, effect.colours_at, effect.bytes);
        break;
    default:
        break;
    }
    return next_command_after(effect.length);
}
EVENT_OPERATION(change_object_colours, self, colour_effect)

// $C0:4A4E
/* An effect on any of the screen's colours: a fade started on some of them,
 * or colours of the script's written over them. */
static EventFlow change_screen_colours(GameState *g, ColourEffect effect)
{
    if (effect.kind == COLOURS_FADE) {
        FreeSlot free = free_animation_slot(g);
        if (!free.found)
            return end_turn_after(effect.length);
        start_fade(g, free.slot, &effect, effect.first);
    } else if (effect.kind == COLOURS_SET) {
        write_script_colours(g, effect.first, effect.colours_at, effect.bytes);
    }
    return next_command_after(effect.length);
}
EVENT_OPERATION(change_screen_colours, colour_effect)

/* ---- What the port needs ------------------------------------------------------------------- */

/* Colours a script writes lie inside the palette and their bytes inside the
 * script, the bytes' count as the script gives it (one less than two counts
 * none and wraps into a whole bank); and the object keeps one of the eight
 * slots, and a rotation it remembers is in a slot. The game's are. */
static bool colours_written_fit(const GameState *g, ColourEffect effect, unsigned first)
{
    (void)g;
    return effect.bytes != 0 && effect.bytes < EVENT_SCRIPT_BYTES &&
           first * 2u + effect.bytes <= PALETTE_WORKING_COUNT * 2u &&
           (unsigned)effect.colours_at + effect.bytes <= EVENT_SCRIPT_BYTES;
}

static bool object_colours_reachable(const GameState *g, FieldObject self, ColourEffect effect)
{
    if (!object_palette_slot_is_one(g, self))
        return false;
    if (effect.kind == COLOURS_STOP && remembers_effect(g, self) &&
        field_object_colour_effect(g, self) % SLOT_BYTES != 0)
        return false;
    if (effect.kind == COLOURS_STOP && remembers_effect(g, self) &&
        field_object_colour_effect(g, self) / SLOT_BYTES >= PALETTE_ANIMATIONS_COUNT)
        return false;
    return effect.kind != COLOURS_SET || colours_written_fit(g, effect, object_first_colour(g, self) | effect.first);
}
EVENT_CHECK(object_colours_reachable, self, colour_effect)

static bool screen_colours_reachable(const GameState *g, ColourEffect effect)
{
    return effect.kind != COLOURS_SET || colours_written_fit(g, effect, effect.first);
}
EVENT_CHECK(screen_colours_reachable, colour_effect)

EVENT_OPCODES(event_colours_opcodes,
    { 0x33, OPERATION(take_palette_slot), .palette = OPERAND_BITS(1, 0, 0x07), .length = 2 },
    { 0x88, OPERATION(change_object_colours), .colour_effect = OBJECT_COLOUR_EFFECT(1), .length = 2,
      CHECK(object_colours_reachable) },
    { 0x2E, OPERATION(change_screen_colours), .colour_effect = SCREEN_COLOUR_EFFECT(1), .length = 2,
      CHECK(screen_colours_reachable) });
