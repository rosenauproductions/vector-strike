#!/bin/bash
# Web build: C++ -> WebAssembly with Emscripten, output in web/dist/ (a static site: index.html + .js + .wasm + .data).
# Needs emcc with its SDL2 port (-sUSE_SDL=2). See docs/WEB_BUILD.md.
set -e
cd "$(dirname "$0")"
OUT=web/dist
PACK="--preload-file modelpack@/modelpack"
[ "$1" = "--no-pack" ] && PACK=""   # built-in generic ships only
# --sounds: also pack Chris's sound files (sounds_pack/, never committed) into web/dist-sounds/ (gitignored).
# That folder is what goes to the live Vercel site, uploaded straight from here (see docs/WEB_BUILD.md).
[ "$1" = "--sounds" ] && { OUT=web/dist-sounds; PACK="$PACK --preload-file sounds_pack@/sounds"; }
mkdir -p $OUT
em++ -O2 -std=c++17 -Wno-unused-function -Wno-misleading-indentation -Isrc src/main.cpp \
  -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728 -sEXPORTED_FUNCTIONS=_vs_start,_vs_tvkey -sEXPORTED_RUNTIME_METHODS=FS -lidbfs.js -sFORCE_FILESYSTEM=1 \
  -sENVIRONMENT=web $PACK -o $OUT/vectorstrike.js
cp web/index.html $OUT/index.html
ls -la $OUT
