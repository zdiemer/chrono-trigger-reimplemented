/* Battle: what a combatant's pose shows beside its frames.
 *
 * A combatant's statuses give it a pose (combatant_choose_pose,
 * battle/animation.c), and a pose has a look besides the animation it plays:
 * a marker sprite over the combatant (a clock while it is stopped, the
 * confusion's, sleep's, the seal on its techs, blindness's, poison's), its
 * palette cycling (hasted, slowed, shielded, behind a barrier), or for one
 * pose a marker and two colours of a party member's palette changed. The look
 * is put on when the pose changes: the old look is taken off first (the
 * marker, the cycle, a party member's colours put back) for every pose but
 * the stopped one, which keeps the colours it had.
 *
 * Every few frames the battle chooses the pose of a combatant in turn, going
 * round the combatants (the poses' round), and puts on the look of any pose
 * that changed; the action's showing goes the round once at its end, so that
 * every combatant takes its pose afresh (battle/action_showing.c). */
#include "battle/animation.h"
#include "battle/animation_data.h"
#include "battle/battle.h"
#include "battle/sprite_data.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "charge.h"
#include "original_effects.h"
#include "vars.h"

/* The poses a combatant's statuses give it ($CC:F77F), by what the look
 * shows; a pose with the top bit set (knocked out, or weak with low HP) looks
 * plain. Poses 1, 10 and 11 no status gives. */
typedef enum {
    POSE_PLAIN = 0,
    POSE_STOPPED = 2,     /* stop */
    POSE_HELD = 3,        /* the fourth status byte's top status */
    POSE_CONFUSED = 4,
    POSE_ASLEEP = 5,
    POSE_SHIELDED = 6,
    POSE_BARRIER = 7,
    POSE_NO_TECHS = 8,
    POSE_BLIND = 9,
    POSE_HASTED = 12,
    POSE_SLOWED = 13,
    POSE_POISONED = 14,
    POSES = 15,
} Pose;

enum { POSE_LOOKS_PLAIN = 0x80 };

/* The markers' kinds, each its four frames' place among the markers'
 * sprites (battle_marker_kind). */
typedef enum {
    NO_MARKER = 0,
    MARKER_CLOCK = 0x10,
    MARKER_HELD = 0x20,
    MARKER_CONFUSED = 0x30,
    MARKER_ASLEEP = 0x40,
    MARKER_NO_TECHS = 0x50,
    MARKER_BLIND = 0x60,
    MARKER_POISON = 0x70,
} MarkerKind;

/* A pose's look: whether the old look is taken off first, the marker it
 * shows, whether its palette cycles, and whether it changes a member's
 * colours. */
typedef struct {
    bool takes_off_the_old;
    MarkerKind marker;
    bool cycles;
    bool tints;
} PoseLook;

static const PoseLook looks[POSES] = {
    [POSE_PLAIN] = { .takes_off_the_old = true },
    [1] = { .marker = MARKER_CLOCK },
    [POSE_STOPPED] = { .marker = MARKER_CLOCK },
    [POSE_HELD] = { .takes_off_the_old = true, .marker = MARKER_HELD, .tints = true },
    [POSE_CONFUSED] = { .takes_off_the_old = true, .marker = MARKER_CONFUSED },
    [POSE_ASLEEP] = { .takes_off_the_old = true, .marker = MARKER_ASLEEP },
    [POSE_SHIELDED] = { .takes_off_the_old = true, .cycles = true },
    [POSE_BARRIER] = { .takes_off_the_old = true, .cycles = true },
    [POSE_NO_TECHS] = { .takes_off_the_old = true, .marker = MARKER_NO_TECHS },
    [POSE_BLIND] = { .takes_off_the_old = true, .marker = MARKER_BLIND },
    [10] = { .marker = MARKER_CLOCK },
    [11] = { .takes_off_the_old = true, .cycles = true },
    [POSE_HASTED] = { .takes_off_the_old = true, .cycles = true },
    [POSE_SLOWED] = { .takes_off_the_old = true, .cycles = true },
    [POSE_POISONED] = { .takes_off_the_old = true, .marker = MARKER_POISON },
};

/* ---- A party member's colours ------------------------------------------------------------- */

/* A party member's sprite palette is its object palette's sixteen colours
 * (battle_object_palette, the attribute bits, twice the palette's number)
 * among the sprites' eight, from colour 128. */
