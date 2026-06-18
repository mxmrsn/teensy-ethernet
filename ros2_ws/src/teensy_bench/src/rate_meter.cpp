// rate_meter -- C++ (rclcpp) received-rate + drop meter for the Teensy galvo
// telemetry. Faster than the Python rclpy meter, so it raises the best-effort
// receive ceiling. Drops detected via the monotonic sequence packed in Point32.z.
//
// Usage: ros2 run teensy_bench rate_meter [topic] [seconds] [reliable|best_effort]
#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/point32.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

using std::chrono::steady_clock;
using std::chrono::duration;

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  std::string topic = (argc > 1) ? argv[1] : "/teensy/galvo_state";
  double dur = (argc > 2) ? std::stod(argv[2]) : 5.0;
  bool reliable = (argc > 3) && (std::string(argv[3]).rfind("rel", 0) == 0);

  auto node = std::make_shared<rclcpp::Node>("rate_meter_cpp");
  rclcpp::QoS qos(rclcpp::KeepLast(100));
  if (reliable) qos.reliable(); else qos.best_effort();

  long count = 0, first = 0, last = 0;
  bool started = false;
  steady_clock::time_point t0;

  auto sub = node->create_subscription<geometry_msgs::msg::Point32>(
    topic, qos,
    [&](const geometry_msgs::msg::Point32::SharedPtr m) {
      long seq = static_cast<long>(std::llround(m->z));
      if (!started) { started = true; t0 = steady_clock::now(); first = seq; }
      last = seq;
      count++;
    });

  // Tight spin (no rate limit) so the meter itself isn't the bottleneck.
  auto start = steady_clock::now();
  while (rclcpp::ok()) {
    rclcpp::spin_some(node);
    if (started && duration<double>(steady_clock::now() - t0).count() >= dur) break;
    if (duration<double>(steady_clock::now() - start).count() > dur + 10.0) break;
  }

  if (count >= 2 && started) {
    double el = duration<double>(steady_clock::now() - t0).count();
    long sent = last - first + 1, drops = sent - count;
    std::printf("topic     : %s  [%s sub, C++]\n", topic.c_str(),
                reliable ? "RELIABLE" : "BEST_EFFORT");
    std::printf("received  : %ld msgs in %.2fs  ->  %.0f Hz\n", count, el, count / el);
    std::printf("seq span  : %ld..%ld = %ld published\n", first, last, sent);
    std::printf("drops     : %ld  (%.2f%%)\n", drops, 100.0 * drops / sent);
  } else {
    std::printf("no messages received (QoS mismatch / wrong topic / not connected)\n");
  }
  rclcpp::shutdown();
  return 0;
}
