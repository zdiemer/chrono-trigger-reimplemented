/* The healing a tonic or a tech gives in the menus, and the numbers that
 * show it.
 *
 * What restores says so in menu_use_effect: HP, MP or both, for the member
 * it is used on or for every active member at once. How much is in
 * menu_heal_power and menu_heal_factor: a fixed amount (the power times the
 * factor), or one drawn around the power, the same draw for every member's
 * HP and MP from the frame's place in the random numbers; or, where the
 * factor says all, each member's greatest. HP goes up to the greatest the
 * member's earring allows (the record's hp_cap) and MP to its greatest; the
 * amounts given are noted for the numbers (menu_heal_shown, none $FFFF).
 *
 * The numbers then rise from each healed member's sprite (the screens
 * stand the party's sprite records in a row): the HP's first, four digits
 * each, then the MP's, two, each with its sound, bouncing up along a path
 * of heights and settling. A task of the menus' runs them a frame at a time
 * (menu_raise_amounts), and the screen waits until it is done. The raising
 * of a figure by a tab shows its +1 the same way. */
#include "menu/healing.h"

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/digits.h"
#include "menu/item_screen.h"
#include "menu/members.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/sprites.h"
#include "menu/text.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* ---- The healing ------------------------------------------------------------------------------- */

/* The amounts: an HP and an MP amount for each of the six places of a
 * draw. */
enum { DRAWS = 6, HP_DRAWS = 0, MP_DRAWS = 3, NO_AMOUNT = 0xFFFF, MOST_HP = 999 };

/* An amount shown that is negative is no number (none is $FFFF). */
enum { NOT_A_NUMBER = 0x8000 };

/* How much a heal gives: a fixed amount (the power's low bits times the
 * factor's low nibble), or the power times the factor's low five bits and a
 * share of the power drawn at random, taken away instead of given where the
 * factor's top bit is set. The factor's bit 6 says all of it. */
enum { FIXED = 0x80, POWER_BITS = 0x7F, FIXED_FACTOR_BITS = 0x0F, DRAWN_FACTOR_BITS = 0x1F, TAKEN = 0x80,
       ALL_OF_IT = 0x40 };

/* For the original's code and shadow mode: it counts the members it heals
 * down in the menus' first cell. */
static void leave_members_healed(GameState *g) { set_menu_scratch_restoring_members_left(g, 0); }

/* ... and the amounts worked out, which it keeps on the menus' page. */
static void leave_amounts(GameState *g, const uint16_t amounts[DRAWS])
{
    for (unsigned i = 0; i < DRAWS; i++)
        set_menu_heal_amounts(g, i, amounts[i]);
}

/* ... and for a draw, the random bytes and the base it keeps in the menus'
 * first cells, and the multiplier as the last draw leaves it. */
static void leave_draws(GameState *g, uint8_t from, uint16_t base, uint8_t share_of, uint8_t last_drawn)
{
    for (unsigned i = 0; i < DRAWS; i++)
        set_menu_scratch_heal_drawn_byte(g, i, menu_random_byte(g, (unsigned)from + i));
    set_menu_scratch_heal_base(g, base);
    hw_set_multiplicand(g, share_of);
    (void)hw_multiply_by(g, last_drawn);
}

// $C2:8ECC
static void work_out_amounts(GameState *g, uint16_t amounts[DRAWS])
{
    uint8_t power = menu_heal_power(g), factor = menu_heal_factor(g);
    if (power & FIXED) {
        uint16_t amount = (uint16_t)((power & POWER_BITS) * (factor & FIXED_FACTOR_BITS));
        for (unsigned i = 0; i < DRAWS; i++)
            amounts[i] = amount;
        ORIGINAL_EFFECT(hw_set_multiplicand(g, power & POWER_BITS));
        ORIGINAL_EFFECT((void)hw_multiply_by(g, factor & FIXED_FACTOR_BITS));
        return;
    }
    uint16_t base = (uint16_t)(power * (factor & DRAWN_FACTOR_BITS));
    uint8_t from = menu_frame_counter(g);
    uint8_t share_of = (uint8_t)(power + 1), last_drawn = 0;
    for (unsigned i = 0; i < DRAWS; i++) {
        last_drawn = menu_random_byte(g, (unsigned)from + (DRAWS - 1 - i));
        uint16_t amount = (uint16_t)(base + (share_of * last_drawn >> 8));
        amounts[i] = (factor & TAKEN) ? (uint16_t)-amount : amount;
    }
    ORIGINAL_EFFECT(leave_draws(g, from, base, share_of, last_drawn));
}

