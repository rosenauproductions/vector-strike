#!/usr/bin/env python3
"""Solid hulls for the wireframe ships: a low-poly triangle shell of a .glb, in exactly the same frame as its .vsm
(same centring, scale and --matrix as tools/glb2vsm.py), so the game can fill a ship's body black and nothing shows
through it. .hull text: 'v x y z' and 'f a b c' lines. Usage mirrors glb2vsm: glb2hull.py in.glb out.hull [--matrix M] [--faces N]."""
import argparse, numpy as np, trimesh
ap = argparse.ArgumentParser(); ap.add_argument('inp'); ap.add_argument('out')
ap.add_argument('--faces', type=int, default=1200); ap.add_argument('--skip', default=''); ap.add_argument('--matrix', default=''); ap.add_argument('--pitch', type=float, default=0.012)
a = ap.parse_args()
s = trimesh.load(a.inp, force='scene'); parts = []
for name in s.graph.nodes_geometry:
    T, g = s.graph[name]
    if a.skip and any(k.lower() in name.lower() for k in a.skip.split(',')): continue
    if not hasattr(s.geometry[g], 'faces'): continue
    m = s.geometry[g].copy(); m.apply_transform(T); parts.append(m)
allv = np.vstack([p.vertices for p in parts])
c = (allv.min(0) + allv.max(0)) / 2; sc = 1 / np.abs(allv - c).max()
R = np.array(eval(a.matrix)) if a.matrix else np.eye(3)
# Shell: voxelize the whole ship (thin wings included), fill it, marching cubes, then decimate. A watertight shell
# decimates cleanly to the face budget; it's about half a voxel fatter than the wireframe, which the game's depth bias covers.
M = trimesh.util.concatenate([trimesh.Trimesh((p.vertices - c) * sc @ R.T, p.faces, process=False) for p in parts if len(p.faces)])
vox = M.voxelized(pitch=a.pitch).fill(); m = vox.marching_cubes; m.apply_transform(vox.transform); m.merge_vertices()
try: m = m.simplify_quadric_decimation(face_count=a.faces)
except Exception as ex: print('decimate failed', ex)
m.merge_vertices(); V = m.vertices; F = m.faces[m.area_faces > 1e-8]
with open(a.out, 'w') as f:
    f.write('# vector strike solid hull from %s\n' % a.inp.split('/')[-1])
    for q in V: f.write('v %.4f %.4f %.4f\n' % tuple(q))
    for t in F: f.write('f %d %d %d\n' % tuple(t))
print('%s: %d verts %d faces, extents %s' % (a.out, len(V), len(F), (V.max(0) - V.min(0)).round(2)))
