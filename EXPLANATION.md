# Comprehensive Code Explanation & Video Presentation Guide

This guide is designed to help you **understand every part of the C code** and **deliver a clear, confident video demonstration** covering all three grading criteria:
1. **Ability to install the operating system & toolchain (30%)**
2. **Ability to write the C program using signals and timers (40%)**
3. **Ability to explain the program comprehensively (30%)**

---

## Part 1: Video Presentation Structure (Suggested 5–8 Minute Flow)

### Segment 1: Show Your Linux OS & GCC Setup (1–1.5 mins — 30% Rubric)
Run these commands in your terminal while recording and briefly explain each:
```bash
# 1. Show the installed Linux kernel and distribution
uname -a
cat /etc/os-release

# 2. Show the installed GCC C compiler and GNU Make build tool
gcc --version
make --version

# 3. Show the project files and Git commit history
ls -la
git log --oneline
```
**What to say:**
> "First, here is my Linux environment running kernel `uname -r`, along with GCC and GNU Make installed. I organized the project into small modular files (`scheduler.h`, `worker.c`, `scheduler.c`, `main.c`, and `Makefile`) following the Linux Kernel Coding Style, and tracked my iterative progress using Git with conventional commits."

---

### Segment 2: Live Program Demo (1.5–2 mins — 40% Rubric)
Run the build and execute the simulator live:
```bash
# 1. Clean and compile from scratch using the Makefile
make clean
make

# 2. Run the interactive simulator
./timeshare_sim
```
When prompted:
- Enter `3` for **number of worker processes**
- Enter `500` for **time quantum in milliseconds**

**What to point out while it runs on screen:**
1. **Initialization (`[INIT]`)**: The parent process forks 3 child processes (`P1`, `P2`, `P3`). Each child immediately pauses itself in the `READY` state before doing any work.
2. **Dispatch (`[SCHEDULER] Dispatching ... -> sending SIGCONT`)**: The scheduler picks `P1` and sends `SIGCONT` to let it run on the CPU.
3. **Worker Progress (`[WORKER P1 ...] Step 1/3`)**: While `P1` has the CPU, it completes a step of its workload and prints its progress bar.
4. **Timer Preemption (`[TIMER] Tick #1 ... -> SIGALRM received`)**: After 500 ms, the OS interval timer fires `SIGALRM`. The scheduler preempts `P1` by sending `SIGSTOP` (`State: STOPPED`) and switches to `P2`, then `P3`, in Round-Robin order.
5. **Process Completion (`State: TERMINATED`)**: In later rounds, `P1` finishes all 3 steps early, exits, and the scheduler automatically skips `P1` and continues Round-Robin between `P2` and `P3`.
6. **Summary Table**: At the end, the program prints the total time slices used by each process, total timer interrupts (`SIGALRM`), and total context switches.

---

### Segment 3: Code Walkthrough (3–4 mins — 30% Rubric)

Walk through the 4 files in this exact order:

---

## Part 2: File-by-File Code Explanation

### 1. `scheduler.h` — Data Structures (The Process Control Block)

