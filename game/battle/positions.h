/* Battle: where the combatants stand to each other.
 *
 * The battle asks questions of the battlefield's layout: which party member
 * stands nearest an enemy, or farthest from it; whether another combatant is
 * within its reach, level with it, below or to the right of it; whether it
 * stands in the battlefield's upper or lower half; whether it could walk to
 * another without the battlefield's grid or another combatant stopping it.
 * The enemies' scripts ask them in their conditions and in choosing whom to
 * act on, and a party member facing its nearest enemy asks the first.
 *
 * The questions go by the combatants' boxes on the screen (battle_bounds_*,
 * which a search works out afresh around where each stands) and the
 * direction from one to another, an angle in 256ths of a turn.
 *
 * A search leaves its answer where the original's callers look for it as
 * well as returning it: whether its test held (battle_search_failed) and
 * whom it found (battle_target_found). */
#ifndef CT_GAME_BATTLE_POSITIONS_H
#define CT_GAME_BATTLE_POSITIONS_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

/* What a search asks, by the number the original's code gives it. */
typedef enum {
    SEARCH_NEAREST_MEMBER = 0,       /* the party member standing nearest the searcher */
    SEARCH_FARTHEST_MEMBER = 1,      /* the party member standing farthest from it */
    SEARCH_NEAREST_OTHER_ENEMY = 2,  /* the enemy nearest it, itself left out */
    SEARCH_FARTHEST_ENEMY = 3,       /* the enemy farthest from it */
    SEARCH_WITHIN_REACH = 4,         /* whether the other is within its reach */
    SEARCH_LEVEL = 5,                /* whether the other stands level with it, 32 pixels or less above or below */
    SEARCH_OTHER_BELOW = 6,          /* whether the other stands lower on the screen */
    SEARCH_OTHER_TO_THE_RIGHT = 7,   /* whether the other stands to its right */
    SEARCH_WALK = 8,                 /* whether it could walk to within reach of the other */
    SEARCH_IN_LOWER_HALF = 9,        /* whether it stands in the battlefield's lower half */
    SEARCH_IN_UPPER_HALF = 10,       /* whether it stands in the upper half */
    SEARCH_AT_RIGHT_EDGE = 11,       /* whether it stands at the battlefield's right, 176 pixels in or more */
    SEARCH_AT_LEFT_EDGE = 12,        /* whether it stands at the left, less than 80 pixels in */
    SEARCH_NEARER_TO_THIRD = 13,     /* how much nearer the other is to a third combatant than to it */
    SEARCH_WITHIN_WIDE_REACH = 14,   /* whether the other is within its wider reach */
    SEARCH_KINDS
} SearchKind;

typedef struct {
    bool held;        /* the test held (the searches that find someone leave the last reach test's) */
    uint8_t found;    /* whom a find found; for SEARCH_NEARER_TO_THIRD, `nearer` in 256ths */
    uint16_t nearer;  /* SEARCH_NEARER_TO_THIRD: the difference of the squares of the distances */
} SearchAnswer;

/* $C1:2986: ask the battlefield about `searcher` and `other` (a battle slot
 * each), and for SEARCH_NEARER_TO_THIRD and SEARCH_WALK a third (the walk
 * keeps clear of the other combatants when it is nonzero). A find that finds
 * nobody leaves battle_target_found as it was. */
SearchAnswer battle_search(GameState *g, SearchKind kind, uint8_t searcher, uint8_t other, uint8_t third);

/* The search's question alone, on the boxes as they are (each kind is its own
 * routine in the original, $C1:2D81's table, which the search runs after
 * working the two boxes out). */
SearchAnswer battle_search_kind(GameState *g, SearchKind kind, uint8_t searcher, uint8_t other, uint8_t third);

/* The reaches: the square of the distance within which one combatant is
 * within another's reach, as the original's callers name them (the target
 * search's and its walk's, the wider one, and those the enemies' animation
 * effects measure with, $C1:2AE3-$2B1F). */
enum {
    REACH_SEARCH = 0x0400,
    REACH_WIDE = 0x0900,
    REACH_LONG = 0x0640,
    REACH_FAR = 0x1000,
    REACH_TOUCHING = 0x0010,
    REACH_NEAR = 0x0100,
};

/* Whether combatant b's box is within `reach` of a's, by the square of the
 * distance between the positions their boxes were worked out from. */
bool battle_within_reach(GameState *g, uint8_t a, uint8_t b, uint16_t reach);

/* $C1:2926: whether the battlefield's grid stops a combatant standing where its
 * box is now: a cell under it marked with bit 6, or with bit 7 unless
 * battle_search_through_marked. */
bool battle_grid_stops(GameState *g, uint8_t slot);

/* $C1:28B0: which side a combatant whose box overlaps this one's is on:
 * none, a party member, or an enemy. */
typedef enum { OVERLAPS_NOBODY, OVERLAPS_MEMBER, OVERLAPS_ENEMY } Overlap;
Overlap battle_box_overlaps(GameState *g, uint8_t slot);

/* $C1:0222: the direction from one point to another, in 256ths of a turn
 * (0 to the right, 64 down). */
typedef struct {
    uint8_t x, y;
} BattlePoint;
uint8_t battle_direction(GameState *g, BattlePoint from, BattlePoint to);

/* Whether a combatant's box is within `reach` of a point, as the reach tests
 * measure between two boxes (an enemy's spot, battle/enemy_movement.c). */
bool battle_box_within_reach_of(GameState *g, uint8_t slot, BattlePoint at, uint16_t reach);

/* $C1:01F9: how far along an axis a point `radius` pixels away at `angle`
 * lies: the radius times the sine, as a signed pixel count. */
int8_t battle_along(GameState *g, uint8_t angle, uint8_t radius);

#endif
