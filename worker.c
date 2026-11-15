// SPDX-License-Identifier: GPL-2.0
/*
 * Child worker process simulation logic
 *
 * Each child process represents a user task managed by the parent scheduler.
 * Upon creation, the worker stops itself with SIGSTOP and waits for the
 * scheduler to grant it a time slice via SIGCONT.
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "scheduler.h"

/*
 * simulate_cpu_work - Simulate active CPU burst in small time increments
 * @duration_ms: Active execution time required for one workload step
 *
 * Breaks the step duration into small 1-5 ms ticks using nanosleep().
 * When the process is preempted via SIGSTOP, the loop freezes at the
 * current tick and resumes from the remaining ticks upon SIGCONT.
 */
static void simulate_cpu_work(int duration_ms)
{
	struct timespec req;
	int remaining_ms;
	int tick_ms;

	tick_ms = (duration_ms < 50) ? 1 : 5;
	remaining_ms = duration_ms;

	while (remaining_ms > 0) {
		int slice = (remaining_ms < tick_ms) ? remaining_ms : tick_ms;

		req.tv_sec = slice / 1000;
		req.tv_nsec = (long)(slice % 1000) * 1000000L;
		nanosleep(&req, NULL);
		remaining_ms -= slice;
	}
}

/*
 * print_progress_bar - Display visual progress of a worker step
 * @proc_id:     Logical worker ID (1 .. nr_procs)
 * @pid:         Operating system PID of this child process
 * @step:        Completed step number
 * @total_steps: Total steps assigned to this worker
 */
static void print_progress_bar(int proc_id, pid_t pid, int step,
			       int total_steps)
{
	char bar[13];
	int bar_width = 12;
	int filled = (step * bar_width) / total_steps;
	int percent = (step * 100) / total_steps;
	int i;

	for (i = 0; i < bar_width; i++)
		bar[i] = (i < filled) ? '#' : '.';
	bar[bar_width] = '\0';

	printf("  [WORKER P%d | PID %d] Step %d/%d [%s] %3d%%\n",
	       proc_id, (int)pid, step, total_steps, bar, percent);
	fflush(stdout);
}

/*
 * worker_run - Entry point for a child worker process after fork()
 * @proc_id:       Logical worker ID (1 .. nr_procs)
 * @total_steps:   Finite number of workload steps to execute
 * @step_delay_ms: Active CPU time (in ms) required per step
 *
 * Configures line-buffered stdout so each log line is written atomically,
 * then raises SIGSTOP to wait until the parent scheduler dispatches this
 * process with SIGCONT.
 */
void worker_run(int proc_id, int total_steps, int step_delay_ms)
{
	pid_t my_pid = getpid();
	int step;

	/* Line buffering keeps each log line atomic across signals */
	setvbuf(stdout, NULL, _IOLBF, 0);

	/*
	 * Stop immediately after creation so the parent scheduler controls
	 * when this process begins its very first time slice.
	 */
	raise(SIGSTOP);

	for (step = 1; step <= total_steps; step++) {
		simulate_cpu_work(step_delay_ms);
		print_progress_bar(proc_id, my_pid, step, total_steps);
	}

	printf("  [WORKER P%d | PID %d] Completed %d/%d steps. Exiting.\n",
	       proc_id, (int)my_pid, total_steps, total_steps);
	fflush(stdout);
	exit(EXIT_SUCCESS);
}
