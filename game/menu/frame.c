/* The menus: keeping the main loop in step with the display.
 *
 * Each of bank C2's two modes has a main loop that does a frame's work and
 * then waits for its vblank handler: the sprite-object mode's for the next
 * frame to be counted (game/menu/display.c, menu_object_vblank), the menus'
 * for the handler to have sent the uploads the frame queued (menu_vblank).
 * Each mode is in its wait at nearly every frame's end. Between its waits the menus' loop runs a turn of each of their tasks
 * (menu/tasks.c), which a screen starts and stops. */
#include "menu/menu.h"

#include "charge.h"
#include "core/input.h"
#include "overworld/music.h"
#include "platform/audio.h"
#include "platform/timing.h"
#include "port/vars_ram.h"
#include "vars.h"

/// $C2:0346, $C2:046E (the wait's start, and the jump to it)
/* The sprite-object mode's wait for the next frame: until the vblank handler
 * has counted a frame past the one it sees now, a frame at a time. */
enum { FRAME_WAIT = 0xC20346, FRAME_WAIT_LOOP = 0xC20348 };

static void wait_for_the_next_frame(GameState *g, Platform *pf)
{
    uint8_t seen = menu_object_frame_counter(g);
    while (menu_object_frame_counter(g) == seen)
        tm_wait_frame(pf, PF_AT(FRAME_WAIT_LOOP));
}

void menu_wait_for_the_next_frame(GameState *g, Platform *pf) { TIMED_AS(g, FRAME_WAIT, wait_for_the_next_frame(g, pf)); }

// $C2:0454
/* The frames waited, a sound a request left waiting sent after each. A count
 * of 0 is 65,536 frames, as the original counts it down. */
static void wait_frames_sending_the_sound(GameState *g, Platform *pf, uint16_t frames)
{
    uint16_t left = frames;
    do {
        menu_wait_for_the_next_frame(g, pf);
        AudioCommand waiting;
        if (overworld_take_the_waiting_sound(g, &waiting)) {
            au_command(pf, PF_CALL(0xC20462, 0xC70004), waiting);
            overworld_waiting_sound_sent(g);
        }
        left--;
    } while (left != 0);
}

void menu_wait_frames_sending_the_sound(GameState *g, Platform *pf, uint16_t frames)
{
    TIMED_AS(g, 0xC20454, wait_frames_sending_the_sound(g, pf, frames));
}

void menu_stop_tasks(GameState *g, uint8_t tasks)
{
    for (unsigned task = 0; task < MENU_TASKS; task++)
        if (tasks >> task & 1)
            set_menu_tasks_state(g, task, 0);
}
