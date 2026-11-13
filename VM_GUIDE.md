# Virtual Machine Setup & Sharing Guide (QEMU, VirtualBox, Vagrant & OVA)

Because our **Round-Robin Time-Sharing Scheduler (`timeshare_kmod.ko`)** executes directly in **Linux Kernel Space (Ring 0)**, running it inside a **Virtual Machine (VM)** is the safest and most portable way for you, your lecturer, and your friends on other computers (**Windows, macOS, or Linux**) to build, load, and test the kernel module without affecting the host operating system.

This repository provides **four ready-to-use ways** to run and share the Virtual Machine environment.

---

## Method 1: Instant QEMU Kernel VM (Fastest — Boots in ~2 Seconds)

If you are on Linux (or WSL2 on Windows) with `qemu-system-x86_64` installed, you do **not** even need to download a 2 GB ISO or have root (`sudo`) privileges on your host machine!

`vm/qemu-run.sh` automatically packages `timeshare_kmod.ko` and `timeshare_ctl` into a tiny `initramfs.cpio.gz` and boots the Linux kernel (`vmlinuz`) inside an isolated QEMU/KVM Virtual Machine right in your terminal.

### 1. Install QEMU (if not already installed)
- **Fedora / RHEL**:
  ```bash
  sudo dnf install -y qemu-system-x86 cpio gzip gcc make kernel-devel-$(uname -r)
  ```
- **Ubuntu / Debian / WSL2**:
  ```bash
  sudo apt-get update && sudo apt-get install -y qemu-system-x86 cpio gzip build-essential linux-headers-$(uname -r)
  ```

### 2. Boot the Interactive QEMU VM
From the project root directory, run:
```bash
make vm-qemu
```
Within ~2 seconds, the VM boots, runs `insmod /timeshare_kmod.ko` in Ring 0, and presents an interactive menu:
```text
+---------------------------------------------------------------+
|       LINUX KERNEL TIME-SHARING VM ENVIRONMENT (Ring 0)       |
+---------------------------------------------------------------+
|  1) Run Interactive Kernel Scheduler (timeshare_ctl)          |
|  2) Run Quick Demo (3 kernel threads, 400 ms quantum)         |
|  3) Inspect /proc/timeshare output (cat /proc/timeshare)      |
|  4) View Kernel Ring Buffer Logs (dmesg | grep timeshare)     |
|  5) Open Interactive Root Shell (/bin/sh) inside VM           |
|  q) Unload Module & Power Off Virtual Machine                 |
+---------------------------------------------------------------+
Select an option [1-5, q]:
```
- Choose **`1`** to enter any number of worker processes (`1-10`) and time quantum (`10-10000` ms).
- Choose **`5`** to drop into a real `/bin/sh` root shell inside the VM where you can manually run:
  ```sh
  lsmod
  echo "4 300" > /proc/timeshare
  cat /proc/timeshare
  dmesg | grep timeshare_kmod
  rmmod timeshare_kmod
  insmod /timeshare_kmod.ko nr_procs=3 quantum_ms=400
  exit
  ```
- Choose **`q`** to cleanly unload the kernel module and power off the VM.

### 3. One-Command Non-Interactive VM Demo & Tests
```bash
make demo   # Boots QEMU VM, runs 3 kthreads @ 400ms, shows dmesg, powers off
make test   # Boots QEMU VM and runs all 6 automated kernel test suites
```

---

## Method 2: VirtualBox + Vagrant (Cross-Platform for Windows, macOS & Linux)

Your friends on **Windows, macOS, or Linux** can spin up a full Ubuntu 24.04 LTS Virtual Machine with the kernel module pre-compiled and loaded using **two commands**.

