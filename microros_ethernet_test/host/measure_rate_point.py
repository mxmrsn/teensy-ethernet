#!/usr/bin/env python3
"""Measure received + drop rate for the Point32 galvo_state topic.

The Teensy puts a monotonic sequence in Point32.z, so gaps = lost samples.
Subscribes BEST_EFFORT (to match the publisher). ROS sourced:
    python3 measure_rate_point.py [topic] [seconds]
"""
import sys
import time
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy
from geometry_msgs.msg import Point32


class RateMeter(Node):
    def __init__(self, topic, reliable=False):
        super().__init__("rate_meter_pt")
        qos = QoSProfile(depth=100)
        qos.reliability = ReliabilityPolicy.RELIABLE if reliable else ReliabilityPolicy.BEST_EFFORT
        self.count = 0
        self.first = None
        self.last = None
        self.t0 = None
        self.create_subscription(Point32, topic, self.cb, qos)

    def cb(self, msg):
        seq = int(round(msg.z))
        if self.t0 is None:
            self.t0 = time.perf_counter()
            self.first = seq
        self.last = seq
        self.count += 1


def main():
    topic = sys.argv[1] if len(sys.argv) > 1 else "/teensy/galvo_state"
    dur = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0
    reliable = (len(sys.argv) > 3 and sys.argv[3].lower().startswith("rel"))
    rclpy.init()
    node = RateMeter(topic, reliable)
    start = time.perf_counter()
    while rclpy.ok():
        rclpy.spin_once(node, timeout_sec=0.05)
        if node.t0 and (time.perf_counter() - node.t0) >= dur:
            break
        if (time.perf_counter() - start) > dur + 8:
            break

    if node.count >= 2 and node.t0:
        el = time.perf_counter() - node.t0
        sent = node.last - node.first + 1
        drops = sent - node.count
        print(f"topic     : {topic}  [{'RELIABLE' if reliable else 'BEST_EFFORT'} sub]")
        print(f"received  : {node.count} msgs in {el:.2f}s  ->  {node.count/el:.0f} Hz")
        print(f"seq span  : {node.first}..{node.last} = {sent} published by Teensy")
        print(f"drops     : {drops}  ({100.0*drops/sent:.2f}%)")
        print(f"publisher : ~{sent/el:.0f} Hz (Teensy-side)")
    else:
        print("no messages received -- QoS mismatch, wrong topic, or not connected")
    rclpy.shutdown()


if __name__ == "__main__":
    main()
