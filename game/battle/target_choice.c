/* Battle: whom the player can aim the member's action at (battle/choosing.h).
 *
 * Once the member has chosen an attack, a tech or an item, the menu asks each
 * frame whom the action can reach and lets the player pick. How depends on
 * the action's target kind (battle_target_kind): a member of the party, an
 * enemy, the party or the enemies all at once, everyone, the member itself, a
 * fallen member to revive, a particular character, or an area -- a beam, a
 * circle, a row or the party's triangle (area_search.c) -- placed on the
 * member, on an enemy the player picks, or on a particular character. The
 * kinds are a table here: the kind's row says what it picks, and for an area,
 * where the area goes and how the player moves the enemy it is placed on.
 *
 * Whom the action can reach is listed in battle_area_found (the list the area
 * search fills; the member choosing first where it is among them), the
 * player's cursor is a place in that list (battle_target_choice), and what is
 * chosen goes to battle_targets: the one under the cursor, or all of them
 * where the kind takes them together. An action aimed at the party first
 * turns the window over to show the party (window.c), which the menu puts back
 * when the target is chosen or the choosing is called off (menu_input.c). */
#include "battle/choosing.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/menu_bridge.h"
#include "charge.h"
#include "original_effects.h"
#include "vars.h"

/* ---- The list and the cursor ------------------------------------------------------------------------ */

enum { NOBODY_FOUND = 0xFF, LIST_PLACES = AREA_FOUND_PLACES + 1, KNOCKED_OUT = 0x80 };

static uint8_t found(const GameState *g, unsigned place) { return battle_area_found(g, place); }
static uint8_t under_cursor(const GameState *g) { return found(g, battle_target_choice(g)); }

static void clear_list(GameState *g)
{
    for (unsigned place = 0; place < LIST_PLACES; place++) {
        set_battle_area_found(g, place, NOBODY_FOUND);
        set_battle_targets(g, place, NOBODY_FOUND);
    }
}

/* Whether anyone is listed, a place that isn't empty, as the cursor asks
 * before it moves; and whether the choosing came to any target (a combatant,
 * its top bit clear), as it asks at its end. */
static bool list_holds_anyone(const GameState *g)
{
    for (unsigned place = 0; place < AREA_FOUND_PLACES; place++)
        if (found(g, place) != NOBODY_FOUND)
            return true;
    return false;
}

bool battle_anyone_aimed_at(const GameState *g)
{
    for (unsigned place = 0; place < AREA_FOUND_PLACES; place++)
        if (!menu_nobody(battle_targets(g, place)))
            return true;
    return false;
}

/* The cursor goes round the list a place at a time, past its empty places.
 * (The original goes round for ever where every place is empty but one isn't
 * $FF; no list it makes has such a place, and here it stops after a round.) */
static void step_cursor(GameState *g, bool forward)
{
    uint8_t place = battle_target_choice(g);
    for (unsigned tries = 0; tries < AREA_FOUND_PLACES; tries++) {
        place = forward ? (uint8_t)(place + 1 == AREA_FOUND_PLACES ? 0 : place + 1)
                        : (uint8_t)(place == 0 ? AREA_FOUND_PLACES - 1 : place - 1);
        if (!menu_nobody(found(g, place)))
            break;
    }
    set_battle_target_choice(g, place);
}

void battle_next_target(GameState *g) { step_cursor(g, true); }
void battle_previous_target(GameState *g) { step_cursor(g, false); }

/* The one under the cursor is the target; where the kind takes them
 * together, all of them are. */
static void take_targets(GameState *g)
{
    set_battle_targets(g, 0, under_cursor(g));
    if (battle_targets_together(g) & TARGETS_TOGETHER)
        for (unsigned place = 0; place < AREA_FOUND_PLACES; place++)
            set_battle_targets(g, place, found(g, place));
}

static void take_all_found(GameState *g)
{
    set_battle_targets_together(g, TARGETS_TOGETHER);
    for (unsigned place = 0; place < AREA_FOUND_PLACES; place++)
        set_battle_targets(g, place, found(g, place));
}