enum { SPRITE_COLOURS = 128, PALETTE_COLOURS = 16 };

/* The original finds the palette as the attribute bits sixteen times over,
 * in a byte, its colours' bytes from there. */
static unsigned palette_colour(const GameState *g, unsigned member, unsigned colour)
{
    return SPRITE_COLOURS + (uint8_t)(battle_object_palette(g, member) * PALETTE_COLOURS) / 2 + colour;
}

/* Its colours put back as loaded: thirteen of them, from the first. */
enum { COLOURS_PUT_BACK = 13 };

static void put_back_the_colours(GameState *g, unsigned member)
{
    for (unsigned colour = 0; colour < COLOURS_PUT_BACK; colour++) {
        unsigned at = palette_colour(g, member, colour);
        set_palette_working(g, at, palette_base(g, at));
    }
}

/* The held pose's tint: twelve colours put back from the second, two of
 * them then the tint's ($CC:F77B). */
enum { TINT_FIRST = 1, TINT_PUT_BACK = 12, TINTED_FIRST = 3, TINTED = 2 };

/* The original keeps the palette's place in the first work cells. */
static void leave_the_palette_place(GameState *g, unsigned member)
{
    uint8_t place = (uint8_t)(battle_object_palette(g, member) * PALETTE_COLOURS);
    set_battle_work(g, 0, place);
    set_battle_work(g, 1, 0);
}

static void tint_the_colours(GameState *g, unsigned member)
{
    ORIGINAL_EFFECT(leave_the_palette_place(g, member));
    for (unsigned colour = TINT_FIRST; colour < TINT_FIRST + TINT_PUT_BACK; colour++) {
        unsigned at = palette_colour(g, member, colour);
        set_palette_working(g, at, palette_base(g, at));
    }
    for (unsigned k = 0; k < TINTED; k++)
        set_palette_working(g, palette_colour(g, member, TINTED_FIRST + k), battle_held_tint(g, k));
}

/* ---- A look put on ----------------------------------------------------------------------- */

/* Cells the looks set that nothing traced reads: three the plain look
 * clears, one of which the held look counts. */
static void leave_the_plain_cells(GameState *g, unsigned slot)
{
    set_battle_pose_look_unread(g, slot, 0);
    set_battle_pose_look_unread_second(g, slot, 0);
    set_battle_pose_held_count(g, slot, 0);
}

// $C1:30B6
/* The old look taken off: no marker, no cycle, a member's colours as
 * loaded. */
void battle_take_off_the_look(GameState *g, unsigned slot)
{
    set_battle_marker_on(g, slot, 0);
    set_battle_party_palette_cycle_on(g, slot, 0);
    ORIGINAL_EFFECT(leave_the_plain_cells(g, slot));
    if (battle_slot_is_party(slot))
        put_back_the_colours(g, slot);
}

/* The marker's animation starts from before its first frame, at once, at
 * its kind's pace ($CC:F5FD, by kind). */
enum { MARKER_BEFORE_ITS_FIRST_FRAME = 0xFF, MARKER_KIND_STEP = 0x10 };

static void show_the_marker(GameState *g, unsigned slot, MarkerKind kind)
{
    set_battle_marker_on(g, slot, (uint8_t)(battle_marker_on(g, slot) + 1));
    set_battle_party_palette_cycle(g, slot, 0);
    set_battle_marker_frame(g, slot, MARKER_BEFORE_ITS_FIRST_FRAME);
    set_battle_marker_kind(g, slot, (uint8_t)kind);
    set_battle_marker_countdown(g, slot, 1);
    set_battle_marker_period(g, slot, battle_marker_pace(g, (uint8_t)(kind / MARKER_KIND_STEP - 1)));
}

static void put_on_the_look(GameState *g, unsigned slot, PoseLook look)
{
    if (look.takes_off_the_old)
        battle_take_off_the_look(g, slot);
    if (look.marker != NO_MARKER)
        show_the_marker(g, slot, look.marker);
    if (look.cycles) {
        set_battle_party_palette_cycle_on(g, slot, (uint8_t)(battle_party_palette_cycle_on(g, slot) + 1));
        set_battle_party_palette_cycle(g, slot, 0);
    }
    if (look.tints) {
        ORIGINAL_EFFECT(set_battle_pose_held_count(g, slot, (uint8_t)(battle_pose_held_count(g, slot) + 1)));
        if (battle_slot_is_party(slot))
            tint_the_colours(g, slot);
    }
}

