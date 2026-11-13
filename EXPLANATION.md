# Comprehensive Kernel-Level Code Explanation & Video Presentation Guide

This guide helps you **understand every part of the Linux Kernel Module (`timeshare_kmod.ko`)** and **deliver a clear, confident video demonstration** inside a Virtual Machine covering all three grading criteria:
1. **Ability to install the operating system & VM toolchain (30%)**
2. **Ability to write the kernel-level C program using timers and process scheduling (40%)**
3. **Ability to explain the program comprehensively (30%)**

---

## Part 1: Video Presentation Structure (Suggested 5–8 Minute Flow)

### Segment 1: Show Your Linux Virtual Machine & Kernel Build Toolchain (1–1.5 mins — 30% Rubric)
Run these commands in your terminal (or inside your VirtualBox / QEMU VM) while recording:
```bash
# 1. Show the Linux kernel version and OS distribution
uname -a
cat /etc/os-release

# 2. Show the installed GCC compiler, GNU Make, Kernel Headers, and QEMU/VM tools
gcc --version
make --version
ls -d /lib/modules/$(uname -r)/build

# 3. Show the project files and checkpatch compliance
ls -la
make checkpatch
```
**What to say:**
> "First, here is my Linux environment running kernel `uname -r` with the Linux kernel headers (`/lib/modules/$(uname -r)/build`), GCC, and GNU Make installed. Instead of implementing the scheduler only in user space, I implemented the Round-Robin time-sharing scheduler at the **Linux kernel level (Ring 0)** as a Loadable Kernel Module (`timeshare_kmod.ko`), and packaged it with an automated Virtual Machine setup (`make vm-qemu` and `Vagrantfile` / `vm/setup-vm.sh`) so anyone on another machine can safely run and test it inside an isolated VM."

---

### Segment 2: Live Kernel Module & Virtual Machine Demo (1.5–2.5 mins — 40% Rubric)
Demonstrate compiling the kernel module and running it inside the Virtual Machine:
```bash
# 1. Clean and compile the Loadable Kernel Module (.ko) and controller via Kbuild
make clean
make

# 2. Boot the isolated Linux Kernel VM (or load the module with sudo make load)
make vm-qemu
```
Inside the VM menu:
1. Point out the boot banner showing the VM kernel booting and executing `insmod /timeshare_kmod.ko` in Ring 0 (`lsmod | grep timeshare_kmod`).
2. Select **`1` (Run Interactive Kernel Scheduler)**:
   - Enter `3` for **number of worker processes**
   - Enter `400` for **time quantum in milliseconds**
3. **What to point out while it runs on screen:**
   - **Kernel Thread Creation (`[INIT]`)**: The kernel module spawns 3 real kernel threads (`kthread_create`) with kernel PIDs (`P1`, `P2`, `P3`). Each worker thread immediately blocks on its kernel waitqueue (`wait_queue_head_t`) in the `READY` state.
   - **Dispatch (`[SCHEDULER] Dispatch P1 ... -> wake_up_process`)**: The coordinator scheduler `kthread` picks `P1`, sets `run_granted = true`, arms the kernel high-resolution timer (`hrtimer_start`), and wakes `P1`'s waitqueue.
   - **Worker Progress (`[WORKER P1 ...] Step 1/3`)**: While `P1` runs in kernel space, it executes its workload burst and logs its progress bar to both `dmesg` (`pr_info`) and `/proc/timeshare`.
   - **Hardware/Kernel Timer Preemption (`[TIMER] Tick #1 ... -> hrtimer interrupt`)**: After 400 ms, the kernel `hrtimer` fires in interrupt context (`timeshare_timer_cb`), revoking `P1`'s slice (`run_granted = false`) so `P1` parks on its `wait_queue` (`-> STOPPED`), and the scheduler switches to `P2`, then `P3`, in Round-Robin order.
4. Select **`3` (Inspect `/proc/timeshare`)** and **`4` (View Kernel Ring Buffer `dmesg`)** to prove the logs and summary table are generated directly inside the Linux kernel (`timeshare_kmod:`).
5. Select **`q`** to unload the kernel module (`rmmod timeshare_kmod`) and power off the VM cleanly.

---

### Segment 3: Code Walkthrough (3–4 mins — 30% Rubric)

Walk through the 3 source files in this exact order:

---

## Part 2: File-by-File Code Explanation

