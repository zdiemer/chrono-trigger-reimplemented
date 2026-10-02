/* Scenes: the jet bike race, what its parts share.
 *
 * The race is Crono against Johnny along a track that is a Mode 7 plane seen
 * from a camera behind them. Its parts:
 *
 *   race_rules.c    the bikes: speed, steering, boosts, bumping, the finish,
 *                   the score and what it earns, the end of the scene
 *   race_camera.c   the camera: where it stands and looks, how the flat
 *                   layers and the horizon follow it
 *   race_drawing.c  the racers drawn at their distance, the distance bar and
 *                   the numbers
 *   race.c          what the vblank sends: the camera, the plane's columns,
 *                   the racers' tiles, the road strip, the engine's pitch
 *   race_matrix.c   the racers' scaled pictures regrouped into tiles
 *
 * The race runs as the scene program's tasks (scene/frame.c) and its two
 * sequence channels' scripts, which set the camera through channel 0's
 * registers and start the race on channel 1. */
#ifndef CT_GAME_SCENE_RACE_H
#define CT_GAME_SCENE_RACE_H

#include "platform/platform.h"
#include "scene/scene.h"

/* The two bikes, as scene_race_racers counts them. */
typedef enum { RACER_CRONO, RACER_JOHNNY } Racer;

/* Channel 0's registers, as the race's camera script sets them and the race
 * reads them. */
enum {
    CAMERA_HEIGHT = 0,     /* how high the camera is: the plane's scale (its low byte) */
    CAMERA_PLACE = 1,      /* where along the track it is, 0-$3FF around */
    CAMERA_DISTANCE = 2,   /* how far it stands from that place */
    CAMERA_ANGLE = 5,      /* which way it faces, 0-$3FF around */
    CAMERA_PITCH = 6,      /* how far down the screen the horizon is */
    CAMERA_PLANE_TOP = 8,  /* where below the horizon the plane's perspective starts */
    CAMERA_SPEED = 9,      /* how far it moves along each frame */
    RACE_ANIMATION = 10,   /* the bikes' animation: they change frame every other count */
    CAMERA_MIRRORED = 11,  /* nonzero while the perspective is built mirrored */
    CAMERA_LEAN = 12,      /* turned into the bikes' poses with the angle */
};

/* Channel 1's registers the race reads. */
enum {
    RACE_CRONO_SCALE = 0,  /* how big Crono's bike is drawn, at his distance */
    CAMERA_SIDE = 2,       /* where the camera is down the plane, across the track */
    RACE_JOHNNY_SCALE = 6, /* how big Johnny's bike is drawn */
    RACE_STARTED = 13,     /* set when the countdown ends */
};

static inline uint16_t race_camera(const GameState *g, unsigned reg) { return scene_channel_register(g, 0, reg); }
static inline void set_race_camera(GameState *g, unsigned reg, uint16_t v) { set_scene_channel_register(g, 0, reg, v); }
static inline uint16_t race_channel_1(const GameState *g, unsigned reg) { return scene_channel_register(g, 1, reg); }
static inline void set_race_channel_1(GameState *g, unsigned reg, uint16_t v) { set_scene_channel_register(g, 1, reg, v); }

/* The race is the attract sequence's when the scene's mode isn't 0 (the race's
 * setup also marks it in scene_race_demo). */
enum { SCENE_MODE = 0x0F };
static inline bool race_in_attract_mode(const GameState *g) { return (scene_flags(g) & SCENE_MODE) != 0; }

/* The distance the race is run over: a bike has finished when its distance's
 * high word reaches 8. */
#define RACE_FINISH 0x80000ul

/* A bike's lane's high byte as the race reads it: as a word, with the byte
 * after the lane on top. */
static inline uint16_t race_lane_high(const GameState *g, Racer r)
{
    return (uint16_t)(scene_race_racers_lane(g, r) >> 8 | scene_race_racers_after_lane(g, r) << 8);
}

/* Where the race is (scene_race_stage). */
typedef enum { RACE_RUNNING = 0, RACE_CRONO_COASTING = 1, RACE_CRONO_PULLED_AWAY = 2 } RaceStage;

/* A song is on its way to the sound driver: the race sends nothing then. */
static inline bool race_song_sending(const GameState *g) { return scene_song_sending(g) != 0; }

/* The game's sine: 1024 steps a turn, the magnitude in 256ths from the table
 * at $C0:F900, negative for the first half turn; the cosine a quarter turn on. */
int16_t race_sine(const GameState *g, uint16_t angle);
int16_t race_cosine(const GameState *g, uint16_t angle);

/* The scene's tasks this part of the program adds and removes (their
 * addresses, as the lists hold them). */
enum {
    TASK_FADE_OUT_AND_END = 0x34E3,
    TASK_SETTLE_CAMERA = 0x416D,
    TASK_LEAVE = 0x4283,
    TASK_TURN_CAMERA = 0x4339,
    TASK_FOLLOW_FINISH = 0x449C,
    TASK_DRAIN_SCORE = 0x44CE,
    TASK_COLUMN_FROM_CARTRIDGE = 0x4643,
    TASK_COLUMN_FROM_COPY = 0x464D,
    TASK_COAST = 0x4677,
    TASK_AWAIT_VICTORY_SONG = 0x4755,
};

/* scene/race.c: what the race sends the sound driver -- the engine's pitch
 * (Crono's speed's high byte), and the effects it sends from its own calls,
 * which wait for the driver to take them -- and the road strip to VRAM. */
void race_send_engine_pitch(const GameState *g, Platform *pf);
void race_sound_bump(Snes *s);
void race_sound_leaving(Snes *s);
void race_upload_road_strip(Platform *pf);

/* scene/sprites.c: the scene's sprites as C calls (small sprites from a
 * source, a list small or large and clipped), whether more fit in the tables,
 * and the A a clipped list leaves. */
void scene_draw_sprite(GameState *g, uint16_t source);
void scene_draw_sprite_list(GameState *g, uint16_t list, bool clipped);
bool scene_sprites_fit(const GameState *g, unsigned sprites);
uint16_t scene_clipped_list_leaves_a(const GameState *g, uint16_t list, uint16_t a);

/* scene/math.c: the program's signed 16-bit multiply ($7E:33D6), on a page's
 * cells; returns the product. */
uint32_t scene_multiply_signed16(GameState *g, SceneProgramPage page, uint16_t a, uint16_t b);

#endif
