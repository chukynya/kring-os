# Linux Kernel Time-Sharing Scheduler (`timeshare_kmod` — `hrtimer` & `kthread`)

A clean, modular **Linux Kernel-Level Round-Robin (RR) Time-Sharing Scheduler** implemented as a Loadable Kernel Module (`timeshare_kmod.ko`) in C, paired with an interactive user-space controller (`timeshare_ctl`) and a portable **Virtual Machine (VM) environment** (QEMU/KVM, VirtualBox, Vagrant, and `.ova` appliance).

Instead of simulating scheduling in user space, this project executes inside **Linux Kernel Space (Ring 0)** using:
- **High-Resolution Kernel Timers (`struct hrtimer`)** firing in hardware/software interrupt context (`CLOCK_MONOTONIC`) when the time quantum expires.
- **Kernel Worker Threads (`struct task_struct` via `kthread_create`)** managed by a coordinator kernel scheduler thread (`timeshare_sched_fn`).
- **Kernel Waitqueues (`wait_queue_head_t`)** to context-switch worker `kthread`s between running (`PROC_RUNNING`) and sleeping/preempted (`PROC_STOPPED` / `TASK_INTERRUPTIBLE`).
- **`/proc/timeshare` Pseudo-Filesystem Interface (`proc_ops` + `seq_file`)** and kernel ring buffer logging (`pr_info` / `dmesg`).

