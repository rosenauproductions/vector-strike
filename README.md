# Vector Strike

1983-style wireframe space assault for 1-4 players, split screen, for Chris's Batocera box (C++17 / SDL2).
Open-space dogfight with flight controls -> surface towers -> trench run -> exhaust port.

- `src/` game (main.cpp, gfx.h vector renderer, audio.h synth)
- `modelpack/` wireframes the game loads at run time (rebuild all with `tools/build_modelpack.sh`: `tools/glb2vsm.py` for ships, `tools/trench_tile.py` for the trench walls)
- `pkg/` Batocera launcher, keys file, menu images
- `HANDOVER.md` full notes: design, controls, test switches, deploy

## Build
```
pip install ziglang --break-system-packages; apt-get install libsdl2-dev
mkdir -p inc/SDL2 build && cp /usr/include/x86_64-linux-gnu/SDL2/_real_SDL_config.h inc/SDL2/
./build.sh        # -> build/vectorstrike.x86_64 (glibc 2.28 target, links system libSDL2)
```
Test: `VS_HOME=. VS_STATE=game VS_PLAYERS=4 VS_BOTS=1 VS_ADVANCE=600 SDL_VIDEODRIVER=dummy VS_MUTE=1 build/vectorstrike.x86_64`

## Deploy
Copy `build/vectorstrike.x86_64` (+ `modelpack/`; betas go in `dist-beta/` as `vectorstrike_beta.x86_64`) to `/userdata/roms/ports/vectorstrike/` and `pkg/VectorStrike.sh*` to `/userdata/roms/ports/`.
