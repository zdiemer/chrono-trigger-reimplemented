/* The held waits: where the main thread rests at a frame end, the machine at
 * the original's loop head with nothing suspended, and the runtime runs the
 * wait's continuation as C when it goes on (runtime/port/native.h,
 * native_hold; docs/RUNTIME.md, "Held waits"). Each has a number, which the
 * machine's state keeps (Snes.held_wait) so that a ct_drive base or a quick
 * save taken there names the wait; a number is never given to another wait. */
#ifndef CT_GAME_HOLDS_H
#define CT_GAME_HOLDS_H

enum {
    HOLD_NONE = 0,
    /* the scene program's main loop ($7E:3023): the attract sequence, the
     * title, the Epoch's dive, the endings (game/scene/program.c) */
    HOLD_SCENE_MAIN_LOOP = 1,
    /* the time warp's main loop ($FD:EAED, the wait each kind's loop calls):
     * every frame of a trip through a time gate (game/warp/warp.c) */
    HOLD_WARP_FRAME = 2,
    /* the overworld's main loop ($C2:0348, the sprite-object mode's wait for
     * the next frame, called from the loop's head at $C2:23CF): every frame
     * on the world map (game/overworld/main_loop.c) */
    HOLD_OVERWORLD_FRAME = 3,
    /* the overworld's halt ($C2:250D), where the game stops for good */
    HOLD_OVERWORLD_HALT = 4,
    /* the field's wait for the next frame ($C0:EC71, the loop in $C0:EC60):
     * every frame on a map, from the main loop and from every routine of the
     * field's that waits a frame (game/field/main_loop.c) */
    HOLD_FIELD_FRAME = 5,
    /* the field picture waiting for vblank before its handlers turn on */
    HOLD_FIELD_SCREEN = 6,
    /* an event's immediate location change, after its vblank wait */
    HOLD_FIELD_LOCATION = 7,
    /* a script's restart, once the picture reaches vblank */
    HOLD_FIELD_RESTART = 8,
    /* sprite-object screens waiting for their next vblank count */
    HOLD_MENU_FRAME = 9,
    /* menu tasks waiting for the queued picture to be uploaded */
    HOLD_MENU_TASKS = 10,
    /* battle startup waits for graphics and the opening window */
    HOLD_BATTLE_GRAPHICS = 11,
    HOLD_BATTLE_WINDOW = 12,
    /* battle passes keep their dispatch boundaries while the gate remains */
    HOLD_BATTLE_PASS = 13,
};

#endif
