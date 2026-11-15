/*
 * sched.c: CPU schedulers.
 *
 * FCFS and RR are given. TODO checkpoint 1: sjf, srtf, prio, mlfq.
 * Each one is a scheduler_t with the same hooks as FCFS and RR. There are
 * notes above each stub. Functions you will probably need:
 *   proc_burst_remaining(p)   ticks left in p's current cpu burst
 *   proc_total_remaining(p)   cpu ticks left in the whole process
 *   q_pop_min(&q, key)        remove the element with the smallest key
 *   q_min_key(&q, key)        the smallest key, without removing (for preemption)
 */
#include <string.h>
#include "sched.h"
#include "queue.h"

scheduler_t *sched = NULL;

/* FCFS */
static queue_t fcfs_q;

static void   fcfs_init(void)                { q_init(&fcfs_q); }
static void   fcfs_enqueue(pcb_t *p)         { q_push(&fcfs_q, p); }
static bool   fcfs_should_preempt(pcb_t *r)  { (void)r; return false; }
static pcb_t *fcfs_pick(void)                { return q_pop(&fcfs_q); }
static void   fcfs_on_dispatch(pcb_t *p)     { (void)p; }
static void   fcfs_on_tick(pcb_t *r)         { (void)r; }
static int    fcfs_ready_count(void)         { return q_size(&fcfs_q); }

static scheduler_t sched_fcfs = {
    "fcfs", fcfs_init, fcfs_enqueue, fcfs_should_preempt, fcfs_pick,
    fcfs_on_dispatch, fcfs_on_tick, fcfs_ready_count
};

/* RR */
static queue_t rr_q;

static void   rr_init(void)            { q_init(&rr_q); }
static void   rr_enqueue(pcb_t *p)     { q_push(&rr_q, p); }
static pcb_t *rr_pick(void)            { return q_pop(&rr_q); }
static void   rr_on_dispatch(pcb_t *p) { p->quantum_used = 0; }
static void   rr_on_tick(pcb_t *r)     { r->quantum_used++; }
static int    rr_ready_count(void)     { return q_size(&rr_q); }

/* Preempt when the quantum is used up and someone else is ready. If nobody
 * is ready, the process keeps the CPU and starts a new quantum, so a process
 * that arrives later waits until that quantum is over. */
static bool rr_should_preempt(pcb_t *r)
{
    if (r->quantum_used < cfg.quantum) return false;
    r->quantum_used = 0;
    return !q_empty(&rr_q);
}

static scheduler_t sched_rr = {
    "rr", rr_init, rr_enqueue, rr_should_preempt, rr_pick,
    rr_on_dispatch, rr_on_tick, rr_ready_count
};

/* SJF
 * TODO: shortest job first, non-preemptive.
 *   pick(): q_pop_min(&q, key) with key = proc_burst_remaining
 *   should_preempt(): always false
 * For your report: ossim knows the exact length of the next burst, a real
 * OS does not. How would you add exponential averaging (Sec. 5.3.2)? */
static queue_t sjf_q;

//  Return the remaining length of the current CPU burst //
static int sjf_key(const pcb_t *p) {
    return proc_burst_remaining(p);
}

// Initialize the ready queue //
static void sjf_init(void) {
    q_init(&sjf_q);
}

// Add a ready process to the queue //
static void sjf_enqueue(pcb_t *p) {
    q_push(&sjf_q, p);
}

// SJF is non-preemptive //
static bool sjf_should_preempt(pcb_t *r) {
    (void)r;
    return false;
}

// Choose the process with the shortest CPU burst //
static pcb_t *sjf_pick(void) {
    return q_pop_min(&sjf_q, sjf_key);
}

// No special action required on dispatch //
static void sjf_on_dispatch(pcb_t *p) {
    (void)p;
}

// No special action required after each CPU tick //
static void sjf_on_tick(pcb_t *r) {
    (void)r;
}

// Return the number of ready processes //
static int sjf_ready_count(void) {
    return q_size(&sjf_q);
}

// Register the SJF scheduler //
static scheduler_t sched_sjf = {
    "sjf",
    sjf_init,
    sjf_enqueue,
    sjf_should_preempt,
    sjf_pick,
    sjf_on_dispatch,
    sjf_on_tick,
    sjf_ready_count
};

