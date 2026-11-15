# Comprehensive Code Explanation, Theory Mapping & Video Presentation Guide

This guide is designed to help you **master both the user-space C scheduler and the Linux kernel module in 1 week** and deliver a clear, confident presentation covering every Learning Outcome (`LO 3`, `LO 4`) and all three grading rubric rows:

| Rubric Criteria | Weight | How This Project Fulfills It |
| :--- | :---: | :--- |
| **1. Ability to Install Instructional Operating Systems** | **30%** | Installed **Debian Minimal (`netinst`, TTY-only, no GUI)** in a VM with the Linux kernel headers (`linux-headers-$(uname -r)`), `gcc`, and `make`. |
| **2. Ability to write the program and integrate the modified system call into the kernel** | **40%** | Wrote the multi-process Round-Robin scheduler (`scheduler.c`, `worker.c`, `main.c`) using timers (`setitimer`) and signals (`SIGALRM`, `SIGSTOP`, `SIGCONT`, `SIGCHLD`), and integrated it into the Linux kernel via `kmod/timeshare_kmod.c` (`proc_create("/proc/timeshare")`, hooking `read()` and `write()` VFS system calls). |
| **3. Ability to explain the program comprehensively** | **30%** | Connects Linus Torvalds' 1991 Linux `0.01` timer-interrupt context switcher (`LO 3`), the GeeksforGeeks `10–100 ms` Time-Sharing quantum definition, and a line-by-line walkthrough of both user space and kernel space (`LO 4`). |

---

## Part 1: Fundamental OS Theory (`LO 3` & `LO 4` Mapping)

### 1. How Time-Sharing Relates to Round-Robin, Timers, and Signals

> *"This short period of time during which the user gets the attention of the CPU is known as time slice, time slot, or quantum. It is typically of the order of 10 to 100 milliseconds."* — GeeksforGeeks (Time-Sharing Operating System)

- **Time-Sharing Operating System (The Goal)**: An OS architecture where a single CPU is multiplexed across multiple interactive processes so swiftly (`10–100 ms` per turn) that every process appears to run simultaneously.
- **Round-Robin Scheduling (The Algorithm)**: The concrete scheduling policy that implements Time-Sharing. It keeps all runnable processes in a **circular queue** and grants each process exactly one **time quantum (`10–100 ms`)** before preempting it and moving to the next process in line.
- **Why Both a Timer AND Signals Are Required Together**:
  1. **The Timer (`setitimer(ITIMER_REAL)`) is the Clock**: Without a hardware/kernel timer counting down the `10–100 ms` quantum, a running process could monopolize the CPU forever (non-preemptive execution). When the time quantum expires, the kernel timer fires an asynchronous **`SIGALRM`** interrupt.
  2. **The Signals (`SIGALRM`, `SIGSTOP`, `SIGCONT`, `SIGCHLD`) are the Context-Switch Mechanism**:
     - **`SIGALRM`** interrupts the scheduler when the time slice (`10–100 ms`) is over.
     - **`SIGSTOP`** preempts (freezes) the currently running worker process in memory while preserving its CPU registers, stack, and loop state.
     - **`SIGCONT`** dispatches (resumes) the next worker process in the Round-Robin ring from the exact instruction where it was paused.
     - **`SIGCHLD`** notifies the scheduler immediately if a worker completes its workload before its time quantum expires.

### 2. Historical `LO 3` Connection: Linus Torvalds' 1991 Minimal TTY Environment

In early 1991, before Linux even had a file system or graphical interface, **Linus Torvalds** wrote the very first pre-`0.01` prototype of Linux on his Intel 386 PC to learn hardware task switching:
- He created **two processes**: Process A continuously printed `AAAA...` to the terminal, and Process B continuously printed `BBBB...` to the terminal.
- He programmed the **hardware timer interrupt** to fire periodically and switch the CPU context between Process A and Process B (`AAAA` -> timer interrupt -> `BBBB` -> timer interrupt -> `AAAA`).
- When he released **Linux `0.01`** in September 1991, it had **no GUI at all**—it booted directly into a **minimal text console (`tty1`)** running only two user-space programs ported by Linus: **GNU `bash`** (the shell) and **GNU `gcc`** (the C compiler).

