# Vector Strike — notes for Claude sessions

Chris's 1983-style wireframe space shooter (C++17 / SDL2) for his Batocera box, 1-4 players split screen.
Read `HANDOVER.md` first: design, controls, version log (v1-v7), test switches (`VS_*`), deploy steps, md5s.

- Build: see README.md (`pip install ziglang --break-system-packages`, `apt-get install libsdl2-dev xvfb`, then `./build.sh`).
- Test headless with bots: `VS_HOME=. VS_STATE=game VS_PLAYERS=4 VS_BOTS=1 VS_ADVANCE=600 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy VS_MUTE=1 build/vectorstrike.x86_64`
  (copy `modelpack/` into `build/` first). Screenshots: xvfb-run + `VS_SHOT=frame VS_SHOT_PATH=x.bmp VS_WINDOW=1920x1080`.
- Deploy: needs the chat linked to Chris's Mac; the Batocera share is mounted there (recently `/Volumes/share-1/roms`).
  Binary -> `roms/ports/vectorstrike/vectorstrike.x86_64` (keep a `vectorstrike_prev_<md5>.x86_64` backup), verify with md5sum.
- `modelpack/*.vsm` and `assets_src/*.glb` come from Chris's own model files (personal use); the game binary has no franchise content and the title stays "Vector Strike".
- After a change: update HANDOVER.md, commit, push; also refresh the Game Dev project docs `claude/vector-strike-handover.md` and `claude/vector-strike-source-bundle.txt` when the project is attached.
