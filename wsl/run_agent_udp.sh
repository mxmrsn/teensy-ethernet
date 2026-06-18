#!/usr/bin/env bash
# Run the micro-ROS agent over UDP (for the Ethernet firmware).
# Usage: ./run_agent_udp.sh [port]
source /opt/ros/jazzy/setup.bash
source ~/microros_ws/install/local_setup.bash
PORT="${1:-8888}"
echo "micro_ros_agent udp4 --port $PORT"
exec ros2 run micro_ros_agent micro_ros_agent udp4 --port "$PORT"
