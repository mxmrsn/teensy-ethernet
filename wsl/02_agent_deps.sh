#!/usr/bin/env bash
# Second (and final) privileged step: apt deps that rosdep needs to build the
# micro-ROS agent (Micro-XRCE-DDS-Agent + Fast-DDS). List obtained from
# `rosdep install --from-paths src --ignore-src -s`. Needs sudo (your password).
#
#   wsl -d Ubuntu
#   tr -d '\r' < "/mnt/c/Users/memerson/OneDrive - Sciton Inc/Documents/Arduino/teensy/teensy-ethernet/wsl/02_agent_deps.sh" | bash
#
set -e
sudo apt-get update
sudo apt-get install -y python3-vcstool clang-tidy flex bison libncurses-dev usbutils
echo "AGENT_DEPS_DONE"
