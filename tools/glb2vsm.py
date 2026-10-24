#!/usr/bin/env python3
"""Convert a .glb model into a Vector Strike wireframe (.vsm) - Chris's own model files, loaded by the game at run time.
Low-poly look: quadric-decimate to --faces, then keep sharp feature edges (> --angle) and open boundaries.
--matrix: 3x3 rotation (rows = new x,y,z axes) so the model faces +z (forward) with +y up.
--sphere NODE: that node is a smooth sphere: replace it with latitude/longitude rings and an equatorial trench,
  keep feature edges of everything else (Death Star dish).
.vsm text: 'v x y z' (centred, longest half-extent = 1) and 'e a b' lines."""
import argparse, numpy as np, trimesh
ap = argparse.ArgumentParser(); ap.add_argument('inp'); ap.add_argument('out')
ap.add_argument('--angle', type=float, default=28); ap.add_argument('--faces', type=int, default=1600); ap.add_argument('--max', type=int, default=1100)
ap.add_argument('--skip', default=''); ap.add_argument('--matrix', default=''); ap.add_argument('--sphere', default=''); ap.add_argument('--preview', default=''); ap.add_argument('--greeble', type=int, default=0); ap.add_argument('--seed', type=int, default=7)
a = ap.parse_args()
s = trimesh.load(a.inp, force='scene'); parts = []; sph = None
for name in s.graph.nodes_geometry:
    T, g = s.graph[name]
    if a.skip and any(k.lower() in name.lower() for k in a.skip.split(',')): continue
    m = s.geometry[g].copy(); m.apply_transform(T)
    if a.sphere and a.sphere.lower() in name.lower(): sph = m; continue
    parts.append(m)
allv = np.vstack([p.vertices for p in parts] + ([sph.vertices] if sph is not None else []))
c = (allv.min(0) + allv.max(0)) / 2; sc = 1 / np.abs(allv - c).max()
R = np.array(eval(a.matrix)) if a.matrix else np.eye(3)
V, E = [], []
def add_mesh(m, budget):
    m = trimesh.Trimesh((m.vertices - c) * sc @ R.T, m.faces, process=True); m.merge_vertices()
    if len(m.faces) > budget:
        try: m = m.simplify_quadric_decimation(face_count=budget); m.merge_vertices()
        except Exception as ex: print('decimate failed', ex)
    ed = m.face_adjacency_edges[m.face_adjacency_angles > np.radians(a.angle)]
    eu, cnt = np.unique(m.edges_sorted, axis=0, return_counts=True)
    ee = np.unique(np.sort(np.vstack([ed, eu[cnt == 1]]), axis=1), axis=0)
    base = sum(len(x) for x in V); V.append(m.vertices); E.append(ee + base)
if sph is not None:   # drop effect pieces floating outside the hull
    hc = (sph.vertices.min(0) + sph.vertices.max(0)) / 2; hr = np.abs(sph.vertices - hc).max()
    parts = [p for p in parts if np.linalg.norm((p.vertices.min(0) + p.vertices.max(0)) / 2 - hc) < hr * 1.1]
    allv = np.vstack([p.vertices for p in parts] + [sph.vertices]); c = (allv.min(0) + allv.max(0)) / 2; sc = 1 / np.abs(allv - c).max()
FT = sum(len(p.faces) for p in parts) or 1
for p in parts:
    if len(p.faces): add_mesh(p, max(40, int(a.faces * len(p.faces) / FT)))
