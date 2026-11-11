/*
 * scheduler.c - Parent Round-Robin (RR) time-sharing scheduler implementation
 *
 * Uses an interval timer (setitimer with ITIMER_REAL -> SIGALRM) to enforce
 * a fixed time quantum. On each timer interrupt, the scheduler preempts the
 * running child with SIGSTOP and resumes the next runnable child with SIGCONT.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "scheduler.h"

/* Asynchronous flags updated by POSIX signal handlers */
static volatile sig_atomic_t timer_expired;
static volatile sig_atomic_t child_exited;
static volatile sig_atomic_t stop_requested;

/*
 * sigalrm_handler - Handle periodic timer interrupt (SIGALRM)
 * @sig: Signal number (unused)
 */
static void sigalrm_handler(int sig)
{
	(void)sig;
	timer_expired = 1;
}

/*
 * sigchld_handler - Wake scheduler when a child terminates (SIGCHLD)
 * @sig: Signal number (unused)
 */
static void sigchld_handler(int sig)
{
	(void)sig;
	child_exited = 1;
}

/*
 * sigterm_handler - Handle termination signals (SIGINT, SIGTERM)
 * @sig: Signal number (unused)
 */
static void sigterm_handler(int sig)
{
	(void)sig;
	stop_requested = 1;
}

/*
 * proc_state_name - Convert enum proc_state to human-readable string
 * @state: Process state value
 *
 * Return: Constant string name for @state.
 */
const char *proc_state_name(enum proc_state state)
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

/*
 * setup_signal_handlers - Register SIGALRM, SIGCHLD, SIGINT, and SIGTERM
 *
 * Return: 0 on success, -1 on error.
 */
static int setup_signal_handlers(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sa.sa_handler = sigalrm_handler;
	if (sigaction(SIGALRM, &sa, NULL) < 0) {
		perror("sigaction(SIGALRM)");
		return -1;
	}

	sa.sa_handler = sigchld_handler;
	sa.sa_flags = SA_NOCLDSTOP;
	if (sigaction(SIGCHLD, &sa, NULL) < 0) {
		perror("sigaction(SIGCHLD)");
		return -1;
	}

	sa.sa_handler = sigterm_handler;
	sa.sa_flags = 0;
	if (sigaction(SIGINT, &sa, NULL) < 0) {
		perror("sigaction(SIGINT)");
		return -1;
	}
	if (sigaction(SIGTERM, &sa, NULL) < 0) {
		perror("sigaction(SIGTERM)");
		return -1;
	}

	return 0;
}

/*
 * configure_timer - Arm or disarm the real-time interval timer
 * @quantum_ms: Time slice in milliseconds (0 to disarm the timer)
 *
 * Return: 0 on success, -1 on error.
 */
static int configure_timer(int quantum_ms)
{
	struct itimerval timer;

	timer.it_value.tv_sec = quantum_ms / 1000;
	timer.it_value.tv_usec = (quantum_ms % 1000) * 1000;
	timer.it_interval = timer.it_value;

	if (setitimer(ITIMER_REAL, &timer, NULL) < 0) {
		perror("setitimer");
		return -1;
	}

	return 0;
}

/*
 * wait_for_child_stop - Wait until a child stops or exits
 * @pid:    Target child PID
 * @status: Output pointer for waitpid status
 *
 * Return: PID on success, -1 on error.
 */
