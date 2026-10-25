#!/usr/bin/env python3
"""Cut the trench interior (walls + floor + a strip of surface each side) out of death_star_trench_run.glb and
write it in game units as modelpack/trench.vsm: x across (walls at +-TR_W), y up (lip 0, floor TR_FLOOR), z along the
trench (one tile, the game repeats it). Usage: trench_tile.py in.glb out.vsm [--strip 1.0] [--minlen 0.15]"""
import argparse, numpy as np, trimesh
ap = argparse.ArgumentParser(); ap.add_argument('inp'); ap.add_argument('out')
ap.add_argument('--strip', type=float, default=1.0); ap.add_argument('--minlen', type=float, default=0.15); ap.add_argument('--angle', type=float, default=30)
a = ap.parse_args()
s = trimesh.load(a.inp, force='scene'); ms = []
for n in s.graph.nodes_geometry:
    if not n.startswith('DeathStarTrench'): continue
    T, g = s.graph[n]; m = s.geometry[g].copy(); m.apply_transform(T); ms.append(m)
m = trimesh.util.concatenate(ms); m.merge_vertices()
fn = m.face_normals; sel = m.face_adjacency_angles > np.radians(a.angle)
ed = np.sort(m.face_adjacency_edges[sel], axis=1); fa = m.face_adjacency[sel]
es = np.sort(m.edges, axis=1); ef = np.arange(len(es)) // 3
eu, inv, cnt = np.unique(es, axis=0, return_inverse=True, return_counts=True); bm = cnt[inv] == 1
E = np.vstack([ed, es[bm]]); NN = np.vstack([np.hstack([fn[fa[:, 0]], fn[fa[:, 1]]]), np.zeros((int(bm.sum()), 6))])
E, ui = np.unique(E, axis=0, return_index=True); NN = NN[ui]; V = m.vertices
# model trench: runs along x, walls at z -15.55 / -8.1, lip y 5.85, floor y -1.2
ZC, HALF, YLIP, YFL = -11.82, 3.73, 5.85, -1.2
TR_W, TR_FLOOR = 22.0, -42.0
sx = TR_W / HALF; sy = -TR_FLOOR / (YLIP - YFL)
A, B = V[E[:, 0]], V[E[:, 1]]
keep = (np.abs(A[:, 2] - ZC) < HALF + a.strip) & (np.abs(B[:, 2] - ZC) < HALF + a.strip) & (A[:, 1] > YFL - 0.3) & (B[:, 1] > YFL - 0.3)
keep &= np.linalg.norm(A - B, axis=1) > a.minlen
E = E[keep]; NN = NN[keep]; used = np.unique(E); remap = -np.ones(len(V), int); remap[used] = np.arange(len(used))
x0 = V[:, 0].min()
G = np.stack([(V[used, 2] - ZC) * sx, (V[used, 1] - YLIP) * sy, (V[used, 0] - x0) * sx], 1)
with open(a.out, 'w') as f:
    f.write('# vector strike trench tile from %s, length %.1f\n' % (a.inp.split('/')[-1], (V[:, 0].max() - x0) * sx))
    for p in G: f.write('v %.3f %.3f %.3f\n' % tuple(p))
    for e, n in zip(remap[E], NN): f.write(('e %d %d %.2f %.2f %.2f %.2f %.2f %.2f\n' % (e[0], e[1], n[2], n[1], n[0], n[5], n[4], n[3])) if np.any(n) else 'e %d %d\n' % (e[0], e[1]))   # normals in game axes
print('%s: %d verts %d edges, tile length %.1f' % (a.out, len(G), len(E), (V[:, 0].max() - x0) * sx))
