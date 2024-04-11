cmake_minimum_required(VERSION 3.14)
project(topic_monitor)

find_package(ament_cmake_auto REQUIRED)

ament_auto_find_build_dependencies()

# Default to C++17
if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif ()

if (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wunused-function)
endif ()

find_package(rclcpp REQUIRED)
find_package(rosbag2_cpp REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(builtin_interfaces REQUIRED)

include_directories(
        include
        SYSTEM
        ${YAML_CPP_INCLUDE_DIRS}
)
link_libraries(${YAML_CPP_LIBRARIES})

ament_auto_add_library(topic_monitor_component SHARED
                       src/topic_monitor_node.cpp)

rclcpp_components_register_node(
    topic_monitor_component
    PLUGIN "RosbagCheckerLiveNode"
    EXECUTABLE topic_monitor
)

ament_auto_package(
        INSTALL_TO_SHARE
        launch
)

set(ROS_DISTRO $ENV{ROS_DISTRO})
if(${ROS_DISTRO} STREQUAL "rolling")
    add_compile_definitions(ROS_DISTRO_ROLLING)
elseif(${ROS_DISTRO} STREQUAL "foxy")
    add_compile_definitions(ROS_DISTRO_FOXY)
elseif(${ROS_DISTRO} STREQUAL "galactic")
    add_compile_definitions(ROS_DISTRO_GALACTIC)
elseif(${ROS_DISTRO} STREQUAL "humble")
    add_compile_definitions(ROS_DISTRO_HUMBLE)
endif()