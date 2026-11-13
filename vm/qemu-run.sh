#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# Portable QEMU Virtual Machine Runner for timeshare_kmod.ko

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/vm/.build"
ROOTFS_DIR="${BUILD_DIR}/rootfs"
INITRAMFS="${BUILD_DIR}/initramfs.cpio.gz"

MODE="${1:---interactive}"

run_test_suite() {
	echo "--- [TEST 1] Standard Kernel Round-Robin (3 kthreads, 50 ms quantum) ---"
	printf "3\n50\n" | ./timeshare_ctl
	echo "--- [TEST 2] Single kthread boundary (1 kthread, 20 ms quantum) ---"
	printf "1\n20\n" | ./timeshare_ctl
	echo "--- [TEST 3] CLI arguments mode (4 kthreads, 30 ms quantum) ---"
	./timeshare_ctl 4 30
	echo "--- [TEST 4] Max kthreads & min quantum (10 kthreads, 10 ms) ---"
	out="$(./timeshare_ctl 10 10)"
	echo "$out"
	term_count="$(echo "$out" | grep -c -- "-> TERMINATED" || true)"
	if [ "$term_count" -ne 10 ]; then
		echo "FAIL: Expected 10 terminated kthreads, got $term_count"
		return 1
	fi
	echo "--- [TEST 5] Invalid input rejection (CLI & /proc/timeshare) ---"
	! printf "" | ./timeshare_ctl
	! printf "\n100\n" | ./timeshare_ctl
	! printf "0\n100\n" | ./timeshare_ctl
	! printf "11\n100\n" | ./timeshare_ctl
	! printf "3\n5\n" | ./timeshare_ctl
	! printf "abc\n100\n" | ./timeshare_ctl
	! ./timeshare_ctl 0 100
	! ./timeshare_ctl 3 10001
	! ./timeshare_ctl 3
	if (echo "0 100" > /proc/timeshare) 2>/dev/null; then
		echo "FAIL: /proc/timeshare accepted invalid 0 processes"
		return 1
	fi
	if (echo "3 5" > /proc/timeshare) 2>/dev/null; then
		echo "FAIL: /proc/timeshare accepted invalid 5 ms quantum"
		return 1
	fi
	echo "--- [TEST 6] Verify /proc/timeshare & dmesg kernel ring buffer ---"
	grep -q "TIME-SHARING SIMULATION SUMMARY" /proc/timeshare
	dmesg | grep -q "timeshare_kmod:"
	echo "=== All automated kernel tests passed ==="
}

if [ "$MODE" = "--test-local" ]; then
	cd "$PROJECT_DIR"
	run_test_suite
	exit 0
fi

# Locate host kernel image (vmlinuz) to boot inside QEMU
KVER="$(uname -r)"
VMLINUZ=""
for candidate in \
	"/lib/modules/${KVER}/vmlinuz" \
	"/boot/vmlinuz-${KVER}" \
	"/boot/vmlinuz"; do
	if [ -r "$candidate" ]; then
		VMLINUZ="$candidate"
		break
	fi
done

if [ -z "$VMLINUZ" ]; then
	echo "Error: Could not find a readable kernel image (vmlinuz)." >&2
	echo "Try running with sudo or check /boot/vmlinuz-${KVER}." >&2
	exit 1
fi

# Ensure kernel module and controller are built
if [ ! -f "${PROJECT_DIR}/timeshare_kmod.ko" ] || [ ! -f "${PROJECT_DIR}/timeshare_ctl" ]; then
	make -C "$PROJECT_DIR" all
fi

# Build minimal initramfs root filesystem
rm -rf "$ROOTFS_DIR"
mkdir -p "$ROOTFS_DIR"/{bin,sbin,etc,proc,sys,dev,tmp,lib,lib64,usr/bin,usr/sbin}

