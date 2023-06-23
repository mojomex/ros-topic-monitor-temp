# Rosbag Checker

## Description
A ROS2 package to monitor the message count and message frequency of given topic(s). 

### Input:
1. A `yaml` file containing a list of topics, and optionally, frequency requirements ([see example yaml file here for format](https://github.com/bgtier4/rosbag-checker/blob/cpp/input_yaml_format.yaml)).

### Output:
The given list of topics with number of messages recorded by the rosbag, and message frequency.
- Green output indicates that messages corresponding to that topic were found and frequency requirements were met.
- Yellow output indicates that messages corresponding to that topic were found but frequency requirements were NOT met.
- Red output indicates that no messages were found corresponding to that topic.
Output is written to the terminal every `update interval` (default = 1000ms)

## Requirements
- ROS2 Humble
- C++
- `yaml-cpp` C++ library

## How to Install and Run
1. Download repository from github:

```bash
mkdir -p ~/ros2_ws/src
cd ~/ros2_ws/src
git clone <REPO_URL>
```

2. Build and source package

```bash
cd ~/ros2_ws
colcon build
source ~/ros2_ws/install/setup.bash
```

3. Run topic monitor

```bash
ros2 run topic_monitor topic_monitor --ros-args -p topic_list:=<PATH TO INPUT YAML FILE>
```

## List of Parameters and Descriptions
- `topic_list`: path to yaml file containing lists of topics and optionally frequency requirements
- `update_interval`: frequency to output updated statistics (milliseconds) (`default: 1000`)