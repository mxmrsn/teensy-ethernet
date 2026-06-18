// rtt_ping -- round-trip latency for the Teensy link. Publishes /teensy/ping
// (Int32 seq) at a fixed rate; the Teensy echoes each on /teensy/pong; we time
// send->echo per seq and report the RTT distribution. Best-effort QoS (lowest
// latency, representative of the telemetry path).
//
// Usage: ros2 run teensy_bench rtt_ping [count] [rate_hz]
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/int32.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <unordered_map>
#include <vector>

using std::chrono::steady_clock;
using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::nanoseconds;

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  int count = (argc > 1) ? std::stoi(argv[1]) : 2000;
  double rate = (argc > 2) ? std::stod(argv[2]) : 500.0;

  auto node = std::make_shared<rclcpp::Node>("rtt_ping_cpp");
  rclcpp::QoS qos(rclcpp::KeepLast(10));
  qos.best_effort();
  auto pub = node->create_publisher<std_msgs::msg::Int32>("/teensy/ping", qos);

  std::unordered_map<int, steady_clock::time_point> send_t;
  std::vector<double> rtts;
  rtts.reserve(count);

  auto sub = node->create_subscription<std_msgs::msg::Int32>(
    "/teensy/pong", qos,
    [&](const std_msgs::msg::Int32::SharedPtr m) {
      auto it = send_t.find(m->data);
      if (it != send_t.end()) {
        rtts.push_back(duration<double, std::milli>(steady_clock::now() - it->second).count());
        send_t.erase(it);
      }
    });

  // Let discovery / the XRCE session match before timing.
  auto warm = steady_clock::now();
  while (rclcpp::ok() && duration<double>(steady_clock::now() - warm).count() < 1.0)
    rclcpp::spin_some(node);

  auto period = duration_cast<nanoseconds>(duration<double>(1.0 / rate));
  auto next = steady_clock::now();
  for (int seq = 0; rclcpp::ok() && seq < count; ++seq) {
    std_msgs::msg::Int32 m;
    m.data = seq;
    send_t[seq] = steady_clock::now();
    pub->publish(m);
    next += period;
    while (steady_clock::now() < next && rclcpp::ok()) rclcpp::spin_some(node);
  }
  // Drain late echoes.
  auto drain = steady_clock::now();
  while (rclcpp::ok() && duration<double>(steady_clock::now() - drain).count() < 0.5)
    rclcpp::spin_some(node);

  if (!rtts.empty()) {
    std::sort(rtts.begin(), rtts.end());
    double sum = 0;
    for (double x : rtts) sum += x;
    auto pct = [&](double p) {
      return rtts[std::min(rtts.size() - 1, static_cast<size_t>(p * rtts.size()))];
    };
    std::printf("RTT samples : %zu / %d returned (%.1f%%)\n",
                rtts.size(), count, 100.0 * rtts.size() / count);
    std::printf("RTT  ms     : min %.3f  avg %.3f  p50 %.3f  p99 %.3f  max %.3f\n",
                rtts.front(), sum / rtts.size(), pct(0.50), pct(0.99), rtts.back());
    std::printf("one-way ~ms : %.3f  (avg RTT / 2)\n", (sum / rtts.size()) / 2.0);
  } else {
    std::printf("no pongs received -- is the ping->pong firmware flashed and connected?\n");
  }
  rclcpp::shutdown();
  return 0;
}
