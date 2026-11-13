#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# Export VirtualBox VM to a single portable .ova appliance file for friends

set -euo pipefail

VM_NAME="${1:-timeshare-kernel-vm}"
OUTPUT_OVA="${2:-timeshare-kernel-vm.ova}"

if ! command -v VBoxManage >/dev/null 2>&1; then
	echo "Error: VBoxManage not found. Please install VirtualBox first." >&2
	exit 1
fi

if ! VBoxManage list vms | grep -q "\"${VM_NAME}\""; then
	echo "Error: VirtualBox VM '${VM_NAME}' not found." >&2
	echo "Available VMs:" >&2
	VBoxManage list vms >&2
	echo "" >&2
	echo "Tip: Run 'vagrant up' first to create '${VM_NAME}', or pass your VM name:" >&2
	echo "  ./vm/export-ova.sh \"My Ubuntu VM\" timeshare-kernel-vm.ova" >&2
	exit 1
fi

echo "[1/3] Ensuring VM '${VM_NAME}' is powered off before export..."
if VBoxManage list runningvms | grep -q "\"${VM_NAME}\""; then
	VBoxManage controlvm "${VM_NAME}" acpipowerbutton || true
	sleep 5
	if VBoxManage list runningvms | grep -q "\"${VM_NAME}\""; then
		VBoxManage controlvm "${VM_NAME}" poweroff
	fi
fi

rm -f "$OUTPUT_OVA"

echo "[2/3] Exporting '${VM_NAME}' to '${OUTPUT_OVA}'..."
VBoxManage export "${VM_NAME}" \
	--output "${OUTPUT_OVA}" \
	--ovf20 \
	--vsys 0 \
	--product "Linux Kernel Time-Sharing Scheduler VM" \
	--description "Pre-configured Linux VM with timeshare_kmod.ko Round-Robin Scheduler in Ring 0"

echo "[3/3] Export complete!"
ls -lh "$OUTPUT_OVA"
echo ""
echo "Share '${OUTPUT_OVA}' with your friends! They can import it in VirtualBox via:"
echo "  File -> Import Appliance -> Select '${OUTPUT_OVA}' -> Finish"