/* SRTF
 * TODO: SJF with preemption. should_preempt(r) is true when a READY process
 * has less left of its burst than r, that is q_min_key(&q, key) < key(r). */
static queue_t srtf_q;

/* Return the remaining time of the current CPU burst */
static int srtf_key(const pcb_t *p) {
    return proc_burst_remaining(p);
}

/* Initialize the SRTF ready queue */
static void srtf_init(void) {
    q_init(&srtf_q);
}

/* Add a process to the ready queue */
static void srtf_enqueue(pcb_t *p) {
    q_push(&srtf_q, p);
}

/* Preempt if a ready process has a shorter remaining burst */
static bool srtf_should_preempt(pcb_t *r) {
    if (q_empty(&srtf_q)) {
        return false;
    }

    int shortest_ready = q_min_key(&srtf_q, srtf_key);
    int running_remaining = srtf_key(r);

    return shortest_ready < running_remaining;
}

/* Choose the ready process with the shortest remaining burst */
static pcb_t *srtf_pick(void) {
    return q_pop_min(&srtf_q, srtf_key);
}

/* Nothing special is required on dispatch */
static void srtf_on_dispatch(pcb_t *p) {
    (void)p;
}

/* Remaining burst time is updated by the kernel */
static void srtf_on_tick(pcb_t *r) {
    (void)r;
}

/* Count READY processes */
static int srtf_ready_count(void) {
    return q_size(&srtf_q);
}

/* Register SRTF */
static scheduler_t sched_srtf = {
    "srtf",
    srtf_init,
    srtf_enqueue,
    srtf_should_preempt,
    srtf_pick,
    srtf_on_dispatch,
    srtf_on_tick,
    srtf_ready_count
};

/* PRIO
 * TODO: priority with preemption (smaller number = higher priority), and RR
 * between processes of the same priority. Low priorities can starve; aging
 * is a stretch goal. */
static queue_t prio_q;

/* Smaller priority number means higher priority */
static int prio_key(const pcb_t *p) {
    return p->priority;
}

/* Initialize the ready queue */
static void prio_init(void) {
    q_init(&prio_q);
}

/* Add a process to the ready queue */
static void prio_enqueue(pcb_t *p) {
    q_push(&prio_q, p);
}

/* Decide whether the running process should be preempted */
static bool prio_should_preempt(pcb_t *r) {

    /* No ready processes: keep running */
    if (q_empty(&prio_q)) {
        return false;
    }

    /* A higher-priority ready process preempts immediately */
    if (q_min_key(&prio_q, prio_key) < r->priority) {
        return true;
    }

    /* Continue running if the quantum has not expired */
    if (r->quantum_used < cfg.quantum) {
        return false;
    }

    /* Quantum expired: rotate only if an equal-priority
       process is waiting */
    for (int i = 0; i < q_size(&prio_q); i++) {
        pcb_t *p = q_at(&prio_q, i);

        if (p->priority == r->priority) {
            return true;
        }
    }

    /* Nobody of equal priority is waiting.
       Start a fresh quantum and keep running. */
    r->quantum_used = 0;
    return false;
}

/* Select the highest-priority ready process */
static pcb_t *prio_pick(void) {
    return q_pop_min(&prio_q, prio_key);
}

/* Reset quantum when a process is dispatched */
static void prio_on_dispatch(pcb_t *p) {
    p->quantum_used = 0;
}

/* Count one CPU tick toward the quantum */
static void prio_on_tick(pcb_t *r) {
    r->quantum_used++;
}

/* Count ready processes */
static int prio_ready_count(void) {
    return q_size(&prio_q);
}

/* Register the scheduler */
static scheduler_t sched_prio = {
    "prio",
    prio_init,
    prio_enqueue,
    prio_should_preempt,
    prio_pick,
    prio_on_dispatch,
    prio_on_tick,
    prio_ready_count
};

/* MLFQ
 * TODO: multilevel feedback queue with 3 levels, RR with quantum q, 2q and
 * 4q. A process that uses its whole quantum goes down one level. A process
 * that blocks before the quantum ends stays where it is. Every 20 ticks,
 * move everyone back to level 0 so nobody starves. Use p->level. */
