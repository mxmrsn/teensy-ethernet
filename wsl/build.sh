#!/usr/bin/env bash
# Build a micro-ROS firmware in WSL and stage the hex on Windows for flashing.
# Step 1 of the iteration loop:  build.sh (WSL) -> flash_teensy.ps1 (Windows) -> run_agent*.sh (WSL)
#
# Usage: bash build.sh [project_name]
#   project_name defaults to microros_serial_test; pass microros_ethernet_test for UDP.
set -e

NAME="${1:-microros_serial_test}"
REPO="/mnt/c/Users/memerson/OneDrive - Sciton Inc/Documents/Arduino/teensy/teensy-ethernet"
PROJ="$HOME/teensy/$NAME"
OUT="/mnt/c/teensy_build/$NAME/firmware.hex"

# sync source into the space-free build dir (micro_ros_platformio chokes on spaces)
mkdir -p "$PROJ/src"
cp "$REPO/$NAME/platformio.ini" "$PROJ/platformio.ini"
cp "$REPO/$NAME/src/"*.cpp "$PROJ/src/"
[ -f "$REPO/$NAME/colcon.meta" ] && cp "$REPO/$NAME/colcon.meta" "$PROJ/colcon.meta"

# Stage custom interface packages for the micro-ROS lib build. micro_ros_platformio
# compiles anything under <project>/extra_packages into the firmware's type support.
# Single source of truth = ros2_ws/src/teensy_msgs.
if [ "$NAME" = "microros_ethernet_test" ] && [ -d "$REPO/ros2_ws/src/teensy_msgs" ]; then
  rm -rf "$PROJ/extra_packages"
  mkdir -p "$PROJ/extra_packages"
  cp -r "$REPO/ros2_ws/src/teensy_msgs" "$PROJ/extra_packages/teensy_msgs"
fi

cd "$PROJ"
source /opt/ros/jazzy/setup.bash
~/.pio-venv/bin/pio run -e teensy41

mkdir -p "$(dirname "$OUT")"
cp .pio/build/teensy41/firmware.hex "$OUT"
echo "BUILD_DONE -> C:\\teensy_build\\$NAME\\firmware.hex"
