cmake_minimum_required(VERSION 3.8)
project(teensy_bench)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(teensy_msgs REQUIRED)

add_executable(rate_meter src/rate_meter.cpp)
ament_target_dependencies(rate_meter rclcpp geometry_msgs)

add_executable(rate_meter_full src/rate_meter_full.cpp)
ament_target_dependencies(rate_meter_full rclcpp teensy_msgs)

add_executable(rtt_ping src/rtt_ping.cpp)
ament_target_dependencies(rtt_ping rclcpp std_msgs)

install(TARGETS rate_meter rate_meter_full rtt_ping DESTINATION lib/${PROJECT_NAME})

ament_package()
