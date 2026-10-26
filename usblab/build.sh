#!/bin/bash
# USB Lab build: same toolchain as Vector Strike (zig c++, glibc 2.28 target, system libSDL2)
cd "$(dirname "$0")"
mkdir -p build inc/SDL2
[ -f inc/SDL2/_real_SDL_config.h ] || cp /usr/include/x86_64-linux-gnu/SDL2/_real_SDL_config.h inc/SDL2/
python3 -m ziglang c++ -target x86_64-linux-gnu.2.28 -O2 -std=c++17 -Wall -Wno-unused-function -I/usr/include/SDL2 -Iinc -D_REENTRANT main.cpp /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0 -o build/usblab.x86_64
