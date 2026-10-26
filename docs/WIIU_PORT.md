# Vector Strike: possible Wii U port (notes only, not started)

Chris asked (26 Oct) whether the game could run on a Wii U. Short answer: yes; it is moderate work. Frame rate isn't a worry yet: drop to 720p and/or shorten the draw distance if needed.

## Why it's feasible
- About 4,000 lines of C++17 on SDL2 only; drawing goes through SDL's 2D renderer (`SDL_RenderGeometry`), not raw GL.
- The Wii U homebrew toolchain (devkitPro: `devkitPPC` + `wut`, Aroma environment) has an SDL2 port with a GX2 renderer, audio and controllers.
- Models are text `.vsm` files, so the Wii U's big-endian CPU doesn't matter for loading them.
- The web build (docs/WEB_BUILD.md) already proves the platform split: one `#ifdef` for the Wii driver, and the loop in `mainTick()`.

## Work list
1. **Build**: a `build_wiiu.sh` / CMake using devkitPro's `wut` + `wiiu-sdl2`, output a `.wuhb` (icon, title) to copy to `sd:/wiiu/apps/`. Add `#ifdef __WIIU__` next to the existing `__EMSCRIPTEN__` switches.
2. **Wii remotes**: replace `src/wii/` (Linux Bluetooth) with the console's own KPAD API. It gives buttons plus the IR pointer position directly, so `wiiRead()` gets simpler (no dot pairing, no roll maths). Pairing uses the console's sync button. Nunchuk/Classic Controller come along for free.
3. **GamePad + Pro Controllers**: VPAD (GamePad) and Pro Controllers (through KPAD or SDL's game controller layer) map to the normal pad controls. Gyro on the GamePad could feed the existing gyro aim.
4. **GamePad screen** (optional, nice): show the 3D radar / squadron board, or give player 1 their own full view on the GamePad and split the TV between players 2-4.
5. **Files**: read `modelpack/` and save `vectorstrike.cfg` on the SD card (`fs:/vol/external01/...`) or bundle the pack in the `.wuhb` content folder.
6. **Video**: 720p by default (1080p as an option). Use the existing far models sooner and cut the line budget per model (`TRENCH_LINES`, far-model distance) if it drops below 30 fps.
7. **Things to drop**: the Batocera launcher bits, `SDL_WINDOW_FULLSCREEN_DESKTOP` (the console is always fullscreen), Alt+F4.

## Risks / unknowns
- CPU: 3 x PowerPC 750 at 1.24 GHz, much slower per core than the Batocera PC. The game builds every line on the CPU each frame. 1-2 players at 720p is likely fine; 4 players in the fleet battles and on Ice Planet may need the lower-detail options above.
- The SDL2 Wii U port's `SDL_RenderGeometry` support and speed need a check on real hardware (fallback: draw the line quads straight with GX2).
- Testing needs Chris's console (Aroma installed); the cloud session can build but not run it. Cemu (the emulator) can run `.wuhb` homebrew for a first look.
