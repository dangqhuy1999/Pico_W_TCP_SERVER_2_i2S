#!/bin/bash
rm -rf build/
mkdir build
cd build
sleep 2
cmake -DPICO_BOARD=pico_w ..