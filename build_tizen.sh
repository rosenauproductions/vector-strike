#!/bin/bash
# Samsung TV (Tizen) build: the web version packed as a Tizen web app. Output: tizen/app/ (a Tizen Studio web project)
# and tizen/VectorStrike-unsigned.zip. Everything is in ONE .js file (wasm and the modelpack inlined), because TV web
# engines can be fussy about fetching .wasm / .data files from the app's own folder. See docs/TV_BUILDS.md.
set -e
cd "$(dirname "$0")"
mkdir -p tizen/app
em++ -O2 -std=c++17 -Wno-unused-function -Wno-misleading-indentation -Isrc src/main.cpp \
  -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728 -sEXPORTED_FUNCTIONS=_vs_start,_vs_tvkey -sEXPORTED_RUNTIME_METHODS=FS -lidbfs.js -sFORCE_FILESYSTEM=1 \
  -sENVIRONMENT=web -sSINGLE_FILE=1 --embed-file modelpack@/modelpack -o tizen/app/vectorstrike.js
cp tizen/src/index.html tizen/src/config.xml tizen/src/icon.png tizen/app/
(cd tizen/app && rm -f ../VectorStrike-unsigned.zip && zip -q -r ../VectorStrike-unsigned.zip .)
ls -la tizen/app tizen/VectorStrike-unsigned.zip
