#!/usr/bin/bash
source ~/.bashrc
source install/setup.bash
export RCUTILS_CONSOLE_OUTPUT_FORMAT="{message}"
$@
