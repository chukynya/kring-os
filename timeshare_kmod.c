// SPDX-License-Identifier: GPL-2.0
/*
 * Linux Kernel Module: Round-Robin (RR) Time-Sharing Scheduler
 *
 * Implements an operating system Round-Robin time-sharing scheduler inside
 * Linux kernel space (Ring 0) using high-resolution kernel timers (hrtimer),
 * kernel worker threads (kthread), waitqueues, and a /proc/timeshare
 * control and monitoring interface.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "scheduler.h"

#define LOG_BUF_SIZE	65536

/* Optional module parameters to auto-run on insmod */
static int nr_procs;
module_param(nr_procs, int, 0444);
MODULE_PARM_DESC(nr_procs, "Initial number of worker kthreads (1..10)");

static int quantum_ms = 400;
module_param(quantum_ms, int, 0444);
MODULE_PARM_DESC(quantum_ms, "Initial time quantum in ms (10..10000)");

/*
 * struct kproc_pcb - Kernel Process Control Block for each worker kthread
 * @id:              Logical process ID (1 .. nr_procs)
 * @pid:             Kernel thread PID (task_pid_nr)
 * @total_steps:     Total workload steps assigned to this worker
 * @completed_steps: Workload steps completed so far
 * @slices_used:     Number of time quantums dispatched to this worker
 * @state:           Current scheduling state (READY, RUNNING, STOPPED, etc.)
 * @task:            Pointer to kernel task_struct
 * @wq:              Per-worker waitqueue for context switching
 * @ready_ack:       True once worker has initialized and paused at start
 * @run_granted:     True while worker holds the CPU time slice
 * @ack_stopped:     True when worker pauses after preemption
 * @finished:        True when worker finishes all workload steps
 */
struct kproc_pcb {
	int id;
	pid_t pid;
	int total_steps;
	int completed_steps;
	int slices_used;
	enum proc_state state;
	struct task_struct *task;
	wait_queue_head_t wq;
	bool ready_ack;
	bool run_granted;
	bool ack_stopped;
	bool finished;
};

/*
 * struct ksched_ctx - Master kernel context for the Round-Robin scheduler
 * @procs:            Kernel Process Control Block table
 * @nr_procs:         Number of configured worker kthreads
 * @active_procs:     Count of non-terminated worker kthreads
 * @curr_idx:         Index of currently running worker in @procs
 * @quantum_ms:       Time slice duration in milliseconds
 * @step_delay_ms:    Active CPU burst duration per worker step (ms)
 * @total_ticks:      Total hrtimer interrupts handled
 * @context_switches: Total kthread dispatches performed
 * @timer_expired:    Flag set by hrtimer hardirq callback
 * @sim_running:      True while a simulation is actively executing
 * @sim_completed:    True once at least one simulation has completed
 * @quantum_timer:    High-resolution kernel timer for preemption
 * @sched_task:       Coordinator Round-Robin scheduler kthread
 * @sched_wq:         Waitqueue for waking the coordinator scheduler
 * @ctrl_lock:        Mutex serializing simulation start/stop operations
 * @log_lock:         Spinlock protecting @log_buf
 * @log_buf:          Ring/trace buffer exposed via /proc/timeshare
 * @log_len:          Current byte length stored in @log_buf
 * @proc_entry:       /proc/timeshare directory entry
 */
struct ksched_ctx {
	struct kproc_pcb procs[MAX_PROCS];
	int nr_procs;
	int active_procs;
	int curr_idx;
	int quantum_ms;
	int step_delay_ms;
	int total_ticks;
	int context_switches;
	bool timer_expired;
	bool sim_running;
	bool sim_completed;
	struct hrtimer quantum_timer;
	struct task_struct *sched_task;
	wait_queue_head_t sched_wq;
	struct mutex ctrl_lock;
	spinlock_t log_lock;
	char *log_buf;
	size_t log_len;
	struct proc_dir_entry *proc_entry;
};

static struct ksched_ctx ts_ctx;

/*
 * ts_log_clear - Reset the internal kernel trace log buffer
 */
static void ts_log_clear(void)
{
	unsigned long flags;

	spin_lock_irqsave(&ts_ctx.log_lock, flags);
	if (ts_ctx.log_buf) {
		ts_ctx.log_buf[0] = '\0';
		ts_ctx.log_len = 0;
	}
	spin_unlock_irqrestore(&ts_ctx.log_lock, flags);
}