/* The cursor's moving about the window marks the frame it is on to follow it
 * (menu_window.c). */
static void cursor_moves_frame(GameState *g, bool moves) { set_battle_target_moves_frame(g, moves); }

/* ---- Picking from a side ------------------------------------------------------------------------- */

typedef struct {
    uint8_t first, end;
} Slots;

static const Slots PARTY = { 0, BATTLE_PARTY_SIZE }, ENEMIES = { BATTLE_FIRST_ENEMY, BATTLE_COMBATANTS },
                   EVERYONE = { 0, BATTLE_COMBATANTS };

enum { CELL_END = 0x80 };

/* Those on the side who can be aimed at, the member choosing first, then the
 * others in slot order; the list closes up where the member isn't on it. */
static void list_side(GameState *g, Slots side, bool fallen_too)
{
    uint8_t chooser = (uint8_t)battle_target_chooser(g);
    unsigned listed = 0;
    for (uint8_t slot = side.first; slot < side.end; slot++) {
        if (!battle_can_be_aimed_at(g, slot, fallen_too))
            continue;
        if (slot == battle_target_chooser(g))
            set_battle_area_found(g, 0, chooser);
        else
            set_battle_area_found(g, 1 + listed++, slot);
    }
    battle_close_up_finds(g);
    ORIGINAL_EFFECT(battle_leave_work16(g, CELL_END, side.end));
    take_targets(g);
}

/* ---- The kinds ------------------------------------------------------------------------------------- */


/* Where an area goes: on the member choosing, on an enemy the player picks
 * (moving the cursor one way, or either way), or on a particular character;
 * the party's triangle goes on the party. */
typedef enum { ON_CHOOSER, ON_PICKED_ENEMY, ON_CHARACTER, ON_THE_PARTY } AreaPlace;
typedef enum { PRESSES_GO_ON, PRESSES_GO_EITHER_WAY } EnemyCursor;

typedef struct {
    TargetPick pick;
    Slots side;          /* PICK_ONE, PICK_ALL */
    bool fallen_too;     /* PICK_ALL: the knocked-out too */
    uint8_t character;   /* PICK_CHARACTER, and an area ON_CHARACTER */
    AreaKind area;       /* PICK_AREA */
    AreaPlace place;
    EnemyCursor cursor;  /* ON_PICKED_ENEMY, and a beam toward it */
    uint8_t size;        /* the beam's half width in eighths, the circle's distance squared */
    bool short_beam;     /* the beam ends at the enemy */
} TargetKind;

/* The characters some kinds look for. */
enum { FROG = 4, AYLA = 5, ROBO = 3, MAGUS = 6 };

#define ONE(s) { .pick = PICK_ONE, .side = s }
#define ALL(s) { .pick = PICK_ALL, .side = s }
#define BEAM(at, who, short_) { .pick = PICK_AREA, .area = AREA_BEAM, .place = at, .character = who, \
                                .cursor = PRESSES_GO_ON, .size = 2, .short_beam = short_ }
#define CIRCLE(at, n) { .pick = PICK_AREA, .area = AREA_CIRCLE, .place = at, .size = n, .cursor = PRESSES_GO_EITHER_WAY }

static const TargetKind KINDS[TARGET_KINDS] = {
    ONE(PARTY), ALL(PARTY), { .pick = PICK_SELF }, { .pick = PICK_FALLEN },
    { .pick = PICK_ALL, .side = PARTY, .fallen_too = true },
    { .pick = PICK_CHARACTER, .character = AYLA }, { .pick = PICK_CHARACTER, .character = FROG },
    ONE(ENEMIES), ALL(ENEMIES), ALL(EVERYONE), ALL(ENEMIES),
    BEAM(ON_CHOOSER, 0, false), BEAM(ON_CHOOSER, 0, true), BEAM(ON_CHARACTER, ROBO, false),
    ONE(PARTY),
    { .pick = PICK_AREA, .area = AREA_ROW, .place = ON_PICKED_ENEMY, .cursor = PRESSES_GO_EITHER_WAY },
    ONE(PARTY),
    CIRCLE(ON_CHOOSER, 0x10), CIRCLE(ON_PICKED_ENEMY, 0x09),
    { .pick = PICK_AREA, .area = AREA_CIRCLE, .place = ON_CHARACTER, .character = ROBO, .size = 0x10 },
    { .pick = PICK_AREA, .area = AREA_CIRCLE, .place = ON_CHARACTER, .character = ROBO, .size = 0x19 },
    ONE(PARTY), ONE(PARTY), ONE(PARTY),
    { .pick = PICK_AREA, .area = AREA_PARTY_TRIANGLE, .place = ON_THE_PARTY },
    ONE(PARTY),
    CIRCLE(ON_PICKED_ENEMY, 0x19),
    { .pick = PICK_AREA, .area = AREA_CIRCLE, .place = ON_CHARACTER, .character = MAGUS, .size = 0x19 },
    ONE(PARTY), ONE(PARTY), ONE(PARTY), ONE(PARTY), ONE(PARTY),
};