copy_bin_with_libs() {
	local bin_path="$1"
	local dest_dir="${2:-/bin}"

	if [ ! -e "$bin_path" ]; then
		return 0
	fi
	cp -L "$bin_path" "${ROOTFS_DIR}${dest_dir}/$(basename "$bin_path")"
	chmod +x "${ROOTFS_DIR}${dest_dir}/$(basename "$bin_path")"

	ldd "$bin_path" 2>/dev/null | awk '{
		for (i = 1; i <= NF; i++) {
			if ($i ~ /^\//) print $i
		}
	}' | while read -r lib; do
		if [ -f "$lib" ]; then
			mkdir -p "${ROOTFS_DIR}$(dirname "$lib")"
			cp -Ln "$lib" "${ROOTFS_DIR}${lib}" 2>/dev/null || true
		fi
	done
}

BINS=(
	sh bash cat ls mkdir mount umount sleep echo printf grep
	dmesg uname head tail wc tr cut sed awk clear stty
	insmod rmmod lsmod modinfo poweroff reboot
)

for b in "${BINS[@]}"; do
	if p="$(command -v "$b" 2>/dev/null)"; then
		copy_bin_with_libs "$p" "/bin"
	fi
done

copy_bin_with_libs "${PROJECT_DIR}/timeshare_ctl" "/bin"
cp "${PROJECT_DIR}/timeshare_kmod.ko" "${ROOTFS_DIR}/timeshare_kmod.ko"
ln -sf /bin/timeshare_ctl "${ROOTFS_DIR}/timeshare_ctl"

cat > "${ROOTFS_DIR}/init" << 'EOF'
#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true

# Keep console clean during interactive prompts; dmesg still records everything
echo "4 4 1 7" > /proc/sys/kernel/printk 2>/dev/null || true

cmdline="$(cat /proc/cmdline)"
vm_mode="interactive"
for arg in $cmdline; do
	case "$arg" in
		ts_mode=*) vm_mode="${arg#ts_mode=}" ;;
	esac
done

echo ""
echo "================================================================="
echo "  QEMU Linux Kernel VM Booted: $(uname -srm)"
echo "================================================================="
echo "[VM] Loading kernel module: insmod /timeshare_kmod.ko ..."
if insmod /timeshare_kmod.ko; then
	echo "[VM] Module loaded in Ring 0! (/proc/timeshare active)"
	lsmod | grep timeshare_kmod || true
else
	echo "[VM] ERROR: Failed to load /timeshare_kmod.ko"
	poweroff -f
fi

if [ "$vm_mode" = "demo" ]; then
	echo ""
	echo "[VM] Running Kernel Round-Robin Demo (3 kthreads, 400 ms quantum)..."
	/bin/timeshare_ctl 3 400
	echo ""
	echo "[VM] Last 12 kernel ring buffer (dmesg) entries:"
	dmesg | grep "timeshare_kmod:" | tail -n 12
	rmmod timeshare_kmod
	echo "[VM] Demo complete. Powering off VM..."
	poweroff -f
	exit 0
fi

if [ "$vm_mode" = "test" ]; then
	cd /
	echo ""
	echo "--- [TEST 1] Standard Kernel Round-Robin (3 kthreads, 50 ms quantum) ---"
	printf "3\n50\n" | /bin/timeshare_ctl || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	echo "--- [TEST 2] Single kthread boundary (1 kthread, 20 ms quantum) ---"
	printf "1\n20\n" | /bin/timeshare_ctl || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	echo "--- [TEST 3] CLI arguments mode (4 kthreads, 30 ms quantum) ---"
	/bin/timeshare_ctl 4 30 || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	echo "--- [TEST 4] Max kthreads & min quantum (10 kthreads, 10 ms) ---"
	out="$(/bin/timeshare_ctl 10 10)" || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	echo "$out"
	term_count="$(echo "$out" | grep -c -- "-> TERMINATED" || true)"
	if [ "$term_count" -ne 10 ]; then
		echo "FAIL: Expected 10 terminated kthreads, got $term_count"
		echo "TEST_RESULT=FAIL"
		poweroff -f
	fi
	echo "--- [TEST 5] Invalid input rejection (CLI & /proc/timeshare) ---"
	! printf "" | /bin/timeshare_ctl || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	! printf "\n100\n" | /bin/timeshare_ctl || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	! printf "0\n100\n" | /bin/timeshare_ctl || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	! printf "11\n100\n" | /bin/timeshare_ctl || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	! printf "3\n5\n" | /bin/timeshare_ctl || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	! printf "abc\n100\n" | /bin/timeshare_ctl || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	! /bin/timeshare_ctl 0 100 || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	! /bin/timeshare_ctl 3 10001 || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	! /bin/timeshare_ctl 3 || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	if (echo "0 100" > /proc/timeshare) 2>/dev/null; then
		echo "TEST_RESULT=FAIL"
		poweroff -f
	fi
	echo "--- [TEST 6] Verify /proc/timeshare, dmesg, and rmmod ---"
	grep -q "TIME-SHARING SIMULATION SUMMARY" /proc/timeshare || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	dmesg | grep -q "timeshare_kmod:" || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	rmmod timeshare_kmod || { echo "TEST_RESULT=FAIL"; poweroff -f; }
	echo "=== All automated kernel tests passed ==="
	echo "TEST_RESULT=PASS"
	poweroff -f
	exit 0