/* A member's HP or MP given an amount, up to `greatest`; the amount given is
 * noted for its number. */
static uint16_t give(uint16_t now, uint16_t amount, uint16_t greatest)
{
    uint16_t after = (uint16_t)(now + amount);
    return after >= greatest ? greatest : after;
}

static void heal_member(GameState *g, unsigned member, const uint16_t amounts[DRAWS])
{
    uint8_t effect = menu_use_effect(g), character = menu_member_character(g);
    bool all = (menu_heal_factor(g) & ALL_OF_IT) != 0;
    if (effect & MENU_USE_RESTORES_HP) {
        uint16_t amount = all ? character_records_hp_cap(g, character) : amounts[HP_DRAWS + member];
        if (amount >= MOST_HP)
            amount = MOST_HP;
        set_menu_heal_shown(g, member, amount);
        set_character_records_hp(g, character, give(character_records_hp(g, character), amount, character_records_hp_cap(g, character)));
    }
    if (effect & MENU_USE_RESTORES_MP) {
        uint16_t amount = all ? character_records_max_mp(g, character) : amounts[MP_DRAWS + member];
        set_menu_heal_shown(g, MP_SHOWN + member, amount);
        set_character_records_mp(g, character, give(character_records_mp(g, character), amount, character_records_max_mp(g, character)));
    }
}

// $C2:8E2D
void menu_heal(GameState *g)
{
    for (unsigned i = 0; i < MENU_HEAL_SHOWN_COUNT; i++)
        set_menu_heal_shown(g, i, NO_AMOUNT);
    uint16_t amounts[DRAWS];
    work_out_amounts(g, amounts);
    ORIGINAL_EFFECT(leave_amounts(g, amounts));
    bool every = (menu_use_effect(g) & MENU_USE_FOR_EVERY_MEMBER) != 0;
    unsigned first = every ? 0 : menu_use_member(g), members = every ? menu_active_members(g) : 1;
    for (unsigned member = first; member < first + members; member++) {
        TIMED_AS(g, 0xC28820, (void)menu_load_member(g, (uint8_t)member));
        heal_member(g, member, amounts);
    }
    /* the healing goes through the members as the member the menus work on,
     * and leaves that one past the last, which the screen after it reads
     * (the techs screen keeps its high byte as it works on another) */
    menu_work_on_member(g, (uint8_t)(first + members));
    ORIGINAL_EFFECT(leave_members_healed(g));
}

/* ---- The numbers rising ---------------------------------------------------------------------- */

/* The numbers are sprites from the 33rd on, a digit each: the HP's four
 * digits a member, the MP's two, without leading zeros, centred on the
 * member's sprite (eight pixels a digit). Their tiles follow the amounts'
 * digits' first; they take the first sprite palette, in front. */
enum { FIRST_SPRITE = 32, FIRST_DIGIT_TILE = 0x74, DIGIT_ATTRIBUTES = 0x31, DIGIT_WIDTH = 8 };

/* The rising's own sprites' high bits (their size and the X past 255),
 * cleared each frame: a byte holds four sprites'. */
enum { FIRST_HIGH_BYTE = FIRST_SPRITE / 4, HIGH_BYTES = 3 };

/* Each kind of number: its digits, where its amounts are among those
 * shown, its sound, and where its path of heights starts. */
static const struct {
    uint8_t digits;
    uint8_t shown;
    MenuSound sound;
    uint8_t path;
} kinds[MENU_AMOUNT_KINDS] = {
    [MENU_HP_AMOUNTS] = { 4, 0, MENU_SOUND_6, 0x00 },
    [MENU_MP_AMOUNTS] = { 2, MP_SHOWN, MENU_SOUND_7, 0x2A },
};

MenuSound menu_amount_sound(MenuAmountKind kind) { return kinds[kind].sound; }

/* The amounts shown as decimal digits, compacted to the members that have
 * one; where each such member's sprite stands; and which kinds of number
 * rise. A member's HP amount notes the HP's alone, forgetting an earlier
 * member's MP: no healing gives one member MP and a later one HP alone. The
 * original converts each amount in turn ($C2:F13F). */
