#!/usr/bin/env bash
# Synthetic host-load sweep: peg N CPU cores with busy loops and measure the
# Teensy update rate + RTT at each load level. Simulates camera/OpenCV CPU
# contention on the host. Sudo-free (uses `yes` busy loops). 16-core host.
#
# Usage: bash loadsweep.sh <pub_rate_hz> ["space-separated load levels"]
source /opt/ros/jazzy/setup.bash
source ~/ros2_ws/install/local_setup.bash

RATE="${1:-1000}"
LOADS="${2:-0 4 8 12 15}"

start_load() { if [ "$1" -gt 0 ]; then for _ in $(seq 1 "$1"); do yes >/dev/null & done; fi; }
stop_load()  { pkill -x yes 2>/dev/null; sleep 0.5; }

# best-effort Point32 at the test rate (strong burst; short bursts can be missed)
timeout 3 ros2 topic pub -r 20 /teensy/set_mode    std_msgs/msg/Int32 "{data: 0}"     >/dev/null 2>&1
timeout 3 ros2 topic pub -r 20 /teensy/set_rate_hz std_msgs/msg/Int32 "{data: $RATE}" >/dev/null 2>&1
sleep 1

echo "================= TEENSY PUBLISH RATE: ${RATE} Hz ================="
for n in $LOADS; do
  stop_load
  start_load "$n"
  sleep 2
  echo "---------- host load: ${n}/16 cores busy ----------"
  ros2 run teensy_bench rate_meter /teensy/galvo_state 4 best_effort | grep -E "received|drops"
  ros2 run teensy_bench rtt_ping 800 200 | grep -E "samples|RTT  ms"
done
stop_load
echo "DONE ${RATE}Hz"
