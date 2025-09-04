#!/bin/bash
cd build
sleep 2
make -j4 &> ../make.log
