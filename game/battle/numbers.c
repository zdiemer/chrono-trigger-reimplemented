/* Battle: the numbers that rise over the combatants.
 *
 * When an attack or a tech lands, the animation script shows its numbers: the
 * damage or the healing over each combatant it touched, or a miss. Each of the
 * script's number commands shows a set of them (battle_number_records, one a
 * combatant) in three steps, a frame or more each: the numbers' sprites are
 * put away and their colors put back; then each combatant with a number gets
 * up to four digit sprites (its own four), centered on where it stands, or on
 * its cursor point, and the numbers start to rise (battle/object_motion.c
 * moves them); and the command waits until they have risen before the script
 * moves on. The digits are the number sprites' own tiles, their zeros in
 * front left out; a miss is two tiles of its own. */
#include "battle/window.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/window_data.h"
#include "original_effects.h"
#include "vars.h"

/* ---- A combatant's number -------------------------------------------------------------- */

/* How a number shows (the record's kind): not at all, as a miss, and
 * otherwise as digits: in the second palette for kinds 1 and 2, and rising
 * the second way for 2 and 4 on. */
enum { NO_NUMBER = 0, SECOND_PALETTE_FIRST = 1, SECOND_PALETTE_RISING = 2, PLAIN_NUMBER = 3, MISS = 5 };
enum { NUMBER_ATTRIBUTES = 0x31, SECOND_PALETTE_ATTRIBUTES = 0x39 };

static bool in_second_palette(uint8_t kind) { return kind == SECOND_PALETTE_FIRST || kind == SECOND_PALETTE_RISING; }
static bool rises_the_second_way(uint8_t kind) { return kind != SECOND_PALETTE_FIRST && kind != PLAIN_NUMBER; }

/* A number's four digit tiles, the thousands first: the sprites' zero reads
 * as a blank in front. A miss is two tiles after two blanks. */
enum { DIGIT_SPRITES = 4, SPRITE_DIGIT_ZERO = 0x90, TILE_MISS = 0x9A };

typedef struct {
    uint8_t tile[DIGIT_SPRITES];
} SpriteDigits;

static SpriteDigits number_digits(const GameState *g, uint16_t n)
{
    return (SpriteDigits){ { battle_sprite_digit_tile(g, n / 1000), battle_sprite_digit_tile(g, n % 1000 / 100),
                             battle_sprite_digit_tile(g, n % 100 / 10), battle_sprite_digit_tile(g, n % 10) } };
}

static SpriteDigits miss_digits(void)
{
    return (SpriteDigits){ { SPRITE_DIGIT_ZERO, SPRITE_DIGIT_ZERO, TILE_MISS, TILE_MISS + 1 } };
}

/* How many of the first three digits are blanks in front; those aren't shown,
 * and the rest are centered by how many there are. */
static unsigned blanks_in_front(SpriteDigits d)
{
    unsigned blanks = 0;
    while (blanks < DIGIT_SPRITES - 1 && d.tile[blanks] == SPRITE_DIGIT_ZERO)
        blanks++;
    return blanks;
}

/* The original turns the number into digits in the window's digit buffer
 * ($CF:FDAD), leaving it holding the ones less ten, and writes a miss's
 * tiles there with a blank before them too. */
static void leave_number_digits(GameState *g, uint8_t kind, uint16_t n, SpriteDigits d)
{
    if (kind == MISS)
        set_battle_digits(g, 0, SPRITE_DIGIT_ZERO);
    else
        set_battle_number(g, (uint16_t)(n % 10 - 10));
    for (unsigned place = 0; place < DIGIT_SPRITES; place++)
        set_battle_digits(g, BATTLE_DIGIT_THOUSANDS + place, d.tile[place]);
}

/* A combatant's four sprites, and the bits of each in its high table byte:
 * all past the screen's edge until a digit shows. */
enum { SPRITES_PAST_THE_EDGE = 0x55, SPRITE_BITS = 0x03 };

