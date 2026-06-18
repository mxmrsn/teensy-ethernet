#!/usr/bin/env bash
# Source ROS + the agent workspace and run the micro-ROS serial agent.
# Usage: ./run_agent.sh [/dev/ttyACM0] [115200]
source /opt/ros/jazzy/setup.bash
source ~/microros_ws/install/local_setup.bash
DEV="${1:-/dev/ttyACM0}"
BAUD="${2:-115200}"
echo "micro_ros_agent serial --dev $DEV -b $BAUD"
exec ros2 run micro_ros_agent micro_ros_agent serial --dev "$DEV" -b "$BAUD"
