#!/bin/bash
cd "$(dirname "$0")"
python3 -m ziglang c++ -target x86_64-linux-gnu.2.28 -O2 -std=c++17 -Wall -Wno-unused-function -I/usr/include/SDL2 -Iinc -D_REENTRANT src/main.cpp /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0 -o build/vectorstrike.x86_64