Our project directly mirrors that historical architecture (`LO 3`):
1. **Debian Minimal (`netinst`, no GUI)** boots straight into `tty1` with `bash`, `gcc`, and `make`—zero desktop bloat, booting in <5 seconds using <150 MB of RAM.
2. **Our worker processes (`P1..Pn`)** are the modern counterpart of Linus's `AAAA` and `BBBB` tasks, preempted on every timer tick (`setitimer` -> `SIGALRM`) and switched via `SIGSTOP`/`SIGCONT`.
3. **Our Loadable Kernel Module (`kmod/timeshare_kmod.c`)** bridges User Space (Ring 3) and Kernel Space (Ring 0) by registering `/proc/timeshare` in the kernel's Virtual File System (VFS) so every scheduler transition (`INIT`, `DISPATCH`, `PREEMPT`, `TERMINATE`) executes a real kernel `write()` system call handler (`timeshare_proc_write`).

---

## Part 2: Video Presentation Structure (Suggested 6–8 Minute Flow)

### Segment 1: Show Your Debian Minimal VM & Kernel Environment (1.5 mins — 30% Rubric)
Inside your Debian Minimal (`tty1` or SSH) terminal, run:
```bash
# 1. Show the minimal Linux kernel and Debian OS release
uname -a
cat /etc/os-release

# 2. Show the installed GCC compiler, GNU Make, and kernel headers
gcc --version
make --version
ls -d /lib/modules/$(uname -r)/build

# 3. Show the project files and Git commit history
ls -la
ls -la kmod/
git log --oneline -n 8
```
**What to say:**
> "For the instructional operating system requirement, I installed Debian Minimal (`netinst`) without a desktop GUI so it boots directly into a pure Linux `tty1` console with `bash`, `gcc`, `make`, and the Linux kernel headers (`linux-headers-$(uname -r)`), inspired by Linus Torvalds' 1991 Linux `0.01` development environment."

---

### Segment 2: Compile, Load the Kernel Module & Run the Live Demo (2.5 mins — 40% Rubric)
Run these commands live on camera:
```bash
# 1. Clean and compile both the user-space program and the Linux kernel module
make clean && make kmod-clean
make
make kmod

# 2. Load the kernel module into the Linux kernel and verify /proc/timeshare
sudo insmod kmod/timeshare_kmod.ko
lsmod | grep timeshare
cat /proc/timeshare

# 3. Run the simulator with 3 processes and a 50 ms quantum (within the 10-100 ms Time-Sharing range)
./timeshare_sim 3 50

# 4. Read the kernel telemetry via the read() syscall on /proc/timeshare
cat /proc/timeshare

# 5. Show the kernel ring buffer (dmesg) entries written by our write() syscall handler
sudo dmesg | tail -n 20

# 6. Unload the kernel module cleanly
sudo rmmod timeshare_kmod
```
**What to point out while it runs on screen:**
1. **`[INIT]`**: `scheduler_init()` forks `P1`, `P2`, and `P3`. Each child stops itself immediately (`raise(SIGSTOP)`) in the `READY` state, and the parent writes `INIT` to `/proc/timeshare`.
2. **`[SCHEDULER] Dispatch P1 ... -> SIGCONT`**: The scheduler arms the 50 ms interval timer (`setitimer`), resumes `P1` with `SIGCONT`, and writes `DISPATCH` to `/proc/timeshare`.
3. **`[TIMER] Tick #1 (50 ms quantum expired) -> SIGALRM received`**: After 50 ms, the kernel delivers `SIGALRM`. The scheduler preempts `P1` with `SIGSTOP` (`-> STOPPED`), writes `PREEMPT` to `/proc/timeshare`, and switches to `P2`, then `P3` in Round-Robin order.
4. **`cat /proc/timeshare` & `sudo dmesg | tail`**: Show how every user-space `write()` system call was captured in Ring 0 by `timeshare_kmod.ko`, incrementing `Events Logged` and recording each state transition in `dmesg`.

---

### Segment 3: Code Walkthrough (3 mins — 30% Rubric)
Walk through the user-space files (`scheduler.h`, `worker.c`, `scheduler.c`) and the kernel module (`kmod/timeshare_kmod.c`) using the breakdown below.

---

## Part 3: File-by-File & Line-by-Line Code Walkthrough

### 1. `scheduler.h` — Process Control Block (PCB) & State Machine

Just as the Linux kernel tracks every task using `struct task_struct`, our scheduler tracks each worker using `struct proc_control_block`:
- **`enum proc_state`**:
  - `PROC_READY`: Created via `fork()` and stopped, waiting for its first time quantum.
  - `PROC_RUNNING`: Currently executing on the CPU after receiving `SIGCONT`.
  - `PROC_STOPPED`: Preempted via `SIGSTOP` when its `10–100 ms` quantum expired.
  - `PROC_TERMINATED`: Completed all workload steps and exited (`SIGCHLD`).