/*
 * ts_log - Write a formatted message to both dmesg and /proc/timeshare buffer
 * @fmt: Printf-style format string
 */
static __printf(1, 2) void ts_log(const char *fmt, ...)
{
	unsigned long flags;
	char line[256];
	va_list args;
	int len;

	va_start(args, fmt);
	len = vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);

	if (len <= 0)
		return;
	if (len >= (int)sizeof(line))
		len = sizeof(line) - 1;

	pr_info("%s", line);

	spin_lock_irqsave(&ts_ctx.log_lock, flags);
	if (ts_ctx.log_buf && ts_ctx.log_len + len < LOG_BUF_SIZE - 1) {
		memcpy(ts_ctx.log_buf + ts_ctx.log_len, line, len);
		ts_ctx.log_len += len;
		ts_ctx.log_buf[ts_ctx.log_len] = '\0';
	}
	spin_unlock_irqrestore(&ts_ctx.log_lock, flags);
}

/*
 * timeshare_timer_cb - High-resolution kernel timer interrupt callback
 * @timer: Pointer to expired struct hrtimer
 *
 * Executes in hardirq context when the time quantum expires. Preempts the
 * currently running worker kthread by revoking @run_granted and wakes the
 * coordinator scheduler waitqueue.
 *
 * Return: HRTIMER_NORESTART (one-shot per time slice).
 */
static enum hrtimer_restart timeshare_timer_cb(struct hrtimer *timer)
{
	int idx = READ_ONCE(ts_ctx.curr_idx);

	(void)timer;
	WRITE_ONCE(ts_ctx.timer_expired, true);

	if (idx >= 0 && idx < ts_ctx.nr_procs) {
		WRITE_ONCE(ts_ctx.procs[idx].run_granted, false);
		wake_up_interruptible(&ts_ctx.procs[idx].wq);
	}

	wake_up_interruptible(&ts_ctx.sched_wq);
	return HRTIMER_NORESTART;
}

/*
 * wait_for_slice - Block worker kthread until granted a CPU time slice
 * @proc: Worker Process Control Block
 *
 * Return: 0 when granted CPU slice, -EINTR if kthread should stop.
 */
static int wait_for_slice(struct kproc_pcb *proc)
{
	while (!READ_ONCE(proc->run_granted) && !kthread_should_stop()) {
		WRITE_ONCE(proc->ack_stopped, true);
		wake_up_interruptible(&ts_ctx.sched_wq);
		wait_event_interruptible(proc->wq,
					 READ_ONCE(proc->run_granted) ||
					 kthread_should_stop());
	}

	if (kthread_should_stop())
		return -EINTR;

	return 0;
}

/*
 * simulate_cpu_work - Simulate active CPU burst in small kernel ticks
 * @proc:        Worker Process Control Block
 * @duration_ms: Active execution time required for one workload step
 *
 * Breaks the step into 1-5 ms ticks. When the hrtimer interrupt revokes
 * @proc->run_granted, the worker immediately pauses on its waitqueue and
 * resumes from the remaining milliseconds on its next Round-Robin turn.
 *
 * Return: 0 on step completion, -EINTR if kthread was stopped.
 */
static int simulate_cpu_work(struct kproc_pcb *proc, int duration_ms)
{
	int remaining_ms = duration_ms;
	int tick_ms = (duration_ms < 50) ? 1 : 5;

	while (remaining_ms > 0) {
		int slice;

		if (wait_for_slice(proc) < 0)
			return -EINTR;

		slice = (remaining_ms < tick_ms) ? remaining_ms : tick_ms;
		usleep_range(slice * 1000, slice * 1000 + 200);
		remaining_ms -= slice;
	}

	return wait_for_slice(proc);
}

/*
 * log_worker_progress - Format and log visual progress bar for a worker step
 * @proc: Worker Process Control Block
 * @step: Completed step number
 */