### 1. [scheduler.h](file:///home/henry/Documents/project-cawu-6/operating-system/scheduler.h) — Shared Kernel & User Definitions

`scheduler.h` is included by both the kernel module (`timeshare_kmod.c`) and the user-space controller (`main.c`):
1. **Simulation Constants**:
   - `MAX_PROCS` (`10`), `MIN_QUANTUM_MS` (`10`), `MAX_QUANTUM_MS` (`10000`), and `BASE_WORK_STEPS` (`3`).
   - `TIMESHARE_PROC_NAME` (`"timeshare"`) and `TIMESHARE_PROC_PATH` (`"/proc/timeshare"`).
2. **`enum proc_state`** — The 4 states in our process state machine:
   - `PROC_READY`: Kernel thread created and waiting on its waitqueue for its first slice.
   - `PROC_RUNNING`: Currently dispatched by the Round-Robin scheduler.
   - `PROC_STOPPED`: Preempted by the kernel `hrtimer` interrupt and waiting on its waitqueue.
   - `PROC_TERMINATED`: Completed all workload steps and exited.

---

### 2. [timeshare_kmod.c](file:///home/henry/Documents/project-cawu-6/operating-system/timeshare_kmod.c) — The Ring 0 Linux Kernel Module

`timeshare_kmod.c` is the core of the project. Here is how each kernel subsystem is used:

#### A. Kernel Process Control Block (`struct kproc_pcb`) & Scheduler Context (`struct ksched_ctx`)
- Just as the Linux kernel tracks tasks with `struct task_struct`, our module tracks each worker in `struct kproc_pcb`:
  - `id`: Logical ID (`1..nr_procs` for `P1..Pn`).
  - `pid`: Real kernel thread ID obtained via `task_pid_nr(task)`.
  - `task`: Pointer to the kernel's `struct task_struct` returned by `kthread_create()`.
  - `wq`: A per-worker kernel waitqueue (`wait_queue_head_t`) used to put the thread to sleep (`TASK_INTERRUPTIBLE`) when stopped and wake it up when dispatched.
  - `run_granted`, `ack_stopped`, `finished`: Lock-free synchronization flags accessed with kernel `READ_ONCE()` / `WRITE_ONCE()` macros.

#### B. High-Resolution Kernel Timer (`struct hrtimer` & `timeshare_timer_cb`)
```c
static enum hrtimer_restart timeshare_timer_cb(struct hrtimer *timer)
{
	int idx = READ_ONCE(ts_ctx.curr_idx);

	WRITE_ONCE(ts_ctx.timer_expired, true);
	if (idx >= 0 && idx < ts_ctx.nr_procs) {
		WRITE_ONCE(ts_ctx.procs[idx].run_granted, false);
		wake_up_interruptible(&ts_ctx.procs[idx].wq);
	}
	wake_up_interruptible(&ts_ctx.sched_wq);
	return HRTIMER_NORESTART;
}
```
- Instead of user-space `setitimer()` and `SIGALRM`, we use the kernel's high-resolution timer (`struct hrtimer`) initialized with `CLOCK_MONOTONIC` and `HRTIMER_MODE_REL`.
- When `quantum_ms` expires, the kernel invokes `timeshare_timer_cb()` directly in **hardirq (hardware timer interrupt) context**.
- The callback sets `timer_expired = true`, clears `run_granted = false` on the running worker to preempt it immediately, and wakes the coordinator scheduler waitqueue (`ts_ctx.sched_wq`).

#### C. Kernel Worker Threads (`timeshare_worker_fn` & `simulate_cpu_work`)
- Created in `spawn_workers()` via `kthread_create(timeshare_worker_fn, proc, "ts_worker_%d", proc->id)`.
- At startup, each worker signals `ready_ack = true` and calls `wait_for_slice(proc)`, which blocks on `wait_event_interruptible(proc->wq, READ_ONCE(proc->run_granted) || kthread_should_stop())`.
- This is the kernel-level equivalent of `raise(SIGSTOP)`: all workers wait at the starting line until the scheduler dispatches them.
- During its slice, `simulate_cpu_work()` runs in small 1–5 ms ticks (`usleep_range()`). Before every tick, it checks `wait_for_slice(proc)`:
  - When `timeshare_timer_cb` sets `run_granted = false`, the worker immediately sets `ack_stopped = true`, wakes the scheduler, and sleeps on `proc->wq`—preserving its exact loop variables and `remaining_ms` on its kernel stack!

