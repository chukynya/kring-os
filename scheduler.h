/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared definitions for the Linux Kernel Round-Robin Time-Sharing Scheduler
 *
 * Used by both the Loadable Kernel Module (timeshare_kmod.ko) in Ring 0
 * and the user-space controller utility (timeshare_ctl) in Ring 3.
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

/* Simulation limits and default workload parameters */
#define MAX_PROCS		10
#define MIN_QUANTUM_MS		10
#define MAX_QUANTUM_MS		10000
#define BASE_WORK_STEPS		3

/* Procfs interface name and path */
#define TIMESHARE_PROC_NAME	"timeshare"
#define TIMESHARE_PROC_PATH	"/proc/timeshare"

/*
 * enum proc_state - Lifecycle states of a kernel worker thread
 * @PROC_READY:      Created and waiting on its kernel waitqueue for slice #1
 * @PROC_RUNNING:    Dispatched by the kernel scheduler onto a CPU
 * @PROC_STOPPED:    Preempted by the kernel hrtimer interrupt
 * @PROC_TERMINATED: Completed all workload steps and exited
 */
enum proc_state {
	PROC_READY = 0,
	PROC_RUNNING,
	PROC_STOPPED,
	PROC_TERMINATED
};

/*
 * proc_state_name - Convert enum proc_state to human-readable string
 * @state: Process state value
 *
 * Return: Constant string name for @state.
 */
static inline const char *proc_state_name(enum proc_state state)
{
	switch (state) {
	case PROC_READY:
		return "READY";
	case PROC_RUNNING:
		return "RUNNING";
	case PROC_STOPPED:
		return "STOPPED";
	case PROC_TERMINATED:
		return "TERMINATED";
	default:
		return "UNKNOWN";
	}
}

#endif /* SCHEDULER_H */