static uint16_t to_digits(GameState *g, uint16_t amount, MenuAmountKind kind)
{
    uint32_t digits = 0;
    TIMED_AS(g, 0xC2F13F, digits = menu_convert_number(g, amount, kind == MENU_HP_AMOUNTS ? 2 : 1));
    return (uint16_t)digits;
}

/* For the original's code and shadow mode: the listed members counted,
 * doubled, and the last member's numbers. */
static void leave_ready(GameState *g, unsigned listed, unsigned numbers)
{
    set_menu_scratch_amounts_listed_at(g, (uint16_t)(2 * listed));
    set_menu_scratch_member_amounts(g, (uint16_t)numbers);
}

// $C2:8F6C (up to its task's start)
void menu_ready_the_amounts(GameState *g)
{
    TIMED_AS(g, 0xFFF7D9, menu_build_amount_digits(g));
    TIMED_AS(g, 0xC28385, menu_queue_upload(g, menu_upload_record(g, MENU_UPLOAD_AMOUNT_DIGITS)));
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    unsigned listed = 0, numbers = 0;
    uint8_t kinds_shown = 0;
    for (uint8_t member = 0; member < menu_active_members(g); member++) {
        numbers = 0;
        for (unsigned k = 0; k < MENU_AMOUNT_KINDS; k++) {
            uint16_t amount = menu_heal_shown(g, kinds[k].shown + member);
            if (amount & NOT_A_NUMBER)
                continue;
            set_menu_heal_shown(g, kinds[k].shown + listed, to_digits(g, amount, (MenuAmountKind)k));
            kinds_shown = k == MENU_HP_AMOUNTS ? MENU_HP_SHOWN : (uint8_t)(kinds_shown | MENU_MP_SHOWN);
            numbers++;
        }
        if (numbers == 0)
            continue;
        TIMED_AS(g, 0xC28820, (void)menu_load_member(g, member));
        uint8_t record = menu_status_record_character(g);
        ORIGINAL_EFFECT(menu_leave_record_found(g, record));
        set_menu_amounts_places_y(g, listed, menu_record_y(g, record));
        set_menu_amounts_places_x(g, listed, (uint8_t)menu_record_x(g, record));
        listed++;
    }
    set_menu_amounts_listed(g, (uint16_t)listed);
    set_menu_amounts_kinds(g, kinds_shown);
    /* it looks at each member as the member the menus work on, and leaves
     * that one past the last, the whole argument a word, which the screen
     * after it reads (the techs screen keeps its high byte) */
    menu_work_on_member_only(g, menu_active_members(g));
    ORIGINAL_EFFECT(leave_ready(g, listed, numbers));
}

/* A number's digits as sprites, from its highest: a leading zero is no
 * sprite (its sprite is left as it was), the units always one. */
static void draw_number(GameState *g, unsigned *sprite, uint16_t digits, unsigned places, uint8_t x, uint8_t y)
{
    bool started = false;
    int offset = 0;
    for (unsigned place = places; place-- > 0; (*sprite)++) {
        uint8_t digit = (uint8_t)(digits >> (4 * place) & 0x0F);
        if (!started && digit == 0 && place != 0)
            continue;
        if (!started)
            offset = -(int)((place + 1) * DIGIT_WIDTH / 2);
        started = true;
        set_oam_buffer_x(g, *sprite, (uint8_t)(x + offset));
        set_oam_buffer_y(g, *sprite, y);
        set_oam_buffer_tile(g, *sprite, (uint8_t)(FIRST_DIGIT_TILE + digit));
        set_oam_buffer_attributes(g, *sprite, DIGIT_ATTRIBUTES);
        offset += DIGIT_WIDTH;
    }
}

/* A path of heights ends with a negative one. */
enum { PATH_ENDS = 0x80 };

// $C2:904F
bool menu_raise_numbers_a_frame(GameState *g, uint8_t shown_from, uint8_t places, uint8_t step)
{
    for (unsigned i = 0; i < HIGH_BYTES; i++)
        set_oam_high_table_byte(g, FIRST_HIGH_BYTE + i, 0);
    uint8_t height = menu_amount_height(g, step);
    if (height & PATH_ENDS)
        return false;
    unsigned sprite = FIRST_SPRITE;
    for (unsigned listed = 0; listed < menu_amounts_listed(g); listed++) {
        uint16_t digits = menu_heal_shown(g, shown_from + listed);
        if (digits & NOT_A_NUMBER)
            continue;
        draw_number(g, &sprite, digits, places, menu_amounts_places_x(g, listed),
                    (uint8_t)(menu_amounts_places_y(g, listed) - height));
    }
    return true;
}

