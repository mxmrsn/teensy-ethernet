// =============================================================================
// High-performance micro-ROS node over ETHERNET/UDP -- Teensy 4.1
// -----------------------------------------------------------------------------
// Goal: validate >500 Hz publish rates from a Teensy peripheral to ROS2 (Jazzy)
// over UDP, with low latency. Key performance choices:
//
//   * Custom transport on QNEthernet (lwIP) -- non-blocking, ~100 Mbit, no
//     serial framing. Registered with framing=false (UDP is packet-based).
//   * BEST-EFFORT QoS on the high-rate publisher -- reliable QoS would ack every
//     sample over a confirmed XRCE stream and cap the rate; best-effort is
//     fire-and-forget. (Commands stay reliable; they're low-rate.)
//   * Lean hot loop -- publish directly via rcl_publish on a micros() cadence;
//     the executor only services the command subscription (1 handle). No timer.
//   * Liveness ping kept OUT of the hot path (every ~1 s, not every cycle).
//   * Reconnection state machine (rmw_uros_ping_agent) so boot order is free and
//     it survives agent restarts.
//
// Topics (once connected):
//   teensy/hf_counter  std_msgs/Int32  Teensy->ROS2, best-effort, at PUBLISH_HZ
//   teensy/command     std_msgs/Int32  ROS2->Teensy, reliable; LSB -> LED
//
// Agent:  ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
// Verify: ros2 topic hz /teensy/hf_counter
// =============================================================================

#include <Arduino.h>
#include <micro_ros_platformio.h>
#include <QNEthernet.h>

#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>
#include <std_msgs/msg/int32.h>
#include <geometry_msgs/msg/point32.h>
#include <teensy_msgs/msg/galvo_state.h>

using namespace qindesign::network;

// ----------------------------- configuration --------------------------------
// DIRECT point-to-point link (PC Ethernet <-> Teensy), static IPs (no DHCP).
// AGENT_IP = the PC's Ethernet static IP; mirrored WSL networking exposes the
// agent there. Update all four if you change the subnet.
static IPAddress TEENSY_IP(192, 168, 10, 2);    // this Teensy
static IPAddress NETMASK  (255, 255, 255, 0);
static IPAddress GATEWAY  (192, 168, 10, 1);    // = the PC
static IPAddress AGENT_IP (192, 168, 10, 1);    // micro-ROS agent host (the PC)
static const uint16_t AGENT_PORT = 8888;
#define PUBLISH_HZ 1000              // target high-frequency publish rate

// ----------------------------- UDP transport --------------------------------
static EthernetUDP udp;

bool eth_open(uxrCustomTransport* /*t*/) {
  // Bind a local UDP port; we send from and listen on it (agent replies here).
  return udp.begin(AGENT_PORT) != 0;
}
bool eth_close(uxrCustomTransport* /*t*/) {
  udp.stop();
  return true;
}
size_t eth_write(uxrCustomTransport* /*t*/, const uint8_t* buf, size_t len, uint8_t* err) {
  if (!udp.beginPacket(AGENT_IP, AGENT_PORT)) { *err = 1; return 0; }
  size_t w = udp.write(buf, len);
  if (!udp.endPacket())                       { *err = 1; return 0; }
  return w;
}
size_t eth_read(uxrCustomTransport* /*t*/, uint8_t* buf, size_t len, int timeout_ms, uint8_t* err) {
  uint32_t start = millis();
  do {
    int n = udp.parsePacket();
    if (n > 0) {
      int r = udp.read(buf, len);
      return (r > 0) ? (size_t) r : 0;
    }
    yield();                          // let QNEthernet/lwIP process
  } while ((int32_t)(millis() - start) < timeout_ms);
  *err = 1;
  return 0;
}

// ----------------------------- ROS entities ---------------------------------
rcl_publisher_t    publisher;       // best-effort  -> teensy/galvo_state
rcl_publisher_t    publisher_rel;   // reliable     -> teensy/galvo_state_rel
rcl_publisher_t    pong_pub;        // best-effort  -> teensy/pong (RTT echo)
rcl_publisher_t    full_pub;        // best-effort  -> teensy/galvo_full (multi-signal)
rcl_subscription_t subscriber;
rcl_subscription_t rate_sub;
rcl_subscription_t mode_sub;
rcl_subscription_t ping_sub;        //              <- teensy/ping (RTT)
geometry_msgs__msg__Point32 send_msg;   // realistic galvo payload: x,y pos + seq in z
std_msgs__msg__Int32 recv_msg;
std_msgs__msg__Int32 rate_msg;
std_msgs__msg__Int32 mode_msg;
std_msgs__msg__Int32 ping_msg;
teensy_msgs__msg__GalvoState full_msg;
static uint32_t hf_seq = 0;
static int pub_mode = 0;            // 0 = BE Point32, 1 = reliable Point32, 2 = BE GalvoState

