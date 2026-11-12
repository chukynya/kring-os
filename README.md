# UNIX Time-Sharing System Simulator (Signals & Timers in C)

A clean, modular user-space simulation of an operating system **Round-Robin (RR) Time-Sharing Scheduler** written in C for Linux/UNIX. It demonstrates how an OS kernel multiplexes CPU time across multiple processes using **interval timers** (`setitimer` + `SIGALRM`), **process control signals** (`SIGSTOP` and `SIGCONT`), and **process management system calls** (`fork()`, `waitpid()`, `kill()`, `pause()`).

All C source and header files strictly follow the [Linux Kernel Coding Style](https://docs.kernel.org/process/coding-style.html).

---

## Project Structure

| File | Responsibility |
| :--- | :--- |
| `scheduler.h` | Defines `enum proc_state`, `struct proc_control_block` (PCB), `struct scheduler_ctx`, and function prototypes. |
| `worker.c` | Implements the child worker process (`worker_run`) which pauses itself at startup (`raise(SIGSTOP)`) and executes a finite multi-step CPU workload when resumed. |
| `scheduler.c` | Implements the parent Round-Robin scheduler (`scheduler_init`, `scheduler_run`, `scheduler_print_summary`) using `setitimer(ITIMER_REAL)`, `SIGALRM`, `SIGCHLD`, `SIGSTOP`, and `SIGCONT`. |
| `main.c` | Provides the interactive startup prompt (and optional CLI argument parsing), input validation, and top-level execution flow. |
| `Makefile` | Automates building (`make`), running (`make run`), demoing (`make demo`), testing (`make test`), and cleaning (`make clean`). |
| `EXPLANATION.md` | Detailed code walkthrough and step-by-step **Video Presentation Guide** aligned with the grading rubric. |

---

## How Time-Sharing Works in This Simulator

```text
Parent Process (Scheduler)                      Child Processes (Workers P1..Pn)
--------------------------                      --------------------------------
1. fork() children P1..Pn  -------------------> Child calls raise(SIGSTOP)
   waitpid(WUNTRACED)                           State: READY (paused at start)

2. Pick next runnable Pi in Round-Robin
   setitimer(ITIMER_REAL, quantum_ms)
   kill(Pi, SIGCONT)       -------------------> Pi wakes up (State: RUNNING)
   pause()                                      Executes workload steps & logs progress

3. Timer expires -> Kernel sends SIGALRM
   sigalrm_handler sets timer_expired = 1
   kill(Pi, SIGSTOP)       -------------------> Pi freezes immediately (State: STOPPED)
   waitpid(Pi, WUNTRACED)

4. Advance to next process P(i+1) and repeat until all children exit (State: TERMINATED)
```

### Process State Machine

```text
          fork() + raise(SIGSTOP)
[NEW] ------------------------------> [PROC_READY]
                                           |
                                           | Scheduler sends SIGCONT
                                           v
          Scheduler sends SIGSTOP    [PROC_RUNNING] -----> Completes all steps
      +-----------------------------       |               & calls exit(0)
      |                                    |                      |
      v                                    |                      v
[PROC_STOPPED] ----------------------------+              [PROC_TERMINATED]
      (Scheduler sends SIGCONT on next turn)
```

---

## Prerequisites & Build Instructions

### 1. Check Linux & Compiler Environment
```bash
uname -a
gcc --version
make --version
```

### 2. Build the Executable
```bash
make
```
This compiles `main.c`, `scheduler.c`, and `worker.c` using strict flags (`-Wall -Wextra -Werror -std=c11 -pedantic`) and produces the `./timeshare_sim` executable.

### 3. Run Interactively (Recommended for Video Demo)
```bash
make run
# or directly:
./timeshare_sim
```
You will be prompted to enter:
1. **Number of worker processes** (`1` to `10`, e.g., `3`)
2. **Time quantum in milliseconds** (`10` to `10000`, e.g., `500`)

You can also pass the parameters directly via command-line arguments:
```bash
./timeshare_sim 3 500
```

### 4. Run Quick Non-Interactive Demo or Automated Tests
```bash
make demo
make test
```

---

## Sample Output

```text
=========================================================
   UNIX Time-Sharing System Simulator (Signals & Timers) 
=========================================================
Enter number of worker processes [1-10]: 3
Enter time quantum in milliseconds [10-10000]: 400

=== Initializing Process Table (3 processes, 400 ms quantum) ===
[INIT] Process P1 created (PID 12041) | State: READY  | Workload: 3 steps
[INIT] Process P2 created (PID 12042) | State: READY  | Workload: 4 steps
[INIT] Process P3 created (PID 12043) | State: READY  | Workload: 5 steps

=== Starting Round-Robin Time-Sharing Scheduler ===

[SCHEDULER] Dispatching Process P1 (PID 12041) | Slice #1 -> sending SIGCONT
  [WORKER P1 | PID 12041] Step 1/3 [####........]  33%
[TIMER] Tick #1 (400 ms quantum expired) -> SIGALRM received
[SCHEDULER] Preempted Process P1 (PID 12041) with SIGSTOP -> State: STOPPED

[SCHEDULER] Dispatching Process P2 (PID 12042) | Slice #1 -> sending SIGCONT
  [WORKER P2 | PID 12042] Step 1/4 [###.........]  25%
[TIMER] Tick #2 (400 ms quantum expired) -> SIGALRM received
[SCHEDULER] Preempted Process P2 (PID 12042) with SIGSTOP -> State: STOPPED
...
+-------------------------------------------------------+
|             TIME-SHARING SIMULATION SUMMARY           |
+---------+----------+--------------+--------+----------+
| Process |   PID    | Final State  | Steps  |  Slices  |
+---------+----------+--------------+--------+----------+
|   P1    | 12041    | TERMINATED   | 3      | 2        |
|   P2    | 12042    | TERMINATED   | 4      | 3        |
|   P3    | 12043    | TERMINATED   | 5      | 4        |
+---------+----------+--------------+--------+----------+
| Time Quantum     : 400    ms                          |
| Timer Interrupts : 6      (SIGALRM ticks)             |
| Context Switches : 9      (SIGCONT dispatches)        |
+-------------------------------------------------------+
```

---

## Linux Kernel Coding Style Compliance

This project follows [Linux Kernel Coding Style](https://docs.kernel.org/process/coding-style.html):
- **Indentation**: 8-character hard tabs (`\t`).
- **Line Width**: Strictly $\le 80$ columns across all source and header files.
- **Braces**: K&R style (`{` on the next line for functions; `{` on the same line for `if`, `for`, `while`, `switch`, `struct`, and `enum`).
- **Naming**: Lowercase `snake_case` for variables, functions, and struct types; `UPPER_SNAKE_CASE` for macros and enum constants; no `typedef` hiding `struct` or `enum` types.
- **Comments**: C89 `/* ... */` multi-line comments with kernel-doc function headers.
