/* The leader's walk (field/walk.c): each frame the pad walks the leader, the
 * map and its objects stop it or turn it aside, what it steps onto takes
 * effect, and the screen follows it; the screen back onto the 16-pixel grid
 * before a map change; and the party's start on a map. */
#ifndef CT_GAME_FIELD_WALK_H
#define CT_GAME_FIELD_WALK_H

#include <stdbool.h>

#include "snes/system.h"
#include "state.h"

/* The walk, each frame from the field's main loop ($C0:881E). */
void leader_walks(GameState *g);

/* The screen's recentring ($C0:885A): true where it moves this frame, after
 * which the recentring is under way. */
bool screen_recentres(GameState *g);
void screen_recentring_moved(GameState *g);
/* No recentring under way; and one starting, before a tile copy. */
void screen_counts_as_recentred(GameState *g);
void screen_starts_recentring(GameState *g);
bool screen_is_recentred(const GameState *g);

/* A map's start, after the objects' openings ($C0:9DC3); and whether the
 * port can make it (the openings' entry hands it to the original where not:
 * event/bridge.c). */
void party_start_on_the_map(GameState *g);
bool party_start_fits(const GameState *g);

/* The registered entries: the walk and the recentring, which run the
 * original's layers' scroll and screen edges after them; and their guards. */
void field_walk_the_leader(Snes *s);
void field_recentre_the_screen(Snes *s);
bool field_walk_can_run(Snes *s);
bool field_recentring_can_run(Snes *s);

#endif
