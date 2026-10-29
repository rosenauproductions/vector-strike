# Vector Strike — handover (29 Oct 2026, v21)

**Code lives on GitHub: `rosenauproductions/vector-strike` (private, branch main).** Cloud sessions: add_repo it and read CLAUDE.md + HANDOVER.md in the repo (HANDOVER.md there is the canonical copy of this doc). Pushed 24 Oct ~21:05 with full history.


Chris asked for a complete redo of the 1983 Atari vector arcade game (the line-drawn space trench shooter) for 1–4 players. Built as an **original** game with its own name, ships and fortress (no Star Wars names, ships or music) but the same play: wireframe 3D, first-person, three stages per wave.

## v21 (29 Oct, Cowork session) — the canyon crosses the tower field; railed turn-in (md5 f40bfc416e6585904aef7e1ab3fbf2f4)
- **The trench now cuts straight across your path at the end of the tower field**, at `G.crossZ` = start + surfLen + 250. The tower layout carries on beyond it as scenery: towers flagged `side = 9`, spread ±420, up to 1300 past the trench. They don't fire and don't count toward TOWER TOPS. The HUD says `TRENCH IN n`. The surface grid is wider (±1600) with a gap where the canyon is.
- **How the crossing is drawn:** the ordinary `drawTrench` code is used, through a rotated copy of the camera (`drawCrossing`, `crossToT`): surface coords S → trench coords T is T.x = d·(crossZ − S.z), T.y = S.y − GROUND_Y, T.z = d·S.x, where d = `crossD` = ±1, random left or right. It's drawn twice, once for each direction along the crossing.
- **PH_TURNIN (new phase 12):** 230 units before the canyon the autopilot takes over, with no steering or firing.
  - Each player rides a quadratic Bezier, from their position through the corner above the canyon to mid-depth in it (T.y −14). Their formation offset is kept (±14 sideways, ±8 up/down, `zoff` along the path).
  - The ship banks up to 0.75 rad into the turn, in the cockpit view. Only your own ship is drawn.
  - Takes 1.6–3 s (path ≈ 1.6 R at the surface speed). The banner reads INTO THE TRENCH; log line `game: turning left/right into the trench`.
  - At the end, `G.z` = d·P2.x, so the trench run starts exactly where the curve ends, heading down it.
  - The towers stay on screen through the turn: `beginPhase` doesn't clear `G.en` for PH_TURNIN.
- Removed the old straight-in trench entrance drawing, and the stray surface grid at trench-floor height at the start of the trench.
- Test switch `VS_SURFLEN=n` (short tower field).
- **Soak** (1 / 2 / 4 bots, 400 s): every run turned into the trench. 2–3 trench hits, no pulling up out of the trench; port hit with 2 and 4 bots.

## v20 (29 Oct, Cowork session) — colour modes, colour per item, arcade-style shield arch (md5 5224779f330518421c554de85f8133fe)
**29 Oct ~07:55: installed on ARCADE-21 as the only Vector Strike** (md5 verified on the share; replaces v14, so v15-v19 arrive with it; no backup; the 23 model files on the box already match the repo). Not yet run on the box. `dist-beta/vectorstrike_beta.x86_64` = this build. No model changes; `web/dist` still v18.
- **Options > COLORS** (new row, A opens the editor, left/right on the row cycles the mode). `COLS[CI_N]` holds one colour per item and everything draws from it.
  - Items: TIE fighters, Darth Vader, fireballs, your lasers, battle station, station surface, towers, tower tops, trench walls, trench turrets, catwalks, exhaust port, Star Destroyers, freighter, bombers, asteroids, stars, shield bar. The old `C_ENEMY` / `C_BALL` / `C_TRENCH` / `C_GROUND` / `C_TOWER` / `C_CAT` / `C_CIT` are now macros onto the table.
  - Modes: **VECTOR** (the game's own colours), **ARCADE 1983**, **GREEN SCREEN**, **WHITE VECTOR**, **CUSTOM**.
  - ARCADE 1983 palette: yellow Death Star surface and battle station, green TIEs, white Vader, red/orange fireballs, red lasers, green towers with red tops, blue trench, red trench turrets, amber catwalks, red exhaust port, green shield bar.
  - Sources only confirm the yellow Death Star grid, the red trench turrets and the green "USE THE FORCE" text. The rest is from screenshots and memory, so Chris can fix anything by eye.
  - The editor lists every item with a swatch, a glowing line sample and the nearest of 13 named palette colours. A preview panel draws the selected item (the TIE model for fighters, the destroyer model, etc.). Changing any item switches to CUSTOM, starting from what's on screen.
  - Saved as `colmode=` and `colc_<key>=0xRRGGBB` (decimal) in vectorstrike.cfg. Round-trip checked.
  - Player ship and HUD colours stay per player.
- **Shields, the arcade way:** the charges now sit in an arch at the top centre of every view (the arcade shows its shield charges top centre), with the count in the middle. The old blocks at the top right are gone.
  - Colour: green / your colour when healthy, yellow at 2–3, red and flashing on the last one. The segment you just lost flashes white.
  - The objective line and bomber bars moved down under the arch.
- **Fix (29 Oct ~09:10, deployed, md5 71a17235…):** Chris's first run started ON RAILS with an Xbox pad. `rails=1` had been saved in the cfg by an earlier Wii-remote session and was loaded on every start. ON RAILS is now per session and never loaded:
  - **In the lobby:** it follows the controllers, on while a Wii remote player is in and off otherwise (`lobby: ON RAILS on/off` in the log).
  - **Manual toggle:** BACK / - / TAB sets it by hand (`railsManual`) until you go back to the title.
  - The box's cfg was also set to `rails=0`.
- **Tower tops (29 Oct ~09:40, md5 f361503ef9859da7d108d7ed5d5cbbe8, NOT yet deployed: the Mac link dropped):** now a block on top of each tower, the same 10 x 10 footprint as the tower (Chris), 8 tall (he asked for 200% taller), a seam along the face), like the arcade's, instead of the diamond. Hit sphere at its middle (y+4.6, radius 7); fly-over clearance, fireball muzzle and bot aim moved up to match. Latest md5 f55dc9932d6e8bf519f5fae48e5791bc.
- Test switches: `VS_COLMODE=0-4`, `VS_STATE=colors` (+ `VS_COLSEL=row`).
- Soak: 4 bots × 300 s in each colour mode, exit 0.

