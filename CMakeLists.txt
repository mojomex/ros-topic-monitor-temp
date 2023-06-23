cmake_minimum_required(VERSION 3.8)
project(topic_monitor)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rosbag2_cpp REQUIRED)
find_package(yaml-cpp REQUIRED)

add_executable(topic_monitor src/topic_monitor_node.cpp)
ament_target_dependencies(topic_monitor rclcpp rosbag2_cpp yaml-cpp)

install(TARGETS
  topic_monitor
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