All C source and header files strictly follow the [Linux Kernel Coding Style](https://docs.kernel.org/process/coding-style.html) and pass `scripts/checkpatch.pl` with **0 errors and 0 warnings**.

---

## Project Structure

| File | Responsibility |
| :--- | :--- |
| [scheduler.h](file:///home/henry/Documents/project-cawu-6/operating-system/scheduler.h) | Defines shared constants (`MAX_PROCS`, `MIN_QUANTUM_MS`, `MAX_QUANTUM_MS`), `/proc/timeshare` paths, `enum proc_state`, and `proc_state_name()`. |
| [timeshare_kmod.c](file:///home/henry/Documents/project-cawu-6/operating-system/timeshare_kmod.c) | **Ring 0 Linux Kernel Module (`timeshare_kmod.ko`)**: implements `struct kproc_pcb`, `struct ksched_ctx`, `hrtimer` interrupt callback, worker `kthread`s, Round-Robin scheduler `kthread`, and `/proc/timeshare`. |
| [main.c](file:///home/henry/Documents/project-cawu-6/operating-system/main.c) | **User-Space Controller (`timeshare_ctl`)**: interactive prompt & CLI validator that writes `<nr_procs> <quantum_ms>` to `/proc/timeshare` and streams the live kernel execution trace. |
| [Makefile](file:///home/henry/Documents/project-cawu-6/operating-system/Makefile) | Integrates Linux Kbuild (`obj-m += timeshare_kmod.o`) and automates building, loading/unloading, running inside QEMU VM (`make vm-qemu`), demoing (`make demo`), testing (`make test`), and style checking (`make checkpatch`). |
| [Vagrantfile](file:///home/henry/Documents/project-cawu-6/operating-system/Vagrantfile) | Automated VirtualBox + Ubuntu 24.04 LTS VM definition so friends on Windows, macOS, or Linux can run `vagrant up && vagrant ssh`. |
| [vm/qemu-run.sh](file:///home/henry/Documents/project-cawu-6/operating-system/vm/qemu-run.sh) | Portable QEMU/KVM runner that packages `timeshare_kmod.ko` into an `initramfs` and boots a real Linux kernel in the terminal in ~2 seconds. |
| [vm/setup-vm.sh](file:///home/henry/Documents/project-cawu-6/operating-system/vm/setup-vm.sh) | Universal VM provisioning script for Ubuntu/Debian (`apt`) and Fedora/RHEL (`dnf`) virtual machines. |
| [vm/export-ova.sh](file:///home/henry/Documents/project-cawu-6/operating-system/vm/export-ova.sh) | Exports a VirtualBox VM into a single `timeshare-kernel-vm.ova` file for easy sharing with friends. |
| [VM_GUIDE.md](file:///home/henry/Documents/project-cawu-6/operating-system/VM_GUIDE.md) | Complete step-by-step guide on setting up, using, and sharing the Virtual Machine environment. |
| [EXPLANATION.md](file:///home/henry/Documents/project-cawu-6/operating-system/EXPLANATION.md) | Detailed code walkthrough and **Video Presentation Guide** aligned with the grading rubric. |

---

## How Kernel-Level Time-Sharing Works

```text
User Space (Ring 3)                             Linux Kernel Space (Ring 0 — timeshare_kmod.ko)
-------------------                             -----------------------------------------------
1. ./timeshare_ctl (or echo "3 400") ---------> /proc/timeshare (timeshare_proc_write)
                                                Spawns coordinator kthread (ts_sched_rr)
                                                & worker kthreads P1..Pn (ts_worker_1..n)
                                                Each worker pauses on wait_queue (State: READY)

2. Round-Robin Dispatch                         Coordinator picks next runnable Pi
                                                hrtimer_start(quantum_ms, HRTIMER_MODE_REL)
                                                Sets Pi->run_granted = true
                                                wake_up_interruptible(&Pi->wq) (State: RUNNING)
                                                Pi executes workload ticks & logs via pr_info()

3. Hardware/Kernel Timer Interrupt              quantum_ms expires -> timeshare_timer_cb() fires
                                                Sets timer_expired = true, Pi->run_granted = false
                                                Pi pauses on wait_queue (State: STOPPED)

4. Repeat Round-Robin P1 -> P2 -> ... -> Pn     Until all worker kthreads complete all steps
   Streams trace & summary via /proc/timeshare  (State: TERMINATED) & logs summary to dmesg
```

### Kernel Process State Machine

```text
         kthread_create() + wait_event_interruptible()
[NEW] -------------------------------------------------> [PROC_READY]
                                                              |
                                                              | Scheduler sets run_granted = true
                                                              | & calls wake_up_interruptible()
                                                              v
         hrtimer expires (timeshare_timer_cb)          [PROC_RUNNING] -----> Completes all steps
      +-----------------------------------------------        |              & sets finished = true
      |                                                       |                      |
      v                                                       |                      v
[PROC_STOPPED] -----------------------------------------------+              [PROC_TERMINATED]
      (Woken on next Round-Robin turn via wake_up_interruptible)
```

---

## Quick Start: Running in a Virtual Machine

See **[VM_GUIDE.md](file:///home/henry/Documents/project-cawu-6/operating-system/VM_GUIDE.md)** for full instructions on all VM platforms. Here are the quickest ways to run:

### Option A: Instant QEMU Kernel VM in Terminal (No `sudo` Needed!)
```bash
# Build the kernel module and boot the interactive QEMU Linux VM:
make vm-qemu

# Or run the non-interactive 3-process, 400 ms demo inside the QEMU VM:
make demo

# Or run the full automated test suite inside the QEMU VM:
make test
```

### Option B: VirtualBox + Vagrant (Windows / macOS / Linux)
```bash
vagrant up
vagrant ssh
cd ~/operating-system
make run
```

### Option C: Inside Any Linux VM (Ubuntu / Fedora) with Kernel Headers
```bash
# 1. Automated one-step setup (installs kernel headers, builds, and loads module):
bash vm/setup-vm.sh

# 2. Or build and load manually:
make
sudo make load

# 3. Run interactively via timeshare_ctl:
make run

# 4. Or interact directly with the kernel module via /proc/timeshare & dmesg:
echo "3 400" > /proc/timeshare
cat /proc/timeshare
sudo dmesg | grep timeshare_kmod

# 5. Unload the kernel module when finished:
sudo make unload
```

---

## Sample Output (`/proc/timeshare` & `dmesg`)

```text
=========================================================
   Linux Kernel Time-Sharing Scheduler (hrtimer & kthread)
=========================================================
Enter number of worker processes [1-10]: 3
Enter time quantum in milliseconds [10-10000]: 400

=== Process Table Init (3 processes, 400 ms quantum) ===
[INIT] P1 (PID 158) | State: READY  | Steps: 3
[INIT] P2 (PID 159) | State: READY  | Steps: 4
[INIT] P3 (PID 160) | State: READY  | Steps: 5

=== Starting Round-Robin Time-Sharing Scheduler ===

[SCHEDULER] Dispatch P1 (PID 158) | Slice #1 -> wake_up_process
  [WORKER P1 | PID 158] Step 1/3 [####........]  33%
[TIMER] Tick #1 (400 ms quantum expired) -> hrtimer interrupt
[SCHEDULER] Preempted P1 (PID 158) wait_queue -> STOPPED

[SCHEDULER] Dispatch P2 (PID 159) | Slice #1 -> wake_up_process
  [WORKER P2 | PID 159] Step 1/4 [###.........]  25%
[TIMER] Tick #2 (400 ms quantum expired) -> hrtimer interrupt
[SCHEDULER] Preempted P2 (PID 159) wait_queue -> STOPPED
...
+-------------------------------------------------------+
|             TIME-SHARING SIMULATION SUMMARY           |
+---------+----------+--------------+--------+----------+
| Process |   PID    | Final State  | Steps  |  Slices  |
+---------+----------+--------------+--------+----------+
|   P1    | 158      | TERMINATED   | 3      | 2        |
|   P2    | 159      | TERMINATED   | 4      | 3        |
|   P3    | 160      | TERMINATED   | 5      | 4        |
+---------+----------+--------------+--------+----------+
| Time Quantum     : 400    ms                          |
| Timer Interrupts : 6      (hrtimer ticks)             |
| Context Switches : 9      (kthread dispatches)        |
+-------------------------------------------------------+
```

---

## Linux Kernel Coding Style Compliance

Verify compliance at any time with:
```bash
make checkpatch
```
- **Indentation**: 8-character hard tabs (`\t`).
- **Line Width**: Strictly $\le 80$ columns across all source and header files.
- **Braces & Naming**: K&R style, `snake_case` functions/types, `UPPER_SNAKE_CASE` macros/enums, kernel-doc comments.