#### D. Round-Robin Coordinator Thread (`timeshare_sched_fn` & `find_next_runnable`)
- `find_next_runnable()` scans `(curr_idx + i) % nr_procs` circularly for the next worker in `PROC_READY` or `PROC_STOPPED`.
- For each time slice:
  1. Marks `proc->state = PROC_RUNNING`, increments `proc->slices_used` and `ts_ctx.context_switches`.
  2. Sets `proc->run_granted = true` and starts the timer via `hrtimer_start(&ts_ctx.quantum_timer, ms_to_ktime(ts_ctx.quantum_ms), HRTIMER_MODE_REL)`.
  3. Wakes the worker via `wake_up_interruptible(&proc->wq)`.
  4. Sleeps on `wait_event_interruptible(ts_ctx.sched_wq, ...)` until either the worker finishes early (`proc->finished`) or the `hrtimer` expires (`ts_ctx.timer_expired`).
  5. Cancels the timer (`hrtimer_cancel`) and updates the worker's state to `PROC_TERMINATED` or `PROC_STOPPED`.

#### E. `/proc/timeshare` Interface (`timeshare_proc_ops`)
- Registered in `timeshare_init()` using `proc_create("timeshare", 0666, NULL, &timeshare_proc_ops)`.
- **Write handler (`timeshare_proc_write`)**: Copies `"<nr_procs> <quantum_ms>"` from user space via `copy_from_user()`, validates bounds (`1..10` processes, `10..10000` ms quantum), and calls `start_simulation()`.
- **Read handler (`timeshare_proc_show` via `seq_file`)**: Outputs the chronological trace buffer (`ts_ctx.log_buf`) and current kernel status (`[KERNEL_STATUS: RUNNING]` or `[KERNEL_STATUS: COMPLETED]`).

---

### 3. [main.c](file:///home/henry/Documents/project-cawu-6/operating-system/main.c) — User-Space Controller (`timeshare_ctl`)

- Validates user input from either interactive prompts or command-line arguments (`./timeshare_ctl 3 400`).
- `trigger_kernel_sim()` opens `/proc/timeshare` and writes `"<nr_procs> <quantum_ms>\n"`, instructing the kernel module to start the simulation in Ring 0.
- `stream_kernel_output()` polls `/proc/timeshare` every 20 ms and prints new trace output incrementally until `[KERNEL_STATUS: COMPLETED]` is observed.

---

## Part 3: Likely Questions & Answers for Your Assessment

1. **Q: How does this kernel-level implementation differ from a user-space simulation?**
   - **A:** In user space, a parent process simulates scheduling using POSIX signals (`SIGSTOP`, `SIGCONT`, `SIGALRM`) and `setitimer()`. In our kernel-level implementation (`timeshare_kmod.ko`), the scheduler runs inside **Ring 0 (kernel space)**: workers are real Linux kernel threads (`struct task_struct` created via `kthread_create`), the timer is a kernel high-resolution timer (`struct hrtimer`) executing in interrupt context, context switching uses kernel waitqueues (`wait_queue_head_t` and `wake_up_interruptible`), and user communication happens through a `/proc/timeshare` virtual file (`proc_ops` + `seq_file`).

2. **Q: Why do we run and test a Loadable Kernel Module inside a Virtual Machine (QEMU / VirtualBox)?**
   - **A:** Kernel modules execute with full hardware privilege in Ring 0; a bug or crash in kernel space can cause a kernel panic on the host machine. Running inside an isolated Virtual Machine (such as our `make vm-qemu` environment or VirtualBox VM) protects the host OS and ensures any classmate or lecturer on Windows, macOS, or Linux can reproduce the exact same kernel environment.

3. **Q: Why do we use `READ_ONCE()`, `WRITE_ONCE()`, `spin_lock_irqsave()`, and `mutex` in `timeshare_kmod.c`?**
   - **A:** Because the Linux kernel is preemptible and SMP (multi-core), the `hrtimer` callback (`timeshare_timer_cb`) runs asynchronously in hardware interrupt context while the scheduler and worker `kthread`s run concurrently across CPU cores. `READ_ONCE()` and `WRITE_ONCE()` prevent compiler reordering/caching of shared flags, `spin_lock_irqsave()` safely protects the trace buffer against interrupt-context races, and `mutex_lock()` serializes `/proc/timeshare` start/stop requests.
