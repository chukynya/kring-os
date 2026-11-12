/*
 * scheduler.h - Data structures and interface for the time-sharing scheduler
 *
 * Simulates an operating system Round-Robin (RR) time-sharing scheduler
 * in user space using POSIX signals (SIGSTOP, SIGCONT, SIGALRM, SIGCHLD)
 * and interval timers (setitimer).
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <sys/types.h>

/* Simulation limits and default workload parameters */
#define MAX_PROCS		10
#define MIN_QUANTUM_MS		10
#define MAX_QUANTUM_MS		10000
#define BASE_WORK_STEPS		3

/*
 * enum proc_state - Lifecycle states of a simulated worker process
 * @PROC_READY:      Created and stopped, waiting for its first time slice
 * @PROC_RUNNING:    Currently executing on the CPU (resumed via SIGCONT)
 * @PROC_STOPPED:    Preempted by the scheduler (paused via SIGSTOP)
 * @PROC_TERMINATED: Finished all workload steps and exited
 */
enum proc_state {
	PROC_READY = 0,
	PROC_RUNNING,
	PROC_STOPPED,
	PROC_TERMINATED
};

/*
 * struct proc_control_block - Process Control Block (PCB) for each worker
 * @id:          Logical process number (1 .. nr_procs)
 * @pid:         OS Process ID returned by fork()
 * @total_steps: Total workload units assigned to this process
 * @slices_used: Count of time quantums allocated to this process
 * @state:       Current scheduling state in the state machine
 */
struct proc_control_block {
	int id;
	pid_t pid;
	int total_steps;
	int slices_used;
	enum proc_state state;
};

/*
 * struct scheduler_ctx - Master state of the Round-Robin scheduler
 * @procs:            Array of Process Control Blocks (process table)
 * @nr_procs:         Total number of worker processes created
 * @active_procs:     Number of processes that have not yet terminated
 * @curr_idx:         Index of the currently scheduled process in @procs
 * @quantum_ms:       Time slice duration in milliseconds
 * @total_ticks:      Total timer interrupts (SIGALRM) handled
 * @context_switches: Total process switches performed by the scheduler
 */
struct scheduler_ctx {
	struct proc_control_block procs[MAX_PROCS];
	int nr_procs;
	int active_procs;
	int curr_idx;
	int quantum_ms;
	int total_ticks;
	int context_switches;
};

/* Worker process entry point (implemented in worker.c) */
void worker_run(int proc_id, int total_steps, int step_delay_ms);

/* Scheduler lifecycle functions (implemented in scheduler.c) */
int scheduler_init(struct scheduler_ctx *ctx, int nr_procs, int quantum_ms);
int scheduler_run(struct scheduler_ctx *ctx);
void scheduler_print_summary(const struct scheduler_ctx *ctx);
const char *proc_state_name(enum proc_state state);

#endif /* SCHEDULER_H */
