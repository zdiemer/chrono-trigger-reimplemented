/* The event engine (event/engine.c): objects' turns, and a map's openings,
 * in plain C over the language (event/script.h). */
#ifndef CT_GAME_EVENT_ENGINE_H
#define CT_GAME_EVENT_ENGINE_H

#include "event/script.h"

/* How the engine runs each command of a turn. `run` runs the command at `at`
 * and says where the script goes (false: it handed the rest of the turn to
 * someone else, the original on the development system). `state_is_ours`
 * says whether the game's state is still one the port reaches as it expects
 * after what `run` ran (on the development system, whether a command the
 * original ran left the data bank elsewhere); a native runner is always
 * true. */
typedef struct {
    bool (*run)(void *ctx, uint16_t at, EventStep *step);
    bool (*state_is_ours)(void *ctx);
    void *ctx;
} EventRunner;

typedef enum {
    TURN_OVER,             /* the turn ran its course, and where the script stopped is kept */
    TURN_HANDED_BACK,      /* a command handed the rest to someone else */
    TURN_OFF_THE_SCRIPT,   /* the script ran to its last offset, which no script reaches */
    TURN_NOT_OURS,         /* after the commands, the state isn't one the port reaches */
    TURN_NOT_OURS_BEFORE,  /* the same, before an object's opening */
} EventTurnEnd;

/* One object's turn this frame (the object its slot doubled): commands from
 * where its script left off until one ends the turn or the turn's budget
 * (event_commands_per_turn) is spent. `stopped_at` is where the script got
 * to. */
EventTurnEnd event_run_turn(GameState *g, uint8_t object, const EventRunner *r, uint16_t *stopped_at);

/* A map's openings, as it loads: every object's script up to its first zero
 * byte, all at once. `object` is the one (doubled) the openings got to. */
EventTurnEnd event_run_openings(GameState *g, const EventRunner *r, uint16_t *object, uint16_t *stopped_at);

#endif