### 1. Prerequisites (Windows / macOS / Linux)
1. Install **[Oracle VirtualBox](https://www.virtualbox.org/wiki/Downloads)**
2. Install **[HashiCorp Vagrant](https://developer.hashicorp.com/vagrant/install)**
3. Clone this repository:
   ```bash
   git clone <your-repo-url>
   cd operating-system
   ```

### 2. Boot and Enter the VM
```bash
vagrant up
vagrant ssh
```
**What `vagrant up` does automatically (via [Vagrantfile](file:///home/henry/Documents/project-cawu-6/operating-system/Vagrantfile) and [setup-vm.sh](file:///home/henry/Documents/project-cawu-6/operating-system/vm/setup-vm.sh)):**
1. Downloads Ubuntu 24.04 LTS (`bento/ubuntu-24.04`) and creates a 2-CPU, 2 GB RAM VirtualBox VM named `timeshare-kernel-vm`.
2. Installs `gcc`, `make`, and `linux-headers-$(uname -r)` inside the VM.
3. Compiles `timeshare_kmod.ko` and `timeshare_ctl` in `~/operating-system`.
4. Loads `timeshare_kmod.ko` into the guest kernel (`insmod timeshare_kmod.ko`).

### 3. Run the Kernel Scheduler Inside `vagrant ssh`
Once inside the VM:
```bash
cd ~/operating-system
make run                       # Interactive prompt
make demo                      # 3 kernel worker threads, 400 ms quantum
cat /proc/timeshare            # Read kernel PCB table & trace buffer
sudo dmesg | grep timeshare    # View kernel printk / ring buffer logs
```

To stop or delete the VM when finished:
```bash
exit              # Leave SSH session
vagrant halt      # Shut down the VM
vagrant destroy   # Remove the VM completely (optional)
```

---

## Method 3: Share a Single `.ova` VirtualBox Appliance with Friends (Zero Setup!)

If your friends only have **VirtualBox** installed and don't want to use the command line or Vagrant to set up the VM, you can export your configured VM into a single `.ova` file and share it via Google Drive, OneDrive, or a USB flash drive.

### Step A: How You Export the `.ova` File
1. Build the VM first (using `vagrant up` or VirtualBox GUI).
2. Run our export helper script:
   ```bash
   make vm-export-ova
   # or specify a custom VirtualBox VM name:
   ./vm/export-ova.sh "timeshare-kernel-vm" timeshare-kernel-vm.ova
   ```
3. This creates `timeshare-kernel-vm.ova` in your project folder.

### Step B: How Your Friends Import and Use `timeshare-kernel-vm.ova`
1. Open **Oracle VirtualBox**.
2. Click **File -> Import Appliance...** (`Ctrl+I`).
3. Select `timeshare-kernel-vm.ova`, click **Next**, and click **Finish**.
4. Select `timeshare-kernel-vm` in the VirtualBox sidebar and click **Start**.
5. Log in (for the Vagrant box, username: `vagrant`, password: `vagrant`) and run:
   ```bash
   cd ~/operating-system
   sudo make load
   make run
   ```

---

## Method 4: Manual GUI VM Setup (Ubuntu or Fedora ISO in VirtualBox / VMware / UTM)

If you or your friends want to create a brand-new Virtual Machine manually from an `.iso` installer (which is also great to show in your video demonstration for the **"Ability to install OS"** rubric!):

### 1. Create the VM in VirtualBox / VMware
1. Download **[Ubuntu 24.04 LTS Server/Desktop ISO](https://ubuntu.com/download)** or **[Fedora Workstation/Server ISO](https://fedoraproject.org/)**.
2. Open VirtualBox and click **New**:
   - **Name**: `OS-Kernel-Timeshare`
   - **ISO Image**: Select your downloaded `.iso`
   - **Memory**: `2048 MB` (2 GB) or `4096 MB` (4 GB)
   - **Processors**: `2 CPUs`
   - **Hard Disk**: `20 GB`
3. Boot the VM and complete the standard Linux installation.

### 2. Clone the Project & Run `vm/setup-vm.sh` Inside the VM
Open a terminal inside your newly installed Linux VM and run:
```bash
git clone <your-repo-url>
cd operating-system
bash vm/setup-vm.sh
```
`vm/setup-vm.sh` automatically detects Ubuntu/Debian (`apt`) or Fedora (`dnf`), installs the matching kernel headers (`linux-headers-$(uname -r)` or `kernel-devel-$(uname -r)`), compiles `timeshare_kmod.ko` and `timeshare_ctl`, and loads the module into the kernel!

### 3. Use the Kernel Module Inside Your VM
```bash
# 1. Verify the kernel module is loaded in Ring 0
lsmod | grep timeshare_kmod

# 2. Run the interactive Round-Robin scheduler
make run

# 3. Or control the kernel module directly via /proc/timeshare
echo "3 400" > /proc/timeshare
cat /proc/timeshare

# 4. Inspect the Linux kernel log (printk / dmesg)
sudo dmesg | grep timeshare_kmod

# 5. Unload the kernel module when done
sudo make unload
```