uint8_t battle_target_kind_number(uint8_t kind)
{
    uint8_t number = (uint8_t)(kind & ~TARGET_KIND_ALLY);
    return number < TARGET_KINDS ? number : TARGET_KINDS - 1;
}

static const TargetKind *kind_of(uint8_t kind) { return &KINDS[battle_target_kind_number(kind)]; }

TargetPick battle_target_pick(uint8_t kind) { return kind_of(kind)->pick; }

static void nobody_to_aim_at(GameState *g) { set_battle_no_targets(g, MENU_NOBODY); }

/* The party member who is the character, by party slot; the original looks
 * among the party's places, whoever is in the battle. */
static int party_slot_of(const GameState *g, uint8_t character)
{
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++)
        if (party_members(g, slot) == character)
            return (int)slot;
    return -1;
}

/* The member who is the character, where it can act, looked for from the
 * last slot. */
static void pick_character(GameState *g, uint8_t character)
{
    cursor_moves_frame(g, true);
    ORIGINAL_EFFECT(battle_leave_work8(g, CELL_END, character));
    for (int slot = BATTLE_PARTY_SIZE - 1; slot >= 0; slot--) {
        if (battle_slot_present(g, slot) && !battle_member_sitting_out(g, slot) && party_members(g, slot) == character) {
            set_battle_area_found(g, 0, (uint8_t)slot);
            set_battle_targets(g, 0, (uint8_t)slot);
            return;
        }
    }
    nobody_to_aim_at(g);
}

/* The party's members knocked out, to be revived. */
static void pick_fallen(GameState *g)
{
    cursor_moves_frame(g, true);
    unsigned listed = 0;
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++)
        if (battle_slot_present(g, slot) && !battle_member_sitting_out(g, slot)
            && (battle_stats_condition(g, slot) & KNOCKED_OUT))
            set_battle_area_found(g, listed++, (uint8_t)slot);
    if (menu_nobody(found(g, 0)))
        nobody_to_aim_at(g);
    set_battle_targets(g, 0, under_cursor(g));
}

static void pick_self(GameState *g)
{
    uint8_t member = battle_menu_member(g);
    set_battle_area_found(g, 0, member);
    set_battle_targets(g, 0, member);
}

/* How a kind is chosen: the buttons the menu took, and the host that
 * clicks. */
typedef struct {
    Buttons pressed;
    MenuHost *host;
} Choosing;

/* The enemy an area is placed on, picked as one enemy is, the cursor moved by
 * the buttons pressed; each move clicks. A beam takes its finds together from
 * the first. */
static uint8_t pick_enemy_for_area(GameState *g, const TargetKind *k, const Choosing *c)
{
    Buttons pressed = c->pressed;
    list_side(g, ENEMIES, false);
    cursor_moves_frame(g, false);
    if (k->area == AREA_BEAM)
        set_battle_targets_together(g, TARGETS_TOGETHER);
    bool on = (pressed & (BUTTON_UP | BUTTON_RIGHT)) != 0, back = !on && (pressed & (BUTTON_DOWN | BUTTON_LEFT));
    if (on || back) {
        c->host->click(c->host, on ? CLICK_AREA_ENEMY_ON : CLICK_AREA_ENEMY_BACK);
        if (list_holds_anyone(g))
            step_cursor(g, on || k->cursor == PRESSES_GO_ON);
    }
    return under_cursor(g);
}

