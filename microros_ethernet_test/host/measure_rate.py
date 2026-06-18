#!/usr/bin/env python3
"""Measure the received rate AND drop rate of a best-effort Int32 topic.

The Teensy publishes a monotonically incrementing counter, so gaps in the
sequence = lost samples. This subscribes with BEST_EFFORT QoS (required to match
the publisher) and reports received Hz + drop %.

Usage (ROS sourced):  python3 measure_rate.py [topic] [seconds]
"""
import sys
import time
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy
from std_msgs.msg import Int32


class RateMeter(Node):
    def __init__(self, topic):
        super().__init__("rate_meter")
        qos = QoSProfile(depth=100)
        qos.reliability = ReliabilityPolicy.BEST_EFFORT
        self.count = 0
        self.first = None
        self.last = None
        self.t0 = None
        self.create_subscription(Int32, topic, self.cb, qos)

    def cb(self, msg):
        if self.t0 is None:
            self.t0 = time.perf_counter()
            self.first = msg.data
        self.last = msg.data
        self.count += 1


def main():
    topic = sys.argv[1] if len(sys.argv) > 1 else "/teensy/hf_counter"
    dur = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0
    rclpy.init()
    node = RateMeter(topic)
    start = time.perf_counter()
    while rclpy.ok():
        rclpy.spin_once(node, timeout_sec=0.05)
        if node.t0 and (time.perf_counter() - node.t0) >= dur:
            break
        if (time.perf_counter() - start) > dur + 8:
            break

    if node.count >= 2 and node.t0:
        elapsed = time.perf_counter() - node.t0
        sent = node.last - node.first + 1
        drops = sent - node.count
        print(f"topic     : {topic}")
        print(f"received  : {node.count} msgs in {elapsed:.2f}s  ->  {node.count/elapsed:.0f} Hz")
        print(f"seq span  : {node.first}..{node.last}  = {sent} published by Teensy")
        print(f"drops     : {drops}  ({100.0*drops/sent:.2f}%)")
        print(f"publisher : ~{sent/elapsed:.0f} Hz (Teensy-side, from sequence)")
    else:
        print("no messages received -- QoS mismatch (need BEST_EFFORT), wrong topic, or not connected")
    rclpy.shutdown()


if __name__ == "__main__":
    main()
