#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "common.h"

#define SCHED_MAX_TASKS     16
#define SCHED_STACK_SIZE    16384
#define WORKER_QUEUE_SIZE   32

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_DONE
} task_state_t;

typedef void (*sched_entry_t)(void *arg);

typedef struct {
    int id;
    char name[24];
    task_state_t state;
    int priority;
    u32 switches;
    u32 ticks;
} sched_task_info_t;

typedef struct {
    int active_tasks;
    int total_switches;
    int workers_count;
    int jobs_completed;
    int jobs_pending;
} sched_stats_t;

/* Core kernel multitasking scheduler */
void sched_init(void);
int  sched_spawn(const char *name, sched_entry_t entry, void *arg, int priority);
void sched_yield(void);
void sched_sleep_ticks(u32 ticks);
void sched_exit(void);
int  sched_is_running(int task_id);
int  sched_current_id(void);
void sched_get_stats(sched_stats_t *out);
int  sched_get_task_info(int idx, sched_task_info_t *out);

/* Parallel worker pool */
void worker_pool_init(int num_workers);
int  worker_submit(const char *job_name, sched_entry_t fn, void *arg);

#endif