typedef struct {
    uint8_t x, y;
} NumberPlace;

/* The original's work cells: the record's place and the sprites' (bytes);
 * the combatant's high bits; the attributes; the digits' offsets (after the
 * count of blanks); a word it clears; where the number stands. */
enum { RECORD_PLACE = 0x80, SPRITE_PLACE = 0x82, HIGH_BITS = 0x84, ATTRIBUTES = 0x8C, OFFSETS = 0x8E,
       CLEARED = 0x92, PLACE = 0x94, RECORD_BYTES = 4, SPRITE_BYTES = 4 };

static void leave_drawn_cells(GameState *g, unsigned blanks, uint8_t high_bits)
{
    battle_leave_work8(g, HIGH_BITS, high_bits);
    for (unsigned place = 0; place < DIGIT_SPRITES; place++)
        battle_leave_work8(g, (uint8_t)(OFFSETS + place), battle_digit_sprite_offset(g, blanks, place));
}

static void leave_combatant_cells(GameState *g, NumberPlace at, uint8_t attributes)
{
    battle_leave_work8(g, ATTRIBUTES, attributes);
    battle_leave_work8(g, PLACE, at.x);
    battle_leave_work8(g, (uint8_t)(PLACE + 1), at.y);
}

/* Show a combatant's number: the digits after the blanks in front, each
 * where its offset puts it. */
static void show_number(GameState *g, unsigned slot, NumberPlace at, SpriteDigits d, uint8_t attributes)
{
    unsigned blanks = blanks_in_front(d);
    uint8_t high_bits = SPRITES_PAST_THE_EDGE;
    for (unsigned place = blanks; place < DIGIT_SPRITES; place++) {
        unsigned sprite = DIGIT_SPRITES * slot + place;
        set_battle_number_sprites_tile(g, sprite, d.tile[place]);
        set_battle_number_sprites_x(g, sprite, (uint8_t)(at.x + battle_digit_sprite_offset(g, blanks, place)));
        set_battle_number_sprites_y(g, sprite, at.y);
        set_battle_number_sprites_attributes(g, sprite, attributes);
        high_bits &= (uint8_t)~(SPRITE_BITS << (2 * place));
    }
    set_battle_number_sprite_high(g, slot, high_bits);
    set_battle_prepared_sprites_on(g, slot, 1);
    ORIGINAL_EFFECT(leave_drawn_cells(g, blanks, high_bits));
}

static NumberPlace number_place(const GameState *g, unsigned slot)
{
    if (battle_number_at_cursor(g, slot))
        return (NumberPlace){ combatant_cursor_x(g, slot), combatant_cursor_y(g, slot) };
    return (NumberPlace){ combatant_x(g, slot), combatant_y(g, slot) };
}

// $C1:5B2D
/* The set's numbers, over every combatant that has one; the numbers start to
 * rise. The set is the command's (its low bits; the table of where each set
 * starts has six). */
static void show_numbers(GameState *g, unsigned set)
{
    set_battle_numbers_set(g, (uint8_t)set);
    unsigned first = battle_number_set_start(g, set) / RECORD_BYTES;
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        NumberPlace at = number_place(g, slot);
        uint8_t kind = battle_number_records_kind(g, first + slot);
        uint8_t attributes = in_second_palette(kind) ? SECOND_PALETTE_ATTRIBUTES : NUMBER_ATTRIBUTES;
        ORIGINAL_EFFECT(leave_combatant_cells(g, at, attributes));
        if (kind == NO_NUMBER)
            continue;
        if (kind != MISS && rises_the_second_way(kind))
            set_battle_number_rise_alt(g, slot, 1);
        uint16_t n = battle_number_records_value(g, first + slot);
        SpriteDigits d = kind == MISS ? miss_digits() : number_digits(g, n);
        ORIGINAL_EFFECT(leave_number_digits(g, kind, n, d));
        show_number(g, slot, at, d, attributes);
    }
    ORIGINAL_EFFECT(battle_leave_work16(g, RECORD_PLACE, (uint16_t)(RECORD_BYTES * (first + BATTLE_COMBATANTS))));
    ORIGINAL_EFFECT(battle_leave_work16(g, SPRITE_PLACE, (uint16_t)(SPRITE_BYTES * DIGIT_SPRITES * BATTLE_COMBATANTS)));
    ORIGINAL_EFFECT(battle_leave_work16(g, CLEARED, 0));
    set_battle_numbers_row(g, 0);
    set_battle_numbers_rising(g, 1);
}

