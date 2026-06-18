#include <Arduino.h>
#include <micro_ros_platformio.h>

#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>

#include <std_msgs/msg/bool.h>
#include <std_msgs/msg/empty.h>
#include <geometry_msgs/msg/point32.h>

#include <Adafruit_NeoPixel.h>
#include <Adafruit_MCP4728.h>
#include <Wire.h>

#include "pin_defs.h"
#include "utils.h"

#include "test_function.h" // example including lib function

#define RCCHECK(fn) { rcl_ret_t temp_rc = fn; if((temp_rc != RCL_RET_OK)){error_loop();}}
#define RCSOFTCHECK(fn) { rcl_ret_t temp_rc = fn; if((temp_rc != RCL_RET_OK)){}}

#define DURATION_MS_TIMEOUT_GALVO 30*1000  // 30 sec
#define INTERVAL_MS_HEARTBEAT 100          // 100 msec

// galvo related
uint16_t galvo_pos[] = {0, 0};
bool new_galvo_pos_msg = false;
uint32_t last_command_time_ms = 0;

// auto footswitch related
bool new_fire_msg = false;
bool trigger_laser = false;
bool clutch_engaged = false;
uint32_t fire_duration_ms = 0;
uint32_t last_trigger_time_ms = 0;
bool is_triggering = false;
bool stop_flag = false;

// hearbeat related
uint32_t last_hearbeat_time_ms = 0;

// ROS Related
rcl_publisher_t heartbeat_pub;
rcl_publisher_t trigger_laser_pub;
rcl_publisher_t clutch_state_pub;  // NEW
rcl_subscription_t trigger_laser_sub;
rcl_subscription_t stop_flag_sub;
rcl_subscription_t galvo_pos_sub;

std_msgs__msg__Empty heartbeat_msg;
geometry_msgs__msg__Point32 trigger_laser_msg;
std_msgs__msg__Bool stop_flag_msg;
std_msgs__msg__Bool clutch_state_msg;  // NEW
geometry_msgs__msg__Point32 galvo_pos_msg;

rclc_executor_t executor;
rclc_support_t support;
rcl_allocator_t allocator;
rcl_node_t node;
rcl_timer_t timer;

// Error handle loop
void error_loop() {
  //while(1) {  // blocking error loop
    
    {
    delay(1000);
    setStripYellow();
    delay(1000);
    strip.clear();
    strip.show();
  }
}