static queue_t mlfq_q[3];
static tick_t mlfq_last_boost;
static pcb_t *mlfq_preempted;

/* Quantum depends on priority level */
static int mlfq_quantum(int level) {
    return cfg.quantum * (1 << level);
}

/* Initialize the three queues */
static void mlfq_init(void) {
    for (int i = 0; i < 3; i++) {
        q_init(&mlfq_q[i]);
    }
    mlfq_last_boost = -1;
    mlfq_preempted = NULL;
}

/* Enqueue an arriving, waking, or preempted process */
static void mlfq_enqueue(pcb_t *p) {
    /*
     * Keep unused quantum after preemption.
     * Reset quantum after blocking or on new arrival.
     */
    if (p != mlfq_preempted) {
        p->quantum_used = 0;
    }
    mlfq_preempted = NULL;

    if (p->level < 0 || p->level > 2) {
        p->level = 0;
    }

    q_push(&mlfq_q[p->level], p);
}

/* Boost priorities every 20 ticks */
static void mlfq_boost(void) {
    if (now == 0 || now % 20 != 0 ||
        mlfq_last_boost == now) {
        return;
    }

    mlfq_last_boost = now;

    /* Move level-1 and level-2 READY processes to level 0 */
    for (int level = 1; level < 3; level++) {
        while (!q_empty(&mlfq_q[level])) {
            pcb_t *p = q_pop(&mlfq_q[level]);
            p->level = 0;
            p->quantum_used = 0;
            q_push(&mlfq_q[0], p);
        }
    }

    /* Reset scheduling level for all unfinished processes */
    for (int i = 0; i < nprocs; i++) {
        if (procs[i].state == P_TERMINATED) {
            continue;
        }

        procs[i].level = 0;
        procs[i].quantum_used = 0;
    }
}

/* Decide whether to preempt the running process */
static bool mlfq_should_preempt(pcb_t *r) {
    mlfq_boost();

    int slice = mlfq_quantum(r->level);

    /* Process used its full quantum */
    if (r->quantum_used >= slice) {

        /* Demote, unless already at the lowest level */
        if (r->level < 2) {
            r->level++;
        }

        r->quantum_used = 0;

        /* Yield if another process is READY */
        for (int i = 0; i < 3; i++) {
            if (!q_empty(&mlfq_q[i])) {
                mlfq_preempted = r;
                return true;
            }
        }

        /* No competition: continue running */
        return false;
    }

    /* Preempt when a higher-priority queue is nonempty */
    for (int i = 0; i < r->level; i++) {
        if (!q_empty(&mlfq_q[i])) {
            mlfq_preempted = r;
            return true;
        }
    }

    return false;
}

/* Pick the first process in the highest nonempty queue */
static pcb_t *mlfq_pick(void) {
    mlfq_boost();

    for (int i = 0; i < 3; i++) {
        if (!q_empty(&mlfq_q[i])) {
            return q_pop(&mlfq_q[i]);
        }
    }

    return NULL;
}

/* Preserve quantum across higher-priority preemption */
static void mlfq_on_dispatch(pcb_t *p) {
    (void)p;
}

/* Count CPU ticks used */
static void mlfq_on_tick(pcb_t *r) {
    r->quantum_used++;
}

/* Count all READY processes */
static int mlfq_ready_count(void) {
    return q_size(&mlfq_q[0]) +
           q_size(&mlfq_q[1]) +
           q_size(&mlfq_q[2]);
}

/* Register the MLFQ scheduler */
static scheduler_t sched_mlfq = {
    "mlfq",
    mlfq_init,
    mlfq_enqueue,
    mlfq_should_preempt,
    mlfq_pick,
    mlfq_on_dispatch,
    mlfq_on_tick,
    mlfq_ready_count
};

static scheduler_t *all[] = {
    &sched_fcfs, &sched_rr, &sched_sjf, &sched_srtf, &sched_prio, &sched_mlfq
};

void sched_select(const char *name)
{
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        if (strcmp(all[i]->name, name) == 0) {
            sched = all[i];
            sched->init();
            return;
        }
    }
    die("unknown scheduler '%s' (fcfs, rr, sjf, srtf, prio, mlfq)", name);
}