fi

# Interactive VM Menu
while true; do
	echo ""
	echo "+---------------------------------------------------------------+"
	echo "|       LINUX KERNEL TIME-SHARING VM ENVIRONMENT (Ring 0)       |"
	echo "+---------------------------------------------------------------+"
	echo "|  1) Run Interactive Kernel Scheduler (timeshare_ctl)          |"
	echo "|  2) Run Quick Demo (3 kernel threads, 400 ms quantum)         |"
	echo "|  3) Inspect /proc/timeshare output (cat /proc/timeshare)      |"
	echo "|  4) View Kernel Ring Buffer Logs (dmesg | grep timeshare)     |"
	echo "|  5) Open Interactive Root Shell (/bin/sh) inside VM           |"
	echo "|  q) Unload Module & Power Off Virtual Machine                 |"
	echo "+---------------------------------------------------------------+"
	printf "Select an option [1-5, q]: "
	if ! read -r choice; then
		break
	fi
	case "$choice" in
		1)
			/bin/timeshare_ctl || true
			;;
		2)
			/bin/timeshare_ctl 3 400 || true
			;;
		3)
			echo ""
			cat /proc/timeshare
			;;
		4)
			echo ""
			dmesg | grep "timeshare_kmod:" | tail -n 35
			;;
		5)
			echo ""
			echo "Entering VM root shell. Type 'exit' to return to the VM menu."
			echo "Try commands like: lsmod, cat /proc/timeshare, echo '3 400' > /proc/timeshare, dmesg"
			/bin/sh
			;;
		q|Q|6)
			break
			;;
		*)
			echo "Invalid option: $choice"
			;;
	esac
done

echo "[VM] Unloading timeshare_kmod and powering off VM..."
rmmod timeshare_kmod 2>/dev/null || true
poweroff -f
EOF

chmod +x "${ROOTFS_DIR}/init"

(
	cd "$ROOTFS_DIR"
	find . -print0 | cpio --null -ov --format=newc 2>/dev/null | gzip -9 > "$INITRAMFS"
)

KVM_ARGS=()
if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
	KVM_ARGS=(-enable-kvm -cpu host)
fi

QEMU_MODE="interactive"
if [ "$MODE" = "--demo" ]; then
	QEMU_MODE="demo"
elif [ "$MODE" = "--test" ]; then
	QEMU_MODE="test"
fi

if [ "$QEMU_MODE" = "test" ]; then
	LOG_FILE="${BUILD_DIR}/qemu-test.log"
	qemu-system-x86_64 \
		"${KVM_ARGS[@]}" \
		-smp 2 \
		-m 512M \
		-kernel "$VMLINUZ" \
		-initrd "$INITRAMFS" \
		-nographic \
		-no-reboot \
		-append "console=ttyS0 quiet panic=-1 ts_mode=test" | tee "$LOG_FILE"
	grep -q "TEST_RESULT=PASS" "$LOG_FILE"
else
	qemu-system-x86_64 \
		"${KVM_ARGS[@]}" \
		-smp 2 \
		-m 512M \
		-kernel "$VMLINUZ" \
		-initrd "$INITRAMFS" \
		-nographic \
		-no-reboot \
		-append "console=ttyS0 quiet panic=-1 ts_mode=${QEMU_MODE}"
fi