## v19 (28 Oct, Cowork session) — bomber gauntlet, tower fight every wave, Falcon crew stations (md5 690d3b1d186e7612db12f04b500556b5)
`dist-beta/vectorstrike_beta.x86_64` = this build. **Not deployed yet**: the Batocera share wasn't mounted on Chris's Mac when it was built. Deploy as the only game (`roms/ports/vectorstrike/vectorstrike.x86_64`, no backup). No model changes. `web/dist` was NOT rebuilt (no emcc in that session), so the web build is still v18.
- **Level 1 bomber run (the Y-wings), destroyer to destroyer** (`setupEscortRun` / `updateEscortRun` in levels.h, `updateCapital` / `updateFighters` in main.cpp):
  - **Route:** every bend of the bombers' S-route now has a Star Destroyer lying broadside across it. The bombers skim about 34 over its deck, crossing a little ahead of the middle and clear of the bridge tower. Every third one, they dive under the keel instead. `Escort.lift` eases them clear of a hull if a drifting destroyer gets in the way.
  - **Flak:** within 1700, each destroyer fires green turbolaser bolts at the nearest bomber every 0.12–0.3 s. The bolts lead the bomber and mostly miss, with flak rings bursting around it. Only 1.5 % of shots hit; it's mostly spectacle.
  - **Fighters on the bombers:** fighters going for a bomber (`escortTargetOf`, about 3 in 4) launch off the destroyers. Each slots in 50–80 behind its bomber, weaving, so it sits between you and the bomber, and fires green bolts (33 % hit).
  - **Bomber toughness:** bombers have 14 + wave hit points (was 10 + wave). Their tail gunners now kill only 10 % of the time (was 25 %), so protecting them is the player's job.
  - **Measured:** about 11–16 hits over the run, both bombers usually reach the break-off. Test switch `VS_ESCLOG=1` prints `esc: flak hit` / `esc: fighter hit`.
  - **Autopilot:** bots and ON RAILS players use new `hullAvoid()` (runs after `formationAdjust`). It looks 40–280 ahead and pulls up or down off a hull. Hull bounces dropped from 5–7 to 0–5 per run with 4 bots.
- **Tower fight before every level-1 trench** (it used to start at wave 2), like the arcade:
  - **HUD:** `TOWER TOPS n / m    TRENCH IN x`.
  - **Bonus:** destroying every top gives all joined players +10000 ("ALL TOWER TOPS DESTROYED").
  - **Shields:** leaving the towers gives +1 shield, or +2 for a clean sweep (max 6). The message shows the tally.
  - **Tower fire:** towers fire every 1.6–4.4 s, with at most `1 + wave/2 + players/2` fireballs in the air.
  - **Trench entry:** for the first 2.5 s of the trench, ships are pulled down under the lip, and the trench-lip guns stay quiet for 3 s. Without this, ships coming off the towers stayed above the lip and got shredded (4 bots: about 21 trench hits vs about 4; now back to normal).
  - Log line: `game: towers 5 / 32`.
- **Level 2 turret fight: each player mans a different part of the freighter**, assigned in join order (log line `game: P3 mans the PILOT`):
  1. **TOP GUN** and 2. **BOTTOM GUN** work as before. The old top-rear / bottom-rear duplicates are gone.
  3. **PILOT** sits in the cockpit (`cockpitEye`, model point 0.58/0.075/0.42 × FAL_S, behind a canopy-strut and console overlay). The stick jinks the whole freighter (±170 × ±95, rolls with it; `G.falRoll`). While jinking, shots from ahead spray 2.6× wider. Fire = the forward guns on the mandibles, aimed at the centre.
  4. **CO-PILOT** sits in the next seat. Stick or Wii pointer moves a sight; fire = concussion missile (0.8 s reload bar, 2.6× wider lock, 3 damage, a blast that kills fighters within 80).
  - Fighters shooting from ahead hit the cockpit crew, otherwise the gunner on that side.
  - Fighters no longer latch onto the freighter's "tail" in this phase, so there are no HELP markers or "SHAKEN OFF" messages there.
  - Bots fly and shoot every station. 4 bots: kills 11 / 2 / 2 / 7, missiles took out extra fighters.
