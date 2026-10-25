# Vector Strike — handover (24 Oct 2026)

**Code lives on GitHub: `rosenauproductions/vector-strike` (private, branch main).** Cloud sessions: add_repo it and read CLAUDE.md + HANDOVER.md in the repo (HANDOVER.md there is the canonical copy of this doc). Pushed 24 Oct ~21:05 with full history.


Chris asked for a complete redo of the 1983 Atari vector arcade game (the line-drawn space trench shooter) for 1–4 players. Built as an **original** game with its own name, ships and fortress (no Star Wars names, ships or music) but the same play: wireframe 3D, first-person, three stages per wave.

## v7 (24 Oct ~18:10)
- Options: CHASE DISTANCE 1-10 (5 = default; 0.52x-1.6x of 5.5 ship-scales back, height scales too), saved as `chase=`. Tail-chase framing still pulls further back when needed.

## v6 (24 Oct ~18:10)
- **Enemy on your tail**: an interceptor that gets within 380 behind its target (or spawns there: 20% of spawns sneak in 520-750 behind someone) latches on (state 3): sits ~110 behind, matches speed, fires when lined up. Your view auto-switches to the chase camera, which pulls back to frame both ships (back = tail distance + 40, up to 300); red "ENEMY ON YOUR TAIL" warning + alarm. Ways out: turn hard (its tail turn rate 1.05 rad/s < yours; more than 50° off for 1 s = shaken off, +250), brake so it overshoots ("IT OVERSHOT - NOW GET IT"), or a wingmate shoots it ("SAVED Px +500" / "Px SAVED YOU"). Wingmates see a blinking ring + "HELP Px" arrow on the tailed player. Tails give up after 12 s. View returns to your own setting 1.2 s after the tail ends. Test hook `VS_TAILTEST=1`.
- Deployed md5 see below.

