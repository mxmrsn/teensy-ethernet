#!/usr/bin/env bash
# ONE-TIME: enable flashing the Teensy directly from WSL (no Windows tools, no
# usbipd unbind/bind dance). Installs teensy-loader-cli and the PJRC udev rules.
# The 0666 rules also make the serial device accessible without dialout/chmod.
# Needs sudo (your password). Run:
#   tr -d '\r' < 03_flash_setup.sh | bash
set -e

sudo apt-get update
sudo apt-get install -y teensy-loader-cli

sudo tee /etc/udev/rules.d/00-teensy.rules >/dev/null <<'EOF'
# PJRC Teensy rules: non-root access to the serial port + HalfKay bootloader.
ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="04*", ENV{ID_MM_DEVICE_IGNORE}="1"
ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="04*", ENV{MTP_NO_PROBE}="1"
SUBSYSTEMS=="usb", ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="04*", MODE:="0666"
KERNEL=="ttyACM*", ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="04*", MODE:="0666"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
echo "FLASH_SETUP_DONE"