rclc_support_t  support;
rcl_allocator_t allocator;
rcl_node_t      node;
rclc_executor_t executor;

#define RCCHECK(fn) { rcl_ret_t rc = fn; if (rc != RCL_RET_OK) { return false; } }

#define EXECUTE_EVERY_N_MS(MS, X) do {              \
    static volatile int64_t _t = -1;                \
    if (_t == -1) { _t = millis(); }                \
    if ((int64_t) millis() - _t > (MS)) { X; _t = millis(); } \
  } while (0)

enum AgentState { WAITING_AGENT, AGENT_AVAILABLE, AGENT_CONNECTED, AGENT_DISCONNECTED } state;

static uint32_t pub_period_us = 1000;
static uint32_t last_pub_us   = 0;

void sub_cb(const void* msgin) {
  const std_msgs__msg__Int32* m = (const std_msgs__msg__Int32*) msgin;
  digitalWrite(LED_BUILTIN, (m->data & 1) ? HIGH : LOW);
}

// Runtime publish-rate control: set the hf_counter rate without reflashing.
void rate_cb(const void* msgin) {
  const std_msgs__msg__Int32* m = (const std_msgs__msg__Int32*) msgin;
  int32_t hz = m->data;
  if (hz < 1)      hz = 1;
  if (hz > 50000)  hz = 50000;          // sanity clamp (20 us floor)
  pub_period_us = 1000000UL / (uint32_t) hz;
}

// Select QoS for the high-rate publisher: 0 = best-effort, 1 = reliable.
void mode_cb(const void* msgin) {
  const std_msgs__msg__Int32* m = (const std_msgs__msg__Int32*) msgin;
  int v = m->data;
  if (v < 0) v = 0;
  if (v > 2) v = 2;
  pub_mode = v;
}

// RTT echo: bounce the ping value straight back on /teensy/pong (host times it).
void ping_cb(const void* msgin) {
  (void) rcl_publish(&pong_pub, msgin, NULL);
}

bool create_entities() {
  allocator = rcl_get_default_allocator();
  RCCHECK(rclc_support_init(&support, 0, NULL, &allocator));
  RCCHECK(rclc_node_init_default(&node, "teensy_eth_test", "", &support));

  // High-rate telemetry: BEST-EFFORT (the key throughput lever).
  RCCHECK(rclc_publisher_init_best_effort(
      &publisher, &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Point32), "teensy/galvo_state"));

  // Reliable variant (same payload) for the QoS comparison.
  RCCHECK(rclc_publisher_init_default(
      &publisher_rel, &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Point32), "teensy/galvo_state_rel"));

  // RTT echo publisher (best-effort = lowest latency).
  RCCHECK(rclc_publisher_init_best_effort(
      &pong_pub, &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), "teensy/pong"));

  // Multi-signal custom payload (best-effort).
  RCCHECK(rclc_publisher_init_best_effort(
      &full_pub, &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(teensy_msgs, msg, GalvoState), "teensy/galvo_full"));

  // Commands: reliable (low rate).
  RCCHECK(rclc_subscription_init_default(
      &subscriber, &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), "teensy/command"));

  // Runtime publish-rate control (reliable, low rate).
  RCCHECK(rclc_subscription_init_default(
      &rate_sub, &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), "teensy/set_rate_hz"));
  RCCHECK(rclc_subscription_init_default(
      &mode_sub, &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), "teensy/set_mode"));
  RCCHECK(rclc_subscription_init_best_effort(
      &ping_sub, &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), "teensy/ping"));

  // Executor services the subscriptions; publishing is done directly.
  executor = rclc_executor_get_zero_initialized_executor();
  RCCHECK(rclc_executor_init(&executor, &support.context, 4, &allocator));
  RCCHECK(rclc_executor_add_subscription(
      &executor, &subscriber, &recv_msg, &sub_cb, ON_NEW_DATA));
  RCCHECK(rclc_executor_add_subscription(
      &executor, &rate_sub, &rate_msg, &rate_cb, ON_NEW_DATA));
  RCCHECK(rclc_executor_add_subscription(
      &executor, &mode_sub, &mode_msg, &mode_cb, ON_NEW_DATA));
  RCCHECK(rclc_executor_add_subscription(
      &executor, &ping_sub, &ping_msg, &ping_cb, ON_NEW_DATA));
  return true;
}

