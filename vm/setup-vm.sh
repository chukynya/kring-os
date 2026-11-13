#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# Universal Virtual Machine Setup Script (Ubuntu/Debian & Fedora/RHEL)
# Installs kernel headers, GCC, and GNU Make, compiles timeshare_kmod.ko,
# and loads the Round-Robin scheduler module into the VM kernel.

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KVER="$(uname -r)"

echo "================================================================="
echo "  Setting up Kernel Time-Sharing Environment on $(uname -srm)"
echo "================================================================="

if [ "$(id -u)" -ne 0 ]; then
	SUDO="sudo"
else
	SUDO=""
fi

if command -v apt-get >/dev/null 2>&1; then
	echo "[1/3] Detected Debian/Ubuntu. Installing kernel headers & build tools..."
	$SUDO apt-get update -y
	$SUDO apt-get install -y \
		build-essential \
		gcc \
		make \
		"linux-headers-${KVER}" \
		kmod \
		git
elif command -v dnf >/dev/null 2>&1; then
	echo "[1/3] Detected Fedora/RHEL. Installing kernel-devel & build tools..."
	$SUDO dnf install -y \
		gcc \
		make \
		"kernel-devel-${KVER}" \
		kernel-headers \
		kmod \
		git
else
	echo "Warning: Unknown package manager. Ensure gcc, make, and kernel headers for ${KVER} are installed."
fi

echo ""
echo "[2/3] Building kernel module (timeshare_kmod.ko) and controller (timeshare_ctl)..."
cd "$PROJECT_DIR"
make clean
make all

echo ""
echo "[3/3] Loading timeshare_kmod.ko into the VM kernel..."
if lsmod | grep -q "^timeshare_kmod"; then
	$SUDO rmmod timeshare_kmod
fi
$SUDO insmod "${PROJECT_DIR}/timeshare_kmod.ko"

echo ""
echo "================================================================="
echo "  VM Setup Complete! Kernel module is active at /proc/timeshare"
echo "================================================================="
echo "Try running:"
echo "  make run                       # Interactive kernel scheduler"
echo "  make demo                      # 3 kernel threads, 400 ms quantum"
echo "  cat /proc/timeshare            # Read kernel PCB & execution log"
echo "  echo '4 300' > /proc/timeshare # Trigger run directly via procfs"
echo "  sudo dmesg | grep timeshare    # Inspect kernel ring buffer"
echo "================================================================="