static void log_worker_progress(const struct kproc_pcb *proc, int step)
{
	char bar[13];
	int bar_width = 12;
	int filled = (step * bar_width) / proc->total_steps;
	int percent = (step * 100) / proc->total_steps;
	int i;

	for (i = 0; i < bar_width; i++)
		bar[i] = (i < filled) ? '#' : '.';
	bar[bar_width] = '\0';

	ts_log("  [WORKER P%d | PID %d] Step %d/%d [%s] %3d%%\n",
	       proc->id, (int)proc->pid, step, proc->total_steps,
	       bar, percent);
}

/*
 * timeshare_worker_fn - Entry point for each simulated worker kthread
 * @data: Pointer to struct kproc_pcb for this worker
 *
 * Return: 0 on clean exit.
 */
static int timeshare_worker_fn(void *data)
{
	struct kproc_pcb *proc = data;
	int step;

	/* Signal readiness to scheduler and wait for first SIGCONT/dispatch */
	WRITE_ONCE(proc->ready_ack, true);
	wake_up_interruptible(&ts_ctx.sched_wq);

	if (wait_for_slice(proc) < 0)
		return 0;

	for (step = 1; step <= proc->total_steps; step++) {
		if (simulate_cpu_work(proc, ts_ctx.step_delay_ms) < 0)
			return 0;

		WRITE_ONCE(proc->completed_steps, step);
		log_worker_progress(proc, step);
	}

	ts_log("  [WORKER P%d | PID %d] Completed %d/%d steps. Exiting.\n",
	       proc->id, (int)proc->pid, proc->total_steps, proc->total_steps);

	WRITE_ONCE(proc->finished, true);
	wake_up_interruptible(&ts_ctx.sched_wq);

	/* Wait for coordinator to call kthread_stop() during cleanup */
	wait_event_interruptible(proc->wq, kthread_should_stop());
	return 0;
}

/*
 * find_next_runnable - Select next runnable worker in Round-Robin order
 *
 * Return: Index in @ts_ctx.procs, or -1 if no runnable worker remains.
 */
static int find_next_runnable(void)
{
	int i;

	for (i = 1; i <= ts_ctx.nr_procs; i++) {
		int idx = (ts_ctx.curr_idx + i) % ts_ctx.nr_procs;
		enum proc_state st = READ_ONCE(ts_ctx.procs[idx].state);

		if (st == PROC_READY || st == PROC_STOPPED)
			return idx;
	}

	return -1;
}

/*
 * cleanup_workers - Stop all spawned worker kthreads
 * @count: Number of workers in @ts_ctx.procs to clean up
 */
static void cleanup_workers(int count)
{
	int i;

	for (i = 0; i < count; i++) {
		struct kproc_pcb *proc = &ts_ctx.procs[i];

		if (proc->task) {
			WRITE_ONCE(proc->run_granted, false);
			wake_up_interruptible(&proc->wq);
			kthread_stop(proc->task);
			proc->task = NULL;
		}
	}
}

/*
 * spawn_workers - Create and initialize all worker kthreads in READY state
 *
 * Return: 0 on success, negative errno on kthread creation failure.
 */
static int spawn_workers(void)
{
	int i;

	ts_log("\n=== Process Table Init (%d processes, %d ms quantum) ===\n",
	       ts_ctx.nr_procs, ts_ctx.quantum_ms);

	for (i = 0; i < ts_ctx.nr_procs; i++) {
		struct kproc_pcb *proc = &ts_ctx.procs[i];
		struct task_struct *task;

		proc->id = i + 1;
		proc->total_steps = BASE_WORK_STEPS + (i % 3);
		proc->completed_steps = 0;
		proc->slices_used = 0;
		proc->state = PROC_READY;
		proc->ready_ack = false;
		proc->run_granted = false;
		proc->ack_stopped = false;
		proc->finished = false;
		init_waitqueue_head(&proc->wq);

		task = kthread_create(timeshare_worker_fn, proc,
				      "ts_worker_%d", proc->id);
		if (IS_ERR(task)) {
			int err = PTR_ERR(task);

			proc->task = NULL;
			cleanup_workers(i);
			return err;
		}

		proc->task = task;
		proc->pid = task_pid_nr(task);
		wake_up_process(task);

		wait_event_interruptible(ts_ctx.sched_wq,
					 READ_ONCE(proc->ready_ack) ||
					 kthread_should_stop());

		ts_log("[INIT] P%d (PID %d) | State: %-6s | Steps: %d\n",
		       proc->id, (int)proc->pid,
		       proc_state_name(proc->state), proc->total_steps);
	}

	return 0;
}