uint8_t menu_amounts_path_start(MenuAmountKind kind) { return kinds[kind].path; }
uint8_t menu_amounts_digits(MenuAmountKind kind) { return kinds[kind].digits; }
uint8_t menu_amounts_shown_from(MenuAmountKind kind) { return kinds[kind].shown; }

/* ---- The healing shown, one sequence for the machine and for C ----------------------------------- */

/* The healing's numbers' colours, which the showing uses. */
static void colour_the_amounts(GameState *g)
{
    for (unsigned i = 0; i < MENU_AMOUNT_COLOURS; i++)
        set_menu_palette(g, MENU_FIRST_AMOUNT_COLOUR + i, menu_rom_colour(g, MENU_ROM_AMOUNT_COLOURS, i));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * MENU_AMOUNT_COLOURS));
}

// $C2:8F55
/* The healing, then its numbers in their colours, waited out. */
bool menu_heal_and_show_with(GameState *g, const HealingSteps *n, void *ctx)
{
    TIMED_AS(g, 0xC28E2D, menu_heal(g));
    colour_the_amounts(g);
    n->registers_at(ctx, HEALING_AT_THE_COLOURS);
    return n->run(ctx, HEALING_SHOWS_THE_AMOUNTS, 0);
}

// $C2:8F6C
/* The amounts made ready and their task started; then the wait for it to
 * finish, a frame at a time. */
bool menu_show_amounts_with(GameState *g, const HealingSteps *n, void *ctx)
{
    menu_ready_the_amounts(g);
    menu_start_task(g, MENU_AMOUNTS_TASK, MENU_RAISE_AMOUNTS_TASK_START);
    do
        if (!n->wait(ctx, HEALING_AT_THE_AMOUNTS_SHOWN))
            return false;
    while (menu_tasks_state(g, MENU_AMOUNTS_TASK) != 0);
    n->note(ctx, HEALING_NOTE_WAITED, 0);
    return true;
}

// $C2:9016
/* Each kind that rises, its own way, then the task's end. */
bool menu_raise_amounts_with(GameState *g, const HealingSteps *n, void *ctx)
{
    uint8_t kinds = (uint8_t)menu_amounts_kinds(g);
    for (unsigned k = 0; k < MENU_AMOUNT_KINDS; k++) {
        n->note(ctx, HEALING_NOTE_KINDS_LEFT, (uint8_t)(kinds >> (k + 1)));
        if (!(kinds & 1u << k))
            continue;
        set_menu_amounts_step(g, menu_amounts_path_start((MenuAmountKind)k));
        set_menu_amounts_last_digit(g, (uint8_t)(menu_amounts_digits((MenuAmountKind)k) - 1));
        set_menu_amounts_from(g, (uint8_t)(2 * menu_amounts_shown_from((MenuAmountKind)k)));
        n->registers_at(ctx, HEALING_AT_A_KIND);
        if (!n->run(ctx, HEALING_RAISES_A_KIND, (uint8_t)k))
            return false;
    }
    n->registers_at(ctx, HEALING_AT_THE_RISE_S_END);
    return n->ends(ctx);
}

/* The kind rising, as the task set it up: the one whose amounts start where
 * the task's cells say. */
static MenuAmountKind kind_rising(const GameState *g)
{
    for (unsigned k = 0; k < MENU_AMOUNT_KINDS; k++)
        if (menu_amounts_from(g) == 2 * menu_amounts_shown_from((MenuAmountKind)k))
            return (MenuAmountKind)k;
    return MENU_HP_AMOUNTS;
}

// $C2:9047
/* A kind's numbers rising: its sound, then a frame at a time along its
 * path, a wait between frames, until the path ends. */
bool menu_raise_amount_kind_with(GameState *g, const HealingSteps *n, void *ctx)
{
    if (!n->run(ctx, HEALING_SENDS_THE_KIND_S_SOUND, menu_amount_sound(kind_rising(g))))
        return false;
    uint8_t step = menu_amounts_step(g), last_digit = menu_amounts_last_digit(g), from = menu_amounts_from(g);
    for (;;) {
        n->registers_at(ctx, HEALING_AT_A_RISING_FRAME);
        set_menu_amounts_step(g, (uint8_t)(step + 1));
        if (!menu_raise_numbers_a_frame(g, (uint8_t)(from / 2), (uint8_t)(last_digit + 1), step)) {
            n->note(ctx, HEALING_NOTE_RISEN, step);
            return true;
        }
        n->note(ctx, HEALING_NOTE_A_FRAME_RISEN, step);
        step++;
        n->registers_at(ctx, HEALING_AT_THE_RISING_WAIT);
        if (!n->wait(ctx, HEALING_AT_THE_RISING_WAIT))
            return false;
    }
}