- **`struct proc_control_block`**: Stores `id` (`P1..Pn`), `pid` (OS PID from `fork()`), `total_steps`, `slices_used`, and `state`.
- **`struct scheduler_ctx`**: Holds the process table `procs[MAX_PROCS]`, `nr_procs`, `active_procs`, `curr_idx`, `quantum_ms`, `total_ticks`, and `context_switches`.

---

### 2. `worker.c` — Child Process Synchronization & CPU Burst Simulation

When `scheduler_init()` calls `fork()`, each child runs `worker_run(proc_id, total_steps, step_delay_ms)`:
1. **Atomic Line-Buffered Output (`setvbuf(stdout, NULL, _IOLBF, 0)`)**: Ensures each progress bar line is flushed in one atomic `write()` call so `SIGSTOP` never splits a line mid-print.
2. **Self-Stop at Startup (`raise(SIGSTOP)`)**:
   - Right after `fork()`, the child pauses itself before executing any work.
   - The parent waits for this exact stop via `waitpid(pid, &status, WUNTRACED)`, guaranteeing all children start at the starting line (`PROC_READY`) until the scheduler sends `SIGCONT`.
3. **Granular CPU Burst (`simulate_cpu_work`)**:
   - Each step requires ~60% of a time quantum (`step_delay_ms = (quantum_ms * 3) / 5`), broken into 1–5 ms `nanosleep()` ticks inside `while (remaining_ms > 0)`.
   - When `SIGSTOP` arrives on timer expiration, the kernel freezes the child mid-loop; when `SIGCONT` arrives on its next turn, it resumes from the exact remaining milliseconds.

---

### 3. `scheduler.c` — Timer/Signal Scheduler & `/proc/timeshare` Syscall Helper

#### A. Kernel System Call Telemetry Helper (`log_kernel_event`)
```c
static void log_kernel_event(const char *fmt, ...)
{
	char buf[128];
	va_list ap;
	ssize_t ret;
	int fd;
	int len;

	fd = open("/proc/timeshare", O_WRONLY);
	if (fd < 0)
		return;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (len > 0) {
		if ((size_t)len >= sizeof(buf))
			len = (int)(sizeof(buf) - 1);
		ret = write(fd, buf, (size_t)len);
		(void)ret;
	}

	close(fd);
}
```
- **How it works**: Invokes the raw POSIX `open()`, `write()`, and `close()` system calls to send each scheduler transition (`INIT`, `DISPATCH`, `PREEMPT`, `TERMINATE`) across the User/Kernel boundary into `/proc/timeshare`.
- **Safe Fallback**: If `timeshare_kmod.ko` is not loaded, `open("/proc/timeshare", O_WRONLY)` returns `-1` and `log_kernel_event()` returns immediately so `./timeshare_sim` works anywhere without root.

#### B. Signal Handlers & Interval Timer (`setup_signal_handlers`, `configure_timer`)
- Registers `sigalrm_handler` (`SIGALRM` -> `timer_expired = 1`), `sigchld_handler` (`SIGCHLD` with `SA_NOCLDSTOP` -> `child_exited = 1`), and `sigterm_handler` (`SIGINT`/`SIGTERM` -> `stop_requested = 1`) using `volatile sig_atomic_t` flags.
- `configure_timer(quantum_ms)` calls `setitimer(ITIMER_REAL, &timer, NULL)` to arm the kernel interval timer for `quantum_ms` milliseconds (or `0` to disarm it).

#### C. Round-Robin Selection & Preemption (`find_next_runnable`, `preempt_running_proc`, `scheduler_run`)
- `find_next_runnable()` uses modular arithmetic `(ctx->curr_idx + i) % ctx->nr_procs` to pick the next `PROC_READY` or `PROC_STOPPED` child in circular order, skipping `PROC_TERMINATED` children.
- `scheduler_run()` dispatches the chosen process with `kill(proc->pid, SIGCONT)`, logs `DISPATCH` to `/proc/timeshare`, and sleeps via `pause()` until woken by either `SIGCHLD` (child finished early -> `TERMINATE`) or `SIGALRM` (quantum expired -> `kill(proc->pid, SIGSTOP)` -> `PREEMPT`).

---

### 4. `kmod/timeshare_kmod.c` — Line-by-Line Kernel Module Explanation

`kmod/timeshare_kmod.c` is deliberately concise (91 lines) and consists of **3 core parts** that are easy to explain during your video demonstration:

