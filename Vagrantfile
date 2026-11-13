# -*- mode: ruby -*-
# vi: set ft=ruby :
# SPDX-License-Identifier: GPL-2.0
# Vagrantfile - Automated VirtualBox Linux VM for Kernel Time-Sharing Scheduler

Vagrant.configure("2") do |config|
  # Official Ubuntu 24.04 LTS cloud image for VirtualBox
  config.vm.box = "bento/ubuntu-24.04"
  config.vm.hostname = "timeshare-kernel-vm"

  config.vm.provider "virtualbox" do |vb|
    vb.name = "timeshare-kernel-vm"
    vb.memory = "2048"
    vb.cpus = 2
    vb.customize ["modifyvm", :id, "--uartmode1", "disconnected"]
  end

  # Sync project folder into /vagrant and copy to /home/vagrant/operating-system
  # so Kbuild can create symlinks and object files on a native ext4 filesystem
  config.vm.synced_folder ".", "/vagrant", disabled: false

  config.vm.provision "shell", inline: <<-SHELL
    set -e
    echo ">>> Syncing project to /home/vagrant/operating-system..."
    mkdir -p /home/vagrant/operating-system
    cp -r /vagrant/Makefile /vagrant/scheduler.h /vagrant/timeshare_kmod.c \
          /vagrant/main.c /vagrant/README.md /vagrant/EXPLANATION.md \
          /vagrant/VM_GUIDE.md /vagrant/vm /home/vagrant/operating-system/ 2>/dev/null || true
    chown -R vagrant:vagrant /home/vagrant/operating-system

    echo ">>> Provisioning kernel headers & building timeshare_kmod.ko..."
    bash /home/vagrant/operating-system/vm/setup-vm.sh

    if ! grep -q "operating-system" /home/vagrant/.bashrc; then
      cat >> /home/vagrant/.bashrc << 'EOF'
cd ~/operating-system 2>/dev/null || true
echo ""
echo "================================================================="
echo "  Welcome to the Kernel Time-Sharing VM ($(uname -srm))"
echo "  Project directory: ~/operating-system"
echo "  Quick commands:"
echo "    make run                  # Interactive prompt"
echo "    make demo                 # Run 3-process 400ms demo"
echo "    cat /proc/timeshare       # View kernel scheduler state"
echo "    sudo dmesg | tail -n 30   # View kernel ring buffer logs"
echo "================================================================="
EOF
    fi
  SHELL
end