/*
 * log_summary_table - Write final simulation summary table to log buffer
 */
static void log_summary_table(void)
{
	int i;

	ts_log("\n+-------------------------------------------------------+\n");
	ts_log("|             TIME-SHARING SIMULATION SUMMARY           |\n");
	ts_log("+---------+----------+--------------+--------+----------+\n");
	ts_log("| Process |   PID    | Final State  | Steps  |  Slices  |\n");
	ts_log("+---------+----------+--------------+--------+----------+\n");

	for (i = 0; i < ts_ctx.nr_procs; i++) {
		const struct kproc_pcb *p = &ts_ctx.procs[i];

		ts_log("|   P%-4d | %-8d | %-12s | %-6d | %-8d |\n",
		       p->id, (int)p->pid, proc_state_name(p->state),
		       p->total_steps, p->slices_used);
	}

	ts_log("+---------+----------+--------------+--------+----------+\n");
	ts_log("| Time Quantum     : %-6d ms                          |\n",
	       ts_ctx.quantum_ms);
	ts_log("| Timer Interrupts : %-6d (hrtimer ticks)             |\n",
	       ts_ctx.total_ticks);
	ts_log("| Context Switches : %-6d (kthread dispatches)        |\n",
	       ts_ctx.context_switches);
	ts_log("+-------------------------------------------------------+\n");
}

/*
 * timeshare_sched_fn - Main kernel Round-Robin scheduler thread
 * @data: Unused pointer
 *
 * Return: 0 on completion.
 */
static int timeshare_sched_fn(void *data)
{
	(void)data;

	if (spawn_workers() < 0) {
		WRITE_ONCE(ts_ctx.sim_running, false);
		return 0;
	}

	ts_log("\n=== Starting Round-Robin Time-Sharing Scheduler ===\n");

	while (ts_ctx.active_procs > 0 && !kthread_should_stop()) {
		struct kproc_pcb *proc;
		int next_idx = find_next_runnable();

		if (next_idx < 0)
			break;

		WRITE_ONCE(ts_ctx.curr_idx, next_idx);
		proc = &ts_ctx.procs[next_idx];
		WRITE_ONCE(proc->state, PROC_RUNNING);
		proc->slices_used++;
		ts_ctx.context_switches++;

		ts_log("\n[SCHEDULER] Dispatch P%d (PID %d) | Slice #%d -> wake_up_process\n",
		       proc->id, (int)proc->pid, proc->slices_used);

		WRITE_ONCE(ts_ctx.timer_expired, false);
		WRITE_ONCE(proc->ack_stopped, false);
		WRITE_ONCE(proc->run_granted, true);

		hrtimer_start(&ts_ctx.quantum_timer,
			      ms_to_ktime(ts_ctx.quantum_ms),
			      HRTIMER_MODE_REL);

		wake_up_interruptible(&proc->wq);

		wait_event_interruptible(ts_ctx.sched_wq,
					 READ_ONCE(ts_ctx.timer_expired) ||
					 READ_ONCE(proc->finished) ||
					 kthread_should_stop());

		hrtimer_cancel(&ts_ctx.quantum_timer);

		if (kthread_should_stop())
			break;

		WRITE_ONCE(proc->run_granted, false);
		wake_up_interruptible(&proc->wq);

		if (READ_ONCE(proc->finished)) {
			WRITE_ONCE(proc->state, PROC_TERMINATED);
			ts_ctx.active_procs--;
			ts_log("[SCHEDULER] P%d (PID %d) finished -> TERMINATED (%d left)\n",
			       proc->id, (int)proc->pid, ts_ctx.active_procs);
			continue;
		}

		/* Wait for worker to pause at tick boundary after preemption */
		wait_event_interruptible(ts_ctx.sched_wq,
					 READ_ONCE(proc->ack_stopped) ||
					 READ_ONCE(proc->finished) ||
					 kthread_should_stop());

		ts_ctx.total_ticks++;
		ts_log("[TIMER] Tick #%d (%d ms quantum expired) -> hrtimer interrupt\n",
		       ts_ctx.total_ticks, ts_ctx.quantum_ms);

		if (READ_ONCE(proc->finished)) {
			WRITE_ONCE(proc->state, PROC_TERMINATED);
			ts_ctx.active_procs--;
			ts_log("[SCHEDULER] P%d (PID %d) exited -> TERMINATED (%d left)\n",
			       proc->id, (int)proc->pid, ts_ctx.active_procs);
		} else {
			WRITE_ONCE(proc->state, PROC_STOPPED);
			ts_log("[SCHEDULER] Preempted P%d (PID %d) wait_queue -> STOPPED\n",
			       proc->id, (int)proc->pid);
		}
	}

	hrtimer_cancel(&ts_ctx.quantum_timer);
	cleanup_workers(ts_ctx.nr_procs);

	if (!kthread_should_stop()) {
		ts_log("\n=== All Worker Processes Completed ===\n");
		log_summary_table();
		WRITE_ONCE(ts_ctx.sim_completed, true);
	}

	WRITE_ONCE(ts_ctx.sim_running, false);
	wake_up_interruptible(&ts_ctx.sched_wq);

	wait_event_interruptible(ts_ctx.sched_wq, kthread_should_stop());
	return 0;
}

