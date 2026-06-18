#!/usr/bin/env bash
# Sweep the Teensy publish rate and measure received + actual output rate.
# Run in WSL with ROS sourced. Uses the no-space copy at ~/teensy/measure_rate.py.
source /opt/ros/jazzy/setup.bash
M="$HOME/teensy/measure_rate.py"
RATES="${*:-1000 2000 5000 10000 20000}"

for hz in $RATES; do
  # set the rate (reliable sub; short burst to guarantee delivery)
  timeout 2 ros2 topic pub -r 10 /teensy/set_rate_hz std_msgs/msg/Int32 "{data: $hz}" >/dev/null 2>&1
  sleep 1
  echo "===================== TARGET ${hz} Hz ====================="
  python3 "$M" /teensy/hf_counter 5
done