void timer_callback(rcl_timer_t* timer, int64_t last_call_time) {
  (void) last_call_time;

  // galvo related
  if (new_galvo_pos_msg) {  // check ifi new command
    dac.setChannelValue(MCP4728_CHANNEL_A, galvo_pos[0]);
    dac.setChannelValue(MCP4728_CHANNEL_B, galvo_pos[1]);
    new_galvo_pos_msg = false;
    last_command_time_ms = millis();
  }
  if (millis() - last_command_time_ms >= DURATION_MS_TIMEOUT_GALVO) {   // check if timeout reached
    galvo_pos[0] = 2048; // center
    galvo_pos[1] = 2048;
    dac.setChannelValue(MCP4728_CHANNEL_A, galvo_pos[0]);
    dac.setChannelValue(MCP4728_CHANNEL_B, galvo_pos[1]);
  }

  // auto footswitch related
  clutch_engaged = digitalRead(CLUTCH_PIN);

  // Publish clutch_engaged state
  clutch_state_msg.data = clutch_engaged;
  rcl_publish(&clutch_state_pub, &clutch_state_msg, NULL);

  if (new_fire_msg) { // check if new msg
    if (trigger_laser) {
      last_trigger_time_ms = millis();
      is_triggering = true;
      digitalWrite(TRIG_PIN, HIGH);
    } else {
      digitalWrite(TRIG_PIN, LOW);
      is_triggering = false;
    }
    new_fire_msg = false;
  }
  if (is_triggering && (millis() - last_trigger_time_ms >= fire_duration_ms)) { // check if timeout reached
    digitalWrite(TRIG_PIN, LOW);
    trigger_laser_msg.x = 0.0;
    trigger_laser_msg.y = 0.0;
    trigger_laser_msg.z = 1.0;
    rcl_publish(&trigger_laser_pub, &trigger_laser_msg, NULL);
    is_triggering = false;
    trigger_laser = false;
  }
  if (is_triggering && !clutch_engaged) { // check if clutch comes up
    digitalWrite(TRIG_PIN, LOW);
    trigger_laser_msg.x = 0.0;
    trigger_laser_msg.y = 0.0;
    trigger_laser_msg.z = 2.0;
    rcl_publish(&trigger_laser_pub, &trigger_laser_msg, NULL);
    is_triggering = false;
    trigger_laser = false;
  }
  if (stop_flag) {  // check for tx end/e-stop
    digitalWrite(TRIG_PIN, LOW);
    trigger_laser_msg.x = 0.0;
    trigger_laser_msg.y = 0.0;
    trigger_laser_msg.z = 3.0;
    rcl_publish(&trigger_laser_pub, &trigger_laser_msg, NULL);
  }
  // if (millis() - last_hearbeat_time_ms >= INTERVAL_MS_HEARTBEAT) {  // heartbeat
  //   last_hearbeat_time_ms = millis();
  //   rcl_ret_t ret = rcl_publish(&heartbeat_pub, &heartbeat_msg, NULL);
  //   if (ret != RCL_RET_OK) { // detect if connected to ROS network
  //     setStripBlue();
  //   } else {
  //     setStripRed();
  //     if (!is_triggering){
  //       digitalWrite(TRIG_PIN, LOW);
  //     }
  //   }
  // }

  if (millis() - last_hearbeat_time_ms >= INTERVAL_MS_HEARTBEAT) {  // heartbeat
    last_hearbeat_time_ms = millis();
    rcl_ret_t ret = rcl_publish(&heartbeat_pub, &heartbeat_msg, NULL);
    if (ret != RCL_RET_OK) { // detect if connected to ROS network
      setStripRed();
      digitalWrite(TRIG_PIN, LOW);
    } else {
      setStripBlue();
    }
  }
}
void stop_flag_sub_callback(const void* msg_in) {
  const std_msgs__msg__Bool* msg = (const std_msgs__msg__Bool*) msg_in;
  stop_flag = msg->data;
}
void trigger_laser_sub_callback(const void* msg_in) {
  const geometry_msgs__msg__Point32* msg = (const geometry_msgs__msg__Point32*) msg_in;
  trigger_laser = msg->x;
  fire_duration_ms = uint32_t(msg->y);
  new_fire_msg = true;
}
void galvo_pos_sub_callback(const void* msg_in) {
  const geometry_msgs__msg__Point32* msg = (const geometry_msgs__msg__Point32*) msg_in;
  if (msg->x >= 0 && msg->x <= 4095 && msg->y >= 0 && msg->y <= 4095) {
    galvo_pos[0] = int(msg->x);
    galvo_pos[1] = int(msg->y);
    new_galvo_pos_msg = true;
  }
}

void setup() {
  set_microros_serial_transports(Serial);
  
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(CLUTCH_PIN, INPUT);
  
  while (!dac.begin()) {
    error_loop(); // blink led if cannot find DAC
  }
  
  strip.begin();
  strip.show();

  delay(500);

  allocator = rcl_get_default_allocator();

  rclc_support_init(&support, 0, NULL, &allocator);
  rclc_node_init_default(&node, "bcc_hardware_interface", "", &support);
  rclc_publisher_init_default(&heartbeat_pub, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Empty), "hardware_heartbeat");
  rclc_publisher_init_default(&trigger_laser_pub, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Point32), "trigger_laser");

  rclc_publisher_init_default(&clutch_state_pub, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Bool), "clutch_engaged");  // NEW

  rclc_subscription_init_default(&trigger_laser_sub, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Point32), "trigger_laser");
  rclc_subscription_init_default(&stop_flag_sub, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Bool), "stop_flag");
  rclc_subscription_init_default(&galvo_pos_sub, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Point32), "galvo_pos");
  rclc_timer_init_default(&timer, &support, RCL_MS_TO_NS(10), timer_callback);

  rclc_executor_init(&executor, &support.context, 4, &allocator);
  rclc_executor_add_subscription(&executor, &trigger_laser_sub, &trigger_laser_msg, &trigger_laser_sub_callback, ON_NEW_DATA);
  rclc_executor_add_subscription(&executor, &galvo_pos_sub, &galvo_pos_msg, &galvo_pos_sub_callback, ON_NEW_DATA);
  rclc_executor_add_subscription(&executor, &stop_flag_sub, &stop_flag_msg, &stop_flag_sub_callback, ON_NEW_DATA);
  rclc_executor_add_timer(&executor, &timer);

}

void loop() {
  delayMicroseconds(10);
  rclc_executor_spin_some(&executor, RCL_MS_TO_NS(1));
  // TODO: detect when node loses connection and retry broadcasting connection; indicating with leds to show status
}