- **Soak test, 900 s each, bots:** 1 / 2 / 4 players from level 1, 4 from level 2, 3 from level 3. No crashes, every level progresses, exit 0.

## v18 (cloud session) — web build for Vercel + Wii U notes
- **Web build**: the game now also compiles to WebAssembly and runs in the browser: keyboard, mouse and gamepads, split screen, sound, and scores saved in browser storage. `./build_web.sh` -> `web/dist/`, which is committed. `vercel.json` serves it as a static site. Details, build recipe and deploy steps are in `docs/WEB_BUILD.md`. Deployed 26 Oct to https://vector-strike-gamma.vercel.app (Vercel project `vector-strike`, Vercel login required on every URL because of the personal-use models).
- Desktop code changes, all small: the main loop body moved into `mainTick()`; the Wii driver is compiled out under `__EMSCRIPTEN__`; joining with a Wii remote now uses `wiiRumble()`. The desktop binary is otherwise unchanged (checked with a 4-bot run). `dist-beta` is refreshed.
- **Wii U**: feasibility notes and a work list in `docs/WIIU_PORT.md` (not started).

## v17 BETA (cloud session) — tailing fighters overtake you (`dist-beta/vectorstrike_beta.x86_64`, md5 6814f26a...)
Not deployed yet; no model changes.
- Open space: a fighter that latches on to your tail now powers past you after 1.8-3.8 s (Vader 3.5-6 s), new state 5 in `updateFighters`. It surges by on one side (the chase view stays on while it goes past), then sits about 85 ahead of you for 2.4-3.4 s (Vader 1.8-2.6 s, jinking harder), matching your speed with a gentle weave so you can line it up. "IT OVERSHOT - NOW GET IT" / "VADER OVERSHOT - GET HIM". The red tail warning clears as soon as it is in front. Then it breaks away and comes round again. In a 4-bot soak, 68 of 86 tails ended with an overtake.
- Trench and tunnel pursuers: every 5-8 s one of them (Vader included) surges about 55 ahead of you for ~2.5 s, holding fire while it is in front, then drops back. Pursuers can now be shot (1000 each). Vader there has 10 + 2 x wave hit points instead of being untouchable; knock them off and he spins out of the trench (+3000).
- Log lines: `P1's tail is overtaking`, `pursuer (ace) surges ahead of P1`, `P1 shot a pursuer`.

## v16 BETA (cloud session) — staggered ship positions in multiplayer (`dist-beta/vectorstrike_beta.x86_64`, md5 4dd73f9c...)
Not deployed yet; no model changes.
- With 2-4 players the autopilot (bots, and humans on ON RAILS) no longer flies everyone into the same spot: `formationAdjust()` (main.cpp, run after `botThink`) gives each player a formation slot round the squad's middle (P1 high-left, P2 high-right, P3 low-left, P4 low-right) on the surface / trench / tunnel / Star Fox rails, and in open space, the asteroids and Ice Planet steers off any wingmate closer than 80 (45 on the ice planet).
- Measured with 4 bots over 60 s (closest pair of ships, sampled each second, "stacked" = under 10 apart): trench stacked 43/59 -> 4/59; Star Fox 56/59 -> 3/59; space average gap 35 -> 97 (0 stacked); ice planet 48/59 -> 0/59. Test switch `VS_SPREADLOG=1` prints it.

## v15 BETA (25 Oct, cloud session) — ENEMY WAVES setting (`dist-beta/vectorstrike_beta.x86_64`, md5 08c8a203... built on v14)
Not deployed yet; no model changes.
- Options -> ENEMY WAVES: FEWER / NORMAL / MORE / LOTS = x0.6 / x1 / x1.5 / x2.1 (`foes=` in vectorstrike.cfg, `foeMul()` / `foeN()` in main.cpp). Scales, in every mode: fighters on screen at once and how fast they come, kill quotas (space, turret fight, escort cover, tank waves), the escort's attack waves, trench / tunnel wall turrets, surface tower density, Star Fox drone flights and tanks, Ice Planet walkers (max 6) and probe droids.
- Checked: tank wave quota 5 / 8 / 17 and trench enemies 10 / 16 / 34 for FEWER / NORMAL / LOTS. Soak on LOTS, 4 bots x 300 s in all four modes: no crashes. Perf note: LOTS in 4-player space measured 19 fps on the cloud's software renderer (NORMAL ~29); on the box use MORE if LOTS stutters.