/* ---- The number command ---------------------------------------------------------------- */

/* Put the numbers away: every number sprite off the screen (all its bytes
 * the off-screen value), their high bits clear, the object palette
 * animations stopped, and the numbers' colors (the first four of sprite
 * palettes 0 and 4) put back. */
enum { NUMBER_SPRITES = DIGIT_SPRITES * BATTLE_COMBATANTS, NUMBER_SPRITE_OFF = 0xF0, PALETTE_ANIMATIONS = 4 };
enum { NUMBER_COLORS = 4, FIRST_PALETTE_COLOR = 128, SECOND_PALETTE_COLOR = 192 };

static void put_numbers_away(GameState *g)
{
    for (unsigned sprite = 0; sprite < NUMBER_SPRITES; sprite++) {
        set_battle_number_sprites_x(g, sprite, NUMBER_SPRITE_OFF);
        set_battle_number_sprites_y(g, sprite, NUMBER_SPRITE_OFF);
        set_battle_number_sprites_tile(g, sprite, NUMBER_SPRITE_OFF);
        set_battle_number_sprites_attributes(g, sprite, NUMBER_SPRITE_OFF);
    }
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
        set_battle_number_sprite_high(g, slot, 0);
    for (unsigned i = 0; i < PALETTE_ANIMATIONS; i++)
        set_battle_palette_anim_on(g, i, 0);
    for (unsigned i = 0; i < NUMBER_COLORS; i++) {
        set_palette_working(g, FIRST_PALETTE_COLOR + i, battle_number_colors(g, i));
        set_palette_working(g, SECOND_PALETTE_COLOR + i, battle_number_colors(g, NUMBER_COLORS + i));
    }
}

/* The command's steps; a step past the second waits for the numbers to rise. */
enum { PUT_AWAY = 0, SHOW = 1, WAIT_FOR_RISE = 2 };
enum { NUMBER_SETS_MASK = 0x07 };

// $C1:5ADB
/* A number command's step; true when the script moves on past it. */
static bool run_number_command(GameState *g, uint8_t command)
{
    switch (battle_number_step(g)) {
    case PUT_AWAY:
        set_battle_number_step(g, SHOW);
        put_numbers_away(g);
        return false;
    case SHOW:
        set_battle_number_step(g, WAIT_FOR_RISE);
        show_numbers(g, command & NUMBER_SETS_MASK);
        return false;
    default:
        if (battle_numbers_rising(g))
            return false;
        set_battle_number_step(g, PUT_AWAY);
        return true;
    }
}

/* The script moves past a one-byte command. (The original adds to the
 * pointer's bank in a word, which only a pointer at the end of the address
 * space would carry into the byte after it.) */
static void next_script_command(GameState *g)
{
    set_battle_script_pointer(g, (battle_script_pointer(g) + 1) & 0xFFFFFF);
}

/* ---- The entry ------------------------------------------------------------------------- */

/* The script's commands $50-$55 (the command in battle_script_command). It
 * ends, as the script commands do, with A clear. */
void battle_script_build_number_sprites(Snes *s)
{
    GameState *g = state_of(s);
    if (run_number_command(g, battle_script_command(g)))
        next_script_command(g);
    s->cpu.a = 0;
    m_rts(s);
}
