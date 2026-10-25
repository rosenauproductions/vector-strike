#!/bin/bash
# Rebuild modelpack/*.vsm from Chris's GLBs in assets_src/ (pip install trimesh fast_simplification).
# The station is not rebuilt here (see HANDOVER.md for its command); everything else is.
set -e; cd "$(dirname "$0")/.."; C=tools/glb2vsm.py; A=assets_src; M=modelpack
XM='[[0.9265,-0.0065,0.3761],[0.003,-0.9998,-0.0245],[-0.3758,-0.0243,0.9266]]'   # X-wing: nose +z, levelled 3.9 deg
TM='[[-0.941,-0.322,-0.101],[-0.296,0.932,-0.211],[-0.162,0.169,0.972]]'
FLIP='[[-1,0,0],[0,1,0],[0,0,-1]]'
python3 $C $A/244d67ca-xwing_sketchfab.glb $M/player.vsm --matrix "$XM" --faces 12000 --max 3500 --angle 40
python3 $C $A/244d67ca-xwing_sketchfab.glb $M/cockpit.vsm --matrix "$XM" --faces 40000 --max 99999 --angle 8 --keep-x 0.58   # cockpit view: outer wings + cannons only
python3 $C $A/2e756271-3d_t.i.e_fighter_-_star_wars_model.glb $M/enemy.vsm --matrix "$TM" --faces 12000 --max 3500 --angle 40
python3 $C $A/y-wing.glb $M/ywing.vsm --matrix "$FLIP" --faces 12000 --max 3500 --angle 40
python3 $C $A/millennium_falcon.glb $M/falcon.vsm --matrix '[[-0.9461,0.324,0.0],[0.3212,0.9381,0.1298],[-0.0421,-0.1228,0.9915]]' --faces 14000 --max 4000 --angle 40   # levelled from its disc, mirrored so the cockpit is on the right
python3 $C $A/star_destroyer.glb $M/destroyer.vsm --matrix "$FLIP" --faces 12000 --max 4000 --angle 35
python3 tools/trench_tile.py $A/death_star_trench_run.glb $M/trench.vsm --strip 1.0 --minlen 0.5
python3 $C $A/asf-01_b-wing_starfighter.glb $M/bwing.vsm --faces 12000 --max 3500 --angle 40
python3 $C $A/asf-01_b-wing_starfighter.glb $M/bwing_ck.vsm --faces 40000 --max 99999 --angle 8 --keep-x 0.45   # heavy bomber cockpit: S-foils + cannons
# far versions (about 600 lines) used once a ship is small on screen
python3 $C $A/244d67ca-xwing_sketchfab.glb $M/player_far.vsm --matrix "$XM" --faces 1500 --max 600 --angle 35
python3 $C $A/2e756271-3d_t.i.e_fighter_-_star_wars_model.glb $M/enemy_far.vsm --matrix "$TM" --faces 1500 --max 600 --angle 35
python3 $C $A/y-wing.glb $M/ywing_far.vsm --matrix "$FLIP" --faces 1500 --max 600 --angle 35
python3 $C $A/asf-01_b-wing_starfighter.glb $M/bwing_far.vsm --faces 1500 --max 600 --angle 35
python3 $C $A/millennium_falcon.glb $M/falcon_far.vsm --matrix '[[-0.9461,0.324,0.0],[0.3212,0.9381,0.1298],[-0.0421,-0.1228,0.9915]]' --faces 2000 --max 700 --angle 35
python3 $C $A/star_destroyer.glb $M/destroyer_far.vsm --matrix "$FLIP" --faces 2500 --max 900 --angle 30