if sph is not None:
    pv = (sph.vertices - c) * sc @ R.T; cen = (pv.min(0) + pv.max(0)) / 2; r = np.abs(pv - cen).max()
    pts, ed = [], []
    def ring(fn, n=48):
        b = len(pts) + sum(len(x) for x in V)
        for i in range(n): pts.append(fn(2 * np.pi * i / n))
        for i in range(n): ed.append((b + i, b + (i + 1) % n))
    for lat in [-75, -62, -50, -38, -26, -14, 14, 26, 38, 50, 62, 75]:
        la = np.radians(lat); ring(lambda t, la=la: cen + r * np.array([np.cos(la) * np.cos(t), np.sin(la), np.cos(la) * np.sin(t)]), 64)
    for dy in (-0.055, 0.055):   # trench walls' outer edges
        ring(lambda t, dy=dy: cen + r * np.array([np.sqrt(1 - dy * dy) * np.cos(t), dy, np.sqrt(1 - dy * dy) * np.sin(t)]), 64)
    for dy in (-0.025, 0.025):   # equatorial trench
        ring(lambda t, dy=dy: cen + r * np.array([np.sqrt(1 - dy * dy) * np.cos(t), dy, np.sqrt(1 - dy * dy) * np.sin(t)]), 64)
    for k in range(24):
        lo = 2 * np.pi * k / 24; b = len(pts) + sum(len(x) for x in V); n = 36
        for i in range(n + 1):
            la = -np.pi / 2 + np.pi * i / n; pts.append(cen + r * np.array([np.cos(la) * np.cos(lo), np.sin(la), np.cos(la) * np.sin(lo)]))
        for i in range(n):
            if abs(i - n / 2) > 0.6: ed.append((b + i, b + i + 1))   # skip across the trench
    # surface plating: small lat/long-aligned panels and city blocks scattered over the hull (not on the dish)
    rng = np.random.default_rng(a.seed); dishdir = None
    if parts:
        dv = np.vstack([(p.vertices - c) * sc @ R.T for p in parts]); dishdir = (dv.mean(0) - cen); dishdir /= np.linalg.norm(dishdir)
    def sp(la, lo, h=1.0): return cen + r * h * np.array([np.cos(la) * np.cos(lo), np.sin(la), np.cos(la) * np.sin(lo)])
    for _ in range(a.greeble):
        la = np.arcsin(rng.uniform(-0.93, 0.93)); lo = rng.uniform(0, 2 * np.pi)
        if abs(la) < np.radians(5): continue
        if dishdir is not None and np.dot(sp(la, lo) - cen, dishdir) / r > 0.88: continue
        dla = rng.uniform(0.015, 0.05); dlo = rng.uniform(0.02, 0.07) / max(0.3, np.cos(la)); h = 1 + rng.uniform(0.002, 0.012)
        b = len(pts) + sum(len(x) for x in V)
        if rng.random() < 0.6:   # raised block outline
            for q in [(la, lo), (la, lo + dlo), (la + dla, lo + dlo), (la + dla, lo)]: pts.append(sp(*q, h))
            ed += [(b, b + 1), (b + 1, b + 2), (b + 2, b + 3), (b + 3, b)]
        else:                     # panel seam: an L of two lines
            pts += [sp(la, lo, h), sp(la, lo + dlo * 2, h), sp(la + dla * 2, lo, h)]; ed += [(b, b + 1), (b, b + 2)]
    V.append(np.array(pts)); E.append(np.array(ed))
V = np.vstack(V); E = np.vstack(E)
L = np.linalg.norm(V[E[:, 0]] - V[E[:, 1]], axis=1); E = E[L > 0.003]; L = L[L > 0.003]
if len(E) > a.max: E = E[np.argsort(-L)[:a.max]]
used = np.unique(E); remap = -np.ones(len(V), int); remap[used] = np.arange(len(used)); V = V[used]; E = remap[E]
with open(a.out, 'w') as f:
    f.write('# vector strike wireframe from %s\n' % a.inp.split('/')[-1])
    for p in V: f.write('v %.4f %.4f %.4f\n' % tuple(p))
    for e in E: f.write('e %d %d\n' % tuple(e))
print('%s: %d verts %d edges, extents %s' % (a.out, len(V), len(E), (V.max(0) - V.min(0)).round(2)))
if a.preview:
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    fig, axs = plt.subplots(1, 3, figsize=(15, 5), facecolor='black')
    for ax, (i, j, t) in zip(axs, [(0, 2, 'top (z fwd)'), (0, 1, 'front'), (2, 1, 'side')]):
        ax.set_facecolor('black'); ax.set_title(t, color='w')
        for e in E: ax.plot(V[e, i], V[e, j], color='cyan', lw=0.6)
        ax.set_xlim(-1.05, 1.05); ax.set_ylim(-1.05, 1.05); ax.set_aspect('equal')
    plt.savefig(a.preview, dpi=60)