/*
 * stop_active_simulation - Stop any currently running scheduler kthread
 *
 * Caller must hold @ts_ctx.ctrl_lock.
 */
static void stop_active_simulation(void)
{
	if (ts_ctx.sched_task) {
		wake_up_interruptible(&ts_ctx.sched_wq);
		kthread_stop(ts_ctx.sched_task);
		ts_ctx.sched_task = NULL;
	}
	hrtimer_cancel(&ts_ctx.quantum_timer);
	WRITE_ONCE(ts_ctx.sim_running, false);
}

/*
 * start_simulation - Configure and launch a new kernel scheduler simulation
 * @procs:   Number of worker kthreads (1 .. MAX_PROCS)
 * @quantum: Time quantum in milliseconds (MIN_QUANTUM_MS .. MAX_QUANTUM_MS)
 *
 * Return: 0 on success, negative errno on invalid arguments or kthread error.
 */
static int start_simulation(int procs, int quantum)
{
	struct task_struct *task;

	if (procs < 1 || procs > MAX_PROCS)
		return -EINVAL;
	if (quantum < MIN_QUANTUM_MS || quantum > MAX_QUANTUM_MS)
		return -EINVAL;

	mutex_lock(&ts_ctx.ctrl_lock);
	stop_active_simulation();
	ts_log_clear();

	ts_ctx.nr_procs = procs;
	ts_ctx.active_procs = procs;
	ts_ctx.curr_idx = -1;
	ts_ctx.quantum_ms = quantum;
	ts_ctx.step_delay_ms = (quantum * 3) / 5;
	if (ts_ctx.step_delay_ms < 1)
		ts_ctx.step_delay_ms = 1;
	ts_ctx.total_ticks = 0;
	ts_ctx.context_switches = 0;
	ts_ctx.timer_expired = false;
	WRITE_ONCE(ts_ctx.sim_completed, false);
	WRITE_ONCE(ts_ctx.sim_running, true);

	task = kthread_run(timeshare_sched_fn, NULL, "ts_sched_rr");
	if (IS_ERR(task)) {
		int err = PTR_ERR(task);

		ts_ctx.sched_task = NULL;
		WRITE_ONCE(ts_ctx.sim_running, false);
		mutex_unlock(&ts_ctx.ctrl_lock);
		return err;
	}

	ts_ctx.sched_task = task;
	mutex_unlock(&ts_ctx.ctrl_lock);
	return 0;
}

/*
 * timeshare_proc_show - Output trace log and status to /proc/timeshare
 * @m: Seq_file output descriptor
 * @v: Unused iterator pointer
 *
 * Return: 0 on success.
 */
static int timeshare_proc_show(struct seq_file *m, void *v)
{
	unsigned long flags;
	bool running = READ_ONCE(ts_ctx.sim_running);
	bool completed = READ_ONCE(ts_ctx.sim_completed);

	(void)v;

	spin_lock_irqsave(&ts_ctx.log_lock, flags);
	if (ts_ctx.log_buf && ts_ctx.log_len > 0)
		seq_puts(m, ts_ctx.log_buf);
	spin_unlock_irqrestore(&ts_ctx.log_lock, flags);

	if (running)
		seq_puts(m, "\n[KERNEL_STATUS: RUNNING]\n");
	else if (completed)
		seq_puts(m, "\n[KERNEL_STATUS: COMPLETED]\n");
	else
		seq_puts(m, "[KERNEL_STATUS: IDLE]\n");

	return 0;
}

