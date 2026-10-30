#!/bin/bash
# Web build: C++ -> WebAssembly with Emscripten, output in web/dist/ (a static site: index.html + .js + .wasm + .data).
# Needs emcc with its SDL2 port (-sUSE_SDL=2). See docs/WEB_BUILD.md.
set -e
cd "$(dirname "$0")"
mkdir -p web/dist
PACK="--preload-file modelpack@/modelpack"
[ "$1" = "--no-pack" ] && PACK=""   # built-in generic ships only
em++ -O2 -std=c++17 -Wno-unused-function -Wno-misleading-indentation -Isrc src/main.cpp \
  -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728 -sEXPORTED_FUNCTIONS=_vs_start,_vs_tvkey -sEXPORTED_RUNTIME_METHODS=FS -lidbfs.js -sFORCE_FILESYSTEM=1 \
  -sENVIRONMENT=web $PACK -o web/dist/vectorstrike.js
cp web/index.html web/dist/index.html
ls -la web/dist
