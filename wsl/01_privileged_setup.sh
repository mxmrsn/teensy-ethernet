#!/usr/bin/env bash
# ONE-TIME privileged WSL setup. Run this yourself in a WSL terminal -- it needs
# sudo (your password). Everything after this can run unprivileged.
#
#   wsl -d Ubuntu
#   cd "/mnt/c/Users/memerson/OneDrive - Sciton Inc/Documents/Arduino/teensy/teensy-ethernet/wsl"
#   tr -d '\r' < 01_privileged_setup.sh | bash      # tr strips Windows CRLF
#
set -e

echo "==> apt deps (pip, venv, rosdep, agent build deps)"
sudo apt-get update
sudo apt-get install -y \
  python3-pip python3-venv python3-rosdep \
  python3-colcon-common-extensions git build-essential cmake \
  libasio-dev libtinyxml2-dev

echo "==> rosdep init"
if [ ! -f /etc/ros/rosdep/sources.list.d/20-default.list ]; then
  sudo rosdep init
fi

echo "PRIVILEGED_SETUP_DONE"
