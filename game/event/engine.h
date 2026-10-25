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

/* One object's turn this frame: commands from
 * where its script left off until one ends the turn or the turn's budget
 * (event_commands_per_turn) is spent. `stopped_at` is where the script got
 * to. */
EventTurnEnd event_run_turn(GameState *g, FieldObject object, const EventRunner *r, uint16_t *stopped_at);

/* A map's openings, as it loads: every object's script up to its first zero
 * byte, all at once. `object` is the one the openings got to. */
EventTurnEnd event_run_openings(GameState *g, const EventRunner *r, FieldObject *object, uint16_t *stopped_at);

/* The running object's script started over, as a character's is after a
 * menu changed the party: its opening up to its first zero byte, and the
 * calls it was in forgotten. */
EventTurnEnd event_restart_the_running_object(GameState *g, const EventRunner *r, uint16_t *stopped_at);

/* The map's own script, as the map starts or comes back: object 0's second
 * function, up to its first zero byte, all at once. */
EventTurnEnd event_run_map_script_commands(GameState *g, const EventRunner *r, uint16_t *stopped_at);

/* A runner with no machine under it: each command's operation as C, timed as
 * its handler. A command only the machine runs yet (a bridge, or a state the
 * port doesn't take) hands the turn back, the program stopping there
 * (core/program.h, program_needs_the_machine). */
typedef struct {
    GameState *game;
    Platform *pf;
} EventAsC;
EventRunner event_runner_as_c(EventAsC *c);

/* The bridges' work as C, for a program with no machine under it (the
 * runner as C, above): each takes the command at `at` and says where the
 * script goes, or false where the turn ends with the field (the program going
 * on, or no further, from where the command said: core/program.h). A bridge
 * with none here is the machine's yet. */
#define EVENT_BRIDGES_AS_C(X) \
    X(event_play_effect_as_c, 0xE8) \
    X(event_play_song_as_c, 0xEA) \
    X(event_fade_music_as_c, 0xEB) \
    X(event_send_sound_command_as_c, 0xEC) \
    X(event_screen_command_as_c, 0xFF) \
    X(event_copy_data_as_c, 0x4E) \
    X(event_bring_into_the_party_as_c, 0xD3) \
    X(event_send_to_the_reserve_as_c, 0xD4) \
    X(event_cast_npc_as_c, 0x82) \
    X(event_cast_enemy_as_c, 0x83)

#define EVENT_DECLARE_AS_C(name, opcode) bool name(GameState *g, Platform *pf, uint16_t at, EventStep *step);
EVENT_BRIDGES_AS_C(EVENT_DECLARE_AS_C)

#endif
