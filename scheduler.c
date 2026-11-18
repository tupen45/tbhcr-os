#include "scheduler.h"
#include "string.h"
#include <stdint.h>

extern void sched_ctx_switch(u32 *old_esp, u32 new_esp);

typedef struct {
    int id;
    char name[24];
    task_state_t state;
    int priority;
    u32 esp;
    u32 switches;
    u32 ticks;
    u32 sleep_until;
    sched_entry_t entry;
    void *arg;
} tcb_t;

typedef struct {
    char name[24];
    sched_entry_t fn;
    void *arg;
} worker_job_t;

static tcb_t tasks[SCHED_MAX_TASKS];
static u8 task_stacks[SCHED_MAX_TASKS][SCHED_STACK_SIZE] __attribute__((aligned(16)));
static int current_task = 0;
static int sched_ready = 0;
static u32 sched_global_ticks = 0;
static u32 sched_total_switches = 0;

/* Worker pool state */
static worker_job_t job_q[WORKER_QUEUE_SIZE];
static int job_head = 0;
static int job_tail = 0;
static int workers_started = 0;
static int jobs_done = 0;

static void sched_trampoline(void) {
    tcb_t *t = &tasks[current_task];
    if (t->entry) {
        t->entry(t->arg);
    }
    sched_exit();
}

void sched_init(void) {
    memset(tasks, 0, sizeof(tasks));
    tasks[0].id = 0;
    strncpy(tasks[0].name, "kernel-ui", sizeof(tasks[0].name) - 1);
    tasks[0].state = TASK_RUNNING;
    tasks[0].priority = 10;
    tasks[0].switches = 1;
    current_task = 0;
    sched_global_ticks = 0;
    sched_total_switches = 0;
    sched_ready = 1;
}

int sched_spawn(const char *name, sched_entry_t entry, void *arg, int priority) {
    if (!sched_ready) sched_init();
    if (!entry) return -1;

    int slot = -1;
    for (int i = 1; i < SCHED_MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_DONE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    tcb_t *t = &tasks[slot];
    memset(t, 0, sizeof(*t));
    t->id = slot;
    if (name) {
        strncpy(t->name, name, sizeof(t->name) - 1);
        t->name[sizeof(t->name) - 1] = '\0';
    } else {
        snprintf(t->name, sizeof(t->name), "task-%d", slot);
    }
    t->state = TASK_READY;
    t->priority = priority;
    t->entry = entry;
    t->arg = arg;

    /* Prepare initial stack frame matching sched_ctx_switch */
    u32 *sp = (u32 *)(task_stacks[slot] + SCHED_STACK_SIZE);
    *--sp = (u32)sched_trampoline; /* return address for ret */
    *--sp = 0; /* ebp */
    *--sp = 0; /* ebx */
    *--sp = 0; /* esi */
    *--sp = 0; /* edi */
    t->esp = (u32)sp;

    return slot;
}

void sched_exit(void) {
    tasks[current_task].state = TASK_DONE;
    sched_yield();
    while (1) {
        asm volatile("hlt");
    }
}

void sched_sleep_ticks(u32 ticks) {
    if (!sched_ready) return;
    tasks[current_task].sleep_until = sched_global_ticks + ticks;
    tasks[current_task].state = TASK_SLEEPING;
    sched_yield();
}

void sched_yield(void) {
    if (!sched_ready) return;

    sched_global_ticks++;
    tasks[current_task].ticks++;

    /* Wake any sleeping tasks whose timer has elapsed */
    for (int i = 0; i < SCHED_MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING && sched_global_ticks >= tasks[i].sleep_until) {
            tasks[i].state = TASK_READY;
        }
    }

    int next = -1;
    int best_prio = -1;
    for (int offset = 1; offset <= SCHED_MAX_TASKS; offset++) {
        int idx = (current_task + offset) % SCHED_MAX_TASKS;
        if (tasks[idx].state == TASK_READY) {
            if (next < 0 || tasks[idx].priority > best_prio) {
                next = idx;
                best_prio = tasks[idx].priority;
            }
        }
    }

    if (next < 0 || next == current_task) {
        return;
    }

    int prev = current_task;
    if (tasks[prev].state == TASK_RUNNING) {
        tasks[prev].state = TASK_READY;
    }
    tasks[next].state = TASK_RUNNING;
    tasks[next].switches++;
    sched_total_switches++;
    current_task = next;

    sched_ctx_switch(&tasks[prev].esp, tasks[next].esp);
}

int sched_is_running(int task_id) {
    if (task_id < 0 || task_id >= SCHED_MAX_TASKS) return 0;
    return (tasks[task_id].state == TASK_READY ||
            tasks[task_id].state == TASK_RUNNING ||
            tasks[task_id].state == TASK_SLEEPING);
}

int sched_current_id(void) {
    return current_task;
}

void sched_get_stats(sched_stats_t *out) {
    if (!out) return;
    int active = 0;
    for (int i = 0; i < SCHED_MAX_TASKS; i++) {
        if (tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_DONE) {
            active++;
        }
    }
    int pending = (job_tail >= job_head) ? (job_tail - job_head) : (WORKER_QUEUE_SIZE - job_head + job_tail);
    out->active_tasks = active;
    out->total_switches = (int)sched_total_switches;
    out->workers_count = workers_started;
    out->jobs_completed = jobs_done;
    out->jobs_pending = pending;
}

int sched_get_task_info(int idx, sched_task_info_t *out) {
    if (idx < 0 || idx >= SCHED_MAX_TASKS || !out) return 0;
    if (tasks[idx].state == TASK_UNUSED) return 0;
    out->id = tasks[idx].id;
    strncpy(out->name, tasks[idx].name, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
    out->state = tasks[idx].state;
    out->priority = tasks[idx].priority;
    out->switches = tasks[idx].switches;
    out->ticks = tasks[idx].ticks;
    return 1;
}

static void worker_thread_main(void *arg) {
    (void)arg;
    while (1) {
        if (job_head != job_tail) {
            worker_job_t job = job_q[job_head];
            job_head = (job_head + 1) % WORKER_QUEUE_SIZE;
            if (job.fn) {
                job.fn(job.arg);
            }
            jobs_done++;
        }
        sched_yield();
    }
}

void worker_pool_init(int num_workers) {
    if (!sched_ready) sched_init();
    if (workers_started > 0) return;
    if (num_workers < 1) num_workers = 1;
    if (num_workers > 6) num_workers = 6;

    for (int i = 0; i < num_workers; i++) {
        char wname[24];
        snprintf(wname, sizeof(wname), "worker-%d", i);
        if (sched_spawn(wname, worker_thread_main, (void *)(uintptr_t)i, 5) >= 0) {
            workers_started++;
        }
    }
}

int worker_submit(const char *job_name, sched_entry_t fn, void *arg) {
    if (!fn) return -1;
    int next = (job_tail + 1) % WORKER_QUEUE_SIZE;
    if (next == job_head) return -1;
    if (job_name) {
        strncpy(job_q[job_tail].name, job_name, sizeof(job_q[job_tail].name) - 1);
        job_q[job_tail].name[sizeof(job_q[job_tail].name) - 1] = '\0';
    } else {
        strncpy(job_q[job_tail].name, "job", sizeof(job_q[job_tail].name) - 1);
    }
    job_q[job_tail].fn = fn;
    job_q[job_tail].arg = arg;
    job_tail = next;
    return 0;
}