void destroy_entities() {
  rmw_context_t* rmw_context = rcl_context_get_rmw_context(&support.context);
  (void) rmw_uros_set_context_entity_destroy_session_timeout(rmw_context, 0);
  rcl_publisher_fini(&publisher, &node);
  rcl_publisher_fini(&publisher_rel, &node);
  rcl_publisher_fini(&pong_pub, &node);
  rcl_publisher_fini(&full_pub, &node);
  rcl_subscription_fini(&subscriber, &node);
  rcl_subscription_fini(&rate_sub, &node);
  rcl_subscription_fini(&mode_sub, &node);
  rcl_subscription_fini(&ping_sub, &node);
  rclc_executor_fini(&executor);
  rcl_node_fini(&node);
  rclc_support_fini(&support);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Ethernet.begin(TEENSY_IP, NETMASK, GATEWAY);   // static (no DHCP on direct link)
  Ethernet.waitForLink(5000);                    // up to 5 s for physical link

  rmw_uros_set_custom_transport(
      false,                              // framing=false -> packet/UDP mode
      NULL,
      eth_open, eth_close, eth_write, eth_read);

  state = WAITING_AGENT;
  hf_seq = 0;
  send_msg.x = 0; send_msg.y = 0; send_msg.z = 0;
  pub_period_us = 1000000UL / PUBLISH_HZ;
  last_pub_us = micros();
}

void loop() {
  switch (state) {
    case WAITING_AGENT:
      EXECUTE_EVERY_N_MS(500,
        state = (RMW_RET_OK == rmw_uros_ping_agent(100, 1)) ? AGENT_AVAILABLE : WAITING_AGENT;);
      break;

    case AGENT_AVAILABLE:
      state = create_entities() ? AGENT_CONNECTED : WAITING_AGENT;
      if (state == WAITING_AGENT) destroy_entities();
      break;

    case AGENT_CONNECTED: {
      // Liveness only -- once per second, off the hot path.
      EXECUTE_EVERY_N_MS(1000,
        state = (RMW_RET_OK == rmw_uros_ping_agent(100, 1)) ? AGENT_CONNECTED : AGENT_DISCONNECTED;);
      if (state != AGENT_CONNECTED) break;

      // High-rate publish on a drift-free micros() cadence.
      uint32_t now = micros();
      if (now - last_pub_us >= pub_period_us) {
        last_pub_us += pub_period_us;
        // Resync if we've fallen a full period behind, so a stall (or a target
        // period near the loop time) can't accumulate a backlog and run away.
        if ((uint32_t)(now - last_pub_us) >= pub_period_us) last_pub_us = now;

        if (pub_mode == 2) {
          // multi-signal custom payload (teensy_msgs/GalvoState)
          full_msg.seq     = hf_seq;
          full_msg.galvo_x = (float) (hf_seq % 4096);
          full_msg.galvo_y = (float) (4095 - (hf_seq % 4096));
          full_msg.trigger = 0.0f;
          full_msg.temp_c  = 37.5f;
          full_msg.encoder = (int32_t) hf_seq;
          full_msg.clutch  = (uint8_t) (hf_seq & 1);
          full_msg.status  = 0;
          (void) rcl_publish(&full_pub, &full_msg, NULL);
        } else {
          // Point32: x,y = position pattern, z = sequence (drop detect)
          send_msg.x = (float) (hf_seq % 4096);
          send_msg.y = (float) (4095 - (hf_seq % 4096));
          send_msg.z = (float) hf_seq;
          (void) rcl_publish(pub_mode ? &publisher_rel : &publisher, &send_msg, NULL);
        }
        hf_seq++;
      }

      // Service inbound commands without blocking (0 timeout).
      rclc_executor_spin_some(&executor, RCL_MS_TO_NS(0));
      break;
    }

    case AGENT_DISCONNECTED:
      destroy_entities();
      state = WAITING_AGENT;
      break;
  }

  // Status LED: blink while not connected (command owns it when connected).
  if (state != AGENT_CONNECTED) {
    EXECUTE_EVERY_N_MS(300, digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)););
  }
}