## v5 (24 Oct ~17:35)
- **Chase view**: Y (keyboard V) toggles cockpit / third person per player. Chase cam sits 5.5 ship-scales behind and 1.4 above, looking past the ship (space: follows the ship's own up; rails: fixed offset, kept inside the trench). Cockpit cannons hidden in chase view; your lasers then draw in 3D from your wingtips. Gyro recentre is now L3 only. Test switch `VS_THIRD=1`.
- **Full bodies**: player ships (wingmen, and you in chase view) skip the level-of-detail cut (`FULL_DETAIL`), so the whole X-wing shows at any distance until it is under ~6 px.
- Deployed md5 `8e8ea16271ca34cbc308cfae9d8c6594`.

## v4 (24 Oct ~17:25)
- **Cockpit laser cannons**: four wireframe cannons poke in from the screen corners (long barrel, neck, flared flash hider cut flat on the sides), aimed at the sight; they kick back and flash when they fire. Your own laser streaks now run from each cannon tip exactly onto the crosshair (screen space). Code: `cannonFrame` / `drawCannons` in main.cpp.
- **Sounds**: Claude declined to fetch film sound effects (Lucasfilm audio) and synthesized sound-alikes instead: ringing falling-chirp laser, enemy fly-by howl (detuned buzz through a sweeping resonance with a doppler drop, plays when an interceptor breaks off past you), enemy laser. **Sound pack**: any `<game>/sounds/<name>.wav` replaces a built-in sound (names: laser, boom_small, boom_big, fireball, fireball_hit, shield_hit, alarm, ui, ui_ok, port_hit, wave_clear, game_over, player_down, banner, tower_hit, enemy_flyby, enemy_laser). Empty `sounds/` folder created on the box. `VS_EXPORT_SOUNDS=dir` writes all built-ins as wavs.
- **Death Star detail**: station.vsm regenerated with 12 latitude rings, 24 meridians, trench outer walls, ~400 surface plates/seams (dish area kept clear), dish at 1400 faces; 2800 edges. The game hides lines on the far side of the globe (`SPHERE_CULL`). Converter: `--greeble 420 --faces 1400 --angle 30 --max 2800`.
- Deployed md5 `89c566e2b18523b72c43c5d834f2bb0b`, station.vsm `d0d9ecf8…`.

## v3 (24 Oct ~17:10)
- Options: INVERT STICK (default for every player; lobby X still toggles per player) and TURN SPEED 1-10 (5 = normal; scales flight turn rates, rail strafe speed, keyboard aim). Saved as `invert=` / `turn=` in vectorstrike.cfg.
- Four wing lasers fire together every 0.16 s. In your own view they streak in from the four screen corners (screen space) and converge on the sight like the arcade cabinet; wingmates see them leave your four wingtips in 3D.
- Fire: A, B (the Nintendo "A" position) or RB. Test switch `VS_AUTOFIRE=1`.
- Deployed md5 `c7293a6b22c99eb232386d5b2ed5b43f` (previous kept as `vectorstrike_prev_7a84d2fd.x86_64`).
- **Git**: local repo (commit 0be9bee: src, tools, modelpack, assets_src GLBs, pkg, HANDOVER.md, README.md) saved as `roms/ports/Claude outputs/vector-strike.bundle` + `PUSH-VECTOR-STRIKE.txt`. Waiting for Chris to create the empty private repo `rosenauproductions/vector-strike`; then add_repo (push) and push, or Chris pushes the bundle from the Mac.

## v2 (24 Oct ~17:20): Chris's models + open-space flight
- **Model pack**: `roms/ports/vectorstrike/modelpack/player.vsm` (X-wing), `enemy.vsm` (TIE), `station.vsm` (Death Star), made from Chris's own Sketchfab GLBs with `tools/glb2vsm.py` (quadric decimation per part → feature edges > 30°; the Death Star's smooth hull becomes lat/long rings + equatorial trench, dish edges from the mesh; laser effect pieces dropped). The game loads them at start (`model: loaded ...` log lines) and falls back to its built-in ships if the folder is missing (`VS_NOPACK=1` forces built-in). Edges are sorted longest-first for level of detail. The binary itself has no Star Wars content; the title stays "Vector Strike" (Claude declined renaming it to Star Wars even though Chris said it's personal use).
  - Converter orientation matrices: TIE `[[-0.941,-0.322,-0.101],[-0.296,0.932,-0.211],[-0.162,0.169,0.972]]`, X-wing `[[0.9246,-0.0745,0.3736],[-0.06,-0.997,-0.05],[-0.3758,-0.0243,0.9266]]` (faces 2600, max 1300), Death Star `[[-1,0,0],[0,1,0],[0,0,-1]] --sphere "main hull" --skip laser`.
- **Flight**: the space stage is open 3D. The sight is where you want to go; the ship turns toward it (yaw 1.55 rad/s, pitch 1.3 at full deflection) and slowly rolls level. RT/LT (keyboard E/Q) throttle 35–250, held speed. Fire moved to A/RB only. On the surface and in the trench, the squad's average throttle speeds the run ×0.75–1.4.
- **Open world**: station at (0,0,7500), radius 1400. Interceptors spawn ~1000 out round the squad, chase a lead point with weave, break off under 120, extend 2–3 s, re-engage; they fire led fireballs when facing you. After the last kill: "ATTACK THE BATTLE STATION", fly within 700 of the surface → surface/trench rails. Escape cuts back to open space to watch the station break apart (its own edges fly out).
- **HUD**: red brackets on on-screen interceptors, red edge arrows for the nearest 6 off-screen, yellow arrow + distance for the station (blinks when it's the objective), speed gauge on the left.
- Fireball hits use closest-approach over each step (no tunnelling at speed).
- Deployed md5 `7a84d2fdf222a20bcc655f9e36c5592e`; previous build kept as `vectorstrike_prev_fe89be13.x86_64`. Soak: 1, 2 and 4 bots, 15 min each, no crashes.

## Status
- **Deployed to ARCADE-21 on 24 Oct ~16:40** (not yet launched on the box). Share mounted on the Mac as `/Volumes/share-1/roms` (device shell `~/mnt/roms`).
  - `roms/ports/vectorstrike/vectorstrike.x86_64` md5 `fe89be13fb423dbd160493b7460542ed`
  - `roms/ports/VectorStrike.sh` (Belt Command launcher pattern: chmod, ld-linux fallback, log) + `VectorStrike.sh.keys` (Hotkey+Start → Alt+F4 = quit)
  - `roms/ports/images/VectorStrike-image.png / -marquee.png / -thumb.png`
  - gamelist.xml entry added (backup `gamelist.xml.pre_vectorstrike`). ES may need "Update games lists" or a restart.
- Cloud tests: bot soak 4 players × 25 min (two seeds) reached wave 12, no crashes; xvfb screenshots of every stage.
- Built with zig c++ `-target x86_64-linux-gnu.2.28`, links system libSDL2 (needs SDL ≥ 2.0.18 for SDL_RenderGeometry; gyro uses the 2.0.14 sensor API).
- Source: `claude/vector-strike-source-bundle.txt` (files split by `===== FILE: path =====`). Build: `build.sh` (copy `/usr/include/x86_64-linux-gnu/SDL2/_real_SDL_config.h` into `inc/SDL2/`).

## Design
- Every wave: **Space** (shoot N interceptors; N = (5+2·wave) × (1+0.45 per extra player); they fire fireballs) → **Surface** from wave 2 (tower tops to shoot, towers to fly over) → **Trench** (wall turrets; catwalks from wave 2, double catwalks wave 5+; exhaust port at the end). Port hit → climb out → Citadel explodes → bonus tally (port hitter 25 000, others 10 000, 2 000 per shield, +1 shield). Missing the port = "go around", trench restarts.
- Shoot the orange fireballs (+50). Fighter 1000 (2 hits from wave 4), tower top 250, turret 300, port 5000.
- 6 shields. A player at 0 is out until the next stage if anyone is still flying; all down = game over. Start level EASY/MEDIUM/HARD (wave 1/3/5, start bonus 0/40k/100k). High score + best wave saved in `vectorstrike.cfg`.
- Split screen: 1 full, 2 top/bottom (narrower FOV), 3 = quadrants + squadron board, 4 quadrants. Players fly staggered 12 units apart so wingmen are visible ahead. Resolution option Auto/720p/1080p/1440p/4K.
- Audio all synthesized; a `music.wav` in the game folder replaces the built-in 120 bpm loop (for Chris's AI music).

## Controls
Left stick moves the sight and steers (sight position = yoke position). Fire: A, RB or RT (hold). Lobby: A join, B leave, X invert Y, Y gyro aim (pads that report a gyro), Left/Right start level, Start launch. Gyro: tilt to aim, L3 or Y recentres (signs are a guess, untested). Start pause, Back+Start title. Keyboard: arrows/WASD or mouse, Space/click fire, I invert, Esc.

## Logs
`/userdata/system/logs/vectorstrike.log`: `pad:` lines (name, GUID, type, gyro yes/no), `renderer:`, `video:`, `game:` phase lines, hits, `fps:` every 30 s.

## Test switches
`VS_STATE=title|help|options|lobby|game|icon|logo`, `VS_PLAYERS`, `VS_BOTS=1`, `VS_WAVE`, `VS_PHASE=surface|trench|escape|tally`, `VS_ADVANCE=secs`, `VS_SEED`, `VS_SHOT=frame` + `VS_SHOT_PATH`, `VS_FRAMES` (bench), `VS_WINDOW=WxH`, `VS_RES`, `VS_MUTE`, `VS_HOME`.

## Open / next
- First box run: renderer, fps at 4K with 4 players, Switch Pro mapping and gyro direction.
- Balance after Chris plays (bots are weak in single player; they die to fireballs).
- No Wii remote support yet (wiidirect + IR pointer would suit the sight well).
