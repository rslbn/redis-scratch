#!/bin/bash
set -e

if [ ! -d "build" ]; then
    mkdir -p build
fi

g++ src/server.cpp src/utils.cpp src/hashtable.cpp -I include/ -o build/server

./build/server