/* Those in an area. The area's search is asked in its cells (area_search.c),
 * each kind of area the parts of them it looks at. */
static void pick_area(GameState *g, const TargetKind *k, const Choosing *c)
{
    uint8_t picked = 0;
    if (k->place == ON_PICKED_ENEMY || k->area == AREA_BEAM) {
        picked = pick_enemy_for_area(g, k, c);
    }
    AreaAsked a = { .among_party = false, .toward = picked, .size = k->size, .flag = k->short_beam };
    if (k->place == ON_CHARACTER && k->area != AREA_BEAM)
        set_battle_area_search_among_party(g, false);   /* a circle notes its side before it looks */
    switch (k->place) {
    case ON_CHOOSER: a.centre = (uint8_t)battle_target_chooser(g); break;
    case ON_PICKED_ENEMY: a.centre = picked; break;
    case ON_THE_PARTY: a.centre = 0; break;
    case ON_CHARACTER: {
        int slot = party_slot_of(g, k->character);
        if (slot < 0)
            return;
        a.centre = (uint8_t)slot;
        break;
    }
    }
    battle_note_area_asked(g, k->area, &a);
    battle_find_in(g, k->area, &a);
    take_all_found(g);
}

/* ---- Choosing ------------------------------------------------------------------------------------- */

/* An action aimed at the party turns the window over to the party: the page
 * to go back to is kept, the command page's window is rebuilt for it, and
 * the frames are put back as the battle starts them. */
static void show_the_party(GameState *g)
{
    keep_page_for_targets(g, (MenuPage)battle_menu_page(g));
    set_battle_menu_page(g, PAGE_COMMANDS);
    set_battle_menu_slot_shown(g, NO_SLOT_SHOWN);
    set_battle_ally_target_window(g, (uint8_t)(battle_ally_target_window(g) + 1));
    TIMED_AS(g, 0xC11C3A, battle_reset_frames(g));
}

static void start_over(GameState *g, uint8_t chooser)
{
    set_battle_target_chooser(g, chooser);
    set_battle_no_targets(g, 0);
    set_battle_targets_together(g, 0);
    cursor_moves_frame(g, false);
    ORIGINAL_EFFECT(set_battle_target_start_spares(g, 0));
    ORIGINAL_EFFECT(set_battle_target_start_spare(g, 0));
    clear_list(g);
}

// $C1:1F79
bool battle_choose_targets(GameState *g, Buttons pressed, MenuHost *host)
{
    start_over(g, battle_menu_member(g));
    uint8_t kind = battle_target_kind(g);
    if ((kind & TARGET_KIND_ALLY) && !party_shown_for_targets(g))
        show_the_party(g);
    const TargetKind *k = kind_of(kind);
    Choosing c = { pressed, host };
    switch (k->pick) {
    case PICK_ONE:
        list_side(g, k->side, false);
        cursor_moves_frame(g, true);
        break;
    case PICK_ALL:
        set_battle_targets_together(g, TARGETS_TOGETHER);
        list_side(g, k->side, k->fallen_too);
        break;
    case PICK_SELF: pick_self(g); break;
    case PICK_FALLEN: pick_fallen(g); break;
    case PICK_CHARACTER: pick_character(g, k->character); break;
    case PICK_AREA: pick_area(g, k, &c); break;
    }
    if (!battle_anyone_aimed_at(g))
        set_battle_no_targets(g, battle_targets(g, AREA_FOUND_PLACES - 1));
    return menu_nobody(battle_no_targets(g));
}

/* ---- The entry ----------------------------------------------------------------------------------- */

/* What choosing takes on trust: the cursor on one of the eleven places, and
 * A's high byte clear, which the original carries into the chooser's word. */
bool battle_choose_targets_ready(Snes *s)
{
    vars_peek(true);
    bool fits = battle_target_choice(state_of(s)) < AREA_FOUND_PLACES;
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && fits;
}


