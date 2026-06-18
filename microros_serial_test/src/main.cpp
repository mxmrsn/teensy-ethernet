// =============================================================================
// Minimal micro-ROS node over SERIAL -- Teensy 4.1 comms test (reconnecting)
// -----------------------------------------------------------------------------
// Validates the full Teensy <-> micro_ros_agent <-> ROS2 (Jazzy) path with the
// least possible firmware: a counter publisher and a command subscriber. No DAC,
// no NeoPixel, no external libraries -- runs with nothing wired up but USB.
//
// Unlike the textbook example, this uses the standard micro-ROS RECONNECTION
// state machine (rmw_uros_ping_agent). That means:
//   * boot order doesn't matter -- the Teensy waits for the agent to appear
//   * if the agent restarts, the Teensy tears down and re-creates its entities
// A one-shot init (RCCHECK -> error_loop) would instead hang forever if the
// agent wasn't already running at boot. Essential for a real peripheral.
//
// Topics (once connected):
//   teensy/counter   std_msgs/Int32   published at 10 Hz (incrementing)
//   teensy/command   std_msgs/Int32   subscribed; LED reflects last value's LSB
//
// Verify on the host:
//   ros2 topic echo /teensy/counter
//   ros2 topic pub /teensy/command std_msgs/msg/Int32 "{data: 1}" -1
//
// LED: blinks while waiting for / disconnected from the agent; once connected it
// follows the last command's low bit (so `ros2 topic pub` visibly toggles it).
// =============================================================================

#include <Arduino.h>
#include <micro_ros_platformio.h>

#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>
#include <std_msgs/msg/int32.h>

rcl_publisher_t    publisher;
rcl_subscription_t subscriber;
std_msgs__msg__Int32 send_msg;     // outgoing counter
std_msgs__msg__Int32 recv_msg;     // incoming command

rclc_support_t  support;
rcl_allocator_t allocator;
rcl_node_t      node;
rcl_timer_t     timer;
rclc_executor_t executor;

// Return false (not error_loop) on failure so the caller can retry the agent.
#define RCCHECK(fn)     { rcl_ret_t rc = fn; if (rc != RCL_RET_OK) { return false; } }
#define RCSOFTCHECK(fn) { rcl_ret_t rc = fn; (void) rc; }

// Run X at most once per MS milliseconds (non-blocking cadence).
#define EXECUTE_EVERY_N_MS(MS, X) do {              \
    static volatile int64_t init = -1;              \
    if (init == -1) { init = millis(); }            \
    if ((int64_t) millis() - init > (MS)) { X; init = millis(); } \
  } while (0)

enum AgentState { WAITING_AGENT, AGENT_AVAILABLE, AGENT_CONNECTED, AGENT_DISCONNECTED } state;

// 10 Hz: publish the counter, then increment.
void timer_callback(rcl_timer_t* timer, int64_t last_call_time) {
  (void) last_call_time;
  if (timer != NULL) {
    RCSOFTCHECK(rcl_publish(&publisher, &send_msg, NULL));
    send_msg.data++;
  }
}

// Command received from ROS2: mirror the low bit to the onboard LED.
void subscription_callback(const void* msgin) {
  const std_msgs__msg__Int32* msg = (const std_msgs__msg__Int32*) msgin;
  digitalWrite(LED_BUILTIN, (msg->data & 1) ? HIGH : LOW);
}

// Create all ROS entities. Returns false on any failure (-> retry).
bool create_entities() {
  allocator = rcl_get_default_allocator();
  RCCHECK(rclc_support_init(&support, 0, NULL, &allocator));
  RCCHECK(rclc_node_init_default(&node, "teensy_serial_test", "", &support));
  RCCHECK(rclc_publisher_init_default(
      &publisher, &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), "teensy/counter"));
  RCCHECK(rclc_subscription_init_default(
      &subscriber, &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), "teensy/command"));
  RCCHECK(rclc_timer_init_default(&timer, &support, RCL_MS_TO_NS(100), timer_callback));

  executor = rclc_executor_get_zero_initialized_executor();
  RCCHECK(rclc_executor_init(&executor, &support.context, 2, &allocator));
  RCCHECK(rclc_executor_add_timer(&executor, &timer));
  RCCHECK(rclc_executor_add_subscription(
      &executor, &subscriber, &recv_msg, &subscription_callback, ON_NEW_DATA));
  return true;
}

void destroy_entities() {
  rmw_context_t* rmw_context = rcl_context_get_rmw_context(&support.context);
  (void) rmw_uros_set_context_entity_destroy_session_timeout(rmw_context, 0);

  rcl_publisher_fini(&publisher, &node);
  rcl_subscription_fini(&subscriber, &node);
  rcl_timer_fini(&timer);
  rclc_executor_fini(&executor);
  rcl_node_fini(&node);
  rclc_support_fini(&support);
}

void setup() {
  Serial.begin(115200);
  set_microros_serial_transports(Serial);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  state = WAITING_AGENT;
  send_msg.data = 0;
}

void loop() {
  switch (state) {
    case WAITING_AGENT:
      // Poll for the agent every 500 ms (1 ping attempt, 100 ms timeout).
      EXECUTE_EVERY_N_MS(500,
        state = (RMW_RET_OK == rmw_uros_ping_agent(100, 1)) ? AGENT_AVAILABLE : WAITING_AGENT;);
      break;

    case AGENT_AVAILABLE:
      state = create_entities() ? AGENT_CONNECTED : WAITING_AGENT;
      if (state == WAITING_AGENT) destroy_entities();   // clean up partial init
      break;

    case AGENT_CONNECTED:
      // Detect agent loss every 200 ms (3 ping attempts).
      EXECUTE_EVERY_N_MS(200,
        state = (RMW_RET_OK == rmw_uros_ping_agent(100, 3)) ? AGENT_CONNECTED : AGENT_DISCONNECTED;);
      if (state == AGENT_CONNECTED) {
        rclc_executor_spin_some(&executor, RCL_MS_TO_NS(100));
      }
      break;

    case AGENT_DISCONNECTED:
      destroy_entities();
      state = WAITING_AGENT;
      break;
  }

  // Status LED: blink while not connected (command callback owns it when connected).
  if (state != AGENT_CONNECTED) {
    EXECUTE_EVERY_N_MS(300, digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)););
  }
}
