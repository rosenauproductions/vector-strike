#!/bin/bash
# Solid hulls (black ship bodies) for modelpack/*.vsm, from the same GLBs and matrices as tools/build_modelpack.sh.
set -e; cd "$(dirname "$0")/.."; C=tools/glb2hull.py; A=assets_src; M=modelpack
XM='[[0.9265,-0.0065,0.3761],[0.003,-0.9998,-0.0245],[-0.3758,-0.0243,0.9266]]'
TM='[[-0.941,-0.322,-0.101],[-0.296,0.932,-0.211],[-0.162,0.169,0.972]]'
FLIP='[[-1,0,0],[0,1,0],[0,0,-1]]'
python3 $C $A/244d67ca-xwing_sketchfab.glb $M/player.hull --matrix "$XM" --faces 1400
python3 $C $A/2e756271-3d_t.i.e_fighter_-_star_wars_model.glb $M/enemy.hull --matrix "$TM" --faces 1000
python3 $C $A/y-wing.glb $M/ywing.hull --matrix "$FLIP" --faces 1400
python3 $C $A/millennium_falcon.glb $M/falcon.hull --matrix '[[-0.9461,0.324,0.0],[0.3212,0.9381,0.1298],[-0.0421,-0.1228,0.9915]]' --faces 1600
python3 $C $A/star_destroyer.glb $M/destroyer.hull --matrix "$FLIP" --faces 1600
python3 $C $A/asf-01_b-wing_starfighter.glb $M/bwing.hull --faces 1400
python3 $C $A/tie_advanced.glb $M/ace.hull --faces 1000
python3 $C $A/arwing.glb $M/arwing.hull --faces 400
python3 $C $A/imperial_atat.glb $M/atat.hull --faces 1600
python3 $C $A/star_wars_t-47_airspeeder__snowspeeder.glb $M/speeder.hull --faces 1200
python3 $C $A/arakyd_viper_probe_droid.glb $M/probe.hull --faces 800
python3 $C $A/hoth_defense_turret_-_star_wars.glb $M/hturret.hull --faces 600