In a real operating system kernel (like Linux's `struct task_struct`), the scheduler tracks every process using a **Process Control Block (PCB)**. In `scheduler.h`, we define three core types:

1. **`enum proc_state`** — Represents the 4 states of a process in our state machine:
   - `PROC_READY`: Process has been created via `fork()` and stopped, waiting for its first turn.
   - `PROC_RUNNING`: Process is currently resumed via `SIGCONT` and executing on the CPU.
   - `PROC_STOPPED`: Process was preempted by the timer interrupt via `SIGSTOP`.
   - `PROC_TERMINATED`: Process completed its finite workload and exited.

2. **`struct proc_control_block`** — Stores per-process metadata:
   - `id`: Logical number (`1`, `2`, `3`, ... so logs say `P1`, `P2`, `P3`).
   - `pid`: Real Linux Process ID returned by `fork()`.
   - `total_steps`: How many workload steps this process must finish before exiting.
   - `slices_used`: How many time quantums the scheduler has given this process.
   - `state`: Current `enum proc_state`.

3. **`struct scheduler_ctx`** — Holds the process table (`procs[MAX_PROCS]`), the number of active processes (`active_procs`), the currently running index (`curr_idx`), the time quantum (`quantum_ms`), and counters for timer ticks and context switches.

---

### 2. `worker.c` — How Child Processes Simulate CPU Work

When `scheduler_init()` calls `fork()`, the child process executes `worker_run(proc_id, total_steps, step_delay_ms)`:

1. **Line-Buffered Atomic Output (`setvbuf(stdout, NULL, _IOLBF, 0)`)**:
   - Configures `stdout` as line-buffered and formats the entire progress bar into a single `printf` call followed by `fflush(stdout)`. This ensures each progress line is written in a single system call and never gets split mid-line when `SIGSTOP` arrives.
2. **Initial Synchronization (`raise(SIGSTOP)`)**:
   - Immediately after `fork()`, the child sends `SIGSTOP` to itself!
   - **Why is this important?** Without `raise(SIGSTOP)`, a newly forked child might start running before the parent finishes creating the other children. By stopping itself immediately, every child waits at the starting line until the scheduler explicitly sends `SIGCONT`.
3. **Simulating Active CPU Bursts (`simulate_cpu_work`)**:
   - Each step takes ~60% of one time quantum (`step_delay_ms = quantum_ms * 3 / 5`), broken into tiny 1–5 ms `nanosleep()` ticks inside a `while (remaining_ms > 0)` loop.
   - **Why small ticks instead of one big sleep?** When the parent sends `SIGSTOP`, the Linux kernel freezes the child process mid-loop. The `remaining_ms` counter cannot decrease while the child is stopped! When the scheduler later sends `SIGCONT`, the child resumes from the exact remaining milliseconds where it was preempted.
4. **Completion (`exit(EXIT_SUCCESS)`)**:
   - Once the `for` loop finishes `1 .. total_steps`, the worker prints its completion message and exits with `exit(EXIT_SUCCESS)`, which triggers `SIGCHLD` in the parent.

---

### 3. `scheduler.c` — How the Signal & Timer Scheduler Works

`scheduler.c` is the heart of the project. Here is how each function works:

#### A. Signal Handlers (`sigalrm_handler`, `sigchld_handler`, `sigterm_handler`)
```c
static volatile sig_atomic_t timer_expired;
static volatile sig_atomic_t child_exited;
static volatile sig_atomic_t stop_requested;
```
- We use `volatile sig_atomic_t` flags because the C standard guarantees that writing to `sig_atomic_t` inside an asynchronous signal handler is atomic and safe.
- Instead of calling non-async-signal-safe functions like `printf()` inside the signal handler, the handlers simply set `timer_expired = 1` (on `SIGALRM`), `child_exited = 1` (on `SIGCHLD`), or `stop_requested = 1` (on `SIGINT`/`SIGTERM`), waking up `pause()` in the main loop.
- `setup_signal_handlers()` registers these handlers using `sigaction()`, passing `SA_NOCLDSTOP` for `SIGCHLD` so `SIGCHLD` is only delivered when a child actually terminates (not when it pauses on `SIGSTOP`).

#### B. Hardware/Kernel Interval Timer (`configure_timer`)
```c
timer.it_value.tv_sec = quantum_ms / 1000;
timer.it_value.tv_usec = (quantum_ms % 1000) * 1000;
timer.it_interval = timer.it_value;
setitimer(ITIMER_REAL, &timer, NULL);
```
- `setitimer(ITIMER_REAL, ...)` asks the Linux kernel to count down real wall-clock time.
- When `quantum_ms` elapses, the kernel automatically delivers `SIGALRM` to the parent scheduler process.
- Passing `0` disarms the timer at the end of each slice so no stale timer interrupt fires during context switching.

#### C. Spawning the Process Table (`scheduler_init`)
- Loops `i = 0` to `nr_procs - 1` and calls `fork()`.
- Assigns slightly varied workloads (`BASE_WORK_STEPS + (i % 3)` -> 3, 4, 5 steps) so you can see processes finishing at different times.
- In the parent branch (`pid > 0`), calls `waitpid(pid, &status, WUNTRACED)` to confirm the child has stopped itself (`raise(SIGSTOP)`) before recording it as `PROC_READY`.

#### D. Round-Robin Selection (`find_next_runnable`)
```c
for (i = 1; i <= ctx->nr_procs; i++) {
	int idx = (ctx->curr_idx + i) % ctx->nr_procs;
	enum proc_state st = ctx->procs[idx].state;

	if (st == PROC_READY || st == PROC_STOPPED)
		return idx;
}
```
- Uses modular arithmetic `(curr_idx + i) % nr_procs` to wrap around circularly from the last process back to `P1`.
- Automatically skips any process in `PROC_TERMINATED`.

#### E. The Main Scheduling Loop (`scheduler_run`)
While `ctx->active_procs > 0`:
1. **Select**: Calls `find_next_runnable(ctx)` to pick the next process `proc`.
2. **Dispatch**: Marks `proc->state = PROC_RUNNING`, increments `proc->slices_used` and `ctx->context_switches`, arms the timer (`configure_timer(ctx->quantum_ms)`), and sends `kill(proc->pid, SIGCONT)`.
3. **Wait (`pause()`)**: The parent sleeps via `pause()` while the child executes:
   - If the child finishes all its work before the timer expires, the child exits, the kernel sends `SIGCHLD`, `pause()` wakes up, and `check_child_terminated()` reaps the child via `waitpid(proc->pid, &status, WNOHANG)` and sets its state to `PROC_TERMINATED`.
   - If the time quantum runs out first, the kernel sends `SIGALRM`, `timer_expired` becomes `1`, and `preempt_running_proc()` sends `kill(proc->pid, SIGSTOP)`, waits for the child to stop with `waitpid(proc->pid, &status, WUNTRACED)`, and sets its state to `PROC_STOPPED`.
4. **Disarm**: After each slice (and when all processes finish), `configure_timer(0)` stops the interval timer.

---

### 4. `main.c` — User Input & Program Lifecycle

- `read_configuration()` either reads the interactive prompts from `stdin` (`Enter number of worker processes [1-10]` and `Enter time quantum in milliseconds [10-10000]`) or accepts two command-line arguments (`./timeshare_sim 3 500`).
- `parse_bounded_int()` uses `strtol()` to strictly reject non-numeric strings, negative numbers, or out-of-range values.
- `main()` calls `scheduler_init()`, `scheduler_run()`, and `scheduler_print_summary()` in sequence — keeping `main()` short, readable, and easy to follow.

---

## Part 3: Likely Questions & Answers for Your Assessment

1. **Q: Why do we use `SIGSTOP` and `SIGCONT` instead of `SIGTERM` or `SIGKILL`?**
   - **A:** `SIGTERM` and `SIGKILL` permanently terminate a process, destroying its memory and execution state. `SIGSTOP` pauses a process in memory while preserving its CPU registers, stack, and loop variables, and `SIGCONT` resumes it from the exact instruction where it paused—just like an OS context switch.

2. **Q: Why do we use `setitimer(ITIMER_REAL, ...)` instead of `sleep()` in the scheduler?**
   - **A:** `setitimer` configures an asynchronous kernel interval timer that delivers a `SIGALRM` interrupt when the time quantum expires, closely modeling a hardware timer interrupt in a real OS kernel. Meanwhile, using `pause()` lets the scheduler wake up early on `SIGCHLD` if a child process finishes before its time quantum is over.

3. **Q: Why do we call `waitpid()` with `WNOHANG` and `WUNTRACED`?**
   - **A:** `WNOHANG` checks if a running child has terminated without blocking the scheduler if the child is still alive. `WUNTRACED` allows `waitpid()` to return as soon as a child enters the stopped state (`SIGSTOP`), guaranteeing the child has completely paused before the scheduler prints its preemption log and dispatches the next child. This prevents race conditions and keeps terminal output cleanly ordered.