static pid_t wait_for_child_stop(pid_t pid, int *status)
{
	pid_t ret;

	do {
		ret = waitpid(pid, status, WUNTRACED);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

/*
 * cleanup_spawned_procs - Terminate and reap all non-terminated children
 * @ctx:   Scheduler context
 * @count: Number of children spawned in @ctx->procs
 */
static void cleanup_spawned_procs(struct scheduler_ctx *ctx, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (ctx->procs[i].pid > 0 &&
		    ctx->procs[i].state != PROC_TERMINATED) {
			kill(ctx->procs[i].pid, SIGKILL);
			waitpid(ctx->procs[i].pid, NULL, 0);
			ctx->procs[i].state = PROC_TERMINATED;
		}
	}
}

/*
 * scheduler_init - Fork worker processes and initialize the process table
 * @ctx:        Scheduler context to initialize
 * @nr_procs:   Number of worker processes to create (1 .. MAX_PROCS)
 * @quantum_ms: Time quantum in milliseconds
 *
 * Return: 0 on success, -1 on invalid arguments or fork failure.
 */
int scheduler_init(struct scheduler_ctx *ctx, int nr_procs, int quantum_ms)
{
	int step_delay_ms;
	int i;

	if (!ctx || nr_procs < 1 || nr_procs > MAX_PROCS)
		return -1;
	if (quantum_ms < MIN_QUANTUM_MS || quantum_ms > MAX_QUANTUM_MS)
		return -1;

	memset(ctx, 0, sizeof(*ctx));
	ctx->nr_procs = nr_procs;
	ctx->active_procs = nr_procs;
	ctx->curr_idx = -1;
	ctx->quantum_ms = quantum_ms;

	/* Each step takes ~60% of a quantum so workers need multiple slices */
	step_delay_ms = (quantum_ms * 3) / 5;
	if (step_delay_ms < 1)
		step_delay_ms = 1;

	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("\n=== Initializing Process Table (%d processes, "
	       "%d ms quantum) ===\n", nr_procs, quantum_ms);

	for (i = 0; i < nr_procs; i++) {
		int steps = BASE_WORK_STEPS + (i % 3);
		int status = 0;
		pid_t pid = fork();

		if (pid < 0) {
			perror("fork");
			cleanup_spawned_procs(ctx, i);
			return -1;
		}

		if (pid == 0)
			worker_run(i + 1, steps, step_delay_ms);

		ctx->procs[i].id = i + 1;
		ctx->procs[i].pid = pid;
		ctx->procs[i].total_steps = steps;
		ctx->procs[i].slices_used = 0;
		ctx->procs[i].state = PROC_READY;

		/* Parent waits until child pauses itself with raise(SIGSTOP) */
		if (wait_for_child_stop(pid, &status) < 0) {
			perror("waitpid");
			cleanup_spawned_procs(ctx, i + 1);
			return -1;
		}

		printf("[INIT] Process P%d created (PID %d) | State: %-6s | "
		       "Workload: %d steps\n",
		       ctx->procs[i].id, (int)pid,
		       proc_state_name(ctx->procs[i].state), steps);
	}

	return 0;
}

/*
 * find_next_runnable - Select next runnable process in Round-Robin order
 * @ctx: Scheduler context
 *
 * Return: Index in @ctx->procs, or -1 if no runnable process remains.
 */
static int find_next_runnable(const struct scheduler_ctx *ctx)
{
	int i;

	for (i = 1; i <= ctx->nr_procs; i++) {
		int idx = (ctx->curr_idx + i) % ctx->nr_procs;
		enum proc_state st = ctx->procs[idx].state;

		if (st == PROC_READY || st == PROC_STOPPED)
			return idx;
	}

	return -1;
}

/*
 * check_child_terminated - Non-blocking check if active child has exited
 * @ctx:  Scheduler context
 * @proc: Currently running process control block
 *
 * Return: 1 if @proc has terminated, 0 if still alive.
 */
static int check_child_terminated(struct scheduler_ctx *ctx,
				  struct proc_control_block *proc)
{
	int status = 0;
	pid_t ret;

	do {
		ret = waitpid(proc->pid, &status, WNOHANG);
	} while (ret < 0 && errno == EINTR);

	if (ret == proc->pid && (WIFEXITED(status) || WIFSIGNALED(status))) {
		proc->state = PROC_TERMINATED;
		ctx->active_procs--;
		printf("[SCHEDULER] Process P%d (PID %d) finished -> "
		       "State: TERMINATED (%d active left)\n",
		       proc->id, (int)proc->pid, ctx->active_procs);
		return 1;
	}

	return 0;
}

/*
 * preempt_running_proc - Preempt current process with SIGSTOP on timer tick
 * @ctx:  Scheduler context
 * @proc: Currently running process control block
 */
static void preempt_running_proc(struct scheduler_ctx *ctx,
				 struct proc_control_block *proc)
{
	int status = 0;

	if (kill(proc->pid, SIGSTOP) < 0 && errno != ESRCH) {
		perror("kill(SIGSTOP)");
		return;
	}

	if (wait_for_child_stop(proc->pid, &status) < 0)
		return;

	ctx->total_ticks++;
	printf("[TIMER] Tick #%d (%d ms quantum expired) -> SIGALRM received\n",
	       ctx->total_ticks, ctx->quantum_ms);

	if (WIFEXITED(status) || WIFSIGNALED(status)) {
		proc->state = PROC_TERMINATED;
		ctx->active_procs--;
		printf("[SCHEDULER] Process P%d (PID %d) exited at tick -> "
		       "State: TERMINATED (%d active left)\n",
		       proc->id, (int)proc->pid, ctx->active_procs);
	} else if (WIFSTOPPED(status)) {
		proc->state = PROC_STOPPED;
		printf("[SCHEDULER] Preempted Process P%d (PID %d) with "
		       "SIGSTOP -> State: STOPPED\n",
		       proc->id, (int)proc->pid);
	}
}

/*
 * scheduler_run - Execute the main Round-Robin scheduling loop
 * @ctx: Initialized scheduler context
 *
 * Return: 0 when all worker processes have completed, -1 on error.
 */
int scheduler_run(struct scheduler_ctx *ctx)
{
	if (!ctx)
		return -1;

	timer_expired = 0;
	child_exited = 0;
	stop_requested = 0;

	if (setup_signal_handlers() < 0) {
		cleanup_spawned_procs(ctx, ctx->nr_procs);
		return -1;
	}

	printf("\n=== Starting Round-Robin Time-Sharing Scheduler ===\n");

	while (ctx->active_procs > 0) {
		struct proc_control_block *proc;
		int next_idx = find_next_runnable(ctx);

		if (next_idx < 0)
			break;

		ctx->curr_idx = next_idx;
		proc = &ctx->procs[next_idx];
		proc->state = PROC_RUNNING;
		proc->slices_used++;
		ctx->context_switches++;

		printf("\n[SCHEDULER] Dispatching Process P%d (PID %d) | "
		       "Slice #%d -> sending SIGCONT\n",
		       proc->id, (int)proc->pid, proc->slices_used);

		timer_expired = 0;
		child_exited = 0;

		if (configure_timer(ctx->quantum_ms) < 0) {
			cleanup_spawned_procs(ctx, ctx->nr_procs);
			return -1;
		}

		if (kill(proc->pid, SIGCONT) < 0) {
			perror("kill(SIGCONT)");
			configure_timer(0);
			cleanup_spawned_procs(ctx, ctx->nr_procs);
			return -1;
		}

		while (!timer_expired && !stop_requested) {
			if (child_exited) {
				child_exited = 0;
				if (check_child_terminated(ctx, proc))
					break;
			}
			if (check_child_terminated(ctx, proc))
				break;
			if (timer_expired || child_exited || stop_requested)
				continue;
			pause();
		}

		configure_timer(0);

		if (stop_requested) {
			printf("\n[SCHEDULER] Termination signal received. "
			       "Cleaning up worker processes...\n");
			cleanup_spawned_procs(ctx, ctx->nr_procs);
			return -1;
		}

		if (proc->state == PROC_RUNNING)
			preempt_running_proc(ctx, proc);
	}

	configure_timer(0);
	printf("\n=== All Worker Processes Completed ===\n");
	return 0;
}

/*
 * scheduler_print_summary - Print final execution statistics table
 * @ctx: Completed scheduler context
 */
void scheduler_print_summary(const struct scheduler_ctx *ctx)
{
	int i;

	if (!ctx)
		return;

	printf("\n+-------------------------------------------------------+\n");
	printf("|             TIME-SHARING SIMULATION SUMMARY           |\n");
	printf("+---------+----------+--------------+--------+----------+\n");
	printf("| Process |   PID    | Final State  | Steps  |  Slices  |\n");
	printf("+---------+----------+--------------+--------+----------+\n");

	for (i = 0; i < ctx->nr_procs; i++) {
		const struct proc_control_block *p = &ctx->procs[i];

		printf("|   P%-4d | %-8d | %-12s | %-6d | %-8d |\n",
		       p->id, (int)p->pid, proc_state_name(p->state),
		       p->total_steps, p->slices_used);
	}

	printf("+---------+----------+--------------+--------+----------+\n");
	printf("| Time Quantum     : %-6d ms                          |\n",
	       ctx->quantum_ms);
	printf("| Timer Interrupts : %-6d (SIGALRM ticks)             |\n",
	       ctx->total_ticks);
	printf("| Context Switches : %-6d (SIGCONT dispatches)        |\n",
	       ctx->context_switches);
	printf("+-------------------------------------------------------+\n");
}
