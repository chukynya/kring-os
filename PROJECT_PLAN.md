# Time-Sharing Operating System Simulation

## 1. Project Objective
This project simulates a **time-sharing operating system scheduler** in user space. It demonstrates how an OS kernel preempts running tasks and shares CPU time among multiple processes using **POSIX signals** and **interval timers**.

---

## 2. Core Architecture & Concepts

```
                  +-----------------------------------+
                  |        Scheduler Process          |
                  |  - Process Control Block (PCB)    |
                  |  - Ready Queue (Round-Robin)      |
                  +-----------------+-----------------+
                                    |
          Periodic SIGALRM          | Process Control Signals
       via setitimer()              | (SIGSTOP / SIGCONT)
                                    v
                 +--------------------------------------+
                 | kill(pid, SIGSTOP) -> Preempt / Hold |
                 | kill(pid, SIGCONT) -> Dispatch / Run |
                 +--------------------------------------+
                                    |
            +-----------------------+-----------------------+
            |                                               |
            v                                               v
    +---------------+                               +---------------+
    | Child Proc 1  | (Workload)                    | Child Proc 2  | (Workload)
    +---------------+                               +---------------+
```

### Key Mechanisms
1. **Hardware Timer Simulation**:
   - The scheduler configures `setitimer(ITIMER_REAL, ...)` to generate a periodic `SIGALRM` representing a fixed CPU time quantum (e.g., 500ms or 1s).
2. **Context Switching via Signals**:
   - **Preemption**: When `SIGALRM` fires, the active process is paused using `kill(pid, SIGSTOP)`.
   - **Dispatching**: The next process from the ready queue is resumed using `kill(pid, SIGCONT)`.
3. **Process Lifecycle Tracking**:
   - Child termination is captured asynchronously via `SIGCHLD` and `waitpid(..., WNOHANG)`.

---

## 3. Portability & Standards

To ensure the project compiles and runs identically across native Linux and team members' virtual machines (e.g., VirtualBox with Ubuntu/Debian):
- **Language & Toolchain**: C17 standard (`gcc -std=c17 -Wall -Wextra -pedantic`), requires GCC 8.1 or later.
- **API Standards**: POSIX.1-2008 standard APIs only (no non-standard GNU extensions).
- **Build System**: Plain GNU `Makefile` without third-party dependencies.

---

## 4. Proposed Directory Structure

```text
operating-system/
├── Makefile            # Build, run, and clean targets
├── PROJECT_PLAN.md     # Architecture documentation and roadmap
├── src/
│   ├── main.c          # Entry point and CLI parsing
│   ├── scheduler.c     # Timer setup, signal handlers, dispatch loop
│   ├── scheduler.h     # Process Control Block (PCB) and queue definitions
│   └── process.c       # Simulated child process workload
└── README.md           # Instructions to compile and test
```

---

## 5. Iterative Milestones

Development will proceed step-by-step so every component can be inspected and understood:

| Milestone | Scope | Deliverables |
| :--- | :--- | :--- |
| **Milestone 1** | **Timer & Signal Foundation** | Setup `Makefile`, configure `sigaction` for `SIGALRM` with `setitimer`. |
| **Milestone 2** | **Process Lifecycle & Signals** | Spawn child processes with `fork()`; verify pausing (`SIGSTOP`) and resuming (`SIGCONT`). |
| **Milestone 3** | **PCB & Ready Queue** | Implement Process Control Block struct and a circular/FIFO Ready Queue. |
| **Milestone 4** | **Round-Robin Scheduler Loop** | Connect `SIGALRM` interrupts to queue switching; handle completion via `SIGCHLD`. |
| **Milestone 5** | **Metrics & Cleanup** | Calculate Turnaround Time, Waiting Time, and ensure clean shutdown. |

---

## 6. Open Decisions for the Team

1. **Input Format**: Should processes be defined via command-line arguments, a simple text file (e.g. `arrival_time burst_time`), or a built-in demo suite?
2. **Algorithm Scope**: Start with Round-Robin (RR) first, or will Priority / Multi-Level Feedback Queue (MLFQ) be required later?
