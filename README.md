# UNIX Time-Sharing System Simulator & Linux Kernel `/proc` Module (C)

A clean, modular implementation of an operating system **Round-Robin (RR) Time-Sharing Scheduler** in C for Linux/UNIX, paired with a lightweight **Loadable Kernel Module (`kmod/timeshare_kmod.c`)** that integrates scheduler state transitions into the Linux kernel via `/proc/timeshare` system calls (`open()`, `write()`, `read()`, `close()`).

It is designed around:
- **LO 3**: Relating fundamental OS design (Linus Torvalds' 1991 Linux `0.01` hardware timer interrupt context switching between `AAAA` and `BBBB` in a minimal TTY environment, and the classic Time-Sharing `10–100 ms` time quantum) to modern Linux kernel development.
- **LO 4**: Writing user-space and kernel-space C programs that interact with the operating system via POSIX signals (`SIGSTOP`, `SIGCONT`, `SIGALRM`, `SIGCHLD`), interval timers (`setitimer`), process control system calls (`fork()`, `waitpid()`, `kill()`, `pause()`), and `/proc` Virtual File System (VFS) handlers (`proc_create()`, `copy_from_user()`, `seq_printf()`).

All C source and header files follow the [Linux Kernel Coding Style](https://docs.kernel.org/process/coding-style.html).

---

## Project Structure

| File | Responsibility |
| :--- | :--- |
| `scheduler.h` | Defines `enum proc_state`, `struct proc_control_block` (PCB), `struct scheduler_ctx`, and function prototypes. |
| `worker.c` | Implements the child worker process (`worker_run`) which pauses itself at startup (`raise(SIGSTOP)`) and executes a finite multi-step CPU workload when resumed. |
| `scheduler.c` | Implements the parent Round-Robin scheduler (`scheduler_init`, `scheduler_run`, `scheduler_print_summary`) using `setitimer(ITIMER_REAL)`, `SIGALRM`, `SIGCHLD`, `SIGSTOP`, `SIGCONT`, and writes state transitions (`INIT`, `DISPATCH`, `PREEMPT`, `TERMINATE`) to `/proc/timeshare` via `log_kernel_event()`. |
| `main.c` | Provides the interactive startup prompt (and optional CLI argument parsing), strict input validation, and top-level execution flow. |
| `kmod/timeshare_kmod.c` | Loadable Linux Kernel Module (91 lines, 3 core handlers) that registers `/proc/timeshare`, receives user-space scheduler transitions via `write()` (`copy_from_user` + `pr_info` to `dmesg`), and serves kernel telemetry summaries via `read()` (`seq_printf`). |
| `kmod/Makefile` | Kbuild makefile that compiles `kmod/timeshare_kmod.ko` against `/lib/modules/$(uname -r)/build`. |
| `Makefile` | Automates building user space (`make`), building the kernel module (`make kmod`), running (`make run`), demoing (`make demo`), testing (`make test`), and cleaning (`make clean`, `make kmod-clean`). |
| `EXPLANATION.md` | Comprehensive theory guide (Time-Sharing vs. Round-Robin, Linus Torvalds' 1991 minimal TTY, line-by-line walkthrough of user space + `kmod/timeshare_kmod.c`, and a 1-week study & video presentation plan). |

---

## How Time-Sharing & Kernel `/proc` Integration Work

> *"This short period of time during which the user gets the attention of the CPU is known as time slice, time slot, or quantum. It is typically of the order of 10 to 100 milliseconds."* — GeeksforGeeks (Time-Sharing Operating System)

In a **Time-Sharing Operating System**, multiple interactive processes share a single CPU by dividing execution time into short **time slices (quantums)** of **10 to 100 ms**. **Round-Robin (RR)** is the scheduling algorithm that implements Time-Sharing by cycling through runnable processes in a circular queue using two complementary OS mechanisms:
1. **Timer (`setitimer(ITIMER_REAL)`)**: Measures the fixed time quantum (`10–100 ms`) and raises an asynchronous `SIGALRM` interrupt when the active slice expires.
2. **Signals (`SIGALRM`, `SIGSTOP`, `SIGCONT`, `SIGCHLD`)**: Act as the preemption and context-switching mechanism—pausing the running child (`SIGSTOP`), resuming the next runnable child (`SIGCONT`), and waking the scheduler early if a child finishes (`SIGCHLD`).

```text
User Space (Ring 3: ./timeshare_sim)                Kernel Space (Ring 0: timeshare_kmod.ko)
------------------------------------                ----------------------------------------
1. fork() children P1..Pn
   Child calls raise(SIGSTOP) -> State: READY
   write("/proc/timeshare", "INIT P1 ...")  ------> timeshare_proc_write()
                                                    copy_from_user() -> event_count++
                                                    pr_info() -> logs to dmesg

2. Pick next runnable Pi in Round-Robin
   setitimer(ITIMER_REAL, quantum_ms)
   kill(Pi, SIGCONT) -> State: RUNNING
   write("/proc/timeshare", "DISPATCH ...") ------> timeshare_proc_write() -> dmesg

3. Timer expires -> Kernel delivers SIGALRM
   kill(Pi, SIGSTOP) -> State: STOPPED
   write("/proc/timeshare", "PREEMPT ...")  ------> timeshare_proc_write() -> dmesg

4. Child finishes all steps -> exits (SIGCHLD)
   waitpid() reaps Pi -> State: TERMINATED
   write("/proc/timeshare", "TERMINATE ...") -----> timeshare_proc_write() -> dmesg

5. Inspect kernel state from shell:
   cat /proc/timeshare (read() syscall)     ------> timeshare_proc_show()
                                                    seq_printf() returns total events & last event
```

> **Note**: If `timeshare_kmod.ko` is not loaded, `/proc/timeshare` does not exist and `log_kernel_event()` silently no-ops. Thus `./timeshare_sim` and `make test` always succeed without root privileges.

---

## Debian Minimal (`netinst`, No GUI) VM Setup & Testing Guide (Rubric Row 1 — 30%)

Inspired by Linus Torvalds' 1991 Linux `0.01` environment (`Kernel + tty1 + bash + gcc + make`), the cleanest instructional OS setup is **Debian 12 (`Bookworm`) or Debian 13 (`Trixie`) Minimal (`netinst`)** with **no desktop GUI**:

### Step 1: Download the `amd64` ISO & Create the Virtual Machine
1. Download the official **Debian `netinst` CD image** (~650 MB) from [debian.org/distrib/netinst](https://www.debian.org/distrib/netinst):
   - Architecture to pick: **`amd64`** (for 64-bit Intel/AMD `x86_64` systems).
2. In **VirtualBox**, **Virt-Manager (QEMU/KVM)**, or **VMware**, create a new Linux VM:
   - **Memory (RAM)**: `1024 MB` – `2048 MB`
   - **Processors**: `1` or `2` vCPUs
   - **Hard Disk**: `10 GB` – `20 GB` dynamically allocated

### Step 2: Step-by-Step Debian Installer Choices (TTY-Only, No GUI)
1. **Installer Boot Menu**: Select **`Install`** (text-mode installer) and press **Enter**.
2. **Language, Location & Keyboard**:
   - **Language**: **`English`**
   - **Location**: **`United States`** (or `other` $\rightarrow$ `Asia` $\rightarrow$ `Indonesia`)
   - **Keyboard**: **`American English`**
3. **Network Configuration**:
   - **Hostname**: `debian`
   - **Domain name**: Leave **blank (empty)** and press **Enter**.
4. **Users and Passwords**:
   - **Root password**: Enter a password for `root` and confirm it.
   - **Full name & Username for new user**: Enter `henry` and set a user password.
5. **Partition Disks**:
   - Choose **`Guided - use entire disk`** $\rightarrow$ select **`/dev/vda`** (or `/dev/sda`) $\rightarrow$ **`All files in one partition (recommended for new users)`** $\rightarrow$ **`Finish partitioning and write changes to disk`** $\rightarrow$ **`<Yes>`**.
6. **Configure the Package Manager**:
   - **Scan extra installation media?**: Select **`<No>`**.
   - **Debian archive mirror**: Choose your country $\rightarrow$ **`deb.debian.org`** $\rightarrow$ leave **HTTP proxy** blank.
   - **Package usage survey**: Select **`<No>`**.
7. **Software Selection (`tasksel`)**:
   Use the **Arrow keys** and **Spacebar** so only the minimal CLI tools are selected:
   - `[ ] Debian desktop environment` (**UNCHECK** with Spacebar)
   - `[ ] ... GNOME` (**UNCHECK** with Spacebar)
   - `[*] SSH server` (**CHECK** with Spacebar — allows easy SSH & copy-paste from host)
   - `[*] standard system utilities` (**KEEP CHECKED**)
   Press **Tab** to highlight **`<Continue>`** and press **Enter**.
8. **Install the GRUB Boot Loader**:
   - **Install the GRUB boot loader to your primary drive?**: Select **`<Yes>`**.
   - **Device for boot loader installation**: Select **`/dev/vda`** (or `/dev/sda`, *not* `Enter device manually`) and press **Enter**.
9. **Finish & Boot**:
   - Select **`<Continue>`** to reboot. *(If the VM boots back into the installer menu, eject/remove the `.iso` CD from your VM settings so it boots into the `*Debian GNU/Linux` GRUB menu.)*
   - At GRUB, select **`*Debian GNU/Linux`** to boot into the **`debian login:` (`tty1`)** prompt.

### Step 3: Install GCC, Make, Kernel Headers & `tmux`
At `debian login:`, log in as `root` (or log in as `henry` and run `su -`) and install the build tools and kernel headers:
```bash
su -
apt update
apt install -y build-essential linux-headers-$(uname -r) git sudo tmux
usermod -aG sudo henry
```

### Step 4: Clone & Build Both User Space and the Kernel Module
You can run these commands directly in `tty1`, or connect from your host terminal via SSH (`ip -4 a` inside the VM to see the IP, then `ssh henry@<VM_IP>` on your host):
```bash
git clone https://github.com/chukynya/kring-os.git
cd kring-os

# 1. Build the user-space Round-Robin simulator (timeshare_sim)
make clean && make

# 2. Build the Loadable Kernel Module (kmod/timeshare_kmod.ko)
make kmod
```

### Step 5: Load the Kernel Module & Run the Full Demonstration
```bash
# 1. Insert the kernel module into the running Linux kernel
sudo insmod kmod/timeshare_kmod.ko

# 2. Verify the module is loaded and /proc/timeshare exists
lsmod | grep timeshare
cat /proc/timeshare

# 3. Run the Round-Robin scheduler with a 50 ms time quantum (within 10-100 ms)
./timeshare_sim 3 50

# 4. Read the kernel telemetry summary via the read() syscall on /proc/timeshare
cat /proc/timeshare

# 5. View the kernel ring buffer logs written by timeshare_proc_write()
sudo dmesg | tail -n 20

# 6. Unload the kernel module cleanly when finished
sudo rmmod timeshare_kmod
```

### Step 6: Useful `tty1` Tips (Scrolling Up & Shutting Down the VM)
- **How to Scroll Up/Down in `tty1`**:
  1. Pipe output into `less` (use **Up/Down Arrow** or **Page Up/Page Down** to scroll, press **`q`** to exit):
     ```bash
     ./timeshare_sim 3 50 | less
     sudo dmesg | less
     ```
  2. Or start a `tmux` session (`tmux`), press **`Ctrl + B`** then **`Page Up`** (or **`[`**) to scroll freely with the arrow keys, and press **`q`** to return to typing.
- **How to Shut Down the VM Cleanly**:
  - Make sure you are logged into a shell prompt (e.g., `root@debian:~#` — do *not* type `shutdown` at the `debian login:` username prompt).
  - Run:
    ```bash
    poweroff
    # or:
    sudo shutdown now
    ```

---

## Standard User-Space Build & Test Commands (No Root Required)

```bash
# Compile timeshare_sim with -Wall -Wextra -Werror -std=c11 -pedantic
make

# Run interactively (prompts for number of processes and time quantum in ms)
make run

# Run non-interactive demo (3 processes, 400 ms quantum)
make demo

# Run the automated test suite (5 test suites including boundary & invalid inputs)
make test
```

---

## Sample Output

### 1. User-Space Scheduler Output (`./timeshare_sim 3 50`)
```text
=== Process Table Init (3 processes, 50 ms quantum) ===
[INIT] P1 (PID 219) | State: READY  | Steps: 3
[INIT] P2 (PID 220) | State: READY  | Steps: 4
[INIT] P3 (PID 221) | State: READY  | Steps: 5

=== Starting Round-Robin Time-Sharing Scheduler ===

[SCHEDULER] Dispatch P1 (PID 219) | Slice #1 -> SIGCONT
  [WORKER P1 | PID 219] Step 1/3 [####........]  33%
[TIMER] Tick #1 (50 ms quantum expired) -> SIGALRM received
[SCHEDULER] Preempted P1 (PID 219) SIGSTOP -> STOPPED

[SCHEDULER] Dispatch P2 (PID 220) | Slice #1 -> SIGCONT
  [WORKER P2 | PID 220] Step 1/4 [###.........]  25%
[TIMER] Tick #2 (50 ms quantum expired) -> SIGALRM received
[SCHEDULER] Preempted P2 (PID 220) SIGSTOP -> STOPPED
...
+-------------------------------------------------------+
|             TIME-SHARING SIMULATION SUMMARY           |
+---------+----------+--------------+--------+----------+
| Process |   PID    | Final State  | Steps  |  Slices  |
+---------+----------+--------------+--------+----------+
|   P1    | 219      | TERMINATED   | 3      | 2        |
|   P2    | 220      | TERMINATED   | 4      | 3        |
|   P3    | 221      | TERMINATED   | 5      | 4        |
+---------+----------+--------------+--------+----------+
| Time Quantum     : 50     ms                          |
| Timer Interrupts : 6      (SIGALRM ticks)             |
| Context Switches : 9      (SIGCONT dispatches)        |
+-------------------------------------------------------+
```

### 2. Kernel `/proc/timeshare` & `dmesg` Output (When `timeshare_kmod.ko` is Loaded)
```text
$ cat /proc/timeshare
Kernel Module : timeshare_kmod (ACTIVE)
Events Logged : 21
Last Event    : TERMINATE P3 pid=221 state=TERMINATED left=0

$ sudo dmesg | tail -n 6
[  142.104512] timeshare_kmod: event #16: DISPATCH P2 pid=220 slice=3 state=RUNNING
[  142.134890] timeshare_kmod: event #17: TERMINATE P2 pid=220 state=TERMINATED left=1
[  142.135011] timeshare_kmod: event #18: DISPATCH P3 pid=221 slice=3 state=RUNNING
[  142.185204] timeshare_kmod: event #19: PREEMPT P3 pid=221 tick=6 state=STOPPED
[  142.185340] timeshare_kmod: event #20: DISPATCH P3 pid=221 slice=4 state=RUNNING
[  142.215610] timeshare_kmod: event #21: TERMINATE P3 pid=221 state=TERMINATED left=0
```

---

## Linux Kernel Coding Style Compliance

This project follows [Linux Kernel Coding Style](https://docs.kernel.org/process/coding-style.html):
- **Indentation**: 8-character hard tabs (`\t`).
- **Line Width**: Strictly $\le 80$ columns across all source and header files.
- **Braces**: K&R style (`{` on the next line for functions; `{` on the same line for `if`, `for`, `while`, `switch`, `struct`, and `enum`).
- **Naming**: Lowercase `snake_case` for variables, functions, and struct types; `UPPER_SNAKE_CASE` for macros and enum constants; no `typedef` hiding `struct` or `enum` types.
- **Comments**: C89 `/* ... */` multi-line comments with kernel-doc function headers.