#### Part A: Kernel Headers & Shared State (Lines 1–18)
```c
#define PROC_NAME	"timeshare"
#define EVENT_BUF_LEN	128

/* Protects event_count and last_event across concurrent syscalls */
static DEFINE_MUTEX(timeshare_lock);
static int event_count;
static char last_event[EVENT_BUF_LEN] = "none";
```
- `DEFINE_MUTEX(timeshare_lock)` defines a kernel mutex lock so that if one process reads `/proc/timeshare` while another writes to it, kernel memory (`event_count` and `last_event`) is never corrupted.
- `event_count` tracks the total number of scheduler transitions received since the module was loaded, and `last_event` stores the latest transition string in kernel memory.

#### Part B: Function 1 — Handling the `read()` System Call (`timeshare_proc_show`, Lines 20–34)
```c
static int timeshare_proc_show(struct seq_file *m, void *v)
{
	mutex_lock(&timeshare_lock);
	seq_printf(m, "Kernel Module : %s_kmod (ACTIVE)\n", PROC_NAME);
	seq_printf(m, "Events Logged : %d\n", event_count);
	seq_printf(m, "Last Event    : %s\n", last_event);
	mutex_unlock(&timeshare_lock);
	return 0;
}

static int timeshare_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, timeshare_proc_show, NULL);
}
```
- When you run `cat /proc/timeshare`, `cat` invokes the `open()` and `read()` system calls.
- The kernel VFS routes `open()` to `timeshare_proc_open()`, which uses Linux's `single_open()` helper to call `timeshare_proc_show()`.
- `seq_printf()` formats the kernel's internal state (`event_count` and `last_event`) into the kernel buffer that `seq_read` copies back to user space.

#### Part C: Function 2 — Handling the `write()` System Call (`timeshare_proc_write`, Lines 36–60)
```c
static ssize_t timeshare_proc_write(struct file *file, const char __user *ubuf,
				    size_t count, loff_t *ppos)
{
	char kbuf[EVENT_BUF_LEN];
	size_t len;

	if (count == 0)
		return 0;

	len = (count >= EVENT_BUF_LEN) ? (EVENT_BUF_LEN - 1) : count;
	if (copy_from_user(kbuf, ubuf, len))
		return -EFAULT;

	kbuf[len] = '\0';
	if (kbuf[len - 1] == '\n')
		kbuf[len - 1] = '\0';

	mutex_lock(&timeshare_lock);
	event_count++;
	snprintf(last_event, sizeof(last_event), "%s", kbuf);
	pr_info("timeshare_kmod: event #%d: %s\n", event_count, last_event);
	mutex_unlock(&timeshare_lock);
	return count;
}
```
- When `scheduler.c` calls `write(fd, buf, len)` on `/proc/timeshare`, the CPU traps from User Space (Ring 3) into Kernel Space (Ring 0) and calls `timeshare_proc_write()`.
- **Why `copy_from_user(kbuf, ubuf, len)`?** The kernel must **never** directly dereference a `__user` pointer (`ubuf`), because user memory might be unmapped, paged out, or an invalid address. `copy_from_user()` safely validates the user pointer and copies `len` bytes into the kernel stack buffer `kbuf` (returning `-EFAULT` if invalid).
- After null-terminating `kbuf` and stripping any trailing newline `\n`, it locks `timeshare_lock`, increments `event_count`, updates `last_event`, and calls `pr_info()` to log the transition into the kernel ring buffer (`dmesg`).

#### Part D: Function 3 — Module Init, Exit & VFS Operations Table (Lines 62–91)
```c
static const struct proc_ops timeshare_proc_ops = {
	.proc_open	= timeshare_proc_open,
	.proc_read	= seq_read,
	.proc_write	= timeshare_proc_write,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int __init timeshare_kmod_init(void)
{
	if (!proc_create("timeshare", 0666, NULL, &timeshare_proc_ops))
		return -ENOMEM;

	pr_info("timeshare_kmod: /proc/%s registered\n", PROC_NAME);
	return 0;
}

static void __exit timeshare_kmod_exit(void)
{
	remove_proc_entry(PROC_NAME, NULL);
	pr_info("timeshare_kmod: /proc/%s removed (events=%d)\n",
		PROC_NAME, event_count);
}
```
- `struct proc_ops` maps standard POSIX system calls (`open`, `read`, `write`, `lseek`, `close`) on `/proc/timeshare` to our kernel functions.
- `timeshare_kmod_init()` runs when you execute `sudo insmod kmod/timeshare_kmod.ko` and creates `/proc/timeshare` with permissions `0666` so `./timeshare_sim` can write events without needing `sudo`.
- `timeshare_kmod_exit()` runs when you execute `sudo rmmod timeshare_kmod` and cleanly unregisters `/proc/timeshare`.