/* ---- As C ---- */

typedef struct {
    GameState *game;
    Platform *pf;
} HealingInC;

static bool wait_a_frame(void *ctx, HealingPlace at)
{
    (void)at;
    menu_task_waits(((HealingInC *)ctx)->game, 1);
    return true;
}

static void no_registers(void *ctx, HealingPlace at) { (void)ctx, (void)at; }
static void no_note(void *ctx, HealingNote note, uint8_t value) { (void)ctx, (void)note, (void)value; }
static bool task_ends(void *ctx) { return (void)ctx, true; }

static const HealingSteps healing_in_c;

static bool not_taken(GameState *g, uint32_t routine, const char *what)
{
    program_needs_the_machine(g, routine, what);
    return false;
}

static bool run_in_c(void *ctx, HealingRoutine r, uint8_t value)
{
    HealingInC *c = ctx;
    GameState *g = c->game;
    bool done = false;
    switch (r) {
    case HEALING_SHOWS_THE_AMOUNTS:
        if (!menu_amounts_can_show(g))
            return not_taken(g, 0xC28F6C, "amounts the port doesn't show");
        TIMED_AS(g, 0xC28F6C, done = menu_show_amounts_with(g, &healing_in_c, c));
        return done;
    case HEALING_RAISES_A_KIND:
        if (!menu_amounts_can_rise_now(g))
            return not_taken(g, 0xC29047, "amounts the port doesn't raise");
        TIMED_AS(g, 0xC29047, done = menu_raise_amount_kind_with(g, &healing_in_c, c));
        return done;
    case HEALING_SENDS_THE_KIND_S_SOUND:
        TIMED_AS(g, 0xC2EAF9, menu_sound_played_on_other_voices(g, c->pf, value));
        return true;
    }
    return false;
}

static const HealingSteps healing_in_c = { wait_a_frame, no_registers, run_in_c, no_note, task_ends };

bool menu_heal_and_show(GameState *g, Platform *pf)
{
    if (!menu_healing_can_run(g))
        return not_taken(g, 0xC28F55, "a healing the port doesn't take");
    HealingInC c = { g, pf };
    return menu_heal_and_show_with(g, &healing_in_c, &c);
}

bool menu_show_amounts(GameState *g, Platform *pf)
{
    if (!menu_amounts_can_show(g))
        return not_taken(g, 0xC28F6C, "amounts the port doesn't show");
    HealingInC c = { g, pf };
    return menu_show_amounts_with(g, &healing_in_c, &c);
}

void menu_raise_amounts_task(GameState *g, Platform *pf)
{
    if (!menu_amounts_can_show(g)) {
        (void)not_taken(g, 0xC29016, "amounts the port doesn't raise");
        return;
    }
    HealingInC c = { g, pf };
    (void)menu_raise_amounts_with(g, &healing_in_c, &c);
}

/* The guards' state halves: the healing for at most the three active
 * members, each loaded from a character's record; the showing's numbers for
 * at most the three active members; a kind's rise one of the two kinds as
 * the task sets them up, for the members the showing listed. */
bool menu_healing_can_run(const GameState *g)
{
    bool every = (menu_use_effect(g) & MENU_USE_FOR_EVERY_MEMBER) != 0;
    return every ? menu_active_members(g) <= MP_SHOWN : menu_use_member(g) < MP_SHOWN;
}

bool menu_amounts_can_show(const GameState *g) { return menu_active_members(g) <= MP_SHOWN; }

bool menu_amounts_can_rise_now(const GameState *g)
{
    bool ok = false;
    for (unsigned k = 0; k < MENU_AMOUNT_KINDS; k++)
        ok = ok || (menu_amounts_from(g) == 2 * menu_amounts_shown_from((MenuAmountKind)k)
                    && menu_amounts_last_digit(g) == menu_amounts_digits((MenuAmountKind)k) - 1);
    return ok && menu_amounts_listed(g) >= 1 && menu_amounts_listed(g) <= MP_SHOWN;
}
