/* Tasks as coroutines (core/tasks.h): a task's stack is a fiber, its turn a
 * switch to it, its wait a switch back to the turns. A task that ends keeps
 * its fiber, parked, until its slot is started again or the tasks end, since
 * a fiber can't give back the stack it is running on. */
#include "core/tasks.h"

static void task_body(void *arg)
{
    TaskSlot *self = arg;
    Tasks *t = self->tasks;
    t->body[self->slot](t->g, t->pf, t->arg[self->slot]);
    t->running[self->slot] = false;
    for (;;)
        fiber_switch(t->turns);
}

void tasks_init(Tasks *t, GameState *g, Platform *pf)
{
    *t = (Tasks){ .g = g, .pf = pf, .current = -1 };
    for (uint8_t i = 0; i < TASK_SLOTS; i++)
        t->self[i] = (TaskSlot){ t, i };
}

static void forget(Tasks *t, uint8_t slot)
{
    if (t->fiber[slot])
        fiber_destroy(t->fiber[slot]);
    t->fiber[slot] = 0;
    t->running[slot] = false;
    t->started[slot] = false;
}

void tasks_start(Tasks *t, uint8_t slot, TaskBody body, void *arg)
{
    if (slot >= TASK_SLOTS || slot == t->current)
        return;
    forget(t, slot);
    t->body[slot] = body;
    t->arg[slot] = arg;
    t->running[slot] = true;
}

void tasks_stop(Tasks *t, uint8_t slot)
{
    if (slot < TASK_SLOTS && slot != t->current)
        forget(t, slot);
}

bool tasks_running(const Tasks *t, uint8_t slot) { return slot < TASK_SLOTS && t->running[slot]; }

void tasks_run_a_frame(Tasks *t)
{
    for (uint8_t i = 0; i < TASK_SLOTS; i++) {
        if (!t->running[i])
            continue;
        if (!t->started[i]) {
            t->fiber[i] = fiber_create(task_body, &t->self[i]);
            if (!t->fiber[i]) {
                t->running[i] = false;   /* no stack for it: it never runs */
                continue;
            }
            t->started[i] = true;
        }
        t->turns = fiber_current();
        t->current = (int8_t)i;
        fiber_switch(t->fiber[i]);
        t->current = -1;
    }
}

void tasks_wait_a_frame(Tasks *t) { fiber_switch(t->turns); }

void tasks_end(Tasks *t)
{
    for (uint8_t i = 0; i < TASK_SLOTS; i++)
        if ((int8_t)i != t->current)
            forget(t, i);
}