## v14 (25 Oct, Cowork session) — Wii remotes point-and-shoot + ON RAILS mode
**25 Oct ~15:35: installed on ARCADE-21 as the only Vector Strike** (md5 dce3418a436d269a4ac07b19c6131c39, no backup; model pack unchanged). Not yet run on the box.
- `src/wii/` = the Wii Lab (Controller Lab) v0.1 driver: Dolphin-style own Bluetooth search + raw L2CAP 0x11/0x13, a reader thread per remote, report mode 0x33 (buttons+accel+12-byte IR). Changes: no speaker in the simulator, `leds` action takes the mask, and with no adapter the search idles (looks again every 10 s) instead of spinning.
- The game searches only in menus / pause / game over (`wii: search on/off` in the log) and keeps searching while remotes are connected, so remotes 2-4 can join. Remotes must be FORGOTTEN in Batocera's Bluetooth settings, then press 1+2 (or red sync). Needs root (ports run as root). Known remotes are kept in `known_remotes.txt` next to the binary and paged directly next time.
- Pointer: midpoint of the two sensor-bar dots, roll-corrected from the dot angle (Dead Center's method), widest pair = the bar, one-dot fallback keeps the last bar spacing. **No calibration** - the sight shows where you point; Home in game re-centres (where you point = middle). Mapping: `aim = (-fx/300, fy/225)` in camera units centred on 512/384. If the vertical is upside down on the real bar, flip the sign of fy (or use the lobby's 1 = INVERT Y).
- Buttons: B trigger / A fire, D-pad up/down throttle (menus: D-pad navigates), 1 = X (invert / Star Fox barrel roll / tow cable), 2 = Y (cockpit/chase view), + start/pause, - = BACK (lobby: rails toggle; - and + together = quit to title), Home = back/leave in menus, re-centre in game. LEDs show the player number; rumble 150 ms on join, 250 ms on a hit (600 ms on the last shield). Remote ids are 5000+n per MAC so a dropped remote returns as the same player.
- **ON RAILS** (`G.opt.rails`, saved as `rails=` in vectorstrike.cfg): lobby toggle on BACK / - / TAB, switched on automatically when a Wii remote joins. The bots' pilot (`botThink`) flies the ship (`p.steer`), your sight (`p.aim`) only aims and shoots; throttle still overrides when touched. Not used in TANK BATTLE or the turret. `p.steer` now drives movement / bank / camera everywhere (= aim when rails are off).
- Tests: `VS_WII_SIM=N` (simulated remotes: A tap each 4 s joins them, IR dots circle), `VS_NOWII=1` (driver off), `VS_RAILS=0/1`. Rails test, 1 player no input + autofire: free flight dies in 74 s; on rails 14 kills, reached the station and flew the trench by 400 s. Sim lobby: both remotes joined, pointer ok, rails switched on; 4 bots + 2 sim remotes ran clean; no-adapter path exits clean. `wii: <mac> dots N pointer ok x y roll battery` logs every 10 s.