---

## Part 4: 1-Week Study & Video Preparation Plan

| Day | Focus Area | Action Items (30–45 mins/day) |
| :--- | :--- | :--- |
| **Day 1** | **Debian Minimal VM Setup (30% Rubric)** | Install Debian 12 `netinst` (uncheck GUI in `tasksel`), run `sudo apt install -y build-essential linux-headers-$(uname -r) git`, and clone this repo. |
| **Day 2** | **Core Theory (`LO 3`)** | Read Part 1 above: understand the `10–100 ms` Time-Sharing quantum, why Round-Robin needs both `setitimer` (Timer) and `SIGALRM`/`SIGSTOP`/`SIGCONT` (Signals), and Linus Torvalds' 1991 `AAAA`/`BBBB` TTY context switcher. |
| **Day 3** | **User-Space Scheduler (`scheduler.h`, `worker.c`)** | Trace how `fork()` + `raise(SIGSTOP)` pauses every child in `PROC_READY`, and how `simulate_cpu_work()` uses 1–5 ms `nanosleep()` ticks so `SIGSTOP` freezes progress accurately. |
| **Day 4** | **Round-Robin Loop (`scheduler.c`, `main.c`)** | Trace `scheduler_run()`: how `setitimer(ITIMER_REAL)` arms the quantum, `kill(pid, SIGCONT)` dispatches a child, `pause()` waits for `SIGALRM`/`SIGCHLD`, and `kill(pid, SIGSTOP)` preempts it. |
| **Day 5** | **Kernel Module (`kmod/timeshare_kmod.c`)** | Study the 3 functions in `kmod/timeshare_kmod.c` (`timeshare_proc_show`, `timeshare_proc_write`, `timeshare_kmod_init`/`exit`) and how `log_kernel_event()` in `scheduler.c` calls `open()`/`write()`/`close()` on `/proc/timeshare`. |
| **Day 6** | **Dry Run in Debian Minimal TTY** | Practice running `make && make kmod`, `sudo insmod kmod/timeshare_kmod.ko`, `./timeshare_sim 3 50`, `cat /proc/timeshare`, and `sudo dmesg \| tail -n 20`. |
| **Day 7** | **Record Final Video Presentation** | Follow the 3-segment script in Part 2 (6–8 minutes total) and submit with confidence. |

---

## Part 5: Likely Questions & Answers for Your Assessment

1. **Q: How does Round-Robin relate to a Time-Sharing Operating System, and why do we use a `10–100 ms` quantum?**
   - **A:** Time-Sharing is the operating system design where multiple tasks share a single CPU interactively. Round-Robin is the scheduling algorithm that implements Time-Sharing by cycling through tasks in a circular queue with a fixed time slice (quantum) of `10–100 ms`. If the quantum is too large (e.g., >1 second), interactive responsiveness degrades into First-Come, First-Served; if too small (e.g., <1 ms), context-switching overhead dominates CPU time.

2. **Q: Why do we use `SIGSTOP` and `SIGCONT` instead of `SIGTERM` or `SIGKILL`?**
   - **A:** `SIGTERM` and `SIGKILL` permanently terminate a process, destroying its memory and execution state. `SIGSTOP` pauses a process in memory while preserving its CPU registers, stack, and loop variables, and `SIGCONT` resumes it from the exact instruction where it paused—modeling an OS context switch.

3. **Q: Why do we use `setitimer(ITIMER_REAL, ...)` instead of `sleep()` in the scheduler?**
   - **A:** `setitimer` configures an asynchronous kernel interval timer that delivers a `SIGALRM` interrupt when the time quantum expires, closely modeling a hardware timer interrupt in a real OS kernel. Meanwhile, using `pause()` lets the scheduler wake up early on `SIGCHLD` if a child process finishes before its time quantum is over.

4. **Q: How does the user-space scheduler communicate with the Linux kernel module (`timeshare_kmod.ko`)?**
   - **A:** When `timeshare_kmod.ko` is loaded via `insmod`, it registers `/proc/timeshare` in the kernel's Virtual File System using `proc_create()` and attaches custom `read()` and `write()` system call handlers via `struct proc_ops`. Whenever the user-space scheduler transitions a process (`INIT`, `DISPATCH`, `PREEMPT`, `TERMINATE`), `log_kernel_event()` calls `open("/proc/timeshare", O_WRONLY)` and `write()`. The kernel routes that `write()` system call to `timeshare_proc_write()`, which safely copies the string from user space into kernel space via `copy_from_user()`, increments `event_count`, and logs the event to `dmesg` via `pr_info()`.
