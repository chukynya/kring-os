# SPDX-License-Identifier: GPL-2.0
# Makefile - Kbuild & Build Automation for Kernel Round-Robin Scheduler

ifneq ($(KERNELRELEASE),)
# Invoked by Linux Kernel Kbuild system
obj-m += timeshare_kmod.o
ccflags-y := $(shell grep -q "hrtimer_setup" $(srctree)/include/linux/hrtimer.h 2>/dev/null && echo -DHAVE_HRTIMER_SETUP)
else

KDIR		?= /lib/modules/$(shell uname -r)/build
PWD		:= $(shell pwd)
CC		= gcc
CFLAGS		= -Wall -Wextra -Werror -std=c11 -pedantic
CTL_TARGET	= timeshare_ctl
KMOD_TARGET	= timeshare_kmod.ko

.PHONY: all kmod ctl load unload run demo vm-qemu vm-export-ova test checkpatch clean

all: kmod ctl

kmod:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

ctl: $(CTL_TARGET)

$(CTL_TARGET): main.c scheduler.h
	$(CC) $(CFLAGS) main.c -o $(CTL_TARGET)

load: kmod
	@if lsmod | grep -q "^timeshare_kmod"; then \
		sudo rmmod timeshare_kmod; \
	fi
	sudo insmod $(KMOD_TARGET)
	@echo "Kernel module loaded: /proc/timeshare is ready."

unload:
	@if lsmod | grep -q "^timeshare_kmod"; then \
		sudo rmmod timeshare_kmod; \
		echo "Kernel module timeshare_kmod unloaded."; \
	else \
		echo "Kernel module timeshare_kmod is not currently loaded."; \
	fi

run: all
	@if [ -e /proc/timeshare ]; then \
		./$(CTL_TARGET); \
	else \
		./vm/qemu-run.sh; \
	fi

demo: all
	@if [ -e /proc/timeshare ]; then \
		./$(CTL_TARGET) 3 400; \
	else \
		./vm/qemu-run.sh --demo; \
	fi

vm-qemu: all
	./vm/qemu-run.sh

vm-export-ova:
	./vm/export-ova.sh

test: all
	@if [ -e /proc/timeshare ]; then \
		./vm/qemu-run.sh --test-local; \
	else \
		./vm/qemu-run.sh --test; \
	fi

checkpatch:
	@if [ -x "$(KDIR)/scripts/checkpatch.pl" ]; then \
		$(KDIR)/scripts/checkpatch.pl --no-tree -f timeshare_kmod.c main.c scheduler.h; \
	else \
		echo "checkpatch.pl not found in $(KDIR)/scripts/"; \
	fi

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(RM) $(CTL_TARGET)
	$(RM) -r vm/.build

endif