## v13 BETA (25 Oct) — right-stick glance in every mode (`dist-beta/vectorstrike_beta.x86_64`, md5 6278f520...)
**25 Oct ~10:05: v13 installed on ARCADE-21 as the only Vector Strike** - compiled from debf8c5 in the Cowork session (md5 2894e2635af3d95391424db15a735028; the cloud's dist-beta build is 6278f520, same source), plus new `modelpack/` arwing, atat, hturret, probe, speeder (23 model files). No backup kept (Chris: one game, no backups). Bot soak 4 players x 300 s in each mode (PLAY, STAR FOX, TANK BATTLE, ICE PLANET): no crashes. The share's `vectorstrike/` folder still shows `.smbdelete*` placeholders + an empty `modelpack_v7` from the v10 cleanup; macOS holds them until the share is remounted; harmless.
Not deployed yet; no model changes since v12.
- Right stick looks round (keyboard J / L left/right, K / U down/up): full sideways = almost straight behind, up/down ~50 degrees, eases back when released. Cockpit views turn your head (the ship stays put); chase views (and Star Fox, Ice Planet) swing the camera round the ship. Guns still fire where the ship points (`p.aimCam` = the camera before the glance); centre sights hide while glancing. Not in the level 2 gun turret (there the stick already aims).
- Test switch `VS_GLANCE=x,y`.

## v12 BETA (25 Oct, cloud session) — TANK BATTLE + ICE PLANET modes (`dist-beta/vectorstrike_beta.x86_64`, md5 e8d9a39c...)
Not deployed yet. Copy the beta binary plus the new `modelpack/atat.vsm`, `speeder.vsm`, `probe.vsm`, `hturret.vsm` (from Chris's GLBs in `assets_src/`, rebuilt by `tools/build_modelpack.sh`).
Title menu is now PLAY / STAR FOX MODE / TANK BATTLE / ICE PLANET / HOW TO PLAY / OPTIONS / QUIT (`G.mode` 0-3).
- **TANK BATTLE** (`src/tank.h`, Battlezone style): endless green wireframe plain with cubes and pyramids (wrapped round you), mountains, a smoking volcano and the moon on the horizon. Stick up/down drives, left/right turns; fire = a shell (one in flight at a time, like the original); Y = chase view. Enemies: tanks (1000), fast super tanks from wave 3 (3000), zig-zag ground missiles from wave 2 (2000, 2 shield hits), a harmless saucer (5000). Radar sweep at the top with ENEMY IN RANGE / TO LEFT / RIGHT / REAR, cracked-glass flash when you're hit. Destroy the quota -> tally -> next wave.
- **ICE PLANET** (`src/hoth.h`, Empire Strikes Back battle): snowspeeders (fly low, chase view; Y = cockpit) against AT-ATs marching on the shield generator. The AT-AT model's legs are split off at load (`prepAtatLegs`) and swing from the hips in a diagonal gait. Lasers bounce off walkers ("ARMOUR TOO STRONG - USE THE TOW CABLE"): get low and close, press X (keyboard R) for the tow cable, then circle the walker twice (loops counter, cable wraps drawn round the legs; it snaps beyond 190) -> it topples and explodes (+5000). Probe droids can be shot (500). Rebel turrets near the generator plink at the walkers (they can be destroyed). The walkers' guns wear the generator down (bar at the top) and a walker reaching it ends the game. All walkers down -> tally -> next wave (one more walker, max 5).
- Bug fixed: the lobby's mode hint line (added in v11) was never drawn.
- Test switch `VS_MODE=0..3`. Perf (cloud software GL, 4 players 1080p): tank 63 fps, ice planet ~28 (our code is <10% of that; the rest is the software rasterizer). Soak: 4 bots, tank to wave 3, ice planet to wave 4 (9 walkers tripped), no crashes.

## v11 BETA (25 Oct, cloud session) — STAR FOX MODE (`dist-beta/vectorstrike_beta.x86_64`, md5 5da21ada...)
Not deployed yet. Copy the beta binary plus the new `modelpack/arwing.vsm` (from Chris's `assets_src/arwing.glb`, low-poly SNES fighter; every edge kept).
- Title menu: PLAY / **STAR FOX MODE** / HOW TO PLAY / OPTIONS / QUIT. Code: `src/fox.h` (+ hooks in main.cpp, `G.mode` 1).
- On rails: the course flies forward, the stick slides the ship round the screen, the camera sits behind and trails it. Lasers fire where the ship points (two-box reticle). X (keyboard R) = barrel roll, turns away shots. Hold fire to charge (ring round the reticle), release = homing bomb with a blast. Triggers / E,Q = boost / brake (squad average). Y = cockpit view.
- Course (9500 + 600/wave long): city blocks (some in the path), arches (+100 for flying through) with rings, a pillar field; gold rings = shield +1 / +500. Drone flights (V or side sweeps), ground tanks. At the end the carrier boss flies ahead of you: shoot off its two gun pods, then the caged core opens (red); its health bar is at the top. Then COURSE CLEAR -> tally -> next course (wave +1, denser, faster). Score line shows HITS like the original.
- Everyone flies the star fighter in this mode (the lobby hides the ship picker and shows the controls). Enemies and the boss are generic wireframes made in code.
- Test switch `VS_FOX=1`. Perf (cloud software GL, 1080p): 4 players 49 fps, 1 player 63. Soak: 4 bots 800 s cleared 8 courses, no crashes.

## v10 BETA (25 Oct, cloud session) — Chris's fixes after playing v9 (`dist-beta/vectorstrike_beta.x86_64`, md5 e13056ec...)
**25 Oct ~07:55: installed on ARCADE-21 as the only Vector Strike** - compiled from f14f52b in the Cowork session (md5 51ad341b9e03e2d2b933b2e8c978e73b; differs from the cloud's e13056ec only by build). Chris asked for one game and no backups: all `vectorstrike_prev_*`, the beta binary/launcher and `modelpack_v7` were deleted. 18 model files unchanged since v9.
Not deployed yet. Same install as before; the model pack is unchanged since v9.
- **Level 1 now starts with the escort**: you follow two bombers (Y-wings, white "BOMBER" markers + health bars at the top) along a winding S through the Star Destroyer fleet (a destroyer on the outside of every bend). Fighters come in waves ("FIGHTERS INBOUND"), 3 in 4 go for the bombers: pick them off. Near the station the bombers break off for their run ("COVER US"), you fight one more group (Vader joins here), then the report: "BOMBER GROUP - NO RESPONSE / BOTH BOMBERS WERE DESTROYED" -> "IT'S UP TO YOU - GO IN AND HIT THE TRENCH" -> station -> surface / trench as before.
- **Level 2 now starts in the freighter's gun turrets**: P1 top gun, P2 bottom, P3/P4 the rear-facing top/bottom. Round window, octagon frame in the middle with braces out to the rim, quad cannon in front of you that fires its top pair and bottom pair in turn. The asteroid field is far ahead (only the big rocks show). Shoot down the incoming fighters (5 + 2*wave, more with more players); shots hit the gunner on the side they come from. Then "TAKE THE CONTROLS - INTO THE ASTEROIDS" and the flying part as before.
- **Light speed** streaks now run outward from the centre (they were going backwards).
- **Enemy on your tail**: uses the chase view, pulled back just far enough to show the fighter behind you (it sits ~75 behind now). Same in the trench / tunnel pursuit. The 1st <-> 3rd person zoom and the tail pull-back are slower (~0.75 s / ~1 s).
- **The ace is now "DARTH VADER"** on screen (Chris's call; CLAUDE.md notes the exception).
- **Lobby**: the ship choice is now a big line in each player's panel ("UP / DOWN = CHANGE SHIP", ship name, 1/3) - it was easy to miss.
- Test switches: `VS_PHASE=turret`.
- Soak: 4 bots x 892 s went escort -> trench -> turrets -> asteroids -> light speed -> reactor -> level 1 again, no crashes.

## v9 BETA (25 Oct, cloud session) — three levels + the enemy ace (same branch, `dist-beta/vectorstrike_beta.x86_64`)
**Installed as THE main game on ARCADE-21 25 Oct ~23:35 local**: `vectorstrike.x86_64` = md5 f1b46bbea1296bc6a00f56cf66fb9a17 (v8 kept as `vectorstrike_prev_371d93be.x86_64`), plus `modelpack/ace.vsm` (f4489437...) and `ace_far.vsm` (1c7abcf5...); 18 model files on the box. Not yet run there.
Code for the new parts is in `src/levels.h` (included once from main.cpp). The game now cycles LEVEL 1 -> 2 -> 3 -> 1, wave +1 each level (difficulty keeps rising). Lobby: LB / RB (keyboard 1 / 2) picks the start level, Left/Right still picks EASY/MEDIUM/HARD. Names on screen stay generic.
- **Level 1 BATTLE STATION** (as v8) plus the trench chase: 42% of the way down the trench the enemy ace and two wingmen drop in behind you ("ENEMY ACE ON YOUR TAIL"). You can't shoot backwards; the camera pulls back behind you to frame them (the film's trench shot). They shoot from behind (the wingmen spray, the ace is precise) and the wingmen can clip catwalks. At 80% (or as soon as someone is down to 2 shields) the freighter comes in head-on over the trench ("FREIGHTER INBOUND - YOU'RE ALL CLEAR"), guns the wingmen, and the ace spins out of the trench.
- **Level 2 ASTEROID ESCAPE**: everyone flies the freighter (ship table entry FREIGHTER, 34.75 m, cockpit seat in the side tube). An asteroid field (10000 + 1200/wave long, ~1 rock per 8 units, sizes 4-230, tumbling, procedural lumpy icosahedra in `buildRockModels`) with two Star Destroyers behind you; interceptors come from behind onto your tail and smash into rocks (some swerve); small rocks (<30) can be shot, small ones you hit break, big ones cost a shield and bounce you. Out of the field: "HYPERDRIVE CHARGING 4..1" (fighters still attacking), then LIGHT SPEED (star streaks + white flash) -> tally.
- **Level 3 REACTOR RUN**: the run in past the fleet (same as level 1) to the station, then into the superstructure: a winding girder-filled tunnel (`tunFrame`: centre and size change along z), orange girders to thread (shield hit), wall turrets, the ace + two fighters chase you from 30% (fighters that clip girders burn). The tunnel opens into the reactor chamber: shoot the reactor (12 + 2/wave hits, bombers count 1.5x / 1.9x). Missed it: come round again. Hit it: 2.2 s later the fireball comes after you ("FIREBALL n BEHIND" bar); it is a little faster than base speed, so throttle up (bots floor it). Out of the exit: the station explodes from outside -> tally.
- **The enemy ace** (`ace.vsm` from Chris's TIE Advanced GLB): only ever one, turns/shoots 1.4x better, holds a tail longer, 5 + wave hits; beaten he spins away (+3000) and returns ~40 s later. Shows in level 1 and 3 run-ins, level 2, the trench chase and the tunnel. Radar: violet blip.
- Test switches: `VS_LEVEL=1|2|3`, `VS_PHASE=tunnel|lightspeed` (plus the old ones), `VS_ACE=1` (ace parked ahead of P1).
- Perf note: in 4-player the new levels measured ~22 fps on the cloud's *software* renderer; profiling shows >90% of that is the software rasterizer filling glowing lines (our code is <10% of the frame), so a real GPU should be well above 30. If not: Options -> GLOW OFF.
- Soak (final build): 4 bots x 1489 s went level 1 -> 2 -> 3 three times (wave 9), no crashes. Beta md5 f1b46bbe... Not deployed yet. The box's main game is v8 (md5 371d93be, see below); install v9 the same way as the v8 beta (as `vectorstrike_beta.x86_64` + the VectorStrikeBeta.sh entry, main untouched) and copy the new `modelpack/ace.vsm` + `ace_far.vsm` (the rest of the pack is unchanged since v8).

## v8 BETA (25 Oct, cloud session) — branch `claude/youthful-johnson-83rm5s`, binary `dist-beta/vectorstrike_beta.x86_64`
**25 Oct ~22:30: now THE main game on the box** - `vectorstrike.x86_64` replaced with this build (md5 371d93be...; v7 kept as `vectorstrike_prev_ef92b1b5.x86_64`) and the separate BETA gamelist entry removed (backup `gamelist.xml.pre_vsmerge`; VectorStrikeBeta.sh / vectorstrike_beta.x86_64 files left in place, unused). Earlier install notes: **Deployed to ARCADE-21 25 Oct ~22:10 local** (not yet run): `roms/ports/vectorstrike/vectorstrike_beta.x86_64` md5 371d93be72df754cced5962173e6f394 + all 16 `modelpack/*.vsm` (old pack backed up as `modelpack_v7/`), launcher `roms/ports/VectorStrikeBeta.sh` (+ .keys, log `system/logs/vectorstrike_beta.log`), gamelist entry "Vector Strike BETA" (backup `gamelist.xml.pre_vsbeta`). Main `vectorstrike.x86_64` (v7, md5 ef92b1b5...) untouched; both share `vectorstrike.cfg` and `modelpack/`. Share is now mounted on the Mac as `/Volumes/share` (= /userdata).
- **Models** (all rebuilt by `tools/build_modelpack.sh`): X-wing 3500 lines (was 1300, levelled 3.9 deg), TIE 3500 (was 1100), new Y-wing, B-wing, Millennium Falcon, Star Destroyer, trench tile. Every edge now carries its two face normals: lines whose faces both look away from the camera are not drawn, so ships read as solid (Chris's "fill them solid" idea, without a depth buffer).
- **Cockpit view = the real ship**: you sit in the seat; the body is hidden and the outer wings are pushed forward/apart so the four real cannons sit in the screen corners (`cockpit.vsm` = full-detail outer wings, `--keep-x 0.58 --angle 8`). Lasers streak from the real muzzles onto the sight. Y/V now slides the camera out of the seat to the chase spot (one continuous zoom). The old drawn cannons remain only when there is no model pack.
- **Three ships**, picked per player in the lobby with Up/Down (names on screen stay generic): STRIKE FIGHTER (X-wing, all-round), ASSAULT BOMBER (Y-wing: 1.5x hit size on towers/turrets/port, 0.87x steering), HEAVY BOMBER (B-wing: 1.9x hits, 0.75x steering, flies wing-vertical with the cockpit on top). Table `SHIPS[]` in main.cpp (eye, muzzles, cockpit warp, size).
- **Space stage = the run in** (Chris: "a long run past Star Destroyers and TIE fighters to get to the Death Star"): the goal is the battle station from the start; its surface is 9000 + 1500 per wave (max 18000) ahead. A fleet of 2 + wave Star Destroyers (max 7) sits along the route, alternating sides just off the direct line, 70% nose away from the station (toward you), the rest broadside. Hulls are solid (shield hit + bounce), deck guns fire at anyone close, 75% of interceptors launch from the nearest Star Destroyer ahead; they keep coming until you're 1500 from the surface. Kills still score; the old "destroy N interceptors" gate is off.
- **Scale** (Chris's numbers, X-wing 12.5 m = 11 units): Y-wing 16.2 m (1.3x), B-wing 16.9 m tall (1.35x), TIE about X-wing size, Falcon 34.75 m (15 units half-length), Star Destroyer 1.6 km (1408 units long). The battle station is radius 20000 (`ST_RADIUS`, test `VS_STR`) instead of the true ~70000: at true scale, from a few km out, it is only a flat horizon line on screen.
- **Radar**: a 3D globe at the bottom middle of each view in space (Options -> RADAR ON/OFF, `radar=`): your frame, distance squashed, blips on stalks down to the equator (above/below). Red interceptors (white = the one on your tail), wingmates in their colours, Star Destroyers grey-white, freighter gold, station surface yellow.
- **Freighter (Falcon) to the rescue**: once per wave, when someone is down to 2 shields, a wingmate is shot down, or a fighter has sat on a tail for 4 s, it arrives ("FREIGHTER INBOUND"), goes for tail-chasers first, and its kills count toward the wave.
- **Trench**: twice as long (5600 + 560/wave; turrets and catwalks doubled to keep the density). Walls/floor use the panels from `death_star_trench_run.glb` (`tools/trench_tile.py` -> `trench.vsm`, a 146-unit tile repeated; far tiles get fewer lines, split-screen views fewer too, surface lines beyond the lip are skipped while you're inside). Options -> TRENCH DETAIL OFF/LOW/HIGH (`trench=` in the cfg; default LOW so 4-player stays above 30 fps; HIGH looks best in 1-2 player).
- **Pull up out of the trench**: you can now climb ~9 units above the lip; while up there "TOO HIGH - SURFACE GUNS" flashes and guns on both sides fire at you, faster the longer you stay.
- **Far models**: each ship has a decimated `*_far.vsm` (600-900 lines) drawn once it is under ~70 px on screen, so distant ships keep clean outlines instead of a few longest lines of the full model (`VS_NOFAR=1` to compare).
- **Cinematic scaling** per viewport: far interceptors drawn up to 1.9x (hit spheres match), far wingmates 1.7x, your ship grows when the chase camera pulls back for a tail fight. Distant wingmates use fewer lines (4-player speed).
- Perf (software GL in the cloud, 4 players 1080p): space cockpit ~29-46 fps depending on how much of the fleet is in view (v7: 39), chase ~36 (v7: 28), trench HIGH ~25 (v7: 44; set LOW/OFF if the box struggles). 1 player trench ~60.
- Target (Chris): 1080p, 60 fps nice, 30 acceptable. Cloud numbers are software GL, so the box should do better.
- Soak (final build): 4 bots x 1500 s reached wave 10 (9 ports, freighter arrived 6 times), no crashes.
- New test switches: `VS_SHIP=0|1|2|9` (9 = mixed), `VS_SHOWENEMY=dist`, `VS_SHOWALLY=1`, `VS_PULLUP=1`, `VS_TAILTEST=1`, `VS_EYE=x,y,z`, `VS_WARP=y,z`, `VS_ZOOMTEST=0..1`, `VS_TRLINES=n`.
- Bugs found and fixed in this session (all mine): a comment pasted mid-line had switched off the exhaust-port reset (trench never ended after wave 1), the Star Destroyer position and a trench setting. Worth a glance if anything acts oddly.
- Open: try on the box (fps with trench HIGH at 4K/4 players, how the cockpit framing feels on the TV), Falcon in the trench (it only flies in space for now).

## v7 (24 Oct ~18:10)
- Options: CHASE DISTANCE 1-10 (5 = default; 0.52x-1.6x of 5.5 ship-scales back, height scales too), saved as `chase=`. Tail-chase framing still pulls further back when needed.

## v6 (24 Oct ~18:10)
- **Enemy on your tail** (v17: it now overtakes you after a few seconds, see v17): an interceptor that gets within 380 behind its target (or spawns there: 20% of spawns sneak in 520-750 behind someone) latches on (state 3): sits ~110 behind, matches speed, fires when lined up. Your view auto-switches to the chase camera, which pulls back to frame both ships (back = tail distance + 40, up to 300); red "ENEMY ON YOUR TAIL" warning + alarm. Ways out: turn hard (its tail turn rate 1.05 rad/s < yours; more than 50° off for 1 s = shaken off, +250), brake so it overshoots ("IT OVERSHOT - NOW GET IT"), or a wingmate shoots it ("SAVED Px +500" / "Px SAVED YOU"). Wingmates see a blinking ring + "HELP Px" arrow on the tailed player. Tails give up after 12 s. View returns to your own setting 1.2 s after the tail ends. Test hook `VS_TAILTEST=1`.
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
- Every wave: **Space** (shoot N interceptors; N = (5+2·wave) × (1+0.45 per extra player); they fire fireballs) → **Surface** tower fight on every level-1 wave (tower tops to shoot, towers to fly over) → **Trench** (wall turrets; catwalks from wave 2, double catwalks wave 5+; exhaust port at the end). Port hit → climb out → Citadel explodes → bonus tally (port hitter 25 000, others 10 000, 2 000 per shield, +1 shield). Missing the port = "go around", trench restarts.
- Shoot the orange fireballs (+50). Fighter 1000 (2 hits from wave 4), tower top 250, turret 300, port 5000.
- 6 shields. A player at 0 is out until the next stage if anyone is still flying; all down = game over. Start level EASY/MEDIUM/HARD (wave 1/3/5, start bonus 0/40k/100k). High score + best wave saved in `vectorstrike.cfg`.
- Split screen: 1 full, 2 top/bottom (narrower FOV), 3 = quadrants + squadron board, 4 quadrants. Players fly staggered 12 units apart so wingmen are visible ahead. Resolution option Auto/720p/1080p/1440p/4K.
- Audio all synthesized; a `music.wav` in the game folder replaces the built-in 120 bpm loop (for Chris's AI music).

## Controls
Left stick moves the sight and steers (sight position = yoke position). Fire: A, RB or RT (hold). Lobby: A join, B leave, X invert Y, Y gyro aim (pads that report a gyro), Left/Right start level, Start launch. Gyro: tilt to aim, L3 or Y recentres (signs are a guess, untested). Start pause, Back+Start title. Keyboard: arrows/WASD or mouse, Space/click fire, I invert, Esc.

## Logs
`/userdata/system/logs/vectorstrike.log`: `pad:` lines (name, GUID, type, gyro yes/no), `renderer:`, `video:`, `game:` phase lines, hits, `fps:` every 30 s.

## Test switches
`VS_STATE=title|help|options|lobby|game|icon|logo`, `VS_PLAYERS`, `VS_BOTS=1`, `VS_WAVE`, `VS_PHASE=surface|trench|escape|tally`, `VS_ADVANCE=secs`, `VS_SEED`, `VS_SHOT=frame` + `VS_SHOT_PATH`, `VS_FRAMES` (bench), `VS_WINDOW=WxH`, `VS_RES`, `VS_MUTE`, `VS_HOME`; v8 adds `VS_SHIP`, `VS_SHOWENEMY`, `VS_SHOWALLY`, `VS_PULLUP`, `VS_EYE`, `VS_WARP`, `VS_ZOOMTEST`, `VS_TRLINES`.

## Open / next
- First box run: renderer, fps at 4K with 4 players, Switch Pro mapping and gyro direction.
- Balance after Chris plays (bots are weak in single player; they die to fireballs).
- No Wii remote support yet (wiidirect + IR pointer would suit the sight well).