// $C1:308C
/* Where the combatant's pose changed, its look changes: what the old one
 * showed is stopped (and the combatant is no longer down), and the new one
 * put on. */
void battle_show_pose_look(GameState *g, unsigned slot)
{
    if (!battle_slot_present(g, slot))
        return;
    uint8_t pose = battle_object_pose(g, slot);
    if (pose == battle_object_pose_last(g, slot))
        return;
    set_battle_object_pose_last(g, slot, pose);
    set_battle_marker_on(g, slot, 0);
    set_battle_party_palette_cycle(g, slot, 0);
    set_battle_combatant_down(g, slot, 0);
    set_battle_party_palette_cycle_on(g, slot, 0);
    put_on_the_look(g, slot, looks[(pose & POSE_LOOKS_PLAIN) ? POSE_PLAIN : pose]);
}

/* ---- The poses' round ---------------------------------------------------------------------- */

/* The round goes on from the enemies' first once it has come round. */
enum { ROUND_STARTS_AGAIN_AT = BATTLE_FIRST_ENEMY };

/* The original names the combatant it is at in a work cell, a word. */
enum { AT_CELL = 0x14 };
static void leave_the_combatant_at(GameState *g, unsigned slot)
{
    set_battle_work(g, AT_CELL, (uint8_t)slot);
    set_battle_work(g, AT_CELL + 1, 0);
}

// $C1:305C
/* The next combatant round takes the pose its statuses give it, and every
 * combatant whose pose changed puts its look on. */
void battle_go_round_the_poses(GameState *g)
{
    uint8_t slot = battle_next_enemy_checked(g);
    if (battle_slot_present(g, slot)) {
        ORIGINAL_EFFECT(leave_the_combatant_at(g, slot));
        TIMED_AS(g, 0xC12F97, (void)combatant_choose_pose(g, slot));
    }
    slot = (uint8_t)(slot + 1);
    if (slot == BATTLE_COMBATANTS) {
        set_battle_poses_round_done(g, (uint8_t)(battle_poses_round_done(g) + 1));
        slot = ROUND_STARTS_AGAIN_AT;
    }
    set_battle_next_enemy_checked(g, slot);
    for (unsigned each = 0; each < BATTLE_COMBATANTS; each++)
        TIMED_AS(g, 0xC1308C, battle_show_pose_look(g, each));
    ORIGINAL_EFFECT(leave_the_combatant_at(g, BATTLE_COMBATANTS));
}

/* The party's palettes keep the colours the looks put back among the
 * sprites' (the game's are 5 to 7); one further on would have them run on
 * past the palette. */
bool battle_pose_looks_fit(const GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (palette_colour(g, member, COLOURS_PUT_BACK) > SPRITE_COLOURS + SPRITE_COLOURS - 1)
            return false;
    return true;
}

/* ---- The entries ------------------------------------------------------------------------------ */

/* The round leaves A's high byte clear and the carry of its last count, set. */
void battle_go_round_the_poses_entry(Snes *s)
{
    battle_go_round_the_poses(state_of(s));
    s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* For the combatant the original names in the work cell. */
void battle_show_pose_look_entry(Snes *s)
{
    GameState *g = state_of(s);
    battle_show_pose_look(g, battle_work(g, AT_CELL));
    m_rts(s);
}

/* The poses' looks: a combatant named in the work cell, whose pose is one of
 * the table's or looks plain; the round's next combatant one of them. */
static bool pose_fits(const GameState *g, unsigned slot)
{
    uint8_t pose = battle_object_pose(g, slot);
    return (pose & POSE_LOOKS_PLAIN) || pose < POSES;
}

bool battle_pose_look_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    unsigned slot = battle_work(g, AT_CELL);
    bool fits = battle_work(g, AT_CELL + 1) == 0 && battle_names_combatant((uint16_t)slot) && pose_fits(g, slot)
                && battle_pose_looks_fit(g);
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && fits;
}

bool battle_poses_round_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = battle_names_combatant(battle_next_enemy_checked(g)) && battle_pose_looks_fit(g);
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
        fits = fits && pose_fits(g, slot);
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && fits;
}
