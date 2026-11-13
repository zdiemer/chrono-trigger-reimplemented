/* The map scrolling in (field/streaming.c): each frame the layers take their
 * moves, and the rows and columns of the map that come into view are built
 * into the layers' staging buffers for the vblank's uploads. */
#ifndef CT_GAME_FIELD_STREAMING_H
#define CT_GAME_FIELD_STREAMING_H

#include <stdbool.h>

#include <stdint.h>
#include "platform/platform.h"
#include "state.h"

/* Every frame, after the layers' scroll ($C0:93E1). */
void stream_the_layers(GameState *g);

/* A layer drawn into the staging buffers at a map's load, which the load
 * sends to VRAM ($C0:75E9, $C0:78EC, $C0:7CB5). */
void draw_layer_at_the_maps_load(GameState *g, unsigned layer);
/* The same, where the port draws the layer: false where it doesn't, the
 * program going no further there. */
bool field_layer_drawn_at_the_load(GameState *g, unsigned layer);

/* The columns right of the view built and sent, as the screen is set up or
 * put back where the party is ($C0:87F1). */
void stream_the_right_columns(GameState *g);
enum { FIELD_BRING_IN_THE_RIGHT_COLUMNS_ENTRY = 0xC087F1 };

/* On a view wider than the original's screen, the layers' columns either
 * side of it built from the map and sent: a wider view's own work, in the
 * vblank, which neither changes the game nor takes its time. */
void field_fill_the_views_margins(GameState *g, Platform *pf, bool moved);
/* The margins are built where the layers' edges came in this frame (`moved`),
 * and for a frame or two after the map's screen is sent whole: then the map's
 * looks are the map's, where between them the battle may have put its own
 * data over them. The count is the program's own state, beside the game's. */
typedef struct {
    int16_t left, right;   /* the screen X a layer holds the map across ... */
    uint16_t scroll;       /* ... at this scroll */
} FieldLayerSpan;
typedef struct {
    uint8_t due;
    bool held[3];                  /* the layer holds the field's map (FIELD_LAYERS) */
    FieldLayerSpan span[3];
    /* Whether the map's left and right edge columns are a void where the
     * margins were last built: judged there, while the map's looks are the
     * map's, and kept for the frames between, when a battle may have put its
     * own data over them. */
    bool void_left[3], void_right[3];
} FieldMargins;
/* Module storage supplied by the state owner, independent of mapped RAM. */
FieldMargins *field_margins(GameState *g);
void field_margins_due(GameState *g);
/* ... and how far across each layer holds the map, every frame. The battle,
 * fought over the field's layers, shows them as far at its own scroll
 * (FIELD_LAYERS of them), but for a layer it draws its own picture into. */
void field_show_the_layers_across(GameState *g, Platform *pf);
void field_show_the_layers_under_a_battle(GameState *g, Platform *pf, const uint16_t scroll[3]);
void field_battle_takes_its_own_layer(GameState *g, unsigned layer);

/* Whether each layer's map repeats within the map's tables, and whether a
 * layer's drawing at the map's load fits the staging buffers (what the port
 * draws). */
bool layers_repeat_within_the_maps(const GameState *g);
bool layer_drawing_fits(const GameState *g, unsigned layer);

#endif