/*
 * timeshare_proc_open - Open handler for /proc/timeshare
 * @inode: Procfs inode
 * @file:  Open file struct
 *
 * Return: 0 on success, negative errno on failure.
 */
static int timeshare_proc_open(struct inode *inode, struct file *file)
{
	(void)inode;
	return single_open(file, timeshare_proc_show, NULL);
}

/*
 * timeshare_proc_write - Handle configuration writes to /proc/timeshare
 * @file:  Open file struct
 * @ubuf:  User-space buffer containing "<nr_procs> <quantum_ms>"
 * @count: Number of bytes written
 * @ppos:  File offset
 *
 * Return: @count on success, negative errno on invalid parameters.
 */
static ssize_t timeshare_proc_write(struct file *file, const char __user *ubuf,
				    size_t count, loff_t *ppos)
{
	char kbuf[64];
	int procs = 0;
	int quantum = 0;
	char trailing = '\0';
	int ret;

	(void)file;
	(void)ppos;

	if (count == 0 || count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, ubuf, count))
		return -EFAULT;
	kbuf[count] = '\0';

	if (sscanf(kbuf, "%d %d %c", &procs, &quantum, &trailing) != 2)
		return -EINVAL;

	ret = start_simulation(procs, quantum);
	if (ret < 0)
		return ret;

	return count;
}

static const struct proc_ops timeshare_proc_ops = {
	.proc_open	= timeshare_proc_open,
	.proc_read	= seq_read,
	.proc_write	= timeshare_proc_write,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

/*
 * timeshare_init - Kernel module initialization entry point
 *
 * Return: 0 on success, negative errno on failure.
 */
static int __init timeshare_init(void)
{
	memset(&ts_ctx, 0, sizeof(ts_ctx));
	mutex_init(&ts_ctx.ctrl_lock);
	spin_lock_init(&ts_ctx.log_lock);
	init_waitqueue_head(&ts_ctx.sched_wq);

	ts_ctx.log_buf = kzalloc(LOG_BUF_SIZE, GFP_KERNEL);
	if (!ts_ctx.log_buf)
		return -ENOMEM;

#ifdef HAVE_HRTIMER_SETUP
	hrtimer_setup(&ts_ctx.quantum_timer, timeshare_timer_cb,
		      CLOCK_MONOTONIC, HRTIMER_MODE_REL);
#else
	hrtimer_init(&ts_ctx.quantum_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ts_ctx.quantum_timer.function = timeshare_timer_cb;
#endif

	/* 0666 allows unprivileged users or timeshare_ctl to trigger runs */
	ts_ctx.proc_entry = proc_create(TIMESHARE_PROC_NAME, 0666, NULL,
					&timeshare_proc_ops);
	if (!ts_ctx.proc_entry) {
		kfree(ts_ctx.log_buf);
		return -ENOMEM;
	}

	pr_info("Loaded Round-Robin scheduler module (/proc/%s ready)\n",
		TIMESHARE_PROC_NAME);

	if (nr_procs > 0) {
		int ret = start_simulation(nr_procs, quantum_ms);

		if (ret < 0)
			pr_warn("Ignored invalid module params (%d, %d)\n",
				nr_procs, quantum_ms);
	}

	return 0;
}

/*
 * timeshare_exit - Kernel module cleanup exit point
 */
static void __exit timeshare_exit(void)
{
	if (ts_ctx.proc_entry)
		proc_remove(ts_ctx.proc_entry);

	mutex_lock(&ts_ctx.ctrl_lock);
	stop_active_simulation();
	mutex_unlock(&ts_ctx.ctrl_lock);

	kfree(ts_ctx.log_buf);
	pr_info("Unloaded Round-Robin scheduler module\n");
}

module_init(timeshare_init);
module_exit(timeshare_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Operating Systems Course Project");
MODULE_DESCRIPTION("Kernel-Level Round-Robin Time-Sharing Scheduler");
MODULE_VERSION("2.0");
