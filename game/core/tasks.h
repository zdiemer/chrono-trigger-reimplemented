/* Tasks: work that goes on across frames, each with its own stack, run in
 * turn once a frame (core/tasks.c).
 *
 * The menus run their screens this way: a cursor, a window sliding open, the
 * list being drawn, each a task that does a frame's work and waits for the
 * next frame where it is, deep in its own calls. On the machine each task is a
 * stack the menus' task switch moves between ($C2:81CF, menu/tasks.c); a build
 * with no machine under it gives each task a coroutine (runtime/port/fiber.h)
 * and the same order: every running task, in its slot's order, until it waits,
 * once a frame. A task's wait gives the turn to the next task, not the frame
 * to the driver: the frame ends when the caller of tasks_run_a_frame waits
 * for it. */
#ifndef CT_GAME_CORE_TASKS_H
#define CT_GAME_CORE_TASKS_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "port/fiber.h"
#include "state.h"

enum { TASK_SLOTS = 8 };

/* A task's work: it runs from the start of its first turn, and its task ends
 * where it returns. */
typedef void (*TaskBody)(GameState *g, Platform *pf, void *arg);

typedef struct Tasks Tasks;
typedef struct {
    Tasks *tasks;
    uint8_t slot;
} TaskSlot;

struct Tasks {
    GameState *g;
    Platform *pf;
    Fiber *turns;                 /* the stack tasks_run_a_frame runs on, where a wait comes back to */
    Fiber *fiber[TASK_SLOTS];
    TaskBody body[TASK_SLOTS];
    void *arg[TASK_SLOTS];
    TaskSlot self[TASK_SLOTS];
    bool running[TASK_SLOTS];
    bool started[TASK_SLOTS];
    int8_t current;               /* the slot whose turn it is, or -1 between turns */
};

void tasks_init(Tasks *t, GameState *g, Platform *pf);
/* Start `body` in `slot` (stopping what ran there): it has its first turn the
 * next time the tasks run. */
void tasks_start(Tasks *t, uint8_t slot, TaskBody body, void *arg);
/* Stop the task in `slot`, wherever it waits; not the running task's own. */
void tasks_stop(Tasks *t, uint8_t slot);
bool tasks_running(const Tasks *t, uint8_t slot);
/* A frame's turns: each running task, in slot order, until it waits or ends. */
void tasks_run_a_frame(Tasks *t);
/* From inside a task: wait for the next frame's turn. */
void tasks_wait_a_frame(Tasks *t);
/* Every task stopped, their stacks given back. */
void tasks_end(Tasks *t);

#endif
