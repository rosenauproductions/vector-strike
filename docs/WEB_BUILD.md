# Vector Strike: web build (browser / Vercel)

The same C++ game compiled to WebAssembly with Emscripten. It runs in any modern browser with keyboard, mouse and USB/Bluetooth gamepads (up to 4 players, split screen like the box).

## What's in the repo
- `build_web.sh`: builds `web/dist/` (`index.html`, `vectorstrike.js`, `vectorstrike.wasm` ~1 MB, `vectorstrike.data` ~3.3 MB = the modelpack). `./build_web.sh --no-pack` leaves the modelpack out (built-in generic ships only).
- `web/index.html`: the page (start screen + controls hint). It is copied into `web/dist/` by the build.
- `web/dist/`: the built site, committed so Vercel needs no compiler; Vercel serves it as plain static files.
- `vercel.json`: no install/build step, serves `web/dist`.

## Code differences (all behind `#ifdef __EMSCRIPTEN__` in `src/main.cpp`)
- The Wii remote driver (`src/wii/`, raw Linux Bluetooth) is left out; stub functions take its place. Browsers can't open raw Bluetooth.
- The main loop body is `mainTick()`. The desktop build loops on it; the web build hands it to the browser with `emscripten_set_main_loop`.
- The page calls the exported `vs_start()` on the first click or key, because browsers only allow sound after the player interacts.
- The window follows the browser window (resizable canvas, no fullscreen-desktop flag, no HiDPI doubling).
- Options and high scores are saved in `/save/vectorstrike.cfg`, which is browser storage (IndexedDB, synced after each save). They survive reloads on the same browser.
- QUIT saves and reloads the page back to the start screen.

## Building
Emscripten with its SDL2 port (SDL 2.0.18 or newer, for `SDL_RenderGeometry`). With emsdk: `source emsdk_env.sh && ./build_web.sh`.

Cloud session recipe (26 Oct): the emsdk installer was blocked in the sandbox, so the build used Ubuntu's `apt-get install emscripten` (3.1.6) plus these workarounds:
- That package's cache is frozen and has no SDL2. Copy `/usr/share/emscripten/.emscripten` to a scratch file, set `FROZEN_CACHE = False`, add `CACHE = '<writable dir>'`, and point `EM_CONFIG` at the copy.
- The SDL2 port's GitHub zip no longer matches its pinned hash. Download the official `SDL2-2.0.20.tar.gz` from the libsdl-org GitHub releases (sha256 `c56aba1d7b5b0e7e999e4a7698c70b63a3394ff9704b5f6e1c57e0c16f04dd06`), unpack it, and build with `EMCC_LOCAL_PORTS="sdl2=<path>/SDL2-2.0.20"`.
- Example: `EM_CONFIG=... EMCC_LOCAL_PORTS=... ./build_web.sh`.
- Clang 15 names `main(argc, argv)` `__main_argc_argv`, which this old Emscripten drops. That is why the page starts the game through `vs_start()`, not `callMain`.

## Testing
Serve `web/dist` (`python3 -m http.server`) and open it in Chromium. Headless Playwright with `--use-angle=swiftshader` ran the title, the lobby and a level 1 fight at 60 fps (26 Oct).

## Deploying on Vercel
- **Live (26 Oct):** Vercel project `vector-strike` (team RosenauProductions), https://vector-strike-gamma.vercel.app. Deployed from this branch as production. Vercel Authentication is on for every URL (`ssoProtection: all`), so only logged-in team members can open it.
- Until `main` has the web build, a push to `main` would deploy a production site without `web/dist`. Merge this branch first, or redeploy from the branch.
- In Vercel: Add New, then Project, then import `rosenauproductions/vector-strike`. Leave Framework as "Other". `vercel.json` already sets the output folder.
- Pushes to `main` become production; other branches (like `claude/youthful-johnson-83rm5s`) get preview URLs.
- After a code change, run `./build_web.sh` and commit `web/dist` with it.
- **Before making it public:** the modelpack comes from Chris's own model files for personal use, and the ace is labelled DARTH VADER. A public URL hands those models to anyone who opens it. Either keep the deployment private (Vercel Deployment Protection: Vercel Authentication or a password), or publish a `--no-pack` build (generic built-in ships) and rename the ace.

## Not done / ideas
- Touch controls for phones/tablets (it needs a keyboard or pad now).
- Wii remotes in the browser: only through the WebHID API in Chrome. Possible, but untested and it needs its own driver.
- An AudioWorklet instead of SDL's ScriptProcessor audio (Chrome warns it is deprecated; it still works).
