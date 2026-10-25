// Vector Strike - a 1983-style wireframe space assault for 1-4 players in split screen (C++17 / SDL2).
// Each wave: dogfight the interceptors, strafe the surface towers, then run the trench and hit the exhaust port.
#include "gfx.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <array>

static Font FONT;
static Audio AU;
static Rng R;

// ---------------------------------------------------------------- 3D math
struct V3 { float x = 0, y = 0, z = 0; V3() {} V3(float a, float b, float c) : x(a), y(b), z(c) {} };
inline V3 operator+(V3 a, V3 b) { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline V3 operator-(V3 a, V3 b) { return V3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline V3 operator*(V3 a, float s) { return V3(a.x * s, a.y * s, a.z * s); }
inline V3& operator+=(V3& a, V3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline float dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross3(V3 a, V3 b) { return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline float len3(V3 a) { return std::sqrt(dot3(a, a)); }
inline V3 norm3(V3 a) { float l = len3(a); return l > 1e-6f ? a * (1 / l) : V3(0, 0, 1); }
inline V3 rndDir() { V3 d(R.range(-1, 1), R.range(-1, 1), R.range(-1, 1)); return norm3(d); }

struct Cam {
    V3 p, r, u, f; SDL_Rect vp = { 0, 0, 1, 1 }; float foc = 1, fogNear = 650, fogFar = 1500;
    void set(V3 pos, float yaw, float pitch, float roll, SDL_Rect v, float vfov) {
        p = pos; vp = v;
        V3 fw(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
        V3 rt(std::cos(yaw), 0, -std::sin(yaw));
        V3 up = cross3(fw, rt);
        float c = std::cos(roll), s = std::sin(roll);
        r = rt * c - up * s; u = up * c + rt * s; f = fw;
        foc = vp.h * 0.5f / std::tan(vfov * 0.5f);
    }
    void setBasis(V3 pos, V3 fw, V3 upv, float roll, SDL_Rect v, float vfov) {
        p = pos; vp = v; f = norm3(fw); V3 rt = norm3(cross3(upv, f)); V3 up = cross3(f, rt);
        float c = std::cos(roll), s = std::sin(roll); r = rt * c - up * s; u = up * c + rt * s;
        foc = vp.h * 0.5f / std::tan(vfov * 0.5f);
    }
    V3 toCam(V3 w) const { V3 d = w - p; return V3(dot3(d, r), dot3(d, u), dot3(d, f)); }
    V2 scr(V3 c) const { return V2(vp.x + vp.w * 0.5f + foc * c.x / c.z, vp.y + vp.h * 0.5f - foc * c.y / c.z); }
    float fog(float z) const { return clampf(1 - (z - fogNear) / (fogFar - fogNear), 0, 1); }
    bool proj(V3 w, V2& out, float& z) const { V3 c = toCam(w); if (c.z < 0.8f) return false; out = scr(c); z = c.z; return true; }
    V3 ray(V2 off) const { return norm3(f + r * (off.x / foc) + u * (-off.y / foc)); }
};

static float LW = 1;   // line width scale for the view being drawn
static void line3(Canvas& cv, const Cam& cam, V3 a, V3 b, float w, Col c, float glow = 1) {
    V3 ca = cam.toCam(a), cb = cam.toCam(b); const float N = 0.8f;
    if (ca.z < N && cb.z < N) return;
    if (ca.z < N) ca = ca + (cb - ca) * ((N - ca.z) / (cb.z - ca.z));
    else if (cb.z < N) cb = cb + (ca - cb) * ((N - cb.z) / (ca.z - cb.z));
    if (ca.z > cam.fogFar && cb.z > cam.fogFar) return;
    float tx = cam.vp.w * 0.5f / cam.foc * 1.15f, ty = cam.vp.h * 0.5f / cam.foc * 1.15f;
    if ((ca.x > tx * ca.z && cb.x > tx * cb.z) || (ca.x < -tx * ca.z && cb.x < -tx * cb.z)) return;
    if ((ca.y > ty * ca.z && cb.y > ty * cb.z) || (ca.y < -ty * ca.z && cb.y < -ty * cb.z)) return;
    float dz = std::fabs(cb.z - ca.z); int n = dz > 150 ? std::min(12, (int)(dz / 150) + 1) : 1;
    for (int i = 0; i < n; i++) {
        V3 p0 = ca + (cb - ca) * (i / (float)n), p1 = ca + (cb - ca) * ((i + 1) / (float)n);
        float fg = cam.fog((p0.z + p1.z) * 0.5f); if (fg <= 0.01f) continue;
        cv.line(cam.scr(p0), cam.scr(p1), w * LW, withA(c, fg), glow);
    }
}
static void blob3(Canvas& cv, const Cam& cam, V3 p, float r, Col c, float minPx = 1.5f) {
    V2 s; float z; if (!cam.proj(p, s, z)) return; float fg = cam.fog(z); if (fg <= 0.01f) return;
    cv.glowDisc(s, std::max(minPx * LW, r * cam.foc / z), withA(c, fg));
}

// ---------------------------------------------------------------- models
struct Model { std::vector<V3> v; std::vector<int> e; std::vector<V3> n; bool sorted = false; const Model* far = nullptr;   // far: decimated stand-in once small on screen
      // n: 2 face normals per edge (zero = always drawn)
    void E(std::initializer_list<int> l) { for (int x : l) e.push_back(x); } };
static Model M_FIGHTER, M_SHIP, M_CITADEL, M_TURRET, PK_PLAYER, PK_ENEMY, PK_STATION, PK_COCKPIT, PK_YWING, PK_FALCON, PK_DESTROYER, PK_TRENCH, PK_BWING, PK_BWING_CK, PK_ARWING, PK_ATAT, PK_SPEEDER, PK_PROBE, PK_HTURRET;
static Model PK_FAR[7], PK_ACE;   // *_far.vsm: player, enemy, ywing, bwing, falcon, destroyer, ace; PK_ACE: the enemy ace's ship
// PK_TRENCH: one repeating tile of trench walls/floor (tools/trench_tile.py)
// battle station radius (true scale would be ~70000: from close in that reads as a flat wall)
// PK_COCKPIT / PK_BWING_CK: full-detail outer wings + cannons for the cockpit views; TRENCH_LINES: trench-tile lines up close (longest first)
static float TRENCH_TILE = 0; static float ST_RADIUS = 20000; static int TRENCH_LINES = 1800;
static Model* MP = &M_SHIP; static Model* ME = &M_FIGHTER; static Model* MS = &M_CITADEL;
static float S_PLAYER = 0.9f, S_ENEMY = 2.5f, S_STATION = 0.75f;   // draw scales (station: times its radius)
// .vsm wireframe from tools/glb2vsm.py: 'v x y z' and 'e a b' lines, longest half-extent 1, +z forward, +y up
static bool loadVsm(const std::string& path, Model& m) {
    FILE* f = fopen(path.c_str(), "r"); if (!f) return false;
    Model n; char line[256];
    while (fgets(line, sizeof line, f)) {
        float x, y, z; int a, b;
        if (line[0] == 'v' && sscanf(line + 1, "%f %f %f", &x, &y, &z) == 3) n.v.push_back(V3(x, y, z));
        else if (line[0] == 'e') {
            float q[6] = {}; int k = sscanf(line + 1, "%d %d %f %f %f %f %f %f", &a, &b, q, q + 1, q + 2, q + 3, q + 4, q + 5);
            if (k >= 2 && a >= 0 && b >= 0) { n.e.push_back(a); n.e.push_back(b); n.n.push_back(k == 8 ? V3(q[0], q[1], q[2]) : V3()); n.n.push_back(k == 8 ? V3(q[3], q[4], q[5]) : V3()); }
        }
    }
    fclose(f);
    for (int i : n.e) if (i >= (int)n.v.size()) { printf("model: %s has a bad edge index\n", path.c_str()); return false; }
    if (n.e.size() < 6) return false;
    std::vector<std::pair<float, int>> L; for (size_t i = 0; i + 1 < n.e.size(); i += 2) L.push_back({ -len3(n.v[n.e[i]] - n.v[n.e[i + 1]]), (int)i });
    std::sort(L.begin(), L.end()); std::vector<int> e2; std::vector<V3> n2;
    for (auto& q : L) { e2.push_back(n.e[q.second]); e2.push_back(n.e[q.second + 1]); n2.push_back(n.n[q.second]); n2.push_back(n.n[q.second + 1]); }
    bool any = false; for (auto& v : n2) if (len3(v) > 0.1f) any = true;
    n.e = e2; n.n = any ? n2 : std::vector<V3>(); n.sorted = true; m = n;
    printf("model: loaded %s (%d verts, %d edges)\n", path.c_str(), (int)m.v.size(), (int)m.e.size() / 2);
    return true;
}
static void buildModels() {
    // Interceptor: delta arrowhead with twin wingtip fins and a ventral keel
    M_FIGHTER.v = { V3(0, 0, 4.5f), V3(-4.2f, 0, -3), V3(4.2f, 0, -3), V3(0, 0, -2), V3(0, 1.0f, 0.8f), V3(0, -1.2f, -1.2f),
                    V3(-4.2f, 1.6f, -3.8f), V3(4.2f, 1.6f, -3.8f), V3(-1.6f, 0, -2.4f), V3(1.6f, 0, -2.4f) };
    M_FIGHTER.E({ 0, 1, 0, 2, 1, 8, 8, 3, 3, 9, 9, 2, 0, 4, 4, 3, 0, 5, 5, 3, 1, 6, 6, 8, 2, 7, 7, 9, 4, 8, 4, 9 });
    // Player dart: slim fuselage, straight wings, wingtip cannons, dorsal fin
    M_SHIP.v = { V3(0, 0, 5), V3(-0.9f, 0, -2.5f), V3(0.9f, 0, -2.5f), V3(0, 0.9f, -1.8f), V3(-4.2f, 0, -0.8f), V3(4.2f, 0, -0.8f),
                 V3(-4.2f, 0, -2.6f), V3(4.2f, 0, -2.6f), V3(-4.2f, 0, 1.8f), V3(4.2f, 0, 1.8f), V3(0, 1.8f, -3.0f), V3(-0.9f, 0, -0.4f), V3(0.9f, 0, -0.4f) };
    M_SHIP.E({ 0, 1, 0, 2, 1, 2, 0, 3, 3, 1, 3, 2, 11, 4, 4, 6, 6, 1, 12, 5, 5, 7, 7, 2, 4, 8, 5, 9, 3, 10, 10, 1, 10, 2 });
    // Citadel: hexagonal bipyramid fortress with a girdle trench and spires
    for (int i = 0; i < 6; i++) { float a = i * PI / 3; M_CITADEL.v.push_back(V3(std::cos(a), 0.06f, std::sin(a))); }
    for (int i = 0; i < 6; i++) { float a = i * PI / 3; M_CITADEL.v.push_back(V3(std::cos(a), -0.06f, std::sin(a))); }
    for (int i = 0; i < 6; i++) { float a = i * PI / 3 + PI / 6; M_CITADEL.v.push_back(V3(std::cos(a) * 0.55f, 0.75f, std::sin(a) * 0.55f)); }
    for (int i = 0; i < 6; i++) { float a = i * PI / 3 + PI / 6; M_CITADEL.v.push_back(V3(std::cos(a) * 0.55f, -0.75f, std::sin(a) * 0.55f)); }
    M_CITADEL.v.push_back(V3(0, 1.45f, 0)); M_CITADEL.v.push_back(V3(0, -1.45f, 0)); M_CITADEL.v.push_back(V3(0, 2.0f, 0)); M_CITADEL.v.push_back(V3(0, -1.8f, 0));
    for (int i = 0; i < 6; i++) {
        int j = (i + 1) % 6;
        M_CITADEL.E({ i, j, 6 + i, 6 + j, i, 6 + i, i, 12 + i, j, 12 + i, 6 + i, 18 + i, 6 + j, 18 + i, 12 + i, 12 + j, 18 + i, 18 + j, 12 + i, 24, 18 + i, 25 });
    }
    M_CITADEL.E({ 24, 26, 25, 27 });
    // Wall turret: box with a barrel
    M_TURRET.v = { V3(-1.5f, -1.5f, -1.5f), V3(1.5f, -1.5f, -1.5f), V3(1.5f, 1.5f, -1.5f), V3(-1.5f, 1.5f, -1.5f), V3(-1.5f, -1.5f, 1.5f), V3(1.5f, -1.5f, 1.5f), V3(1.5f, 1.5f, 1.5f), V3(-1.5f, 1.5f, 1.5f), V3(0, 0, 0), V3(0, 0, 4.0f) };
    M_TURRET.E({ 0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7, 8, 9 });
}
static void basisFrom(V3 fw, float roll, V3& rt, V3& up) {
    fw = norm3(fw); rt = cross3(V3(0, 1, 0), fw); if (len3(rt) < 1e-3f) rt = V3(1, 0, 0); rt = norm3(rt); up = cross3(fw, rt);
    if (roll != 0) { float c = std::cos(roll), s = std::sin(roll); V3 r2 = rt * c - up * s, u2 = up * c + rt * s; rt = r2; up = u2; }
}
// hidden back lines: an edge whose two faces both point away from the camera is on the far side of the ship
static inline bool backEdge(const Model& m, size_t i, const Cam& cam, V3 mid, V3 rt, V3 up, V3 fw) {
    if (m.n.empty()) return false;
    V3 a = m.n[i], b = m.n[i + 1]; if (a.x == 0 && a.y == 0 && a.z == 0) return false;
    V3 v = cam.p - mid, na = rt * a.x + up * a.y + fw * a.z, nb = rt * b.x + up * b.y + fw * b.z;
    return dot3(na, v) < 0 && dot3(nb, v) < 0;
}
static bool SPHERE_CULL = false, FULL_DETAIL = false;   // FULL_DETAIL: draw every edge (player ships)   // set while drawing the station: hide lines on the far side of the globe
static void drawModelRU(Canvas& cv, const Cam& cam, const Model& m, V3 pos, V3 fw, V3 rt, V3 up, float s, Col c, float w, float glow) {
    size_t ne = m.e.size();
    if (m.sorted) {   // level of detail: the longest edges first, fewer when small on screen (player ships keep more: whole body up close)
        V3 cc = cam.toCam(pos); if (cc.z < -s * 1.5f) return;
        if (cc.z > 1 && m.far && s * cam.foc / cc.z < 70) { drawModelRU(cv, cam, *m.far, pos, fw, rt, up, s, c, w, glow); return; }
        if (cc.z > 1) { float rpx = s * cam.foc / cc.z; ne = std::min(ne, (size_t)(FULL_DETAIL ? (rpx < 6 ? 200 : std::max(300, (int)(rpx * 12))) : std::max(40, (int)(rpx * 8))) * 2); glow *= rpx < 25 ? 0.5f : 0.35f; w *= 0.85f; }
    }
    for (size_t i = 0; i + 1 < ne; i += 2) {
        V3 a = m.v[m.e[i]], b = m.v[m.e[i + 1]];
        V3 wa = pos + rt * (a.x * s) + up * (a.y * s) + fw * (a.z * s), wb = pos + rt * (b.x * s) + up * (b.y * s) + fw * (b.z * s);
        if (backEdge(m, i, cam, (wa + wb) * 0.5f, rt, up, fw)) { if (ne < m.e.size()) ne += 2; continue; }   // hidden side: spend the budget on a visible line
        if (SPHERE_CULL) {
            V3 mid = (wa + wb) * 0.5f, nrm = mid - pos; float nl = len3(nrm);
            if (nl > s * 0.6f && dot3(nrm, cam.p - mid) < -0.05f * nl * len3(cam.p - mid)) continue;
        }
        line3(cv, cam, wa, wb, w, c, glow);
    }
}
static void drawModelUp(Canvas& cv, const Cam& cam, const Model& m, V3 pos, V3 fw, V3 upv, float roll, float s, Col c, float w = 1.3f, float glow = 1) {
    fw = norm3(fw); V3 rt = norm3(cross3(upv, fw)); V3 up = cross3(fw, rt);
    if (roll != 0) { float cs = std::cos(roll), sn = std::sin(roll); V3 r2 = rt * cs - up * sn, u2 = up * cs + rt * sn; rt = r2; up = u2; }
    drawModelRU(cv, cam, m, pos, fw, rt, up, s, c, w, glow);
}
static void drawModel(Canvas& cv, const Cam& cam, const Model& m, V3 pos, V3 fw, float roll, float s, Col c, float w = 1.3f, float glow = 1) {
    V3 rt, up; basisFrom(fw, roll, rt, up); fw = norm3(fw);
    if (m.sorted) { drawModelRU(cv, cam, m, pos, fw, rt, up, s, c, w, glow); return; }
    for (size_t i = 0; i + 1 < m.e.size(); i += 2) {
        V3 a = m.v[m.e[i]], b = m.v[m.e[i + 1]];
        line3(cv, cam, pos + rt * (a.x * s) + up * (a.y * s) + fw * (a.z * s), pos + rt * (b.x * s) + up * (b.y * s) + fw * (b.z * s), w, c, glow);
    }
}
// flyable ships from the model pack. eye = cockpit seat, muz = the four gun muzzles (model units, +z forward,
// order (sx,sy) = (-,-) (-,+) (+,-) (+,+)), k = size vs the X-wing (12.5 m = 11 units; Y-wing 16.2 m, B-wing 16.9 m tall).
// X-wing: from the seat the real wings sit out of view, so the cockpit view hides the body (|x| < bodyHalf) and moves
// the outer wings forward and apart until the cannons sit in the screen corners. Y-wing: hides the canopy round the eye.
struct Player;
struct Ship { const char* name; Model* m; Model* ck; float k; V3 eye; V3 muz[4]; float bodyHalf, warpY, warpZ, hideR; float turn, bomb; };   // turn: steering x, bomb: hit-size x on towers, turrets and the port
static Ship SHIPS[5] = {
    { "STRIKE FIGHTER", &PK_PLAYER, &PK_COCKPIT, 1.0f, V3(-0.044f, 0.0f, -0.35f), { V3(-0.852f, -0.17f, 0.373f), V3(-0.918f, 0.195f, 0.371f), V3(0.829f, -0.186f, 0.337f), V3(0.765f, 0.198f, 0.314f) }, 0.58f, 0.12f, 0.75f, 0, 1.0f, 1.0f },
    { "ASSAULT BOMBER", &PK_YWING, nullptr, 1.3f, V3(0.0f, 0.1f, 0.33f), { V3(-0.03f, 0.02f, 0.72f), V3(-0.03f, 0.03f, 0.72f), V3(0.03f, 0.02f, 0.72f), V3(0.03f, 0.03f, 0.72f) }, 0, 0, 0, 0.12f, 0.87f, 1.5f },
    { "HEAVY BOMBER", &PK_BWING, &PK_BWING_CK, 1.35f, V3(0.0f, 0.9f, 0.1f), { V3(-0.01f, -0.95f, 0.35f), V3(-0.654f, 0.299f, 0.353f), V3(0.0f, 0.8f, 0.28f), V3(0.653f, 0.299f, 0.353f) }, 0.45f, 0.35f, 0.9f, 0, 0.75f, 1.9f },
    { "FREIGHTER", &PK_FALCON, nullptr, 2.78f, V3(0.6f, 0.07f, 0.40f), { V3(-0.3f, 0.0f, 1.0f), V3(-0.3f, 0.03f, 1.0f), V3(0.28f, 0.0f, 1.0f), V3(0.28f, 0.03f, 1.0f) }, 0, 0, 0, 0.16f, 0.85f, 1.2f },   // level 2: everyone flies it (34.75 m)
    { "STAR FIGHTER", &PK_ARWING, nullptr, 1.25f, V3(0, 0.12f, 0.15f), { V3(-0.94f, -0.16f, -0.6f), V3(-0.94f, -0.1f, -0.6f), V3(0.94f, -0.16f, -0.6f), V3(0.94f, -0.1f, -0.6f) }, 0, 0, 0, 0.25f, 1.0f, 1.2f },   // Star Fox mode
};
static int NSHIPS = 1;   // ships whose model loaded (the X-wing is always first)
static const Ship& shipOf(const Player& p);
static bool modelCockpit(const Player& p) { return MP == &PK_PLAYER && !shipOf(p).m->v.empty(); }
static int muzzleIdx(int sx, int sy) { return (sx > 0 ? 2 : 0) + (sy > 0 ? 1 : 0); }
static V3 cockpitWarp(const Ship& sh, V3 v, float inside) {
    if (sh.bodyHalf <= 0) return v;
    float o = clampf((std::fabs(v.x) - sh.bodyHalf) / 0.2f, 0, 1) * inside;   // the cannons move as solid pieces (no stretching)
    return V3(v.x, v.y + (v.y > 0 ? sh.warpY : -sh.warpY) * o, v.z + sh.warpZ * o);
}
static void box3(Canvas& cv, const Cam& cam, V3 a, V3 b, Col c, float w = 1.2f, bool cross = false) {
    V3 p[8] = { V3(a.x, a.y, a.z), V3(b.x, a.y, a.z), V3(b.x, b.y, a.z), V3(a.x, b.y, a.z), V3(a.x, a.y, b.z), V3(b.x, a.y, b.z), V3(b.x, b.y, b.z), V3(a.x, b.y, b.z) };
    static const int e[24] = { 0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7 };
    for (int i = 0; i < 24; i += 2) line3(cv, cam, p[e[i]], p[e[i + 1]], w, c);
    if (cross) { line3(cv, cam, p[0], p[2], w * 0.7f, withA(c, 0.6f), 0.4f); line3(cv, cam, p[1], p[3], w * 0.7f, withA(c, 0.6f), 0.4f); }
}

// ---------------------------------------------------------------- data
static const Col PCOL[4] = { Col(70, 210, 255), Col(255, 175, 50), Col(130, 255, 110), Col(255, 100, 225) };
static const Col C_WHITE(255, 255, 255), C_DIM(115, 130, 160), C_GOOD(110, 255, 140), C_BAD(255, 80, 80);
static const Col C_ENEMY(255, 70, 90), C_BALL(255, 130, 40), C_TRENCH(60, 130, 255), C_GROUND(50, 120, 230), C_TOWER(90, 255, 150), C_CAT(255, 160, 40), C_PORT(255, 235, 90), C_CIT(150, 170, 255);

enum { ST_TITLE, ST_HELP, ST_OPTIONS, ST_LOBBY, ST_GAME, ST_ICON, ST_LOGO };
enum { PH_SPACE, PH_SURFACE, PH_TRENCH, PH_ESCAPE, PH_TALLY, PH_ASTEROIDS, PH_LIGHTSPEED, PH_TUNNEL, PH_TURRET, PH_FOX, PH_TANK, PH_HOTH };
enum { EN_FIGHTER, EN_TOWER, EN_TURRET, EN_PURSUER, EN_REACTOR };   // pursuer: chases you down the trench / tunnel
static const char* RESNAMES[5] = { "AUTO", "1280 X 720", "1920 X 1080", "2560 X 1440", "3840 X 2160" };
static const int RESH[5] = { 0, 720, 1080, 1440, 2160 };
static const char* STARTNAMES[3] = { "EASY - WAVE 1", "MEDIUM - WAVE 3", "HARD - WAVE 5" };
static const int STARTWAVE[3] = { 1, 3, 5 };
static const int STARTBONUS[3] = { 0, 40000, 100000 };
static const float GROUND_Y = -40, TR_W = 22, TR_FLOOR = -42, TR_LIP_MAX = 9;   // TR_LIP_MAX: how far you can pull up out of the trench (top is y 0)
static const int MAXSH = 6;

struct Enemy { int kind = 0; V3 p, v; float hp = 1, cd = 1, t = 0, h = 0, flash = 0, weave = 0, hide = 0, tailT = 0, lostT = 0; int tgt = 0, state = 0, side = 0; bool dead = false, capDead = false;  bool ace = false; };
struct Ball { V3 p, v; float life = 6, spin = 0; bool dead = false; };
struct Bolt { V3 a, b; float t, max; Col c; int owner; int sx, sy; };
struct Frag { V3 p, v, d; float life, max; Col c; };
struct Flash { V3 p; float life, max, size; Col c; bool ring; };
struct Catwalk { float z, y0, y1; };
struct Star { V3 p; float b; };

enum { B_A, B_B, B_X, B_Y, B_LB, B_RB, B_START, B_BACK, B_UP, B_DOWN, B_LEFT, B_RIGHT, B_LS, NB };
struct Dev {
    int id = -1; SDL_GameController* gc = nullptr; std::string name; bool nintendo = false, hasGyro = false;
    bool b[NB] = {}, pb[NB] = {}; V2 mv; float tl = 0, tr = 0; int navX = 0, navY = 0, nxLast = 0, nyLast = 0; float nxT = 0, nyT = 0;
    bool ml = false, mouseMoved = false; V2 mouse; V3 gyro;
};
struct Player {
    bool joined = false, bot = false, invert = false, gyro = false, devLost = false, third = false; int dev = -9, idx = 0; Col col;
    float x = 0, y = 0, zoff = 0; V2 aim; float fireCd = 0; int gun = 0; int shields = MAXSH; bool down = false;
    float inv = 0, hitT = 0, shake = 0, downT = 0, bank = 0, pitchExtra = 0, botFire = 0; int score = 0, kills = 0, ballsShot = 0;
    V3 pos, fwd = V3(0, 0, 1), up = V3(0, 1, 0); float speed = 110, thr = 0;   // open-space flight
    int ship = 0;   // SHIPS index (lobby Up/Down)
    bool tkShell = false; float crackT = 0; int cable = -1; float cableAng = 0, cableSum = 0;   // tank mode / ice planet tow cable
    float rollT = 0, charge = 0; int rollDir = 1, foxHits = 0; bool botRoll = false;   // Star Fox mode: barrel roll, charge shot
    int turret = 0; float tYaw = 0, tPitch = 0.3f;   // level 2 turret fight: 0 top / 1 bottom gun, its aim
    float expT = 0, gunCd = 0;   // trench: time spent above the lip (exposed to the surface guns)
    Cam ck;   // the cockpit camera this frame (your own ship is drawn locked to it)
    float zoom = -1;   // view blend: 0 = in the cockpit, 1 = chase camera (eases between them)
    bool tailNow = false; float tailHold = 0, tailDist = 0, camBack = 0; std::string msg; float msgT = 0;   // enemy-on-your-tail state
    SDL_Rect vp = { 0, 0, 0, 0 }; Cam cam;
};
struct Opts { int res = 0; bool glow = true, shake = true, invert = false; int sfx = 8, mus = 6, turn = 5, chase = 5, trench = 1; bool radar = true; int best = 0, bestWave = 0; };
static float turnMul();

struct Game {
    SDL_Window* win = nullptr; SDL_Renderer* ren = nullptr; SDL_Texture* target = nullptr;
    int outW = 1280, outH = 720, W = 1280, H = 720; bool useTarget = false;
    Canvas cv; Opts opt; std::string cfgPath, dir;
    int state = ST_TITLE, menuSel = 0, startSel = 0; float stateT = 0;
    std::vector<Dev> devs; Player pl[4];
    // run
    int wave = 1, phase = PH_SPACE; float phaseT = 0, z = 0, speed = 50, zStart = 0, time = 0;
    int killsNeeded = 0, killsDone = 0; float spawnT = 0; float surfLen = 0, trenchZ0 = 0, trenchLen = 0; bool portHit = false; int portBy = -1; int runs = 0;
    float citDist = 3000, citSpin = 0; bool citBoom = false; float baseSpeed = 0, railMul = 1; V3 stC; float stR = 1400; bool approach = false;
    std::string banner, banner2; float bannerT = 0;
    std::vector<Enemy> en; std::vector<Ball> balls; std::vector<Bolt> bolts; std::vector<Frag> frags; std::vector<Flash> flashes; std::vector<Catwalk> cat; std::vector<Star> stars;
    std::vector<V3> sky; float runLen = 9000;
    int mode = 0;   // 0 the three levels, 1 STAR FOX MODE (fox.h), 2 TANK BATTLE (tank.h), 3 ICE PLANET (hoth.h)
    float tkSaucerT = 30, genHp = 100;
    float foxLen = 9000, foxWaveZ = 0, foxBossT = 0, foxBossX = 0, foxBossY = 0, foxClearT = -1; bool foxBoss = false;
    int level = 1, startLevel = 1; float aceCd = 20;   // level 1-3 (see levels.h); the enemy ace returns aceCd seconds after leaving
    bool chaseOn = false, chaseDone = false; float chaseT = 0;   // trench / tunnel pursuit
    float fieldLen = 10000, hyperT = -1; V3 falP; float falSpeed = 95, turretDoneT = -1;   // level 2 (falP: the freighter during the turret fight)
    float tunZ0 = 0, tunLen = 6000, outLen = 4200, fireZ = 0, fireSpeed = 0, fireT = 0; bool reactorHit = false, fireOn = false;   // level 3
    // space-stage extras from the model pack: a capital ship to fight around, and an allied freighter
    struct Capital { V3 p, f, r, u; float L = 704, gunCd = 2; }; std::vector<Capital> caps;   // the fleet you fly past on the way in
    struct Escort { V3 p, f, off; float z = 0, hp = 10, flash = 0, cd = 1; bool alive = true, gone = false; }; std::vector<Escort> esc; int escStage = 3; float escT = 0, escWaveT = 0; int escWaveLeft = 0;   // level 1 bombers
    struct Ally { bool on = false; float joinT = 0; V3 p, v; float cd = 1, t = 0; int tgt = -1; bool shown = false; } ally;
    bool paused = false; int pauseSel = 0; bool over = false; float overT = 0; int overSel = 0; bool newBest = false;
    int tallyBonus[4] = {}; bool quit = false;
    int shotFrame = -1; std::string shotPath = "shot.bmp"; bool bots = false; int frame = 0;
} G;

// ---------------------------------------------------------------- config
static void saveCfg() {
    FILE* f = fopen(G.cfgPath.c_str(), "w"); if (!f) return;
    fprintf(f, "res=%d\nglow=%d\nshake=%d\nsfx=%d\nmusic=%d\ninvert=%d\nturn=%d\nchase=%d\ntrench=%d\nradar=%d\nbest=%d\nbest_wave=%d\n", G.opt.res, G.opt.glow, G.opt.shake, G.opt.sfx, G.opt.mus, G.opt.invert, G.opt.turn, G.opt.chase, G.opt.trench, G.opt.radar, G.opt.best, G.opt.bestWave);
    fclose(f);
}
static void loadCfg() {
    FILE* f = fopen(G.cfgPath.c_str(), "r"); if (!f) return;
    char k[64]; long v;
    while (fscanf(f, " %63[^=]=%ld", k, &v) == 2) {
        std::string s = k;
        if (s == "res") G.opt.res = (int)clampf((float)v, 0, 4); else if (s == "glow") G.opt.glow = v != 0; else if (s == "shake") G.opt.shake = v != 0;
        else if (s == "sfx") G.opt.sfx = (int)clampf((float)v, 0, 10); else if (s == "music") G.opt.mus = (int)clampf((float)v, 0, 10);
        else if (s == "invert") G.opt.invert = v != 0; else if (s == "turn") G.opt.turn = (int)clampf((float)v, 1, 10); else if (s == "chase") G.opt.chase = (int)clampf((float)v, 1, 10); else if (s == "trench") G.opt.trench = (int)clampf((float)v, 0, 2); else if (s == "radar") G.opt.radar = v != 0;
        else if (s == "best") G.opt.best = (int)v; else if (s == "best_wave") G.opt.bestWave = (int)v;
    }
    fclose(f);
}
static float turnMul() { return 0.5f + 0.1f * G.opt.turn; }   // 1..10, 5 = normal
static float chaseMul() { return 0.4f + 0.12f * G.opt.chase; }   // 1..10 -> 0.52x .. 1.6x of the default chase distance
static void applyAudioVol() { AU.sfxVol = G.opt.sfx / 10.f; AU.musVol = G.opt.mus / 10.f * 0.85f; }
static void setupTarget() {
    if (G.target) { SDL_DestroyTexture(G.target); G.target = nullptr; }
    SDL_GetRendererOutputSize(G.ren, &G.outW, &G.outH);
    int h = RESH[G.opt.res];
    if (h == 0 || h == G.outH) { G.useTarget = false; G.W = G.outW; G.H = G.outH; }
    else {
        G.H = h; G.W = (int)(h * (float)G.outW / G.outH + 0.5f);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        G.target = SDL_CreateTexture(G.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, G.W, G.H);
        G.useTarget = G.target != nullptr; if (!G.useTarget) { G.W = G.outW; G.H = G.outH; }
    }
    G.cv.px = G.H / 1080.f;
    printf("video: output %dx%d, render %dx%d\n", G.outW, G.outH, G.W, G.H);
}

// ---------------------------------------------------------------- effects
static void explode3(V3 p, float size, Col c, V3 inherit = V3()) {
    int n = 10 + (int)(size * 2.5f);
    for (int i = 0; i < n && G.frags.size() < 3000; i++) { Frag f; f.p = p + rndDir() * (size * 0.3f); f.v = inherit + rndDir() * R.range(size * 3, size * 12); f.d = rndDir() * R.range(size * 0.2f, size * 0.7f); f.life = f.max = R.range(0.5f, 1.3f); f.c = R.f() < 0.3f ? C_WHITE : c; G.frags.push_back(f); }
    G.flashes.push_back({ p, 0.35f, 0.35f, size * 1.4f, C_WHITE, false });
    G.flashes.push_back({ p, 0.6f, 0.6f, size * 2.6f, c, true });
}
static void shakeAll(float a) { if (!G.opt.shake) return; for (auto& p : G.pl) if (p.joined) p.shake = std::max(p.shake, a); }
static void setBanner(const std::string& a, const std::string& b, float t = 3.0f) { G.banner = a; G.banner2 = b; G.bannerT = t; AU.play(SND_BANNER, 0.6f); }
static int aliveCount() { int n = 0; for (auto& p : G.pl) if (p.joined && !p.down) n++; return n; }
static int joinedCount() { int n = 0; for (auto& p : G.pl) if (p.joined) n++; return n; }
static bool isSpace() { return G.phase == PH_SPACE || (G.phase == PH_ESCAPE && G.citBoom) || G.phase == PH_TALLY || G.phase == PH_ASTEROIDS || G.phase == PH_LIGHTSPEED || G.phase == PH_TURRET; }
static bool freeGround() { return G.phase == PH_TANK || G.phase == PH_HOTH; }
static V3 ppos(const Player& p) { return isSpace() || freeGround() ? p.pos : V3(p.x, p.y, G.z + p.zoff); }
static float railChaseBack(const Player& p);   // how far back the chase camera must sit to frame the trench / tunnel pursuers (0 = none)
static bool viewThird(const Player& p) { if (G.phase == PH_TURRET) return false; if (G.phase == PH_FOX || G.phase == PH_HOTH) return !p.third; if (G.phase == PH_TANK) return p.third; return p.third || (p.tailHold > 0 && (G.phase == PH_SPACE || G.phase == PH_ASTEROIDS)) || railChaseBack(p) > 0; }
static V3 lerp3(V3 a, V3 b, float t) { return a + (b - a) * t; }
static void shipFrame(const Player& o, V3& pos, V3& fw, V3& rt, V3& up);
static void ownFrame(const Player& p, V3& pos, V3& fw, V3& rt, V3& up);
static void pmsg(Player& p, const std::string& m) { p.msg = m; p.msgT = 2.2f; }
static V3 pvel(const Player& p) { return isSpace() || freeGround() ? p.fwd * p.speed : V3(0, 0, G.speed); }
static V3 squadCenter() { V3 c; int n = 0; for (auto& p : G.pl) if (p.joined && !p.down) { c += ppos(p); n++; } return n ? c * (1.f / n) : V3(); }
static int randomAlive() { std::vector<int> v; for (int i = 0; i < 4; i++) if (G.pl[i].joined && !G.pl[i].down) v.push_back(i); return v.empty() ? 0 : v[R.i((int)v.size())]; }
static const Ship& shipOf(const Player& p) { if (G.mode == 1 && G.state == ST_GAME && !PK_ARWING.v.empty()) return SHIPS[4]; if (G.level == 2 && G.state == ST_GAME && !PK_FALCON.v.empty()) return SHIPS[3]; return SHIPS[std::min(p.ship, NSHIPS - 1)]; }
// cinematic scale, per camera: far ships are drawn bigger so they still read on screen (hit spheres grow the same,
// so what you see is what you can hit); 1 up close
static float visScale(V3 camP, V3 at, float from, float range, float extra) { return 1 + clampf((len3(at - camP) - from) / range, 0, 1) * extra; }
static float enemyVis(V3 camP, V3 at) { return isSpace() ? visScale(camP, at, 250, 1000, 0.9f) : visScale(camP, at, 200, 600, 0.5f); }
static float waveMul() { return 1 + 0.12f * (G.wave - 1); }

static void fireBall(V3 from, int tgt, float spdMul = 1, V3 aimOff = V3()) {   // aimOff: deliberate miss offset
    if (G.balls.size() > 60) return;
    Player& T = G.pl[tgt]; V3 tp = ppos(T) + V3(R.range(-2, 2), R.range(-2, 2), 0) + aimOff;
    Ball b; b.p = from; b.spin = R.range(0, 6.28f);
    if (isSpace()) {   // lead the target along its flight path
        float bs = (165 + 9.0f * G.wave) * spdMul; V3 tv = pvel(T), aimp = tp;
        for (int k = 0; k < 3; k++) aimp = tp + tv * (len3(aimp - from) / bs);
        b.v = norm3(aimp - from) * bs; b.life = len3(aimp - from) / bs + 2.0f;
    } else {
        float bs = (95 + 9.0f * G.wave) * spdMul; V3 rel = tp - from;
        float tz = std::max(0.1f, len3(rel) / bs);
        b.v = rel * (1 / tz) + V3(0, 0, G.speed); b.life = tz + 2.5f;   // reaches the player's slot as the squad flies on
    }
    G.balls.push_back(b);
    if (isSpace()) { float d = len3(from - squadCenter()); AU.play(SND_TIELASER, clampf(1.1f - d / 900, 0.15f, 0.8f), R.range(-0.5f, 0.5f), R.range(0.92f, 1.08f), 50); }
    else AU.play(SND_BALL, 0.35f, clampf(from.x / 120, -0.6f, 0.6f), R.range(0.9f, 1.1f), 60);
}

// ---------------------------------------------------------------- phases
static void placeFormation(float spread) {
    const float fx[4] = { -1, 1, -1, 1 }, fy[4] = { 0.5f, 0.5f, -0.6f, -0.6f };
    for (auto& p : G.pl) if (p.joined) { p.x = fx[p.idx] * spread * (joinedCount() > 1 ? 1 : 0); p.y = fy[p.idx] * spread * (joinedCount() > 2 ? 1 : 0); p.zoff = -12.f * p.idx; p.aim = V2(); p.bank = 0; p.pitchExtra = 0; }
}
static void setupAsteroids(); static void setupTunnel(); static void setupTurret(); static void setupEscortRun(); static void foxSetup(); static void tankSetup(); static void hothSetup();
static void beginPhase(int ph) {
    int prev = G.phase; G.railMul = 1;
    for (auto& q : G.pl) { q.tailNow = false; q.tailHold = 0; }
    G.phase = ph; G.phaseT = 0;
    if (prev == PH_SPACE && (ph == PH_SURFACE || ph == PH_TRENCH || ph == PH_TUNNEL)) {   // dive from open space onto the rails
        G.z = 0; placeFormation(10); for (auto& p : G.pl) if (p.joined) p.y += ph == PH_SURFACE ? 30 : -20;
    }
    G.zStart = G.z;
    G.en.clear(); G.balls.clear(); G.cat.clear();
    bool anyAlive = aliveCount() > 0;
    if (ph == PH_SPACE || ph == PH_SURFACE || ph == PH_TRENCH)
        for (auto& p : G.pl) if (p.joined && p.down && anyAlive) { p.down = false; p.shields = 3; p.inv = 2; printf("game: P%d back in with 3 shields\n", p.idx + 1); }
    int np = std::max(1, joinedCount());
    switch (ph) {
    case PH_SPACE: {
        G.speed = 0; G.killsDone = 0; G.killsNeeded = (int)((5 + 2 * G.wave) * (1 + 0.45f * (np - 1))); G.spawnT = 1.0f; G.citBoom = false; G.approach = false;
        G.runLen = 9000 + 1500.f * std::min(G.wave - 1, 6);   // a long run in to the battle station's surface
        G.caps.clear(); G.ally = Game::Ally();
        G.stR = ST_RADIUS; G.stC = V3(0, 0, G.runLen + G.stR);
        if (!PK_DESTROYER.v.empty()) {   // the fleet: Star Destroyers along the route, alternating sides, just off the direct line
            int n = std::min(7, 2 + G.wave); float sd = R.f() < 0.5f ? 1.f : -1.f;
            for (int i = 0; i < n; i++, sd = -sd) {
                Game::Capital c; float z = 2200 + (G.runLen - 4200) * (i + R.range(0.2f, 0.8f)) / n;
                c.p = V3(sd * R.range(480, 1500), R.range(-350, 250), z);
                if (R.f() < 0.7f) c.f = norm3(V3(sd * R.range(0.05f, 0.25f), R.range(-0.05f, 0.05f), -1));   // usually nose away from the station, toward you
                else c.f = norm3(V3(-sd * 0.9f, 0.04f, R.range(-0.4f, 0.4f)));                          // sometimes broadside across the route
                c.r = norm3(cross3(V3(0, 1, 0), c.f)); c.u = cross3(c.f, c.r); c.gunCd = R.range(1, 3);
                G.caps.push_back(c);
            }
        }
        if (!PK_FALCON.v.empty()) { G.ally.on = true; G.ally.joinT = 0; }   // waits until the squad is in trouble
        G.aceCd = 18;
        placeFormation(10);
        for (auto& p : G.pl) if (p.joined) { p.pos = V3(p.x * 5, p.y * 5, p.zoff * 4); p.fwd = V3(0, 0, 1); p.up = V3(0, 1, 0); p.speed = 110; }
        G.approach = true;   // the run in: past the fleet to the battle station, interceptors all the way
        G.escStage = 3; G.esc.clear();
        setBanner("LEVEL " + std::to_string(G.level) + " - WAVE " + std::to_string(G.wave), G.level == 3 ? "THE UNFINISHED STATION - GET INSIDE IT" : G.caps.empty() ? "REACH THE BATTLE STATION" : "FLY PAST THE FLEET TO THE BATTLE STATION", 3.5f);
        if (G.level == 1) setupEscortRun();
        break;
    }
    case PH_SURFACE: {
        G.speed = G.baseSpeed = 150 + 8.f * G.wave; G.surfLen = 2400 + 150.f * G.wave;
        float zz = G.z + 380;
        while (zz < G.z + G.surfLen) {
            int k = 1 + (R.f() < 0.25f + 0.04f * G.wave ? 1 : 0);
            for (int i = 0; i < k; i++) { Enemy e; e.kind = EN_TOWER; e.h = R.range(18, 58); e.p = V3(R.range(-135, 135), GROUND_Y + e.h, zz + R.range(-15, 15)); e.cd = R.range(0.5f, 2.0f); G.en.push_back(e); }
            zz += R.range(70, 120) / std::min(1.6f, 0.9f + 0.08f * G.wave);
        }
        setBanner("SURFACE ASSAULT", "SHOOT THE TOWER TOPS - FLY HIGH OVER THEM");
        break;
    }
    case PH_TRENCH: {
        G.speed = G.baseSpeed = 165 + 11.f * G.wave; G.trenchZ0 = G.z; G.trenchLen = 5600 + 560.f * std::min(G.wave, 8); G.portHit = false; G.portBy = -1;   // long run: ~35 s at wave 1
        for (auto& p : G.pl) if (p.joined) { p.x = clampf(p.x, -16, 16); p.zoff = -12.f * p.idx; p.expT = 0; p.gunCd = 0; }
        G.chaseOn = false; G.chaseDone = false; G.ally.shown = false;
        int nt = 12 + 4 * G.wave;
        for (int i = 0; i < nt; i++) { Enemy e; e.kind = EN_TURRET; e.side = R.f() < 0.5f ? -1 : 1; e.p = V3(e.side * (TR_W - 1.5f), R.range(-36, -8), G.trenchZ0 + R.range(350, G.trenchLen - 250)); e.cd = R.range(0.5f, 2.5f); G.en.push_back(e); }
        int nc = G.wave >= 2 ? std::min(32, 4 + (G.wave - 2) * 4) : 0;
        for (int i = 0; i < nc; i++) {
            float zc = G.trenchZ0 + 600 + (G.trenchLen - 900) * (i + 0.5f) / nc + R.range(-40, 40);
            int lvl = R.i(3); float y0 = TR_FLOOR + 4 + lvl * 12.5f;
            G.cat.push_back({ zc, y0, y0 + 7 });
            if (G.wave >= 5 && R.f() < 0.4f) { int l2 = (lvl + 1 + R.i(2)) % 3; float y2 = TR_FLOOR + 4 + l2 * 12.5f; G.cat.push_back({ zc + 6, y2, y2 + 7 }); }
        }
        setBanner(G.runs ? "GO AROUND - TRENCH AGAIN" : "TRENCH RUN", "HIT THE EXHAUST PORT AT THE END");
        break;
    }
    case PH_ESCAPE: { AU.play(SND_PORT, 1.0f); shakeAll(28); setBanner("DIRECT HIT", "PULL UP"); break; }
    case PH_ASTEROIDS: {
        G.speed = 0; G.killsDone = 0; G.killsNeeded = 0; G.spawnT = 3.0f; G.citBoom = false;
        G.stR = ST_RADIUS; G.stC = V3(0, 0, 1e7f);   // no station on this level
        V3 at(0, 0, -200); if (prev == PH_TURRET) at = G.falP; else setupAsteroids();
        placeFormation(10); G.aceCd = 25; G.killsDone = 0; G.spawnT = 3.0f;
        for (auto& p : G.pl) if (p.joined) { p.pos = at + V3(p.x * 9, p.y * 9, p.zoff * 8); p.fwd = V3(0, 0, 1); p.up = V3(0, 1, 0); p.speed = 140; p.zoom = -1; }
        if (prev != PH_TURRET) setBanner("LEVEL 2 - WAVE " + std::to_string(G.wave), "INTO THE ASTEROIDS - LOSE THEM, THEN JUMP TO LIGHT SPEED", 3.5f);
        else setBanner("INTO THE ASTEROIDS", "LOSE THEM, THEN JUMP TO LIGHT SPEED", 3.0f);
        break;
    }
    case PH_LIGHTSPEED: { AU.play(SND_PORT, 0.9f, 0, 1.3f); setBanner("LIGHT SPEED", "", 2.5f); for (auto& p : G.pl) if (p.joined && !p.down) G.portBy = p.idx; break; }
    case PH_TUNNEL: { setupTunnel(); break; }
    case PH_TANK: case PH_HOTH: { G.speed = 0; G.z = 0; G.stR = ST_RADIUS; G.stC = V3(0, 0, 1e7f); G.caps.clear(); G.ally = Game::Ally(); G.esc.clear(); G.en.clear(); if (ph == PH_TANK) tankSetup(); else hothSetup(); break; }
    case PH_FOX: { G.stR = ST_RADIUS; G.stC = V3(0, 0, 1e7f); G.caps.clear(); G.ally = Game::Ally(); G.esc.clear(); placeFormation(14); for (auto& p : G.pl) if (p.joined) p.y += 10; foxSetup(); break; }
    case PH_TURRET: { G.speed = 0; G.citBoom = false; G.stR = ST_RADIUS; G.stC = V3(0, 0, 1e7f); G.caps.clear(); G.ally = Game::Ally(); setupTurret(); break; }
    case PH_TALLY: {
        for (auto& p : G.pl) if (p.joined) {
            int b = 0; if (!p.down) b += p.idx == G.portBy ? 25000 : 10000; b += 2000 * (p.down ? 0 : p.shields);
            G.tallyBonus[p.idx] = b; p.score += b;
            if (!p.down) p.shields = std::min(MAXSH, p.shields + 1);
        }
        AU.play(SND_WAVE, 0.9f);
        break;
    }
    }
    printf("game: wave %d phase %d z %.0f speed %.0f enemies %d catwalks %d alive %d\n", G.wave, ph, G.z, G.speed, (int)G.en.size(), (int)G.cat.size(), aliveCount());
}

static void layoutViews() {
    std::vector<Player*> v; for (auto& q : G.pl) if (q.joined) v.push_back(&q);
    int W = G.W, H = G.H, g = std::max(2, H / 360), n = (int)v.size();
    if (n == 1) v[0]->vp = { 0, 0, W, H };
    else if (n == 2) { v[0]->vp = { 0, 0, W, H / 2 - g / 2 }; v[1]->vp = { 0, H / 2 + g / 2, W, H - H / 2 - g / 2 }; }
    else if (n >= 3) {
        SDL_Rect q[4] = { { 0, 0, W / 2 - g / 2, H / 2 - g / 2 }, { W / 2 + g / 2, 0, W - W / 2 - g / 2, H / 2 - g / 2 }, { 0, H / 2 + g / 2, W / 2 - g / 2, H - H / 2 - g / 2 }, { W / 2 + g / 2, H / 2 + g / 2, W - W / 2 - g / 2, H - H / 2 - g / 2 } };
        for (int i = 0; i < n; i++) v[i]->vp = q[i];
    }
}
static void startGame() {
    R.s = (uint32_t)(SDL_GetTicks() * 2654435761u) | 1;
    if (getenv("VS_SEED")) R.s = (uint32_t)atoi(getenv("VS_SEED")) | 1;
    G.wave = STARTWAVE[G.startSel]; if (getenv("VS_WAVE")) G.wave = std::max(1, atoi(getenv("VS_WAVE")));
    G.z = 0; G.time = 0; G.runs = 0; G.over = false; G.overT = 0; G.paused = false; G.newBest = false;
    G.bolts.clear(); G.frags.clear(); G.flashes.clear();
    for (auto& p : G.pl) if (p.joined) { p.shields = MAXSH; p.down = false; p.score = STARTBONUS[G.startSel]; p.kills = 0; p.ballsShot = 0; p.inv = 0; p.hitT = 0; p.shake = 0; p.fireCd = 0; }
    G.stars.clear(); for (int i = 0; i < 500; i++) G.stars.push_back({ V3(R.range(0, 1300), R.range(0, 1300), R.range(0, 1300)), R.range(0.3f, 1) });
    layoutViews();
    G.state = ST_GAME; G.stateT = 0;
    G.level = G.startLevel; if (getenv("VS_LEVEL")) G.level = clampf((float)atoi(getenv("VS_LEVEL")), 1, 3);
    if (getenv("VS_FOX")) G.mode = 1;
    if (getenv("VS_MODE")) G.mode = atoi(getenv("VS_MODE"));
    G.phase = PH_SPACE; if (G.mode == 1) beginPhase(PH_FOX); else if (G.mode == 2) beginPhase(PH_TANK); else if (G.mode == 3) beginPhase(PH_HOTH); else beginPhase(G.level != 2 ? PH_SPACE : PK_FALCON.v.empty() ? PH_ASTEROIDS : PH_TURRET);
    printf("game: start level %d wave %d players %d\n", G.level, G.wave, joinedCount());
}

// ---------------------------------------------------------------- shooting
static float crossRange(const Player& p, bool horiz) { float hw = p.vp.w * 0.5f, hh = p.vp.h * 0.5f; return horiz ? std::min(hw, hh * 1.5f) * 0.66f : hh * 0.62f; }
static V2 crossOff(const Player& p) { if (G.phase == PH_TURRET) return V2(); return V2(p.aim.x * crossRange(p, true), -p.aim.y * crossRange(p, false)); }
static V3 portPos() { return V3(0, TR_FLOOR + 0.5f, G.trenchZ0 + G.trenchLen); }

static void hitPlayer(Player& p, const char* why) {
    if (p.down || p.inv > 0) return;
    p.shields--; p.inv = 0.9f; p.hitT = 0.5f; if (G.opt.shake) p.shake = std::max(p.shake, 16.f);
    AU.play(SND_SHIELD, 0.9f);
    printf("game: P%d hit by %s, shields %d\n", p.idx + 1, why, p.shields);
    if (p.shields <= 0) {
        p.down = true; p.downT = 0; AU.play(SND_DOWN, 0.9f); explode3(ppos(p) + V3(0, 0, 8), 4, p.col);
        if (aliveCount() == 0) {
            G.over = true; G.overT = 0; G.overSel = 0; AU.play(SND_LOSE, 0.9f);
            int best = 0; for (auto& q : G.pl) if (q.joined) best = std::max(best, q.score);
            if (best > G.opt.best) { G.opt.best = best; G.newBest = true; } if (G.wave > G.opt.bestWave) G.opt.bestWave = G.wave; saveCfg();
            printf("game: over at wave %d, best score this run %d\n", G.wave, best);
        }
    }
}
#include "levels.h"
static Dev* devById(int id); static bool pr(const Dev& d, int b);
#include "fox.h"
#include "tank.h"
#include "hoth.h"
static void playerFire(Player& p) {
    p.fireCd = 0.16f; p.gun ^= 1;
    Cam& c = p.cam; V3 ray = c.ray(crossOff(p));
    // hitscan: nearest target sphere along the ray
    float bestT = 1400; int type = 0, idx = -1;
    auto test = [&](V3 cen, float rad, int ty, int i) {
        V3 oc = cen - c.p; float t = dot3(oc, ray); if (t < 2 || t > bestT) return;
        float d2 = dot3(oc, oc) - t * t; if (d2 < rad * rad) { bestT = t; type = ty; idx = i; }
    };
    for (int i = 0; i < (int)G.en.size(); i++) {
        Enemy& e = G.en[i]; if (e.dead) continue;
        if (e.kind == EN_FIGHTER && e.state != 4) test(e.p, std::max(6.5f, S_ENEMY * (e.ace ? 1.25f : 1.1f)) * enemyVis(c.p, e.p), 1, i);
        else if (e.kind == EN_REACTOR) test(e.p, 30 * shipOf(p).bomb, 1, i);
        else if (e.kind == EN_TOWER && !e.capDead) test(e.p + V3(0, 3, 0), 7.5f * shipOf(p).bomb, 1, i);
        else if (e.kind == EN_TURRET) test(e.p, 5.5f * shipOf(p).bomb, 1, i);
    }
    for (int i = 0; i < (int)G.balls.size(); i++) if (!G.balls[i].dead) test(G.balls[i].p, 5.0f, 2, i);
    if (G.phase == PH_ASTEROIDS) for (int i = 0; i < (int)ROCKS.size(); i++) { const Rock& k = ROCKS[i]; if (!k.dead && std::fabs(k.p.z - c.p.z) < 1500) test(k.p, k.r * 0.9f, 4, i); }
    if (G.phase == PH_TRENCH && !G.portHit) { V3 pp = portPos(); if (pp.z - c.p.z < 650) test(pp, 9.0f * shipOf(p).bomb, 3, 0); }
    V3 end = c.p + ray * bestT;
    // four wing cannons: from your own cockpit they come in from the screen edges (like the arcade cabinet);
    // wingmates see them leave your four wingtips
    V3 rt, up; basisFrom(isSpace() ? p.fwd : V3(0, 0, 1), 0, rt, up); if (isSpace()) { rt = norm3(cross3(p.up, p.fwd)); up = cross3(p.fwd, rt); }
    V3 ship = ppos(p), fw = isSpace() ? p.fwd : V3(0, 0, 1);
    for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) {
        V3 tip = ship + rt * (sx * 0.78f * S_PLAYER) + up * (sy * 0.25f * S_PLAYER) + fw * (0.3f * S_PLAYER);
        if (G.phase == PH_TURRET) { int pair = p.gun ? 1 : -1; if (sy != pair) continue; tip = tcp(c, turretBarrel((sy > 0 ? 0 : 2) + (sx > 0 ? 1 : 0), 1)); }
        else if (modelCockpit(p)) { V3 sp, sf, sr, su; shipFrame(p, sp, sf, sr, su); V3 m = shipOf(p).muz[muzzleIdx(sx, sy)] * (S_PLAYER * shipOf(p).k); tip = sp + sr * m.x + su * m.y + sf * m.z; }
        G.bolts.push_back({ tip, end, 0.16f, 0.16f, Col(255, 70, 60), p.idx, sx, sy });
    }
    AU.play(SND_LASER, 0.42f, clampf((p.vp.x + p.vp.w * 0.5f) / G.W * 2 - 1, -0.5f, 0.5f), R.range(0.95f, 1.05f), 40);
    if (idx < 0) return;
    if (type == 1) {
        Enemy& e = G.en[idx]; e.hp -= e.kind == EN_REACTOR ? shipOf(p).bomb : 1; e.flash = 0.1f;
        if (e.hp > 0) { AU.play(SND_BALLHIT, 0.4f); if (e.ace) pmsg(p, "HIT VADER - KEEP ON HIM"); return; }
        if (e.ace) { aceSpinAway(e, &p); return; }
        if (e.kind == EN_REACTOR) { e.dead = true; reactorDestroyed(p); return; }
        if (e.kind == EN_FIGHTER && e.state == 3 && e.tgt != p.idx && G.pl[e.tgt].joined) {
            Player& sv = G.pl[e.tgt]; p.score += 500; pmsg(p, "SAVED P" + std::to_string(sv.idx + 1) + "  +500"); pmsg(sv, "P" + std::to_string(p.idx + 1) + " SAVED YOU");
            printf("game: P%d shot the fighter off P%d's tail\n", p.idx + 1, sv.idx + 1);
        }
        if (e.kind == EN_FIGHTER) { e.dead = true; G.killsDone++; p.kills++; p.score += 1000; explode3(e.p, 5, C_ENEMY, V3(0, 0, G.speed)); AU.play(SND_BOOM_S, 0.75f); }
        else if (e.kind == EN_TOWER) { e.capDead = true; p.kills++; p.score += 250; explode3(e.p + V3(0, 3, 0), 5, C_TOWER, V3(0, 0, 0)); AU.play(SND_TOWER, 0.7f); }
        else { e.dead = true; p.kills++; p.score += 300; explode3(e.p, 4, C_CAT); AU.play(SND_BOOM_S, 0.6f); }
    } else if (type == 4) {
        Rock& k = ROCKS[idx];
        if (k.r < 30) { p.score += 100; breakRock(k, V3()); } else { G.flashes.push_back({ c.p + ray * bestT, 0.2f, 0.2f, 4, Col(220, 200, 170), false }); }
    } else if (type == 2) {
        Ball& b = G.balls[idx]; b.dead = true; p.ballsShot++; p.score += 50;
        G.flashes.push_back({ b.p, 0.25f, 0.25f, 6, C_BALL, false }); for (int k = 0; k < 6; k++) { Frag f; f.p = b.p; f.v = b.v * 0.5f + rndDir() * 40; f.d = rndDir() * 1.2f; f.life = f.max = 0.4f; f.c = C_BALL; G.frags.push_back(f); }
        AU.play(SND_BALLHIT, 0.55f);
    } else if (type == 3) {
        G.portHit = true; G.portBy = p.idx; p.score += 5000;
        V3 pp = portPos(); for (int k = 0; k < 4; k++) explode3(pp + V3(R.range(-6, 6), 2, R.range(-6, 6)), 9, C_PORT);
        printf("game: P%d hit the exhaust port on wave %d\n", p.idx + 1, G.wave);
        beginPhase(PH_ESCAPE);
    }
}

// ---------------------------------------------------------------- simulation
// capital ship hull test: a flat wedge (nose +z) plus the bridge tower at the back, in model units
static bool capInside1(const Game::Capital& c, V3 w, float margin) {
    V3 d = w - c.p; float L = c.L; V3 q(dot3(d, c.r) / L, dot3(d, c.u) / L, dot3(d, c.f) / L); float m = margin / L;
    if (q.z < -1 - m || q.z > 1 + m) return false;
    float hw = 0.557f * (1 - q.z) * 0.5f + m;
    if (std::fabs(q.x) < hw && q.y > -0.24f - m && q.y < 0.07f + m) return true;
    return q.z < -0.55f && std::fabs(q.x) < 0.22f + m && q.y > -0.24f && q.y < 0.28f + m;
}
// capital ship hull test: a flat wedge (nose +z) plus the bridge tower at the back, in model units; returns the ship or null
static const Game::Capital* capInside(V3 w, float margin) {
    for (auto& c : G.caps) if (len3(w - c.p) < c.L * 1.3f + margin && capInside1(c, w, margin)) return &c;
    return nullptr;
}
static V3 capSurfacePt(const Game::Capital& c) {   // a random gun spot on the top deck
    float z = R.range(-0.9f, 0.6f), hw = 0.557f * (1 - z) * 0.5f;
    V3 q(R.range(-hw, hw) * 0.85f, 0.07f, z); return c.p + (c.r * q.x + c.u * q.y + c.f * q.z) * c.L;
}
static void updateCapital(float dt) {
    for (auto& c : G.caps) {
        c.p += c.f * (6 * dt);
        for (auto& p : G.pl) if (p.joined && !p.down && capInside1(c, p.pos, 6)) {   // flew into the hull: bounce off the plating
            hitPlayer(p, "capital ship"); V3 n = c.u * (dot3(p.pos - c.p, c.u) > -0.08f * c.L ? 1.f : -1.f);
            p.pos += n * 30; p.fwd = norm3(p.fwd + n * 1.2f);
        }
        c.gunCd -= dt;   // deck guns fire at anyone flying close
        if (c.gunCd <= 0) {
            c.gunCd = std::max(0.5f, 1.6f - 0.08f * G.wave) * R.range(0.7f, 1.3f);
            int best = -1; float bd = 900;
            for (auto& p : G.pl) if (p.joined && !p.down) { float d = len3(p.pos - c.p) - c.L * 0.6f; if (d < bd) { bd = d; best = p.idx; } }
            if (best >= 0) { V3 m = capSurfacePt(c); G.flashes.push_back({ m, 0.2f, 0.2f, 9, C_ENEMY, false }); fireBall(m, best, 0.85f); }
        }
    }
}
// allied freighter: joins a few seconds in, hunts interceptors and helps clear the wave
static void updateAlly(float dt) {
    auto& A = G.ally; if (!A.on) return;
    if (!A.shown) {   // comes to save the day: someone nearly out of shields, a wingmate shot down, or a long tail chase
        bool trouble = false; int alive = 0, joined = 0;
        for (auto& p : G.pl) if (p.joined) { joined++; if (!p.down) { alive++; if (p.shields <= 2) trouble = true; } }
        if (alive < joined && alive > 0) trouble = true;
        for (auto& e : G.en) if (!e.dead && e.kind == EN_FIGHTER && e.state == 3 && e.tailT > 4) trouble = true;
        if (A.joinT > 0) A.joinT -= dt;
        if (trouble && A.joinT <= 0 && G.phaseT > 3 && aliveCount() > 0) A.joinT = -1;   // arm, then arrive a moment later
        if (A.joinT < 0) { A.joinT -= dt; if (A.joinT < -1.6f) {
            A.shown = true; V3 sc = squadCenter(); Player& T = G.pl[randomAlive()];
            A.p = sc - T.fwd * 700 + V3(0, 260, 0); A.v = norm3(sc - A.p) * 190;
            setBanner("FREIGHTER INBOUND", "YOU'RE ALL CLEAR - FRIENDLY, DON'T SHOOT IT"); AU.play(SND_TIEFLY, 1.0f, 0, 0.6f, 600);
            printf("game: freighter arrives to help\n");
        } }
        return;
    }
    A.t += dt; A.cd -= dt;
    if (A.tgt < 0 || A.tgt >= (int)G.en.size() || G.en[A.tgt].dead || G.en[A.tgt].kind != EN_FIGHTER || G.en[A.tgt].ace) {
        A.tgt = -1; float bd = 1e9;   // fighters sitting on someone's tail first
        for (int i = 0; i < (int)G.en.size(); i++) { auto& e = G.en[i]; if (e.dead || e.kind != EN_FIGHTER || e.ace) continue; float d = len3(e.p - A.p) - (e.state == 3 ? 2000 : 0); if (d < bd) { bd = d; A.tgt = i; } }
    }
    V3 want = A.tgt >= 0 ? G.en[A.tgt].p + G.en[A.tgt].v * 0.6f : squadCenter() + V3(std::sin(A.t * 0.4f) * 260, 70, 280);
    if (len3(want - A.p) < 160 && A.tgt >= 0) want = A.p + norm3(A.v) * 400 + V3(0, 160, 0);   // overshoot, come round again
    V3 cur = norm3(A.v), wd = norm3(want - A.p);
    if (auto c = capInside(A.p + cur * 150, 60)) wd = norm3(wd + c->u * 2);
    cur = norm3(cur + (wd - cur) * std::min(1.f, dt * 1.2f));
    A.v = cur * 175; A.p += A.v * dt;
    if (A.tgt >= 0 && A.cd <= 0) {
        Enemy& e = G.en[A.tgt]; V3 to = e.p - A.p; float d = len3(to);
        if (d < 650 && dot3(norm3(to), cur) > 0.93f) {
            A.cd = R.range(0.35f, 0.6f);
            V3 gun = A.p + G.ally.v * 0.02f + V3(0, 3, 0);
            G.bolts.push_back({ gun, e.p, 0.22f, 0.22f, Col(255, 120, 70), 9, 0, 0 });
            if (R.f() < 0.45f) { if (e.state == 3 && G.pl[e.tgt].joined) pmsg(G.pl[e.tgt], "FREIGHTER GOT IT OFF YOUR TAIL"); e.dead = true; G.killsDone++; explode3(e.p, 5, C_ENEMY, e.v * 0.5f); AU.play(SND_BOOM_S, 0.5f); printf("game: freighter shot down a fighter\n"); }
        }
    }
}
static void spawnFighterNear(Enemy& e) {
    V3 sc = squadCenter(); V3 toSt = norm3(G.stC - sc); V3 d = norm3(toSt * 1.1f + rndDir());
    e.p = sc + d * R.range(850, 1150); e.v = norm3(sc - e.p) * 130; e.tgt = randomAlive(); e.state = 0; e.t = R.range(0, 5); e.tailT = e.lostT = 0;
    const Game::Capital* hc = nullptr; float hd = 4500;   // the nearest Star Destroyer ahead of the squad launches them
    for (auto& c : G.caps) { float d = len3(c.p - sc); if (d < hd && c.p.z > sc.z - 600) { hd = d; hc = &c; } }
    if (hc && R.f() < 0.75f && G.phase == PH_SPACE) {
        e.p = hc->p - hc->u * (0.26f * hc->L) + hc->f * (R.range(-0.5f, 0.2f) * hc->L); e.v = norm3(sc - e.p) * 130; return;
    }
    if (G.phase == PH_TURRET) { V3 d = rndDir(); d.y = (R.f() < 0.5f ? 1 : -1) * (0.25f + std::fabs(d.y)); e.p = G.falP + norm3(d + V3(0, 0, 0.6f)) * R.range(900, 1300); e.v = norm3(G.falP - e.p) * 140; return; }
    if (G.phase == PH_ASTEROIDS) {   // level 2: they come from behind (the pursuing Star Destroyers), mostly straight onto a tail
        Player& T = G.pl[e.tgt]; e.p = T.pos - T.fwd * R.range(500, 900) + rndDir() * 120; e.v = T.fwd * 150; return;
    }
    if (R.f() < 0.2f && G.phase == PH_SPACE) {   // sneak in from behind someone
        Player& T = G.pl[e.tgt]; e.p = T.pos - T.fwd * R.range(520, 750) + rndDir() * 60; e.v = T.fwd * 140;
    }
}
static void updateFighters(float dt) {
    int alive = 0; for (auto& e : G.en) if (!e.dead && e.kind == EN_FIGHTER && !e.ace) alive++;
    int maxSim = std::min(8, 2 + G.wave / 2 + joinedCount());
    G.spawnT -= dt;
    float toSurf = len3(G.stC - squadCenter()) - G.stR;
    if (G.phase == PH_ASTEROIDS) toSurf = G.hyperT < 0 ? 1e9f : 0;
    if (G.phase == PH_TURRET) toSurf = G.killsDone + alive < G.killsNeeded && G.turretDoneT < 0 ? 1e9f : 0;
    if (G.phase == PH_SPACE && G.level == 1 && G.escStage == 0) toSurf = G.escWaveLeft > 0 ? 1e9f : 0;
    if (G.phase == PH_SPACE && G.level == 1 && G.escStage == 1) toSurf = G.killsDone + alive < G.killsNeeded ? 1e9f : 0;
    if (G.phase == PH_SPACE && G.level == 1 && G.escStage == 2) toSurf = 0;
    if (G.spawnT <= 0 && alive < maxSim && toSurf > 1500) {   // they keep coming until you're on top of the station
        G.spawnT = R.range(0.8f, 1.6f);
        Enemy e; e.kind = EN_FIGHTER; e.hp = G.wave >= 4 ? 2.f : 1.f; e.cd = R.range(1.5f, 3.0f); e.weave = R.range(0, 6.28f);
        spawnFighterNear(e); G.en.push_back(e); if (G.escWaveLeft > 0) G.escWaveLeft--;
    }
    float spd = 125 + 8.f * G.wave, turn = 1.5f + 0.06f * G.wave;
    V3 sc = squadCenter();
    bool wasTailed[4]; for (int i = 0; i < 4; i++) { wasTailed[i] = G.pl[i].tailNow; G.pl[i].tailNow = false; }
    for (auto& e : G.en) {
        if (e.dead || e.kind != EN_FIGHTER) continue;
        e.t += dt; e.flash = std::max(0.f, e.flash - dt); e.cd -= dt;
        if (e.state == 4) {   // the ace, beaten: tumbling away, back later
            e.hide -= dt; e.v = norm3(e.v + rndDir() * (dt * 3)) * 220; e.p += e.v * dt;
            if (e.hide <= 0) { e.dead = true; G.aceCd = 40; } continue;
        }
        if (G.pl[e.tgt].down || !G.pl[e.tgt].joined) e.tgt = randomAlive();
        Player& T = G.pl[e.tgt]; V3 tp = ppos(T), tv = pvel(T);
        int ei = escortTargetOf(e); if (ei >= 0) { tp = G.esc[ei].p; tv = G.esc[ei].f * 150; if (e.state == 3) e.state = 0; }
        V3 cur = norm3(e.v); float d = len3(tp - e.p); float espd = spd * (e.ace ? 1.18f : 1.f);
        const float am = e.ace ? 1.4f : 1.f;   // the ace turns and shoots better and won't give up a tail
        // got behind its target? latch on to the tail
        if (ei < 0 && e.state == 0 && d < 380 && dot3(e.p - tp, T.fwd) < -40 && !T.down) { e.state = 3; e.tailT = 0; e.lostT = 0; e.cd = std::max(e.cd, 0.8f); }
        if (e.state == 3) {   // tailing: sit ~110 behind and shoot; hard turns or braking shake it off
            e.tailT += dt;
            V3 want = norm3(tp - T.fwd * 75 - e.p);
            float tturn = (1.05f + 0.04f * G.wave) * am;
            cur = norm3(cur + (want - cur) * std::min(1.f, dt * tturn));
            espd = clampf(T.speed * 1.02f + (d - 75) * 0.7f, 40, 320);
            V3 toT = norm3(tp - e.p); float ang = std::acos(clampf(dot3(cur, toT), -1, 1));
            if (ang > 0.87f) e.lostT += dt / am; else e.lostT = std::max(0.f, e.lostT - dt * 0.5f);
            T.tailNow = true; T.tailDist = d;
            if (e.cd <= 0 && d < 420 && ang < 0.3f) { e.cd = std::max(0.9f, 2.0f - 0.08f * G.wave) * R.range(0.8f, 1.2f) / am; turretShot(e) || (fireBall(e.p + cur * 7, e.tgt, e.ace ? 1.25f : 1.f), true); }
            bool overshoot = dot3(e.p - tp, T.fwd) > 0 && d < 60;
            if (e.lostT > 1.0f || overshoot || e.tailT > (e.ace ? 20 : 12) || T.down) {
                if (!T.down && e.tailT <= 12) { if (overshoot) pmsg(T, "IT OVERSHOT - NOW GET IT"); else { pmsg(T, "SHAKEN OFF  +250"); T.score += 250; } }
                e.state = 1; e.hide = R.range(1.5f, 2.5f); AU.play(SND_TIEFLY, 0.8f, R.range(-0.4f, 0.4f), R.range(0.9f, 1.1f), 250);
            }
        } else if (e.state == 0) {   // attack run: chase a point ahead of the target, weaving
            V3 aimp = tp + tv * 0.5f + V3(std::sin(e.t * 1.3f + e.weave) * 35, std::cos(e.t * 1.1f + e.weave) * 25, std::sin(e.t * 0.8f + e.weave) * 35);
            V3 want = norm3(aimp - e.p);
            cur = norm3(cur + (want - cur) * std::min(1.f, dt * turn * am));
            if (d < 120) { e.state = 1; e.hide = R.range(1.8f, 3.2f); cur = norm3(cur + rndDir() * 0.9f); AU.play(SND_TIEFLY, 0.85f, R.range(-0.4f, 0.4f), R.range(0.9f, 1.12f), 250); }
            if (e.cd <= 0 && d < 780 && d > 90 && dot3(cur, norm3(tp - e.p)) > 0.82f) { e.cd = std::max(0.9f, 2.6f - 0.13f * G.wave) * R.range(0.8f, 1.2f); if (ei >= 0) { if (R.f() < 0.45f) escortHit(ei); } else turretShot(e) || (fireBall(e.p + cur * 7, e.tgt), true); }
        } else {              // extend away, then come round again
            e.hide -= dt; if (e.hide <= 0) { e.state = 0; e.tgt = randomAlive(); }
        }
        V3 ds = e.p - G.stC; float dsl = len3(ds); if (dsl < G.stR + 170) cur = norm3(cur + ds * (1 / dsl) * (dt * 4));   // stay off the station
        if (auto c = capInside(e.p + cur * 90, 40)) cur = norm3(cur + c->u * (dt * 6));   // pull up over the capital ship
        e.v = cur * espd; e.p += e.v * dt;
        if (len3(e.p - sc) > 2600) spawnFighterNear(e);
    }
    for (int i = 0; i < 4; i++) {
        Player& q = G.pl[i]; if (!q.joined) continue;
        if (q.tailNow && !wasTailed[i] && q.tailHold <= 0) { AU.play(SND_ALARM, 0.45f, 0, 1.2f, 800); printf("game: P%d has an enemy on the tail\n", i + 1); }
        if (q.tailNow) q.tailHold = 1.2f; else q.tailHold = std::max(0.f, q.tailHold - dt);
    }
}
static void updateTowersTurrets(float dt) {
    for (auto& e : G.en) {
        if (e.dead || e.kind == EN_FIGHTER || e.kind == EN_PURSUER || e.kind == EN_REACTOR) continue;
        e.flash = std::max(0.f, e.flash - dt); e.cd -= dt;
        float relz = e.p.z - G.z;
        if (relz < -80) { e.dead = true; continue; }
        bool canFire = e.kind == EN_TURRET || !e.capDead;
        float lo = e.kind == EN_TOWER ? 180 : 140, hi = e.kind == EN_TOWER ? 820 : 600;
        if (canFire && e.cd <= 0 && relz > lo && relz < hi && aliveCount() > 0) {
            e.cd = std::max(1.0f, 3.2f - 0.18f * G.wave) * R.range(0.8f, 1.3f);
            V3 muzzle = e.kind == EN_TOWER ? e.p + V3(0, 3, 0) : e.p + V3(-e.side * 3.f, 0, 0);
            fireBall(muzzle, randomAlive(), 0.9f);
        }
    }
}
// trench: pull up past the lip and the surface guns on both sides open up on you (faster the longer you stay up)
static void updateSurfaceGuns(float dt) {
    for (auto& p : G.pl) {
        if (!p.joined || p.down) { p.expT = 0; continue; }
        bool up = p.y > -1.0f;
        if (!up) { p.expT = std::max(0.f, p.expT - dt * 2); continue; }
        if (p.expT == 0) { AU.play(SND_ALARM, 0.5f, 0, 1.35f, 600); printf("game: P%d pulled out of the trench\n", p.idx + 1); p.gunCd = 0.35f; }
        p.expT += dt; p.gunCd -= dt;
        if (p.gunCd <= 0) {
            p.gunCd = std::max(0.18f, 0.7f - 0.12f * p.expT - 0.02f * G.wave) * R.range(0.7f, 1.2f);
            float sx = R.f() < 0.5f ? -1.f : 1.f;
            V3 muzzle(sx * (TR_W + R.range(15, 110)), R.range(2, 7), G.z + p.zoff + R.range(160, 420));
            G.flashes.push_back({ muzzle, 0.18f, 0.18f, 5, C_ENEMY, false });
            fireBall(muzzle, p.idx, 1.5f);
        }
    }
}
static void collidePlayers(float zPrev) {
    for (auto& p : G.pl) {
        if (!p.joined || p.down) continue;
        float z0 = zPrev + p.zoff, z1 = G.z + p.zoff;
        if (G.phase == PH_SURFACE) for (auto& e : G.en) if (!e.dead && e.kind == EN_TOWER && e.p.z + 5 > z0 && e.p.z - 5 <= z1) {
            if (std::fabs(p.x - e.p.x) < 7.5f && p.y < GROUND_Y + e.h + (e.capDead ? 0 : 5)) { hitPlayer(p, "tower"); p.y = GROUND_Y + e.h + 9; }
        }
        if (G.phase == PH_TUNNEL) {
            for (auto& b : BEAMS) if (b.z > z0 && b.z <= z1) {
                V3 A, B; beamWorld(b, A, B);
                if (segDist2(V2(p.x, p.y), V2(A.x, A.y), V2(B.x, B.y)) < 3.8f * 3.8f) { hitPlayer(p, "girder"); G.flashes.push_back({ V3(p.x, p.y, b.z), 0.3f, 0.3f, 10, C_CAT, false }); AU.play(SND_BOOM_S, 0.6f); }
            }
            for (auto& e : G.en) if (!e.dead && e.kind == EN_REACTOR && len3(ppos(p) - e.p) < 34) { hitPlayer(p, "reactor"); p.y = e.p.y + 36; }
        }
        if (G.phase == PH_TRENCH) for (auto& c : G.cat) if (c.z > z0 && c.z <= z1) {
            if (p.y > c.y0 - 2.5f && p.y < c.y1 + 2.5f) { hitPlayer(p, "catwalk"); G.flashes.push_back({ V3(p.x, p.y, c.z), 0.3f, 0.3f, 10, C_CAT, false }); AU.play(SND_BOOM_S, 0.6f); }
        }
    }
}
static void clampPlayer(Player& p, float dt) {
    float x0 = -150, x1 = 150, y0 = -95, y1 = 95;
    if (G.phase == PH_SURFACE) { y0 = GROUND_Y + 6; y1 = 75; }
    if (G.phase == PH_TRENCH) { x0 = -TR_W + 4.5f; x1 = TR_W - 4.5f; y0 = TR_FLOOR + 3.5f; y1 = TR_LIP_MAX; }
    if (G.phase == PH_TUNNEL) { V3 c; float hw, hh; tunFrame(G.z + p.zoff, c, hw, hh); x0 = c.x - hw + 5; x1 = c.x + hw - 5; y0 = c.y - hh + 4; y1 = c.y + hh - 4; }
    if (G.phase == PH_FOX) { x0 = -FOX_XW; x1 = FOX_XW; y0 = FOX_GROUND + 6; y1 = FOX_YTOP; }
    if (G.phase == PH_ESCAPE || G.phase == PH_TALLY) return;
    float k = (G.phase == PH_TRENCH || G.phase == PH_TUNNEL) && G.phaseT < 1.5f ? 70 : 400;   // ease into the trench
    auto soft = [&](float& v, float lo, float hi) { if (v < lo) v = std::min(lo, v + k * dt); if (v > hi) v = std::max(hi, v - k * dt); };
    soft(p.x, x0, x1); soft(p.y, y0, y1);
}

static Dev* devById(int id) { for (auto& d : G.devs) if (d.id == id) return &d; return nullptr; }
static bool pr(const Dev& d, int b) { return d.b[b] && !d.pb[b]; }

static void botThink(Player& p, float dt, V2& desired, bool& fire) {
    V3 me = ppos(p); float best = 1e9; V3 tgt; bool has = false;
    if (G.phase == PH_FOX) { foxBot(p, desired, fire); (void)dt; return; }
    if (G.phase == PH_TANK) { tankBot(p, desired, fire); return; }
    if (G.phase == PH_HOTH) { hothBot(p, desired, fire); return; }
    if (G.phase == PH_TURRET) {   // gunner: swing onto the nearest fighter (or incoming shot) and fire when it's in the sight
        float bd = 1e9; V3 tg; bool h = false;
        for (auto& e : G.en) if (!e.dead && e.kind == EN_FIGHTER) { V3 cc = p.cam.toCam(e.p); float d = len3(e.p - me) + (cc.z < 0 ? 800 : 0); if (d < bd) { bd = d; tg = e.p; h = true; } }
        desired = V2(0, 0); fire = false;
        if (h) { V3 cc = p.cam.toCam(tg); float ax = std::atan2(cc.x, cc.z), ay = std::atan2(cc.y, std::max(1.f, std::sqrt(cc.x * cc.x + cc.z * cc.z))); desired = V2(clampf(ax * 2.5f, -1, 1), clampf(ay * 2.5f, -1, 1)); fire = cc.z > 0 && std::fabs(ax) < 0.05f && std::fabs(ay) < 0.05f && bd < 1200; }
        (void)dt; return;
    }
    if (isSpace()) {   // open space: chase the nearest interceptor (or fireball), then head for the station
        for (auto& b : G.balls) { float d = len3(b.p - me); if (d < 260 && dot3(b.p - me, p.fwd) > 0 && d < best) { best = d; tgt = b.p; has = true; } }
        if (!has) for (auto& e : G.en) if (!e.dead && e.kind == EN_FIGHTER) { float d = len3(e.p - me); if (d < best && d < 650) { best = d; tgt = e.p; has = true; } }   // close ones only: the run in matters
        if (!has) for (auto& c : G.caps) if (capInside1(c, me + p.fwd * 260, 80)) { tgt = me + p.fwd * 100 + c.u * 160; has = true; best = 1e9; }   // climb over a Star Destroyer in the way
        if (!has && G.approach) { tgt = G.stC; has = true; }
        if (!has && G.level == 1 && G.escStage < 3) for (auto& e : G.esc) if (e.alive && !e.gone) { tgt = e.p - e.f * 120; has = true; best = 1e9; break; }   // follow the bombers in
        if (G.phase == PH_ASTEROIDS) {   // dodge rocks ahead, otherwise press on down the field (drift back toward its middle)
            float bd = 1e9;
            for (auto& k : ROCKS) { V3 d = k.p - me; float ahead = dot3(d, p.fwd); if (ahead < 0 || ahead > 320) continue; float side = len3(d - p.fwd * ahead); if (side < k.r + 30 && ahead < bd) { bd = ahead; V3 away = me - (k.p - p.fwd * ahead); if (len3(away) < 1) away = p.up; tgt = me + p.fwd * 200 + norm3(away) * (k.r + 80); has = true; best = 1e9; } }
            if (!has) { tgt = V3(me.x * 0.7f, me.y * 0.7f, me.z + 800); has = true; best = 1e9; }
        }
        desired = V2(0, 0); fire = false;
        if (has) {
            V3 c = p.cam.toCam(tgt);
            if (c.z > 1) { V2 off(p.cam.foc * c.x / c.z, -p.cam.foc * c.y / c.z); desired = V2(clampf(off.x / crossRange(p, true), -1, 1), clampf(-off.y / crossRange(p, false), -1, 1)); fire = best < 1000 && dist(off, crossOff(p)) < 40 * LW; }
            else desired = V2(c.x >= 0 ? 1.f : -1.f, 0.3f);
        }
        V3 ds = me - G.stC; if (!G.approach && len3(ds) < G.stR + 400) desired.y = 1;   // pull away from the station
        p.thr = best < 150 ? -0.3f : 0.6f;   // bots keep the pace up on the run in
        (void)dt; return;
    }
    // aim at the most urgent target, dodge catwalks
    for (auto& b : G.balls) if (!b.dead) { float d = b.p.z - me.z; if (d > 20 && d < 500 && d < best) { best = d; tgt = b.p; has = true; } }
    if (!has) for (auto& e : G.en) {
        if (e.dead) continue; if (e.kind == EN_FIGHTER && e.state == 2) continue; if (e.kind == EN_TOWER && e.capDead) continue;
        V3 c = e.kind == EN_TOWER ? e.p + V3(0, 3, 0) : e.p; float d = c.z - me.z; if (d > 30 && d < 900 && d < best) { best = d; tgt = c; has = true; }
    }
    if (G.phase == PH_TRENCH && !G.portHit) { V3 pp = portPos(); float d = pp.z - me.z; if (d > 0 && d < 500) { tgt = pp; has = true; } }
    if (G.phase == PH_TUNNEL) for (auto& e : G.en) if (!e.dead && e.kind == EN_REACTOR) { float d = e.p.z - me.z; if (d > 40 && d < 700) { tgt = e.p; has = true; } }
    desired = V2(0, 0); fire = false;
    if (has) {
        V3 c = p.cam.toCam(tgt);
        if (c.z > 1) { V2 off(p.cam.foc * c.x / c.z, -p.cam.foc * c.y / c.z); desired = V2(clampf(off.x / crossRange(p, true), -1, 1), clampf(-off.y / crossRange(p, false), -1, 1)); fire = dist(off, crossOff(p)) < 45 * LW; }
    }
    if (G.phase == PH_SURFACE) { float want = 30; desired.y = clampf(desired.y * 0.5f + (want - p.y) * 0.03f, -1, 1); }
    if (G.phase == PH_TRENCH) {
        float nearest = 1e9; for (auto& c : G.cat) { float d = c.z - me.z; if (d > 0 && d < 260) nearest = std::min(nearest, d); }
        if (nearest < 1e8) {
            float bestY = p.y, bestCost = 1e9;
            for (float y = TR_FLOOR + 4; y <= -4; y += 0.5f) {
                bool ok = true;
                for (auto& c : G.cat) { float d = c.z - me.z; if (d > 0 && d < nearest + 25 && y > c.y0 - 4 && y < c.y1 + 4) ok = false; }
                if (ok && std::fabs(y - p.y) < bestCost) { bestCost = std::fabs(y - p.y); bestY = y; }
            }
            desired.y = clampf((bestY - p.y) * 0.3f, -1, 1);
        }
    }
    if (G.phase == PH_TUNNEL) {   // follow the tunnel and thread the girders: pick the clear spot nearest the line ahead
        V3 c; float hw, hh; tunFrame(me.z + 160, c, hw, hh);
        V2 want(c.x, c.y), bestP = want; float bestC = 1e9;
        for (int ix = -3; ix <= 3; ix++) for (int iy = -3; iy <= 3; iy++) {
            V2 q(c.x + ix * hw * 0.25f, c.y + iy * hh * 0.25f); bool ok = true;
            for (auto& b : BEAMS) { float d = b.z - me.z; if (d < 0 || d > 260) continue; V3 A, B; beamWorld(b, A, B); if (segDist2(q, V2(A.x, A.y), V2(B.x, B.y)) < 81) { ok = false; break; } }
            float cost = dist(q, V2(p.x, p.y)) + 0.4f * dist(q, want);
            if (ok && cost < bestC) { bestC = cost; bestP = q; }
        }
        V2 mv((bestP.x - p.x) * 0.09f, (bestP.y - p.y) * 0.09f);
        bool reactorNear = false; for (auto& e : G.en) if (!e.dead && e.kind == EN_REACTOR && e.p.z - me.z > 40 && e.p.z - me.z < 700) reactorNear = true;
        bool girderNear = false; for (auto& b : BEAMS) { float d = b.z - me.z; if (d > 0 && d < 140) girderNear = true; }
        if (reactorNear && !girderNear) {}   // line up on the reactor and keep shooting
        else if (!has || std::fabs(mv.x) + std::fabs(mv.y) > 0.5f) desired = V2(clampf(mv.x, -1, 1), clampf(mv.y, -1, 1)); else desired = desired * 0.4f + V2(clampf(mv.x, -1, 1), clampf(mv.y, -1, 1)) * 0.6f;
    }
    if (G.phase == PH_TRENCH && p.idx == 0 && getenv("VS_PULLUP")) desired.y = 1;   // test: P1 climbs out of the trench
    (void)dt;
}

static void updatePlayer(Player& p, float dt) {
    p.inv = std::max(0.f, p.inv - dt); p.hitT = std::max(0.f, p.hitT - dt); p.msgT = std::max(0.f, p.msgT - dt); p.shake *= std::pow(0.02f, dt); if (p.shake < 0.1f) p.shake = 0; p.fireCd -= dt;
    if (p.down) { p.downT += dt; return; }
    bool fire = false;
    if (G.phase == PH_ESCAPE || G.phase == PH_TALLY) { p.aim = p.aim * std::pow(0.1f, dt); }
    else if (p.bot) { V2 want; botThink(p, dt, want, fire); p.aim = p.aim + (want - p.aim) * std::min(1.f, dt * 5); }
    else {
        Dev* d = devById(p.dev);
        if (d) {
            V2 mv = d->mv; if (p.invert) mv.y = -mv.y;
            if (d->id == -1) {   // keyboard: relative; mouse: absolute inside this view
                p.aim = p.aim + V2(mv.x, -mv.y) * (2.4f * turnMul() * dt);
                if (len(mv) < 0.1f) p.aim = p.aim * std::pow(0.25f, dt);
                if (d->mouseMoved) { V2 c(p.vp.x + p.vp.w * 0.5f, p.vp.y + p.vp.h * 0.5f); V2 o = d->mouse - c; p.aim = V2(o.x / crossRange(p, true), -o.y / crossRange(p, false)); }
                fire = d->b[B_A] || d->ml; p.thr = d->tr - d->tl;
            } else if (p.gyro && d->hasGyro) {
                p.aim = p.aim + V2(-d->gyro.y * 1.15f, d->gyro.x * 1.15f * (p.invert ? -1 : 1)) * dt + V2(mv.x, -mv.y) * (1.8f * dt);
                if (pr(*d, B_LS)) p.aim = V2();
                fire = d->b[B_A] || d->b[B_B] || d->b[B_RB]; p.thr = d->tr - d->tl;
            } else {
                V2 want(mv.x, -mv.y); p.aim = p.aim + (want - p.aim) * std::min(1.f, dt * 14);
                fire = d->b[B_A] || d->b[B_B] || d->b[B_RB]; p.thr = d->tr - d->tl;
            }
        }
    }
    if (!p.bot) { Dev* dv = devById(p.dev); if (dv && pr(*dv, B_Y)) { p.third = !p.third; AU.play(SND_UI, 0.5f); } }
    if (getenv("VS_AUTOFIRE")) fire = true;   // test switch
    p.aim.x = clampf(p.aim.x, -1, 1); p.aim.y = clampf(p.aim.y, -1, 1);
    if (G.phase == PH_TANK) { tankPlayer(p, dt, fire); return; }
    if (G.phase == PH_HOTH) { hothPlayer(p, dt, fire); return; }
    if (G.phase == PH_TURRET) turretAim(p, dt);
    else if (G.phase == PH_SPACE || G.phase == PH_ASTEROIDS) {
        // flight: the sight is where you want to go - the ship turns toward it; triggers throttle up / down
        p.speed = clampf(p.speed + p.thr * 120 * dt, 35, 250);
        V3 right = norm3(cross3(p.up, p.fwd));
        float tm = turnMul() * shipOf(p).turn;   // bombers steer heavier
        p.fwd = norm3(p.fwd + right * (p.aim.x * 1.55f * tm * dt) + p.up * (p.aim.y * 1.3f * tm * dt));
        V3 up = norm3(p.up - p.fwd * dot3(p.up, p.fwd)), lv = V3(0, 1, 0) - p.fwd * dot3(V3(0, 1, 0), p.fwd);
        if (len3(lv) > 0.15f) up = norm3(up + norm3(lv) * (0.9f * dt));   // gently roll back level
        p.up = up;
        p.pos += p.fwd * (p.speed * dt);
        V3 ds = p.pos - G.stC; float dl = len3(ds);
        if (dl < G.stR + 6) { p.pos = G.stC + ds * ((G.stR + 40) / dl); p.fwd = norm3(p.fwd + ds * (1 / dl)); hitPlayer(p, "station"); }
    } else if (G.phase != PH_ESCAPE && G.phase != PH_TALLY) {
        float ms = (G.phase == PH_TRENCH ? 42 : G.phase == PH_TUNNEL ? 62 : G.phase == PH_FOX ? 105 : 95) * turnMul() * (0.5f + 0.5f * shipOf(p).turn);
        p.x += p.aim.x * ms * dt; p.y += p.aim.y * ms * dt * 0.85f;
    }
    if (!isSpace()) clampPlayer(p, dt);
    p.bank += (p.aim.x * 0.42f - p.bank) * std::min(1.f, dt * 6);
    if (G.phase == PH_FOX) { foxInput(p, fire, dt); if (fire && p.fireCd <= 0 && !G.over) foxFire(p); return; }
    if (fire && p.fireCd <= 0 && G.phase != PH_ESCAPE && G.phase != PH_TALLY && !G.over) playerFire(p);
}

static void stepGame(float dt) {
    G.time += dt; G.phaseT += dt; G.bannerT = std::max(0.f, G.bannerT - dt); G.citSpin += dt * 0.08f;
    float zPrev = G.z;
    float spd = G.speed; if (G.over) spd *= 0.3f;
    G.z += spd * dt;
    if (!G.over) for (auto& p : G.pl) if (p.joined) updatePlayer(p, dt);
    switch (G.phase) {
    case PH_SPACE:
        updateFighters(dt); updateCapital(dt); updateAlly(dt); maybeSpawnAce(dt); updateEscortRun(dt);
        if (false && G.killsDone >= G.killsNeeded && !G.approach) { bool any = false; for (auto& e : G.en) if (!e.dead && e.kind == EN_FIGHTER) any = true; if (!any && G.phaseT > 2) { G.approach = true; setBanner("ATTACK THE BATTLE STATION", "FOLLOW THE YELLOW ARROW"); } }
        if (G.approach) for (auto& p : G.pl) if (p.joined && !p.down && len3(p.pos - G.stC) < G.stR + 700) { printf("game: P%d reached the station\n", p.idx + 1); if (G.level == 3) beginPhase(PH_TUNNEL); else if (G.wave >= 2) beginPhase(PH_SURFACE); else beginPhase(PH_TRENCH); break; }
        break;
    case PH_SURFACE:
        updateTowersTurrets(dt);
        if (G.z > G.zStart + G.surfLen + 250) beginPhase(PH_TRENCH);
        break;
    case PH_TRENCH:
        updateTowersTurrets(dt); updateSurfaceGuns(dt); updatePursuers(dt); updateTrenchChase(dt);
        if (G.z - 40 > G.trenchZ0 + G.trenchLen + 40 && !G.portHit) { G.runs++; printf("game: port missed, run %d\n", G.runs); AU.play(SND_ALARM, 0.7f); G.z = G.trenchZ0 - 200; beginPhase(PH_TRENCH); }
        break;
    case PH_ESCAPE:
        for (auto& p : G.pl) if (p.joined && !p.down) { p.y += 55 * dt * std::min(1.f, G.phaseT); p.pitchExtra = std::min(0.75f, p.pitchExtra + dt * 0.7f); }
        if (G.phaseT > 2.2f && !G.citBoom) {   // cut to open space and watch the station go
            G.citBoom = true; G.speed = 0;
            V3 cc = G.stC;
            for (auto& p : G.pl) if (p.joined) { p.pos = cc + V3(p.x * 3, p.y * 0.5f + 150, -(G.stR * 2.2f) + p.zoff); p.fwd = norm3(cc - p.pos); p.up = V3(0, 1, 0); p.speed = 30; p.pitchExtra = 0; p.aim = V2(); }
            float sc = G.stR * S_STATION; const Model& M = *MS; int step = std::max(1, (int)M.e.size() / 2 / 700);
            for (size_t i = 0; i + 1 < M.e.size(); i += 2 * step) { V3 a = M.v[M.e[i]] * sc, b = M.v[M.e[i + 1]] * sc; Frag f; f.p = cc + (a + b) * 0.5f; f.d = (b - a) * 0.5f; f.v = norm3((a + b) * 0.5f + rndDir() * 80) * (R.range(150, 520) * G.stR / 1400); f.life = f.max = R.range(2.5f, 4.5f); f.c = C_CIT; G.frags.push_back(f); }
            for (int k = 0; k < 6; k++) G.flashes.push_back({ cc + rndDir() * (G.stR * 0.4f), 1.8f + k * 0.2f, 1.8f + k * 0.2f, G.stR * (0.6f + k * 0.2f), k % 2 ? C_PORT : C_WHITE, k % 2 == 0 });
            G.flashes.push_back({ cc, 3.0f, 3.0f, G.stR * 3.2f, C_PORT, true });
            AU.play(SND_BOOM_L, 1.0f, 0, 0.6f); AU.play(SND_PORT, 0.8f, 0, 0.8f); shakeAll(22);
            setBanner("STATION DESTROYED", "", 3.5f);
        }
        if (G.citBoom) for (auto& p : G.pl) if (p.joined) p.pos += p.fwd * (p.speed * dt);
        if (G.phaseT > 7.0f) beginPhase(PH_TALLY);
        break;
    case PH_TALLY:
        if (G.phaseT > 4.5f && G.mode >= 1) { G.wave++; beginPhase(G.mode == 1 ? PH_FOX : G.mode == 2 ? PH_TANK : PH_HOTH); break; }
        if (G.phaseT > 4.5f) { G.wave++; G.runs = 0; G.level = G.level % 3 + 1; beginPhase(G.level != 2 ? PH_SPACE : PK_FALCON.v.empty() ? PH_ASTEROIDS : PH_TURRET); }
        break;
    case PH_TURRET:
        updateFighters(dt); updateTurretPhase(dt);
        break;
    case PH_FOX:
        foxUpdate(dt);
        break;
    case PH_TANK: tankUpdate(dt); break;
    case PH_HOTH: hothUpdate(dt); break;
    case PH_ASTEROIDS:
        updateFighters(dt); updateCapital(dt); updateRocks(dt); maybeSpawnAce(dt); updateAsteroidStage(dt);
        break;
    case PH_LIGHTSPEED:
        for (auto& p : G.pl) if (p.joined && !p.down) { p.speed = std::min(4000.f, p.speed + 1500 * dt); p.pos += p.fwd * (p.speed * dt); }
        G.en.clear(); G.balls.clear();
        if (G.phaseT > 3.2f) beginPhase(PH_TALLY);
        break;
    case PH_TUNNEL:
        updateTowersTurrets(dt); updatePursuers(dt); updateTunnel(dt);
        for (auto& e : G.en) if (e.kind == EN_REACTOR) e.flash = std::max(0.f, e.flash - dt);
        break;
    }
    // fireballs
    for (auto& b : G.balls) {
        if (b.dead) continue;
        b.p += b.v * dt; b.life -= dt; b.spin += dt * 6;
        if (b.life <= 0 || (!isSpace() && !freeGround() && b.p.z < G.z - 60 && (b.v.z < G.speed || b.p.z < G.z - 700))) { b.dead = true; continue; }   // pursuers' shots come from behind
        for (auto& p : G.pl) {
            if (!p.joined || p.down) continue;
            // closest approach during this step (ball and ship both move), so fast passes can't tunnel through
            V3 r1 = b.p - ppos(p), r0 = r1 - (b.v - pvel(p)) * dt, dr = r1 - r0; float tt = clampf(-dot3(r0, dr) / std::max(1e-6f, dot3(dr, dr)), 0, 1);
            if (len3(r0 + dr * tt) < 3.4f && p.rollT > 0) { b.dead = true; G.flashes.push_back({ b.p, 0.25f, 0.25f, 7, C_WHITE, true }); AU.play(SND_BALLHIT, 0.5f, 0, 1.5f); break; }   // barrel roll
            if (len3(r0 + dr * tt) < 3.4f) { b.dead = true; hitPlayer(p, "fireball"); G.flashes.push_back({ b.p, 0.3f, 0.3f, 6, C_BALL, false }); break; }
        }
    }
    if (!G.over && G.phase != PH_ESCAPE && G.phase != PH_TALLY) collidePlayers(zPrev);
    // effects
    for (auto& b : G.bolts) b.t -= dt;
    G.bolts.erase(std::remove_if(G.bolts.begin(), G.bolts.end(), [](const Bolt& b) { return b.t <= 0; }), G.bolts.end());
    for (auto& f : G.frags) { f.life -= dt; f.p += f.v * dt; }
    G.frags.erase(std::remove_if(G.frags.begin(), G.frags.end(), [](const Frag& f) { return f.life <= 0; }), G.frags.end());
    for (auto& f : G.flashes) f.life -= dt;
    G.flashes.erase(std::remove_if(G.flashes.begin(), G.flashes.end(), [](const Flash& f) { return f.life <= 0; }), G.flashes.end());
    G.en.erase(std::remove_if(G.en.begin(), G.en.end(), [](const Enemy& e) { return e.dead; }), G.en.end());
    G.balls.erase(std::remove_if(G.balls.begin(), G.balls.end(), [](const Ball& b) { return b.dead; }), G.balls.end());
    if (G.phase == PH_SURFACE || G.phase == PH_TRENCH || G.phase == PH_TUNNEL || G.phase == PH_FOX) {   // the squad can throttle the run up or down together
        float t = 0; int n = 0; for (auto& p : G.pl) if (p.joined && !p.down && (!p.bot || G.fireOn)) { t += p.bot ? 1.f : p.thr; n++; }   // bots floor it when the fireball comes
        if (n) G.railMul = clampf(G.railMul + t / n * 0.5f * dt, 0.75f, 1.4f);
        G.speed = G.baseSpeed * G.railMul;
    }
    if (G.over) G.overT += dt;
}

// ---------------------------------------------------------------- rendering
static uint32_t hash2(int a, int b) { uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u; h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15; return h; }
static void drawSky(Canvas& cv, const Cam& cam, bool upperOnly) {
    for (auto& d : G.sky) {
        if (upperOnly && d.y < 0.02f) continue;
        V3 c(dot3(d, cam.r), dot3(d, cam.u), dot3(d, cam.f)); if (c.z < 0.05f) continue;
        V2 s = cam.scr(c); if (s.x < cam.vp.x || s.y < cam.vp.y || s.x > cam.vp.x + cam.vp.w || s.y > cam.vp.y + cam.vp.h) continue;
        float sz = 1.6f * LW; cv.rectAdd(s.x - sz / 2, s.y - sz / 2, sz, sz, Col(200, 215, 255, 150));
    }
}
static void drawStars3(Canvas& cv, const Cam& cam, V3 vel) {
    // space dust tiled round the camera so it never runs out, streaked by your own speed
    const float S = 1300; V3 st = vel * 0.05f;
    for (auto& s : G.stars) {
        V3 q = s.p; q.x += std::floor((cam.p.x - q.x) / S + 0.5f) * S; q.y += std::floor((cam.p.y - q.y) / S + 0.5f) * S; q.z += std::floor((cam.p.z - q.z) / S + 0.5f) * S;
        V3 c = cam.toCam(q); if (c.z < 1) continue; float fg = clampf(1 - c.z / 650, 0, 1); if (fg < 0.02f) continue;
        V2 a = cam.scr(c); if (a.x < cam.vp.x || a.y < cam.vp.y || a.x > cam.vp.x + cam.vp.w || a.y > cam.vp.y + cam.vp.h) continue;
        V3 c2 = cam.toCam(q - st - V3(0, 0, 0.5f)); V2 b = c2.z > 1 ? cam.scr(c2) : a;
        cv.line(a, b, 1.3f * LW, Col(210, 225, 255, (int)(190 * fg * s.b)), 0.3f);
    }
}
static void drawCitadel(Canvas& cv, const Cam& cam, V3 at, float size, float alpha) {
    Cam far = cam; far.fogNear = 4e5f; far.fogFar = 8e5f;
    V3 fw = MS == &PK_STATION ? V3(0, 0, 1) : V3(std::sin(G.citSpin), 0, std::cos(G.citSpin));
    SPHERE_CULL = MS == &PK_STATION;
    drawModel(cv, far, *MS, at, fw, 0, size, withA(C_CIT, alpha), 1.2f, 0.7f);
    SPHERE_CULL = false;
}
static void drawSurface(Canvas& cv, const Cam& cam, float zc) {
    Col gc = withA(C_GROUND, 0.75f);
    for (int i = -8; i <= 8; i++) line3(cv, cam, V3(i * 40.f, GROUND_Y, zc - 30), V3(i * 40.f, GROUND_Y, zc + 1500), 1.0f, gc, 0.4f);
    float z0 = std::floor(zc / 80) * 80;
    for (int k = 0; k < 20; k++) { float zz = z0 + k * 80; line3(cv, cam, V3(-320, GROUND_Y, zz), V3(320, GROUND_Y, zz), 1.0f, gc, 0.4f); }
    // greeble plates
    int b0 = (int)std::floor(zc / 160);
    for (int k = 0; k < 10; k++) {
        uint32_t h = hash2(b0 + k, 7); if ((h & 3) == 0) continue;
        float zz = (b0 + k) * 160.f + (h >> 8 & 63); float xx = ((int)((h >> 14) & 511) - 256) * 1.1f;
        float w = 10 + (h >> 4 & 15), d = 10 + (h >> 20 & 15), hh = 2 + (h >> 24 & 7);
        box3(cv, cam, V3(xx - w, GROUND_Y, zz - d), V3(xx + w, GROUND_Y + hh, zz + d), withA(C_GROUND, 0.6f), 1.0f);
    }
}
static void drawTrench(Canvas& cv, const Cam& cam, float zc, float z0, float zEnd, bool port) {
    float zf = std::max(z0, zc - 40), zt = std::min(zEnd + 160, zc + 1500); if (zt <= zf) return;
    Col tc = C_TRENCH, dim = withA(C_TRENCH, 0.45f);
    for (int s = -1; s <= 1; s += 2) {
        line3(cv, cam, V3(s * TR_W, 0, zf), V3(s * TR_W, 0, zt), 1.5f, tc);
        line3(cv, cam, V3(s * TR_W, TR_FLOOR, zf), V3(s * TR_W, TR_FLOOR, zt), 1.3f, tc);
        line3(cv, cam, V3(s * TR_W, -14, zf), V3(s * TR_W, -14, zt), 1.0f, dim, 0.4f);
        line3(cv, cam, V3(s * TR_W, -28, zf), V3(s * TR_W, -28, zt), 1.0f, dim, 0.4f);
        for (int k = 1; k <= 3; k++) line3(cv, cam, V3(s * (TR_W + k * 45.f), 0, zf), V3(s * (TR_W + k * 45.f), 0, zt), 1.0f, withA(C_GROUND, 0.5f), 0.3f);
    }
    float r0 = std::ceil(zf / 50) * 50;
    for (float zz = r0; zz < zt; zz += 50) {
        int ri = (int)(zz / 50); bool major = ri % 4 == 0; Col c = major ? tc : dim;
        V3 a(-TR_W, 0, zz), b(-TR_W, TR_FLOOR, zz), d(TR_W, TR_FLOOR, zz), e(TR_W, 0, zz);
        line3(cv, cam, a, b, 1.1f, c, major ? 1 : 0.4f); line3(cv, cam, b, d, 1.1f, c, major ? 1 : 0.4f); line3(cv, cam, d, e, 1.1f, c, major ? 1 : 0.4f);
        if (major) { line3(cv, cam, V3(-TR_W - 135, 0, zz), V3(-TR_W, 0, zz), 1.0f, withA(C_GROUND, 0.5f), 0.3f); line3(cv, cam, V3(TR_W, 0, zz), V3(TR_W + 135, 0, zz), 1.0f, withA(C_GROUND, 0.5f), 0.3f); }
        uint32_t h = hash2(ri, 3);
        if (TRENCH_TILE > 0 && G.opt.trench > 0) continue;   // the model's panels replace the random wall boxes
        if (h & 1) { int s = (h & 2) ? 1 : -1; float y = -6 - (h >> 4 & 31); float dz = 10 + (h >> 10 & 15); float hy = 3 + (h >> 16 & 3);
            V3 p[4] = { V3(s * TR_W, y, zz + 8), V3(s * TR_W, y, zz + 8 + dz), V3(s * TR_W, y - hy, zz + 8 + dz), V3(s * TR_W, y - hy, zz + 8) };
            for (int i = 0; i < 4; i++) line3(cv, cam, p[i], p[(i + 1) % 4], 0.9f, dim, 0.3f); }
        if (zz < zEnd) line3(cv, cam, V3(0, TR_FLOOR, zz + 10), V3(0, TR_FLOOR, zz + 30), 1.0f, dim, 0.3f);
    }
    if (TRENCH_TILE > 0 && G.opt.trench > 0) {   // panelled walls and floor from the trench model, repeated down the run; fewer lines far off
        const Model& m = PK_TRENCH; float TL = TRENCH_TILE, zmax = zEnd + 160; bool below = cam.p.y < -1.0f;
        for (float tz = std::floor(zf / TL) * TL; tz < zt; tz += TL) {
            float d = std::max(0.f, tz + TL * 0.5f - zc);
            float TLn = TRENCH_LINES * (G.opt.trench == 1 ? 0.45f : 1.f) * std::min(1.f, cam.vp.h / 1080.f + 0.25f);   // split-screen views get fewer
            size_t ne = std::min(m.e.size(), (size_t)TLn * 2); if (d > 150) ne = std::min(ne, (size_t)std::max(100.f, TLn * std::pow(150.f / d, 1.8f)) * 2);
            Col pc = withA(C_TRENCH, 0.36f * clampf(1.2f - d / 1400, 0.35f, 1));
            for (size_t i = 0; i + 1 < ne; i += 2) {
                V3 a = m.v[m.e[i]], b = m.v[m.e[i + 1]]; a.z += tz; b.z += tz;
                if ((a.z < z0 && b.z < z0) || (a.z > zmax && b.z > zmax)) continue;
                if (backEdge(m, i, cam, (a + b) * 0.5f, V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1))) { if (ne < m.e.size()) ne += 2; continue; }
                if (below && a.y > -1.5f && b.y > -1.5f && std::fabs(a.x) > TR_W + 0.5f && std::fabs(b.x) > TR_W + 0.5f) { if (ne < m.e.size()) ne += 2; continue; }   // surface beyond the lip: hidden by the walls while you are down in the trench
                line3(cv, cam, a, b, 0.9f, pc, 0);
            }
        }
    }
    if (zEnd + 160 < zc + 1500) {   // end wall
        float ze = zEnd + 160; for (int k = 0; k <= 4; k++) { float y = TR_FLOOR * k / 4.f; line3(cv, cam, V3(-TR_W, y, ze), V3(TR_W, y, ze), 1.1f, tc); }
        line3(cv, cam, V3(-TR_W, 0, ze), V3(-TR_W, TR_FLOOR, ze), 1.1f, tc); line3(cv, cam, V3(TR_W, 0, ze), V3(TR_W, TR_FLOOR, ze), 1.1f, tc);
    }
    if (port) {
        V3 pp(0, TR_FLOOR + 0.3f, zEnd); float pulse = 0.5f + 0.5f * std::sin(G.time * 9);
        for (int k = 0; k < 3; k++) {
            float s = 12.f - k * 4;
            V3 q[4] = { pp + V3(-s, 0, -s), pp + V3(s, 0, -s), pp + V3(s, 0, s), pp + V3(-s, 0, s) };
            for (int i = 0; i < 4; i++) line3(cv, cam, q[i], q[(i + 1) % 4], 1.6f, k == 2 ? withA(C_PORT, 0.5f + 0.5f * pulse) : C_PORT, 1.2f);
        }
        blob3(cv, cam, pp, 5 + 2 * pulse, withA(C_PORT, 0.6f));
    }
}
static void drawBall(Canvas& cv, const Cam& cam, const Ball& b) {
    V2 s; float z; if (!cam.proj(b.p, s, z)) return; float fg = cam.fog(z); if (fg < 0.02f) return;
    float r = std::max(2.5f * LW, 2.6f * cam.foc / z);
    cv.glowDisc(s, r * 1.7f, withA(C_BALL, 0.75f * fg)); cv.glowDisc(s, r * 0.7f, withA(Col(255, 240, 200), fg));
    for (int i = 0; i < 6; i++) { float a = b.spin + i * PI / 3; cv.line(s + fromAng(a) * r * 0.4f, s + fromAng(a) * r * 1.6f, std::max(1.f, 1.4f * LW), withA(Col(255, 200, 90), fg), 0.6f); }
}
// ---- cockpit laser cannons: four barrels poking in from the screen corners, aimed at the sight.
// Shape: long spear barrel, a short neck, then a flash hider that flares forward like an inside-out umbrella with its sides cut flat.
static V3 camPt(const Cam& c, V3 q) { return c.p + c.r * q.x + c.u * q.y + c.f * q.z; }
static void cannonFrame(const Player& p, int sx, int sy, V3& base, V3& tip) {
    const Cam& c = p.cam; float hw = c.vp.w * 0.5f / c.foc, hh = c.vp.h * 0.5f / c.foc;
    V2 co = crossOff(p); V3 aimDir = norm3(V3(co.x / c.foc, -co.y / c.foc, 1));
    V3 conv = aimDir * 70.f;                                    // barrels converge on the sight
    base = V3(sx * hw * 1.32f * 1.4f, sy * hh * (sy > 0 ? 0.98f : 1.22f) * 1.4f, 1.4f);
    tip = base + norm3(conv - base) * 1.3f;
}
static float cannonKick(const Player& p, int sx, int sy) {
    float k = 0; for (auto& b : G.bolts) if (b.owner == p.idx && b.sx == sx && b.sy == sy) k = std::max(k, b.t / b.max);
    return k;   // 1 = just fired
}
static void drawCannons(Canvas& cv, const Player& p) {
    const Cam& c = p.cam; Col body = mixc(Col(150, 165, 195), p.col, 0.25f), dark = withA(body, 0.55f);
    for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) {
        V3 b, t; cannonFrame(p, sx, sy, b, t);
        float kick = cannonKick(p, sx, sy);
        V3 A = norm3(t - b); float L = len3(t - b); V3 back = A * (-0.35f * kick * kick); b += back; t += back;
        V3 U = norm3(cross3(V3(0, 1, 0), A)), V = cross3(A, U);   // U = sideways (gets cut), V = up
        float r = 0.05f; const int N = 10;
        auto ringPt = [&](float at, float rad, int i, bool cut) {
            float a = i * 2 * PI / N; float x = std::cos(a) * rad, y = std::sin(a) * rad;
            if (cut) x = clampf(x, -r * 1.25f, r * 1.25f);
            return b + A * (at * L) + U * x + V * y;
        };
        struct Ring { float at, rad; bool cut; };
        const Ring rings[] = { { 0.0f, r, false }, { 0.30f, r, false }, { 0.60f, r, false }, { 0.74f, r * 1.25f, false }, { 0.78f, r * 0.7f, false }, { 0.84f, r * 0.75f, true }, { 1.0f, r * 3.0f, true } };
        const int NR = sizeof(rings) / sizeof(rings[0]);
        for (int k = 0; k < NR; k++) for (int i = 0; i < N; i++) {
            V3 q0 = ringPt(rings[k].at, rings[k].rad, i, rings[k].cut), q1 = ringPt(rings[k].at, rings[k].rad, (i + 1) % N, rings[k].cut);
            line3(cv, c, camPt(c, q0), camPt(c, q1), k == NR - 1 ? 1.5f : 1.0f, k == NR - 1 ? body : dark, 0.3f);
        }
        for (int i = 0; i < N; i += (i % 2 ? 1 : 1)) {
            for (int k = 0; k + 1 < NR; k++) {
                if (k < 3 && i % 2) continue;   // fewer lines along the plain barrel
                V3 q0 = ringPt(rings[k].at, rings[k].rad, i, rings[k].cut), q1 = ringPt(rings[k + 1].at, rings[k + 1].rad, i, rings[k + 1].cut);
                line3(cv, c, camPt(c, q0), camPt(c, q1), 1.0f, k >= NR - 2 ? body : dark, 0.3f);
            }
        }
        if (kick > 0.55f) { V2 s; float z; if (c.proj(camPt(c, t + A * 0.15f), s, z)) { cv.glowDisc(s, 46 * LW * kick, Col(255, 90, 70, (int)(230 * kick))); cv.glowDisc(s, 16 * LW * kick, Col(255, 235, 220, (int)(255 * kick))); } }
    }
}
static void drawScene(Canvas& cv, const Cam& cam, const Player* me) {
    bool space = isSpace();
    if (G.phase != PH_TANK && G.phase != PH_HOTH) drawSky(cv, cam, !space && G.phase != PH_TUNNEL && G.phase != PH_FOX);
    if (space) {
        if (G.phase == PH_LIGHTSPEED) drawLightspeed(cv, cam); else drawStars3(cv, cam, me ? pvel(*me) : V3());
        if (!G.citBoom && G.level != 2) drawCitadel(cv, cam, G.stC, G.stR * S_STATION, 1);
        if (G.phase == PH_ASTEROIDS || G.phase == PH_TURRET) drawRocks(cv, cam);
        if (G.phase == PH_SPACE) drawEscorts(cv, cam);
        if (G.phase == PH_TURRET) drawFalconFromTurret(cv, cam);
        if (G.phase == PH_SPACE || G.phase == PH_ASTEROIDS) { Cam far = cam; far.fogNear = 6000; far.fogFar = 12000; for (auto& c : G.caps) drawModelRU(cv, far, PK_DESTROYER, c.p, c.f, c.r, c.u, c.L, Col(170, 185, 215), 1.2f, 0.6f); }
        if (G.ally.on && G.ally.shown && G.phase == PH_SPACE && G.level != 2) { V3 f = norm3(G.ally.v), r = norm3(cross3(V3(0, 1, 0), f)), u = cross3(f, r); drawModelRU(cv, cam, PK_FALCON, G.ally.p, f, r, u, 15, Col(255, 225, 160), 1.3f, 1); }
    } else if (G.phase == PH_SURFACE) {
        drawSurface(cv, cam, G.z);
        float zt = G.zStart + G.surfLen + 250;   // trench entrance ahead
        if (zt < G.z + 1500) drawTrench(cv, cam, G.z, zt, zt + 3000, false);
    } else if (G.phase == PH_TANK) {
        drawTankWorld(cv, cam, me);
    } else if (G.phase == PH_HOTH) {
        drawHothWorld(cv, cam, me);
    } else if (G.phase == PH_FOX) {
        drawFoxWorld(cv, cam);
    } else if (G.phase == PH_TUNNEL) {
        drawTunnel(cv, cam, G.z);
    } else if (G.phase == PH_TRENCH || G.phase == PH_ESCAPE) {
        drawTrench(cv, cam, G.z, G.trenchZ0 - 400, G.trenchZ0 + G.trenchLen, !G.portHit);
        if (G.trenchZ0 > G.z - 400) drawSurface(cv, cam, G.z);
        if (G.ally.shown && G.phase == PH_TRENCH && !PK_FALCON.v.empty()) {   // the freighter coming in head-on over the trench
            V3 f = norm3(G.ally.v), r = norm3(cross3(V3(0, 1, 0), f)), u = cross3(f, r); drawModelRU(cv, cam, PK_FALCON, G.ally.p, f, r, u, 15, Col(255, 225, 160), 1.3f, 1);
        }
    }
    cv.flush(G.ren);
    for (auto& e : G.en) {
        Col c = e.flash > 0 ? C_WHITE : C_ENEMY;
        if (e.kind == EN_FIGHTER || e.kind == EN_PURSUER) {
            V3 v = len3(e.v) > 1 ? e.v : V3(0, 0, -1); float roll = e.state == 4 ? e.t * 9 : std::sin(e.t * 2 + e.weave) * 0.6f;
            const Model& M = e.ace && !PK_ACE.v.empty() ? PK_ACE : *ME; Col ac = e.ace ? (e.flash > 0 ? C_WHITE : Col(235, 225, 255)) : c;
            drawModel(cv, cam, M, e.p, v, roll, S_ENEMY * (e.ace ? 1.1f : 1.f) * enemyVis(cam.p, e.p), ac, e.ace ? 1.6f : 1.4f);
        }
        else if (e.kind == EN_REACTOR) drawReactor(cv, cam, e);
        else if (e.kind == EN_TOWER) {
            Col tc = e.capDead ? withA(C_TOWER, 0.45f) : C_TOWER;
            box3(cv, cam, V3(e.p.x - 5, GROUND_Y, e.p.z - 5), V3(e.p.x + 5, e.p.y, e.p.z + 5), tc, 1.2f);
            if (!e.capDead) {
                V3 t = e.p + V3(0, 3, 0); Col cc = e.flash > 0 ? C_WHITE : C_ENEMY;
                V3 q[6] = { t + V3(0, 4, 0), t + V3(-3.5f, 0, 0), t + V3(0, 0, -3.5f), t + V3(3.5f, 0, 0), t + V3(0, 0, 3.5f), t + V3(0, -3, 0) };
                for (int i = 1; i <= 4; i++) { int j = i % 4 + 1; line3(cv, cam, q[0], q[i], 1.3f, cc); line3(cv, cam, q[5], q[i], 1.3f, cc); line3(cv, cam, q[i], q[j], 1.3f, cc); }
            }
        } else drawModel(cv, cam, M_TURRET, e.p, V3(-e.side * 1.f, 0, -0.4f), 0, 1.0f, e.flash > 0 ? C_WHITE : C_CAT, 1.3f);
    }
    for (auto& c : G.cat) box3(cv, cam, V3(-TR_W, c.y0, c.z - 1.5f), V3(TR_W, c.y1, c.z + 1.5f), C_CAT, 1.4f, true);
    FULL_DETAIL = true;
    for (auto& o : G.pl) if (o.joined && !o.down && G.phase != PH_TURRET && !freeGround()) {
        V3 sp, sf, sr, su; bool own = &o == me;
        if (own && !modelCockpit(o) && !viewThird(*me)) continue;   // built-in ship: the drawn cannons stand in for it
        if (own) ownFrame(o, sp, sf, sr, su); else shipFrame(o, sp, sf, sr, su);
        const Ship& sh = shipOf(o); float S = S_PLAYER * sh.k; const Model& M = modelCockpit(o) ? *sh.m : *MP; if (!modelCockpit(o)) S = S_PLAYER;
        if (!own) S *= visScale(cam.p, ppos(o), 150, 900, 0.7f);   // far-off wingmates read bigger
        else if (isSpace() && o.zoom > 0.99f) { float base = S_PLAYER * 5.5f * chaseMul() * sh.k; S *= std::sqrt(clampf(o.camBack / base, 1, 4)); }   // pulled way back for a tail fight: keep your ship readable
        if (own && modelCockpit(o) && o.zoom < 0.999f) {   // cockpit (or zooming out of it): warped wings, body faded out
            float in2 = 1 - clampf(o.zoom, 0, 1); in2 = in2 * in2 * (3 - 2 * in2);
            Col cc = withA(mixc(o.col, Col(150, 165, 195), 0.35f * in2), 1 - 0.35f * in2);
            const Model& m = in2 > 0.5f && sh.ck && !sh.ck->v.empty() ? *sh.ck : M;
            for (size_t i = 0; i + 1 < m.e.size(); i += 2) {
                V3 a = m.v[m.e[i]], b = m.v[m.e[i + 1]];
                bool body = sh.bodyHalf > 0 ? std::fabs(a.x) < sh.bodyHalf && std::fabs(b.x) < sh.bodyHalf : len3((a + b) * 0.5f - sh.eye) < sh.hideR;
                Col ec = cc; if (body) { float k = clampf((o.zoom - 0.15f) / 0.5f, 0, 1); if (k <= 0) continue; ec = withA(cc, k); }
                a = cockpitWarp(sh, a, in2) * S; b = cockpitWarp(sh, b, in2) * S;
                V3 wa = sp + sr * a.x + su * a.y + sf * a.z, wb = sp + sr * b.x + su * b.y + sf * b.z;
                if (backEdge(m, i, cam, (wa + wb) * 0.5f, sr, su, sf)) continue;
                line3(cv, cam, wa, wb, 1.3f - 0.3f * in2, ec, 1 - in2);
            }
            continue;
        }
        drawModelRU(cv, cam, M, sp, sf, sr, su, S, o.col, 1.3f, 1);
    }
    FULL_DETAIL = false;
    for (auto& b : G.balls) drawBall(cv, cam, b);
    for (auto& b : G.bolts) {
        float fr = 1 - b.t / b.max;
        if (me && b.owner == me->idx && G.phase == PH_TURRET) {   // turret guns: streaks from the two firing barrels onto the sight
            V2 sc; float zc; if (!cam.proj(tcp(cam, turretBarrel((b.sy > 0 ? 0 : 2) + (b.sx > 0 ? 1 : 0), 1)), sc, zc)) continue;
            V2 st(cam.vp.x + cam.vp.w * 0.5f, cam.vp.y + cam.vp.h * 0.5f);
            float t0 = fr * fr * 0.85f, t1 = std::min(1.f, t0 + 0.42f * (1 - fr) + 0.08f);
            V2 s0 = sc + (st - sc) * t0, s1 = sc + (st - sc) * t1;
            cv.line(s0, s1, std::max(2.f, 10.f * (1 - t0)) * LW, b.c, 1.2f); cv.line(s0, s1, std::max(1.f, 3.f * (1 - t1)) * LW, Col(255, 230, 220), 0.5f);
        } else if (me && b.owner == me->idx && modelCockpit(*me)) {
            // own guns: a bright bolt from the real muzzle (your ship as you see it) that shrinks onto the sight
            float in2 = 1 - clampf(me->zoom, 0, 1); in2 = in2 * in2 * (3 - 2 * in2);
            V3 sp, sf, sr, su; ownFrame(*me, sp, sf, sr, su); V3 m = cockpitWarp(shipOf(*me), shipOf(*me).muz[muzzleIdx(b.sx, b.sy)], in2) * (S_PLAYER * shipOf(*me).k), mz = sp + sr * m.x + su * m.y + sf * m.z;
            V2 sc; float zc; if (!cam.proj(mz, sc, zc)) continue;
            V2 st; if (!cam.proj(b.b, st, zc)) st = V2(cam.vp.x + cam.vp.w * 0.5f, cam.vp.y + cam.vp.h * 0.5f) + crossOff(*me);
            float t0 = fr * fr * 0.85f, t1 = std::min(1.f, t0 + 0.42f * (1 - fr) + 0.08f);
            V2 s0 = sc + (st - sc) * t0, s1 = sc + (st - sc) * t1;
            float w0 = std::max(2.f, 12.f * (1 - t0)) * LW, w1 = std::max(1.f, 12.f * (1 - t1)) * LW;
            V2 dd = norm(s1 - s0), nn = perp(dd);
            V2 q[4] = { s0 + nn * (w0 * 0.5f), s1 + nn * (w1 * 0.5f), s1 - nn * (w1 * 0.5f), s0 - nn * (w0 * 0.5f) };
            cv.quad(cv.addV, q[0], q[1], q[2], q[3], b.c, b.c, b.c, b.c);
            cv.line(s0, s1, std::max(1.f, w1 * 0.35f), Col(255, 230, 220), 0.9f);
        } else if (me && b.owner == me->idx && !viewThird(*me)) {
            // own guns: a thick red streak from the screen edge that thins as it converges on the sight
            V3 cb, ct; cannonFrame(*me, b.sx, b.sy, cb, ct); V2 sc; float zc;
            if (!cam.proj(camPt(cam, ct), sc, zc)) continue;
            V2 st = V2(cam.vp.x + cam.vp.w * 0.5f, cam.vp.y + cam.vp.h * 0.5f) + crossOff(*me);   // always onto the sight
            float t0 = fr * fr * 0.85f, t1 = std::min(1.f, t0 + 0.42f * (1 - fr) + 0.08f);
            V2 s0 = sc + (st - sc) * t0, s1 = sc + (st - sc) * t1;
            float w0 = std::max(2.f, 14.f * (1 - t0)) * LW, w1 = std::max(1.f, 14.f * (1 - t1)) * LW;
            V2 dd = norm(s1 - s0), nn = perp(dd);
            V2 q[4] = { s0 + nn * (w0 * 0.5f), s1 + nn * (w1 * 0.5f), s1 - nn * (w1 * 0.5f), s0 - nn * (w0 * 0.5f) };
            cv.quad(cv.addV, q[0], q[1], q[2], q[3], b.c, b.c, b.c, b.c);
            cv.line(s0, s1, std::max(1.f, w1 * 0.35f), Col(255, 230, 220), 0.9f);
        } else {
            V3 d = b.b - b.a; V3 a = b.a + d * fr, e = b.a + d * std::min(1.f, fr + 0.3f);
            line3(cv, cam, a, e, 2.2f, b.c, 1.3f); line3(cv, cam, a, e, 0.8f, C_WHITE, 0);
        }
    }
    for (auto& f : G.frags) { float fr = f.life / f.max; line3(cv, cam, f.p - f.d, f.p + f.d, 1.3f, withA(f.c, std::min(1.f, fr * 1.8f)), 0.6f); }
    for (auto& f : G.flashes) {
        float fr = f.life / f.max; V2 s; float z;
        Cam far = cam; far.fogFar = 8e5f; far.fogNear = 4e5f;
        if (!far.proj(f.p, s, z)) continue;
        float r = f.size * far.foc / z;
        if (f.ring) far.fogNear = 0, cv.circle(s, r * (1.2f - fr), std::max(1.f, 2.5f * LW * fr), withA(f.c, fr), 1);
        else cv.glowDisc(s, r * (0.6f + 0.4f * fr), withA(f.c, fr));
    }
    cv.flush(G.ren);
}

static const char* btnLab(int devId, int b) {
    const Dev* d = devById(devId);
    if (devId == -1) { switch (b) { case B_A: return "SPACE"; case B_B: return "BKSP"; case B_X: return "I"; case B_Y: return "G"; case B_START: return "ESC"; } return "?"; }
    bool nin = d && d->nintendo;
    switch (b) { case B_A: return nin ? "B" : "A"; case B_B: return nin ? "A" : "B"; case B_X: return nin ? "Y" : "X"; case B_Y: return nin ? "X" : "Y"; case B_START: return nin ? "+" : "START"; case B_BACK: return nin ? "-" : "BACK"; }
    return "?";
}
static float hudU(const SDL_Rect& vp) { return std::min(vp.h / 1080.f * (vp.h < G.H * 0.75f ? 1.55f : 1.0f), vp.w / 1920.f * 1.6f) ; }

// Off-screen arrows / on-screen brackets for interceptors (red) and the station (yellow), plus the speed gauge.
static void drawTargets(Canvas& cv, Player& p, float u) {
    const SDL_Rect& vp = p.vp; V2 cen(vp.x + vp.w * 0.5f, vp.y + vp.h * 0.5f); Cam& cam = p.cam; char buf[32];
    struct T { V3 pos; Col c; float d; int kind; };
    std::vector<T> ts; V3 me = ppos(p);
    for (auto& e : G.en) if (!e.dead && e.kind == EN_FIGHTER) ts.push_back({ e.p, C_ENEMY, len3(e.p - me), 0 });
    std::sort(ts.begin(), ts.end(), [](const T& a, const T& b) { return a.d < b.d; }); if (ts.size() > 6) ts.resize(6);
    if (G.level != 2 && G.approach) ts.push_back({ G.stC, C_PORT, len3(G.stC - me) - G.stR, 1 });
    if (G.phase == PH_SPACE) for (auto& e : G.esc) if (e.alive && !e.gone) ts.push_back({ e.p, e.flash > 0 ? C_BAD : Col(230, 240, 255), len3(e.p - me), 9 });
    for (auto& o : G.pl) if (o.joined && !o.down && &o != &p && o.tailNow) ts.push_back({ ppos(o), o.col, len3(ppos(o) - me), 2 + o.idx });
    for (auto& t : ts) {
        V3 c = cam.toCam(t.pos); float mx = vp.w * 0.5f - 70 * u, my = vp.h * 0.5f - 100 * u;
        bool on = false; V2 sp;
        if (c.z > 1) { sp = cam.scr(c); on = std::fabs(sp.x - cen.x) < mx && std::fabs(sp.y - cen.y) < my; }
        float blink = t.kind == 1 && G.approach ? 0.6f + 0.4f * std::sin(G.time * 8) : 1.f;
        if (on) {
            if (t.kind == 0) {
                float r = std::max(13 * u, S_ENEMY * 1.6f * cam.foc / c.z), k = r * 0.4f;
                for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) { V2 q = sp + V2(sx * r, sy * r); cv.line(q, q - V2(sx * k, 0), 1.6f * u, withA(t.c, 0.8f), 0.4f); cv.line(q, q - V2(0, sy * k), 1.6f * u, withA(t.c, 0.8f), 0.4f); }
            } else if (t.kind == 9) {   // the bombers you're escorting
                float r = std::max(16 * u, S_PLAYER * 1.6f * cam.foc / c.z); cv.circle(sp, r, 1.6f * u, withA(t.c, 0.7f), 0.5f);
                FONT.draw(cv, "BOMBER", sp + V2(0, r + 8 * u), 13 * u, withA(t.c, 0.8f), 1);
            } else if (t.kind >= 2) {
                float bl = 0.5f + 0.5f * std::sin(G.time * 10); float r = std::max(18 * u, S_PLAYER * 1.4f * cam.foc / c.z);
                cv.circle(sp, r, 2 * u, withA(t.c, 0.5f + 0.5f * bl), 0.8f);
                snprintf(buf, sizeof buf, "HELP P%d", t.kind - 1); FONT.draw(cv, buf, sp + V2(0, r + 8 * u), 15 * u, withA(t.c, 0.6f + 0.4f * bl), 1);
            } else if (G.approach || t.d < 4000) {
                snprintf(buf, sizeof buf, "STATION %d", (int)std::max(0.f, t.d) / 10 * 10);
                FONT.draw(cv, buf, sp + V2(0, 30 * u), 14 * u, withA(t.c, 0.85f * blink), 1);
            }
            continue;
        }
        V2 d(c.x, -c.y); if (len(d) < 1e-3f) d = V2(0, 1); d = norm(d);
        float tt = std::min(mx / std::max(std::fabs(d.x), 1e-3f), my / std::max(std::fabs(d.y), 1e-3f));
        V2 e = cen + d * tt, n = perp(d); float sz = (t.kind == 1 ? 22 : 14) * u;
        V2 tri[3] = { e + d * sz, e - d * sz * 0.6f + n * sz * 0.75f, e - d * sz * 0.6f - n * sz * 0.75f };
        cv.fillPoly(tri, 3, withA(t.c, 0.45f * blink)); cv.poly(tri, 3, 1.8f * u, withA(t.c, blink), 0.7f);
        if (t.kind == 1) { snprintf(buf, sizeof buf, "%d", (int)std::max(0.f, t.d) / 10 * 10); FONT.draw(cv, buf, e - d * sz * 2.2f + V2(0, -7 * u), 13 * u, withA(t.c, blink), 1); }
        if (t.kind == 9) FONT.draw(cv, "BOMBER", e - d * sz * 2.6f + V2(0, -7 * u), 13 * u, t.c, 1);
        else if (t.kind >= 2) { snprintf(buf, sizeof buf, "HELP P%d", t.kind - 1); FONT.draw(cv, buf, e - d * sz * 2.6f + V2(0, -7 * u), 14 * u, t.c, 1); }
    }
    // speed gauge (left edge)
    float gx = vp.x + 26 * u, gy0 = vp.y + vp.h * 0.32f, gh = vp.h * 0.36f, fr = (p.speed - 35) / (250 - 35.f);
    cv.rectLine(gx, gy0, 12 * u, gh, 1.2f * u, withA(C_DIM, 0.7f), 0);
    cv.rect(gx + 2 * u, gy0 + gh * (1 - fr), 8 * u, gh * fr, withA(p.col, 0.7f));
    snprintf(buf, sizeof buf, "%d", (int)p.speed); FONT.draw(cv, buf, V2(gx + 6 * u, gy0 + gh + 10 * u), 14 * u, withA(C_WHITE, 0.8f), 1);
    FONT.draw(cv, "SPD", V2(gx + 6 * u, gy0 - 24 * u), 12 * u, withA(C_DIM, 0.9f), 1);
}
// 3D radar globe (space stage): everything round you in your own frame, distance squashed so near things spread out.
// The globe is seen from a little above and behind; each blip hangs on a stalk down to the equator (above / below you).
static void drawRadar(Canvas& cv, const Player& p, float u) {
    const SDL_Rect& vp = p.vp; float R = std::min(vp.h * 0.14f, 120 * u), cx = vp.x + vp.w * 0.5f, cy = vp.y + vp.h - R - 22 * u;
    if (G.phase == PH_TURRET) cx = vp.x + vp.w - R - 26 * u;   // in the turret: bottom-right, clear of the gun
    const float tilt = 0.5f, ct = std::cos(tilt), st = std::sin(tilt);
    V3 me = ppos(p), f = norm3(p.fwd), r = norm3(cross3(p.up, f)), up = cross3(f, r);
    auto proj = [&](V3 d, float& depth) { depth = -d.y * st + d.z * ct; return V2(cx + d.x * R, cy - (d.y * ct + d.z * st) * R); };
    Col gc = withA(p.col, 0.55f), gd = withA(p.col, 0.25f); float dd;
    cv.rect(cx - R * 1.08f, cy - R * 1.08f, R * 2.16f, R * 2.16f, Col(0, 0, 0, 0));
    cv.circle(V2(cx, cy), R, 1.2f * u, gc, 0.3f);
    for (int ring = 0; ring < 3; ring++) {   // equator, a meridian ahead, and an inner range ring
        V2 prev; bool first = true;
        for (int i = 0; i <= 48; i++) {
            float a = i * 2 * PI / 48; V3 q = ring == 0 ? V3(std::cos(a), 0, std::sin(a)) : ring == 1 ? V3(0, std::sin(a), std::cos(a)) : V3(std::cos(a) * 0.5f, 0, std::sin(a) * 0.5f);
            V2 s2 = proj(q, dd); if (!first) cv.line(prev, s2, 1.0f * u, dd < 0 && ring != 2 ? gd : gc, 0.2f); prev = s2; first = false;
        }
    }
    V2 o = proj(V3(), dd); cv.line(o + V2(-5 * u, 0), o + V2(5 * u, 0), 1.4f * u, p.col, 0.4f); cv.line(o, o + V2(0, -7 * u), 1.4f * u, p.col, 0.4f);   // you, nose up the screen
    auto blip = [&](V3 w, Col c, float size) {
        V3 d = w - me; float L = len3(d); if (L < 1) return;
        V3 q(dot3(d, r), dot3(d, up), dot3(d, f)); float k = std::min(1.f, std::sqrt(L / 2600.f)) / L; q = q * k;
        V2 s2 = proj(q, dd), base = proj(V3(q.x, 0, q.z), dd); Col cc = dd < 0 ? withA(c, 0.55f) : c;
        cv.line(base, s2, 1.0f * u, withA(cc, 0.5f), 0.2f); cv.circle(s2, size * 1.5f * u, 2.2f * u, cc, 0.7f, 8);
    };
    for (auto& c : G.caps) blip(c.p, Col(200, 210, 235), 4.5f);
    if (G.level != 2) blip(G.stC - norm3(G.stC - me) * G.stR, C_PORT, 4);   // nearest point of the station's surface
    if (G.ally.on && G.ally.shown) blip(G.ally.p, Col(255, 225, 160), 3.5f);
    for (auto& e : G.esc) if (e.alive && !e.gone) blip(e.p, Col(230, 240, 255), 3.5f);
    for (auto& q : G.pl) if (q.joined && !q.down && &q != &p) blip(ppos(q), q.col, 3);
    for (auto& e : G.en) if (!e.dead && e.kind == EN_FIGHTER) blip(e.p, e.ace ? Col(200, 120, 255) : e.state == 3 && e.tgt == p.idx ? C_WHITE : C_ENEMY, e.ace ? 4.f : 2.5f);
}
static void drawHud(Canvas& cv, Player& p) {
    const SDL_Rect& vp = p.vp; float u = hudU(vp); float cx = vp.x + vp.w * 0.5f, cy = vp.y + vp.h * 0.5f;
    char buf[96];
    if (p.down) {
        cv.rect((float)vp.x, (float)vp.y, (float)vp.w, (float)vp.h, Col(10, 0, 0, 150));
        for (int i = 0; i < 40; i++) { float y = vp.y + R.f() * vp.h; float x = vp.x + R.f() * vp.w; cv.line(V2(x, y), V2(x + R.range(20, 160) * u, y), u, withA(C_BAD, R.range(0.1f, 0.5f)), 0); }
        FONT.draw(cv, "SHIELDS DEPLETED", V2(cx, cy - 40 * u), 40 * u, C_BAD, 1, 0, 1.1f);
        FONT.draw(cv, aliveCount() > 0 ? "BACK IN AT THE NEXT STAGE" : "", V2(cx, cy + 20 * u), 20 * u, withA(C_WHITE, 0.8f), 1);
        snprintf(buf, sizeof buf, "P%d  %d", p.idx + 1, p.score); FONT.draw(cv, buf, V2(vp.x + 24 * u, vp.y + 20 * u), 26 * u, p.col);
        return;
    }
    // crosshair
    if (G.phase == PH_FOX) drawFoxHud(cv, p, u);
    else if (G.phase == PH_TANK) drawTankHud(cv, p, u);
    else if (G.phase == PH_HOTH) drawHothHud(cv, p, u);
    else if (G.phase != PH_ESCAPE && G.phase != PH_TALLY) {
        V2 c = V2(cx, cy) + crossOff(p); float r = 38 * u, lw = 3.0f * u;
        Col cc = p.col;
        for (int i = 0; i < 4; i++) { V2 d = fromAng(i * PI / 2 + PI / 4); V2 a = c + d * r, n = perp(d); cv.line(a, a - d * r * 0.45f + n * r * 0.0f, lw, cc); cv.line(a, a + n * r * 0.35f, lw, cc); cv.line(a, a - n * r * 0.35f, lw, cc); }
        cv.line(c - V2(5 * u, 0), c + V2(5 * u, 0), lw * 0.8f, withA(cc, 0.8f), 0.4f);
    }
    if (G.phase == PH_SPACE || G.phase == PH_ASTEROIDS || G.phase == PH_TURRET) drawTargets(cv, p, u);
    if ((G.phase == PH_SPACE || G.phase == PH_ASTEROIDS || G.phase == PH_TURRET) && G.opt.radar && !p.down) drawRadar(cv, p, u);
    if ((G.phase == PH_SPACE || G.phase == PH_ASTEROIDS) && p.tailHold > 0) {
        float bl = 0.55f + 0.45f * std::sin(G.time * 12);
        FONT.draw(cv, "ENEMY ON YOUR TAIL", V2(cx, vp.y + vp.h * 0.72f), 30 * u, withA(C_BAD, bl), 1, 0, 1.1f);
        FONT.draw(cv, "TURN HARD OR BRAKE TO SHAKE IT - OR GET A WINGMATE TO SHOOT IT", V2(cx, vp.y + vp.h * 0.72f + 42 * u), 14 * u, withA(C_WHITE, 0.85f), 1);
    }
    if (p.msgT > 0) FONT.draw(cv, p.msg.c_str(), V2(cx, vp.y + vp.h * 0.64f), 26 * u, withA(C_GOOD, std::min(1.f, p.msgT * 2)), 1, 0, 1.0f);
    // canopy struts
    Col sc = withA(Col(90, 110, 150), 0.55f);
    cv.line(V2((float)vp.x, (float)(vp.y + vp.h)), V2(vp.x + vp.w * 0.16f, vp.y + vp.h * 0.86f), 2.5f * u, sc, 0.3f);
    cv.line(V2((float)(vp.x + vp.w), (float)(vp.y + vp.h)), V2(vp.x + vp.w * 0.84f, vp.y + vp.h * 0.86f), 2.5f * u, sc, 0.3f);
    // score + shields
    snprintf(buf, sizeof buf, "P%d  %07d", p.idx + 1, p.score); FONT.draw(cv, buf, V2(vp.x + 24 * u, vp.y + 20 * u), 26 * u, p.col);
    snprintf(buf, sizeof buf, "WAVE %d", G.wave); FONT.draw(cv, buf, V2(vp.x + 24 * u, vp.y + 58 * u), 16 * u, withA(C_WHITE, 0.75f));
    float bw = 30 * u, bh = 16 * u, gap = 6 * u, sx = vp.x + vp.w - 24 * u - MAXSH * (bw + gap) + gap, sy = vp.y + 46 * u;
    FONT.draw(cv, "SHIELDS", V2(vp.x + vp.w - 24 * u, vp.y + 20 * u), 16 * u, withA(C_WHITE, 0.75f), 2);
    for (int i = 0; i < MAXSH; i++) {
        bool on = i < p.shields; Col c = p.shields <= 2 ? C_BAD : p.col;
        if (on) cv.rect(sx + i * (bw + gap), sy, bw, bh, withA(c, p.hitT > 0 && i == p.shields ? 1 : 0.75f));
        cv.rectLine(sx + i * (bw + gap), sy, bw, bh, 1.4f * u, on ? c : withA(C_DIM, 0.5f), on ? 0.7f : 0);
    }
    // objective line
    std::string obj; Col oc = withA(C_WHITE, 0.85f);
    switch (G.phase) {
    case PH_SPACE: if (G.level == 1 && G.escStage < 3) {
            if (G.escStage == 0) { obj = "PROTECT THE BOMBERS"; int k = 0; for (auto& e : G.esc) { if (e.alive && !e.gone) { snprintf(buf, sizeof buf, "BOMBER %d  %s", k + 1, std::string((size_t)std::max(0.f, e.hp), '=').c_str()); FONT.draw(cv, buf, V2(cx, vp.y + (52 + 18 * k) * u), 14 * u, e.flash > 0 ? C_WHITE : Col(230, 240, 255, 220), 1); } k++; } }
            else if (G.escStage == 1) obj = "COVER THE BOMBERS  " + std::to_string(std::max(0, G.killsNeeded - G.killsDone)); else obj = "BOMBER GROUP - COME IN";
        } else if (G.approach) { obj = "ATTACK THE STATION " + std::to_string((int)std::max(0.f, len3(G.stC - ppos(p)) - G.stR - 700) / 10 * 10); oc = withA(C_PORT, 0.9f); } else obj = "INTERCEPTORS " + std::to_string(std::max(0, G.killsNeeded - G.killsDone)); break;
    case PH_SURFACE: { int left = (int)std::max(0.f, G.zStart + G.surfLen + 250 - G.z); obj = "TRENCH IN " + std::to_string(left / 10 * 10); break; }
    case PH_FOX: {
        snprintf(buf, sizeof buf, "HITS %03d", p.foxHits); obj = buf;
        if (G.foxBoss && G.foxClearT < 0) {   // carrier health
            float hp = 0, mx = 0; for (auto& e : FOXE) if (e.kind >= FE_POD) { mx += e.kind == FE_CORE ? 26 + 4.f * G.wave : 14 + 2.f * G.wave; if (!e.dead) hp += std::max(0.f, e.hp); }
            float bw = vp.w * 0.4f; cv.rectLine(cx - bw / 2, vp.y + 50 * u, bw, 10 * u, 1.2f * u, withA(C_BAD, 0.7f), 0); cv.rect(cx - bw / 2 + 2 * u, vp.y + 52 * u, (bw - 4 * u) * (mx > 0 ? hp / mx : 0), 6 * u, withA(C_BAD, 0.8f));
        }
        break;
    }
    case PH_TURRET: {
        if (G.turretDoneT < 0) obj = "FIGHTERS " + std::to_string(std::max(0, G.killsNeeded - G.killsDone)); else obj = "INTO THE ASTEROIDS";
        break;
    }
    case PH_ASTEROIDS: {
        if (G.hyperT > 0) { snprintf(buf, sizeof buf, "HYPERDRIVE CHARGING  %d", (int)std::ceil(G.hyperT)); obj = buf; oc = withA(C_PORT, 0.6f + 0.4f * std::sin(G.time * 12)); }
        else obj = "CLEAR OF THE FIELD IN " + std::to_string((int)std::max(0.f, G.fieldLen - ppos(p).z) / 10 * 10);
        break;
    }
    case PH_TUNNEL: {
        if (!G.reactorHit) { int left = (int)std::max(0.f, G.tunZ0 + G.tunLen - G.z); obj = "REACTOR " + std::to_string(left / 10 * 10); if (left < 700) oc = withA(C_PORT, 0.6f + 0.4f * std::sin(G.time * 12)); }
        else { int left = (int)std::max(0.f, tunExitZ() - G.z); obj = "GET OUT  " + std::to_string(left / 10 * 10); oc = withA(C_BALL, 0.7f + 0.3f * std::sin(G.time * 14)); }
        if (G.fireOn && !p.down) {   // how close the fireball is behind you
            float gap = G.z + p.zoff - G.fireZ; snprintf(buf, sizeof buf, "FIREBALL %d BEHIND", (int)std::max(0.f, gap));
            FONT.draw(cv, buf, V2(cx, vp.y + vp.h - 60 * u), 22 * u, withA(gap < 150 ? C_BAD : C_BALL, 0.6f + 0.4f * std::sin(G.time * (gap < 150 ? 20 : 8))), 1);
            float bw = vp.w * 0.3f, f = clampf(gap / 600, 0, 1); cv.rect(cx - bw / 2, vp.y + vp.h - 36 * u, bw * f, 8 * u, withA(gap < 150 ? C_BAD : C_BALL, 0.8f));
        }
        break;
    }
    case PH_TRENCH: { int left = (int)std::max(0.f, G.trenchZ0 + G.trenchLen - G.z); obj = "EXHAUST PORT " + std::to_string(left / 10 * 10); if (left < 650) oc = withA(C_PORT, 0.6f + 0.4f * std::sin(G.time * 12)); break; }
    default: break;
    }
    if (!obj.empty()) FONT.draw(cv, obj.c_str(), V2(cx, vp.y + 22 * u), 20 * u, oc, 1);
    if (G.phase == PH_TRENCH && !p.down && p.y > -1.0f) FONT.draw(cv, "TOO HIGH - SURFACE GUNS", V2(cx, cy + 120 * u), 24 * u, withA(C_ENEMY, 0.55f + 0.45f * std::sin(G.time * 18)), 1);
    if (G.phase == PH_TRENCH) for (auto& c : G.cat) { float d = c.z - G.z; if (d > 0 && d < 330) { FONT.draw(cv, "CATWALK", V2(cx, vp.y + 52 * u), 18 * u, withA(C_CAT, 0.5f + 0.5f * std::sin(G.time * 16)), 1); break; } }
    if (G.phase == PH_TALLY) {
        FONT.draw(cv, (std::string(LEVELNAMES[G.level]) + " CLEAR").c_str(), V2(cx, cy - 90 * u), 42 * u, C_PORT, 1, 0, 1.1f);
        snprintf(buf, sizeof buf, "BONUS %d", G.tallyBonus[p.idx]); FONT.draw(cv, buf, V2(cx, cy - 20 * u), 30 * u, p.col, 1);
        if (p.idx == G.portBy) FONT.draw(cv, "PORT HIT BY YOU", V2(cx, cy + 30 * u), 20 * u, withA(C_WHITE, 0.85f), 1);
    }
    if (G.bannerT > 0 && G.phase != PH_TALLY) {
        float a = std::min(1.f, G.bannerT * 2) * std::min(1.f, (3.5f - G.bannerT) * 4 + 0.3f);
        FONT.draw(cv, G.banner.c_str(), V2(cx, cy - vp.h * 0.22f), 40 * u, withA(C_PORT, a), 1, 0, 1.1f);
        if (!G.banner2.empty()) FONT.draw(cv, G.banner2.c_str(), V2(cx, cy - vp.h * 0.22f + 58 * u), 18 * u, withA(C_WHITE, a * 0.9f), 1);
    }
    if (p.hitT > 0) { float a = p.hitT / 0.5f; cv.rect((float)vp.x, (float)vp.y, (float)vp.w, (float)vp.h, Col(255, 40, 20, (int)(90 * a))); }
    if (p.devLost) FONT.draw(cv, "CONTROLLER DISCONNECTED", V2(cx, cy + 80 * u), 24 * u, C_BAD, 1);
}

// the ship's own frame as wingmates (and the chase camera) see it
static void shipFrame(const Player& o, V3& pos, V3& fw, V3& rt, V3& up) {
    pos = ppos(o);
    if (isSpace()) {
        fw = norm3(o.fwd); rt = norm3(cross3(o.up, fw)); up = cross3(fw, rt);
        float roll = o.bank * 1.6f, cs = std::cos(roll), sn = std::sin(roll); V3 r2 = rt * cs - up * sn, u2 = up * cs + rt * sn; rt = r2; up = u2;
    } else { fw = norm3(V3(o.aim.x * 0.3f, o.aim.y * 0.25f + o.pitchExtra, 1)); basisFrom(fw, -o.bank * 1.4f + foxRoll(o), rt, up); }
}
// your own ship: locked to the cockpit camera when you are inside, easing to its normal frame as the view zooms out
static void ownFrame(const Player& p, V3& pos, V3& fw, V3& rt, V3& up) {
    V3 np, nf, nr, nu; shipFrame(p, np, nf, nr, nu);
    const Cam& c = p.ck; V3 e = shipOf(p).eye * (S_PLAYER * shipOf(p).k); V3 cp = c.p - (c.r * e.x + c.u * e.y + c.f * e.z);
    float t = clampf(p.zoom, 0, 1); t = t * t * (3 - 2 * t);
    pos = lerp3(cp, np, t); fw = norm3(lerp3(c.f, nf, t)); up = lerp3(c.u, nu, t); rt = norm3(cross3(up, fw)); up = cross3(fw, rt);
}
static void setupCam(Player& p) {
    V3 pos = ppos(p);
    float sh = p.shake * G.cv.px * 0.05f; if (sh > 0) pos += V3(R.range(-sh, sh), R.range(-sh, sh), 0);
    float vfov = p.vp.w > p.vp.h * 2.5f ? 0.62f : 0.95f;
    float want = viewThird(p) ? 1.f : 0.f;   // Y / V toggles: the camera slides out of the seat to the chase spot (and back)
    if (p.zoom < 0) p.zoom = want; else p.zoom += clampf(want - p.zoom, -0.022f, 0.022f);   // ~0.75 s cockpit <-> chase
    if (getenv("VS_ZOOMTEST")) p.zoom = (float)atof(getenv("VS_ZOOMTEST"));   // test: freeze the cockpit-to-chase blend
    if (G.phase == PH_TANK) { tankCam(p, p.vp, vfov); return; }
    if (G.phase == PH_HOTH) { hothCam(p, p.vp, vfov); return; }
    if (G.phase == PH_TURRET) {   // in the gun turret: the camera is the turret
        V3 f, up; turretBasis(p, f, up); p.cam.setBasis(turretEye(p), f, up, 0, p.vp, vfov); p.cam.fogNear = 1700; p.cam.fogFar = 3200; p.ck = p.cam; return;
    }
    float t = p.zoom * p.zoom * (3 - 2 * p.zoom);
    V3 eye = shipOf(p).eye * (S_PLAYER * shipOf(p).k); if (!modelCockpit(p)) eye = V3();
    Cam c0, c1;
    if (isSpace()) {
        V3 rt = norm3(cross3(p.up, p.fwd)), up = cross3(p.fwd, rt);
        c0.setBasis(pos + rt * eye.x + up * eye.y + p.fwd * eye.z, p.fwd, p.up, p.bank, p.vp, vfov);
        // chase camera: behind and above, looking past the ship; pulls way back to frame a fighter on your tail
        float base = S_PLAYER * 5.5f * chaseMul() * shipOf(p).k;
        float wb = p.tailHold > 0 ? clampf(p.tailDist * 1.25f + 45, base * 1.5f, base * 6.f) : base;   // fighter on your tail: the chase view, a bit further out
        if (p.camBack <= 0) p.camBack = wb; p.camBack += (wb - p.camBack) * 0.02f;
        float back = p.camBack, upz = S_PLAYER * 1.4f * chaseMul() * shipOf(p).k + std::max(0.f, back - base) * 0.34f;
        V3 cp = pos - p.fwd * back + p.up * upz, look = pos + p.fwd * (S_PLAYER * 12);
        c1.setBasis(cp, look - cp, p.up, p.bank * 0.35f, p.vp, vfov);
        c0.fogNear = c1.fogNear = 1700; c0.fogFar = c1.fogFar = 3200;
        if (G.citBoom) { c0.fogNear = c1.fogNear = 4e5f; c0.fogFar = c1.fogFar = 8e5f; }   // watching the station break up from far out
    } else {
        float yaw = p.aim.x * (G.phase == PH_TRENCH || G.phase == PH_TUNNEL ? 0.12f : 0.22f), pitch = p.aim.y * 0.15f + p.pitchExtra;
        c0.set(pos, yaw, pitch, p.bank, p.vp, vfov); c0.p = pos + c0.r * eye.x + c0.u * eye.y + c0.f * eye.z;
        float back = S_PLAYER * 5.5f * chaseMul() * shipOf(p).k, upz = S_PLAYER * 1.3f * chaseMul() * shipOf(p).k;
        float rb = railChaseBack(p), wantB = rb > 0 ? std::max(back, rb) : back;   // pursuers behind: pull back to show them, like the film's trench shot
        if (p.camBack <= 0) p.camBack = back; p.camBack += (wantB - p.camBack) * 0.02f; upz += (p.camBack - back) * 0.08f; back = p.camBack;
        V3 cp = pos + V3(0, upz, -back); if (G.phase == PH_TRENCH) cp.y = std::min(cp.y, std::max(-1.5f, p.y + 2)); if (G.phase == PH_TUNNEL) { V3 tc; float thw, thh; tunFrame(cp.z, tc, thw, thh); cp.y = std::min(cp.y, tc.y + thh - 3); cp.x = clampf(cp.x, tc.x - thw + 3, tc.x + thw - 3); }
        c1.set(cp, yaw * 0.6f, pitch * 0.6f - 0.06f, p.bank * 0.35f, p.vp, vfov);
        if (G.phase == PH_FOX) foxCam(p, c1, p.vp, vfov);
    }
    p.ck = c0;
    if (t <= 0) { p.cam = c0; return; }
    if (t >= 1) { p.cam = c1; return; }
    p.cam = c1; p.cam.p = lerp3(c0.p, c1.p, t);
    V3 f = norm3(lerp3(c0.f, c1.f, t)), u = lerp3(c0.u, c1.u, t); V3 r = norm3(cross3(u, f)); p.cam.f = f; p.cam.r = r; p.cam.u = cross3(f, r);
}
static void renderGame(Canvas& cv) {
    std::vector<Player*> views; for (auto& q : G.pl) if (q.joined) views.push_back(&q);
    for (Player* p : views) {
        setupCam(*p);
        LW = std::max(0.9f, p->vp.h / 1080.f * (p->vp.h < G.H * 0.75f ? 1.5f : 1.0f) * 1.6f);
        SDL_RenderSetClipRect(G.ren, &p->vp);
        if (!p->down && G.phase == PH_TURRET) { drawScene(cv, p->cam, p); cv.flush(G.ren); drawTurretOverlay(cv, *p); cv.flush(G.ren); }
        else if (!p->down) { drawScene(cv, p->cam, p); if (!viewThird(*p) && !modelCockpit(*p) && G.phase != PH_TALLY && !(G.phase == PH_ESCAPE && G.citBoom)) { drawCannons(cv, *p); cv.flush(G.ren); } }
        drawHud(cv, *p); cv.flush(G.ren);
    }
    if (views.size() == 3) {   // fourth quadrant: squad board
        SDL_Rect q = { G.W / 2 + 2, G.H / 2 + 2, G.W - G.W / 2 - 2, G.H - G.H / 2 - 2 }; SDL_RenderSetClipRect(G.ren, &q);
        float u = q.h / 540.f;
        Cam c; c.set(V3(0, 0, -1500), 0, 0, 0, q, 0.9f); c.fogNear = 5000; c.fogFar = 9000; LW = u;
        float keep = G.citSpin; G.citSpin = G.time * 0.3f; drawModel(cv, c, *MS, V3(0, 120, 0), V3(std::sin(G.citSpin), 0, std::cos(G.citSpin)), 0, MS == &PK_STATION ? 380.f : 330.f, withA(C_CIT, 0.7f), 1.2f, 0.6f); G.citSpin = keep;
        FONT.draw(cv, "SQUADRON", V2(q.x + q.w * 0.5f, q.y + 24 * u), 30 * u, C_WHITE, 1);
        for (int i = 0; i < 4; i++) if (G.pl[i].joined) { char b[64]; snprintf(b, sizeof b, "P%d  %07d  %s", i + 1, G.pl[i].score, G.pl[i].down ? "DOWN" : (std::string(G.pl[i].shields, '|')).c_str()); FONT.draw(cv, b, V2(q.x + q.w * 0.5f, q.y + q.h * 0.62f + i * 42 * u), 22 * u, G.pl[i].col, 1); }
        cv.flush(G.ren);
    }
    SDL_RenderSetClipRect(G.ren, nullptr);
    if (views.size() >= 2) {
        Col sc(60, 80, 120, 255); float g = (float)std::max(2, G.H / 360);
        cv.rect(0, G.H / 2 - g / 2, (float)G.W, g, sc); if (views.size() >= 3) cv.rect(G.W / 2 - g / 2, 0, g, (float)G.H, sc);
        cv.flush(G.ren);
    }
}

// ---------------------------------------------------------------- input
static void openPad(int index) {
    if (!SDL_IsGameController(index)) { printf("pad: joystick %d is not a game controller (%s)\n", index, SDL_JoystickNameForIndex(index)); return; }
    SDL_GameController* gc = SDL_GameControllerOpen(index); if (!gc) return;
    int id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
    if (devById(id)) return;
    Dev d; d.id = id; d.gc = gc; d.name = SDL_GameControllerName(gc) ? SDL_GameControllerName(gc) : "PAD";
    char guid[64]; SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(gc)), guid, sizeof guid);
    SDL_GameControllerType ty = SDL_GameControllerGetType(gc);
    std::string ln = d.name; for (auto& ch : ln) ch = (char)tolower(ch);
    d.nintendo = ty == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO || ln.find("nintendo") != std::string::npos || ln.find("switch") != std::string::npos || ln.find("pro controller") != std::string::npos;
    if (SDL_GameControllerHasSensor(gc, SDL_SENSOR_GYRO)) d.hasGyro = SDL_GameControllerSetSensorEnabled(gc, SDL_SENSOR_GYRO, SDL_TRUE) == 0;
    printf("pad: added id %d \"%s\" guid %s type %d%s gyro %s\n", id, d.name.c_str(), guid, (int)ty, d.nintendo ? " (nintendo layout)" : "", d.hasGyro ? "yes" : "no");
    G.devs.push_back(d);
    for (auto& p : G.pl) if (p.joined && p.devLost && !p.bot) { p.dev = id; p.devLost = false; printf("pad: reassigned to P%d\n", p.idx + 1); SDL_GameControllerSetPlayerIndex(gc, p.idx); break; }
}
static void closePad(int id) {
    for (size_t i = 0; i < G.devs.size(); i++) if (G.devs[i].id == id) { printf("pad: removed id %d \"%s\"\n", id, G.devs[i].name.c_str()); SDL_GameControllerClose(G.devs[i].gc); G.devs.erase(G.devs.begin() + i); break; }
    for (auto& p : G.pl) if (p.joined && p.dev == id) p.devLost = true;
}
static int navAxis(Dev& d, int axis, float dt) {
    int dir = 0;
    if (axis) { if (d.b[B_UP] || d.mv.y < -0.55f) dir = -1; else if (d.b[B_DOWN] || d.mv.y > 0.55f) dir = 1; }
    else { if (d.b[B_LEFT] || d.mv.x < -0.55f) dir = -1; else if (d.b[B_RIGHT] || d.mv.x > 0.55f) dir = 1; }
    int& last = axis ? d.nyLast : d.nxLast; float& t = axis ? d.nyT : d.nxT;
    if (!dir) { last = 0; return 0; }
    if (dir != last) { last = dir; t = 0.36f; return dir; }
    t -= dt; if (t <= 0) { t = 0.1f; return dir; }
    return 0;
}
static void readDevs(float dt, bool inGame) {
    int mx, my; Uint32 mb = SDL_GetMouseState(&mx, &my);
    for (auto& d : G.devs) {
        memcpy(d.pb, d.b, sizeof d.b); memset(d.b, 0, sizeof d.b); d.mv = V2(); d.tl = d.tr = 0;
        if (d.id == -1) {
            const Uint8* k = SDL_GetKeyboardState(nullptr);
            if (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) { d.mv.y -= 1; d.b[B_UP] = true; }
            if (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]) { d.mv.y += 1; d.b[B_DOWN] = true; }
            if (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]) { d.mv.x -= 1; d.b[B_LEFT] = true; }
            if (k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) { d.mv.x += 1; d.b[B_RIGHT] = true; }
            d.ml = (mb & SDL_BUTTON_LMASK) != 0;
            d.b[B_A] = k[SDL_SCANCODE_SPACE] || (!inGame && (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER])) || d.ml;
            d.b[B_B] = k[SDL_SCANCODE_BACKSPACE] || (!inGame && k[SDL_SCANCODE_ESCAPE]);
            d.b[B_X] = k[SDL_SCANCODE_I] || k[SDL_SCANCODE_R]; d.b[B_Y] = k[SDL_SCANCODE_G] || k[SDL_SCANCODE_V];
            d.b[B_START] = (inGame && k[SDL_SCANCODE_ESCAPE]) || k[SDL_SCANCODE_P] || (!inGame && (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER]));
            d.b[B_BACK] = k[SDL_SCANCODE_TAB]; d.b[B_LB] = k[SDL_SCANCODE_1]; d.b[B_RB] = k[SDL_SCANCODE_2];   // 1 / 2 = LB / RB (lobby level)
            d.tr = k[SDL_SCANCODE_E] ? 1.f : 0.f; d.tl = k[SDL_SCANCODE_Q] ? 1.f : 0.f;
            int ww, wh; SDL_GetWindowSize(G.win, &ww, &wh);
            d.mouse = V2(mx * (float)G.W / std::max(ww, 1), my * (float)G.H / std::max(wh, 1));
        } else if (d.gc) {
            auto Bt = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(d.gc, b) != 0; };
            d.b[B_A] = Bt(SDL_CONTROLLER_BUTTON_A); d.b[B_B] = Bt(SDL_CONTROLLER_BUTTON_B); d.b[B_X] = Bt(SDL_CONTROLLER_BUTTON_X); d.b[B_Y] = Bt(SDL_CONTROLLER_BUTTON_Y);
            d.b[B_LB] = Bt(SDL_CONTROLLER_BUTTON_LEFTSHOULDER); d.b[B_RB] = Bt(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
            d.b[B_START] = Bt(SDL_CONTROLLER_BUTTON_START); d.b[B_BACK] = Bt(SDL_CONTROLLER_BUTTON_BACK); d.b[B_LS] = Bt(SDL_CONTROLLER_BUTTON_LEFTSTICK);
            d.b[B_UP] = Bt(SDL_CONTROLLER_BUTTON_DPAD_UP); d.b[B_DOWN] = Bt(SDL_CONTROLLER_BUTTON_DPAD_DOWN); d.b[B_LEFT] = Bt(SDL_CONTROLLER_BUTTON_DPAD_LEFT); d.b[B_RIGHT] = Bt(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
            V2 st(SDL_GameControllerGetAxis(d.gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.f, SDL_GameControllerGetAxis(d.gc, SDL_CONTROLLER_AXIS_LEFTY) / 32767.f);
            float m = len(st); if (m < 0.15f) st = V2(); else st = st / m * clampf((m - 0.15f) / 0.8f, 0, 1);
            V2 dp((d.b[B_RIGHT] ? 1.f : 0.f) - (d.b[B_LEFT] ? 1.f : 0.f), (d.b[B_DOWN] ? 1.f : 0.f) - (d.b[B_UP] ? 1.f : 0.f));
            d.mv = st + dp; if (len(d.mv) > 1) d.mv = norm(d.mv);
            d.tl = clampf(SDL_GameControllerGetAxis(d.gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.f, 0, 1);
            d.tr = clampf(SDL_GameControllerGetAxis(d.gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.f, 0, 1);
            if (d.hasGyro) { float g[3] = { 0, 0, 0 }; if (SDL_GameControllerGetSensorData(d.gc, SDL_SENSOR_GYRO, g, 3) == 0) d.gyro = V3(g[0], g[1], g[2]); }
        }
        d.navX = navAxis(d, 0, dt); d.navY = navAxis(d, 1, dt);
    }
}
struct MenuIn { int x = 0, y = 0; bool a = false, b = false, start = false; };
static MenuIn menuInput() {
    MenuIn m;
    for (auto& d : G.devs) { if (d.navX) m.x = d.navX; if (d.navY) m.y = d.navY; if (pr(d, B_A)) m.a = true; if (pr(d, B_B)) m.b = true; if (pr(d, B_START)) m.start = true; }
    return m;
}

// ---------------------------------------------------------------- menus
static void drawLogo(Canvas& cv, float cx, float y, float size) {
    float u = size / 80;
    FONT.draw(cv, "VECTOR STRIKE", V2(cx + 3 * u, y + 3 * u), size, withA(Col(255, 60, 90), 0.4f), 1, size * 0.09f, 0.4f);
    FONT.draw(cv, "VECTOR STRIKE", V2(cx, y), size, C_PORT, 1, size * 0.09f, 1.3f);
    FONT.draw(cv, "ASSAULT ON THE CITADEL", V2(cx, y + size * 1.35f), size * 0.26f, withA(PCOL[0], 0.95f), 1);
}
static void drawBackdrop(Canvas& cv) {
    // demo flight down the trench
    Cam c; SDL_Rect vp = { 0, 0, G.W, G.H }; float t = G.stateT;
    float zc = t * 140; c.set(V3(std::sin(t * 0.5f) * 9, -20 + std::sin(t * 0.37f) * 9, zc), std::sin(t * 0.4f) * 0.08f, -0.05f, std::sin(t * 0.5f) * 0.25f, vp, 0.95f);
    LW = G.H / 1080.f * 1.6f;
    drawSky(cv, c, true);
    float seg = 3000; float z0 = std::floor(zc / seg) * seg;
    drawTrench(cv, c, zc, z0 - seg, z0 + 2 * seg, false);
    cv.flush(G.ren);
    cv.rect(0, 0, (float)G.W, (float)G.H, Col(0, 0, 0, 120));
    cv.flush(G.ren);
}
static void menuList(Canvas& cv, const char** items, int n, int sel, float cx, float y, float size, float gap) {
    for (int i = 0; i < n; i++) {
        bool s = i == sel; Col c = s ? C_PORT : withA(C_WHITE, 0.65f);
        FONT.draw(cv, items[i], V2(cx, y + i * gap), size, c, 1, 0, s ? 1.0f : 0.3f);
        if (s) { float w = Font::width(items[i], size); float bl = 0.5f + 0.5f * std::sin(G.stateT * 6); FONT.draw(cv, ">", V2(cx - w / 2 - size * 1.4f - bl * size * 0.2f, y + i * gap), size, C_PORT, 0); FONT.draw(cv, "<", V2(cx + w / 2 + size * 0.4f + bl * size * 0.2f, y + i * gap), size, C_PORT, 0); }
    }
}
static void drawPanel(Canvas& cv, float x, float y, float w, float h, Col edge, float u) { cv.rect(x, y, w, h, Col(6, 10, 18, 225)); cv.rectLine(x, y, w, h, 1.3f * u, edge, 0.5f); }
static void joinPlayer(int devId) {
    for (int i = 0; i < 4; i++) if (!G.pl[i].joined) {
        Player& p = G.pl[i]; p = Player(); p.joined = true; p.dev = devId; p.idx = i; p.col = PCOL[i]; p.invert = G.opt.invert;
        Dev* d = devById(devId); if (d && d->gc) SDL_GameControllerSetPlayerIndex(d->gc, i);
        AU.play(SND_UI_OK, 0.7f); printf("lobby: P%d joined (%s)\n", i + 1, devId == -1 ? "keyboard" : d ? d->name.c_str() : "test");
        return;
    }
}
static void updateMenus(float dt) {
    MenuIn m = menuInput(); G.stateT += dt;
    switch (G.state) {
    case ST_TITLE:
        if (m.y) { G.menuSel = (G.menuSel + m.y + 7) % 7; AU.play(SND_UI, 0.6f); }
        if (m.a || m.start) { AU.play(SND_UI_OK, 0.7f); if (G.menuSel <= 3) { G.mode = G.menuSel; G.state = ST_LOBBY; G.menuSel = 0; } else if (G.menuSel == 4) G.state = ST_HELP; else if (G.menuSel == 5) { G.state = ST_OPTIONS; G.menuSel = 0; } else G.quit = true; }
        break;
    case ST_HELP: if (m.a || m.b || m.start) { G.state = ST_TITLE; G.menuSel = 4; AU.play(SND_UI, 0.6f); } break;
    case ST_OPTIONS: {
        if (m.y) { G.menuSel = (G.menuSel + m.y + 11) % 11; AU.play(SND_UI, 0.6f); }
        int dx = m.x + (m.a ? 1 : 0);
        if (dx) {
            AU.play(SND_UI, 0.6f);
            switch (G.menuSel) {
            case 0: G.opt.res = (G.opt.res + dx + 5) % 5; setupTarget(); break;
            case 1: G.opt.glow = !G.opt.glow; break;
            case 2: G.opt.shake = !G.opt.shake; break;
            case 3: G.opt.sfx = (int)clampf((float)(G.opt.sfx + (m.a ? 0 : dx)), 0, 10); break;
            case 4: G.opt.mus = (int)clampf((float)(G.opt.mus + (m.a ? 0 : dx)), 0, 10); break;
            case 5: G.opt.invert = !G.opt.invert; for (auto& q : G.pl) q.invert = G.opt.invert; break;
            case 6: G.opt.turn = (int)clampf((float)(G.opt.turn + (m.a ? 0 : dx)), 1, 10); break;
            case 7: G.opt.chase = (int)clampf((float)(G.opt.chase + (m.a ? 0 : dx)), 1, 10); break;
            case 8: G.opt.trench = (G.opt.trench + dx + 3) % 3; break;
            case 9: G.opt.radar = !G.opt.radar; break;
            case 10: if (m.a) { saveCfg(); G.state = ST_TITLE; G.menuSel = 5; } break;
            }
            applyAudioVol();
        }
        if (m.b) { saveCfg(); G.state = ST_TITLE; G.menuSel = 5; }
        break;
    }
    case ST_LOBBY: {
        bool startNow = false;
        for (auto& d : G.devs) {
            int pi = -1; for (int i = 0; i < 4; i++) if (G.pl[i].joined && G.pl[i].dev == d.id) pi = i;
            if (pi < 0 && (pr(d, B_A) || pr(d, B_START)) && joinedCount() < 4) { joinPlayer(d.id); continue; }
            if (pi >= 0) {
                Player& p = G.pl[pi];
                if (pr(d, B_B)) { p.joined = false; AU.play(SND_UI, 0.6f); }
                if (pr(d, B_X)) { p.invert = !p.invert; AU.play(SND_UI, 0.6f); }
                if (pr(d, B_Y)) { if (d.hasGyro || d.id == -1) { p.gyro = !p.gyro && d.hasGyro; AU.play(SND_UI, 0.6f); } else AU.play(SND_UI, 0.3f, 0, 0.6f); }
                if (d.navX) { G.startSel = (G.startSel + d.navX + 3) % 3; AU.play(SND_UI, 0.6f); }
                if (pr(d, B_LB) || pr(d, B_RB)) { G.startLevel = (G.startLevel + (pr(d, B_RB) ? 1 : 2)) % 3 + 1; AU.play(SND_UI, 0.6f); }
                if (d.navY && NSHIPS > 1) { p.ship = (p.ship + NSHIPS + (d.navY > 0 ? 1 : -1)) % NSHIPS; AU.play(SND_UI, 0.6f); }
                if (pr(d, B_START)) startNow = true;
            } else if (pr(d, B_B) && joinedCount() == 0) { G.state = ST_TITLE; G.menuSel = 0; AU.play(SND_UI, 0.6f); }
        }
        if (startNow && joinedCount() > 0) { AU.play(SND_UI_OK, 0.8f); startGame(); }
        break;
    }
    }
}
static void renderMenus(Canvas& cv) {
    float u = G.H / 1080.f, cx = G.W * 0.5f;
    drawBackdrop(cv);
    switch (G.state) {
    case ST_TITLE: {
        drawLogo(cv, cx, G.H * 0.18f, 100 * u);
        const char* items[7] = { "PLAY", "STAR FOX MODE", "TANK BATTLE", "ICE PLANET", "HOW TO PLAY", "OPTIONS", "QUIT" };
        menuList(cv, items, 7, G.menuSel, cx, G.H * 0.44f, 30 * u, 50 * u);
        char b[80]; snprintf(b, sizeof b, "HIGH SCORE %07d   BEST WAVE %d", G.opt.best, G.opt.bestWave);
        FONT.draw(cv, b, V2(cx, G.H - 110 * u), 18 * u, withA(C_PORT, 0.8f), 1);
        FONT.draw(cv, "1-4 PLAYERS  -  SPLIT SCREEN CO-OP", V2(cx, G.H - 60 * u), 16 * u, withA(C_DIM, 0.95f), 1);
        break;
    }
    case ST_HELP: {
        FONT.draw(cv, "HOW TO PLAY", V2(cx, 70 * u), 44 * u, C_PORT, 1, 0, 1.1f);
        const char* lines[] = {
            "EVERY WAVE HAS THREE STAGES:",
            "1  SPACE - FREE FLIGHT: SHOOT DOWN THE INTERCEPTORS, THEN FLY TO THE STATION",
            "2  SURFACE - SHOOT THE TOWER TOPS, FLY OVER THE TOWERS (FROM WAVE 2)",
            "3  TRENCH - DODGE THE CATWALKS AND HIT THE EXHAUST PORT AT THE END",
            "",
            "SHOOT THE ORANGE FIREBALLS BEFORE THEY REACH YOU.",
            "YOU HAVE 6 SHIELDS. LOSE THEM ALL AND YOU ARE OUT",
            "UNTIL THE NEXT STAGE - IF A WINGMATE IS STILL FLYING.",
            "MISS THE PORT AND THE SQUADRON GOES AROUND FOR ANOTHER RUN.",
            "",
            "LEFT STICK - POINT THE SIGHT WHERE YOU WANT TO GO, THE SHIP TURNS TO IT",
            "A / B OR RB - FIRE (HOLD)    RT / LT - THROTTLE    ARROWS POINT TO TARGETS",
            "OPTIONS: INVERT STICK, TURN SPEED.  LOBBY: X INVERT   Y GYRO   LEFT/RIGHT LEVEL",
            "Y (KEYBOARD V) - COCKPIT / CHASE VIEW.  GYRO: L3 RECENTERS.  START - PAUSE",
            "KEYBOARD: ARROWS / WASD OR MOUSE, SPACE OR CLICK FIRE, E / Q THROTTLE, ESC",
        };
        int n = sizeof(lines) / sizeof(lines[0]);
        drawPanel(cv, cx - 800 * u, 140 * u, 1600 * u, (n * 46 + 40) * u, withA(C_PORT, 0.5f), u);
        for (int i = 0; i < n; i++) FONT.draw(cv, lines[i], V2(cx, 170 * u + i * 46 * u), 20 * u, i >= 10 ? withA(PCOL[0], 0.95f) : withA(C_WHITE, 0.9f), 1);
        FONT.draw(cv, "PRESS A TO GO BACK", V2(cx, G.H - 60 * u), 16 * u, withA(C_DIM, 0.9f), 1);
        break;
    }
    case ST_OPTIONS: {
        FONT.draw(cv, "OPTIONS", V2(cx, 110 * u), 50 * u, C_PORT, 1, 0, 1.1f);
        char l[11][64];
        snprintf(l[0], 64, "RESOLUTION  %s", RESNAMES[G.opt.res]); snprintf(l[1], 64, "GLOW  %s", G.opt.glow ? "ON" : "OFF");
        snprintf(l[2], 64, "SCREEN SHAKE  %s", G.opt.shake ? "ON" : "OFF"); snprintf(l[3], 64, "SOUND  %d", G.opt.sfx); snprintf(l[4], 64, "MUSIC  %d", G.opt.mus);
        snprintf(l[5], 64, "INVERT STICK  %s", G.opt.invert ? "ON - PULL BACK TO CLIMB" : "OFF"); snprintf(l[6], 64, "TURN SPEED  %d", G.opt.turn);
        { char bar[11]; for (int i = 0; i < 10; i++) bar[i] = i < G.opt.chase ? '=' : '-'; bar[10] = 0; snprintf(l[7], 64, "CHASE DISTANCE  %s  %d", bar, G.opt.chase); }
        { const char* TD[3] = { "OFF", "LOW", "HIGH" }; snprintf(l[8], 64, "TRENCH DETAIL  %s", TD[G.opt.trench]); }
        snprintf(l[9], 64, "RADAR  %s", G.opt.radar ? "ON" : "OFF");
        snprintf(l[10], 64, "BACK");
        const char* items[11] = { l[0], l[1], l[2], l[3], l[4], l[5], l[6], l[7], l[8], l[9], l[10] };
        menuList(cv, items, 11, G.menuSel, cx, G.H * 0.2f, 30 * u, 48 * u);
        char info[120]; snprintf(info, sizeof info, "DISPLAY %d X %d   RENDERING %d X %d", G.outW, G.outH, G.W, G.H);
        FONT.draw(cv, info, V2(cx, G.H - 100 * u), 15 * u, withA(C_DIM, 0.9f), 1);
        FONT.draw(cv, "LEFT / RIGHT TO CHANGE.  YOUR OWN MUSIC.WAV AND SOUNDS/*.WAV IN THE GAME FOLDER REPLACE THE BUILT-IN AUDIO", V2(cx, G.H - 60 * u), 15 * u, withA(C_DIM, 0.9f), 1);
        break;
    }
    case ST_LOBBY: {
        FONT.draw(cv, "SQUADRON", V2(cx, 80 * u), 50 * u, C_PORT, 1, 0, 1.1f);
        float pw = 380 * u, ph = 340 * u, gap = 36 * u, x0 = cx - (4 * pw + 3 * gap) / 2, y0 = 190 * u;
        for (int i = 0; i < 4; i++) {
            Player& p = G.pl[i]; float x = x0 + i * (pw + gap);
            drawPanel(cv, x, y0, pw, ph, p.joined ? p.col : withA(C_DIM, 0.4f), u);
            char t[48]; snprintf(t, sizeof t, "P%d", i + 1);
            FONT.draw(cv, t, V2(x + pw / 2, y0 + 30 * u), 60 * u, p.joined ? p.col : withA(C_DIM, 0.4f), 1, 0, p.joined ? 1 : 0.2f);
            if (p.joined) {
                Dev* d = devById(p.dev);
                std::string nm = p.dev == -1 ? "KEYBOARD + MOUSE" : (d ? d->name : "PAD"); for (auto& ch : nm) ch = (char)toupper(ch); if (nm.size() > 22) nm = nm.substr(0, 22);
                FONT.draw(cv, nm.c_str(), V2(x + pw / 2, y0 + 120 * u), 14 * u, C_WHITE, 1);
                Cam c; SDL_Rect r = { (int)x, (int)(y0 + 135 * u), (int)pw, (int)(90 * u) }; c.set(V3(0, 3.0f, -11), 0, -0.25f, 0, r, 0.9f); LW = u * 1.3f;
                const Ship& sh = shipOf(p); bool pk = MP == &PK_PLAYER && !sh.m->v.empty();
                drawModel(cv, c, pk ? *sh.m : *MP, V3(0, 0, 0), V3(std::sin(G.stateT * 0.9f + i), 0, std::cos(G.stateT * 0.9f + i)), 0, pk ? 5.0f * std::min(1.1f, sh.k) : 1.f, p.col, 1.2f);
                if (NSHIPS > 1 && G.mode == 0) {   // ship picker (Star Fox mode: everyone flies the star fighter): big and blinking so it can't be missed
                    float bl = 0.55f + 0.45f * std::sin(G.stateT * 5 + i);
                    FONT.draw(cv, "UP / DOWN = CHANGE SHIP", V2(x + pw / 2, y0 + 232 * u), 12 * u, withA(C_WHITE, 0.45f + 0.5f * bl), 1);
                    FONT.draw(cv, sh.name, V2(x + pw / 2, y0 + 254 * u), 16 * u, p.col, 1, 0, 0.8f);
                    snprintf(t, sizeof t, "%d / %d", std::min(p.ship, NSHIPS - 1) + 1, NSHIPS); FONT.draw(cv, t, V2(x + pw - 30 * u, y0 + 140 * u), 12 * u, withA(p.col, 0.8f), 1);
                    FONT.draw(cv, sh.bomb > 1.6f ? "BIG HITS ON TARGETS - SLOW TURNS" : sh.bomb > 1 ? "BIGGER HITS ON TARGETS - HEAVIER TURNS" : "ALL-ROUND DOGFIGHTER", V2(x + pw / 2, y0 + 276 * u), 10 * u, withA(C_WHITE, 0.6f), 1);
                }
                snprintf(t, sizeof t, "%s INVERT Y: %s", btnLab(p.dev, B_X), p.invert ? "ON" : "OFF"); FONT.draw(cv, t, V2(x + pw / 2, y0 + 300 * u), 12 * u, withA(C_WHITE, 0.8f), 1);
                if (d && d->hasGyro) snprintf(t, sizeof t, "%s GYRO AIM: %s", btnLab(p.dev, B_Y), p.gyro ? "ON" : "OFF"); else snprintf(t, sizeof t, "NO GYRO");
                FONT.draw(cv, t, V2(x + pw / 2, y0 + 320 * u), 12 * u, withA(d && d->hasGyro ? C_WHITE : C_DIM, 0.8f), 1);
            } else {
                float bl = 0.5f + 0.5f * std::sin(G.stateT * 4 + i);
                FONT.draw(cv, "PRESS A", V2(x + pw / 2, y0 + 170 * u), 22 * u, withA(C_WHITE, 0.4f + 0.5f * bl), 1);
                FONT.draw(cv, "TO JOIN", V2(x + pw / 2, y0 + 205 * u), 22 * u, withA(C_WHITE, 0.4f + 0.5f * bl), 1);
            }
        }
        float my = y0 + ph + 80 * u; char ml[96];
        if (G.mode == 1) snprintf(ml, sizeof ml, "STAR FOX MODE - X / R BARREL ROLL, HOLD FIRE TO CHARGE"); else
        if (G.mode == 2) snprintf(ml, sizeof ml, "TANK BATTLE - STICK DRIVES AND TURNS, Y CHASE VIEW"); else
        if (G.mode == 3) snprintf(ml, sizeof ml, "ICE PLANET - X / R TOW CABLE, CIRCLE A WALKER TWICE"); else
        snprintf(ml, sizeof ml, "LB  LEVEL %d - %s  RB", G.startLevel, LEVELNAMES[G.startLevel]);
        FONT.draw(cv, ml, V2(cx, my - 42 * u), 24 * u, withA(C_WHITE, 0.9f), 1);
        snprintf(ml, sizeof ml, "<  %s  >", STARTNAMES[G.startSel]);
        FONT.draw(cv, ml, V2(cx, my), 34 * u, C_PORT, 1, 0, 1.0f);
        if (STARTBONUS[G.startSel]) { snprintf(ml, sizeof ml, "START BONUS %d", STARTBONUS[G.startSel]); FONT.draw(cv, ml, V2(cx, my + 56 * u), 18 * u, withA(C_WHITE, 0.85f), 1); }
        FONT.draw(cv, "A JOIN    B LEAVE    UP/DOWN SHIP    X INVERT    Y GYRO    LB/RB LEVEL    LEFT/RIGHT DIFFICULTY    START LAUNCH", V2(cx, G.H - 70 * u), 16 * u, withA(C_DIM, 0.95f), 1);
        break;
    }
    }
    cv.flush(G.ren);
}

// ---------------------------------------------------------------- overlays
static void updateOverlay(float dt) {
    (void)dt; MenuIn m = menuInput();
    if (G.over) {
        if (G.overT < 2.0f) return;
        if (m.y) { G.overSel = (G.overSel + m.y + 2) % 2; AU.play(SND_UI, 0.6f); }
        if (m.a || m.start) { AU.play(SND_UI_OK, 0.7f); if (G.overSel == 0) startGame(); else { G.state = ST_LOBBY; G.stateT = 0; } }
        return;
    }
    if (G.paused) {
        if (m.y) { G.pauseSel = (G.pauseSel + m.y + 3) % 3; AU.play(SND_UI, 0.6f); }
        if (m.b || (m.start && G.pauseSel == 0)) { G.paused = false; AU.play(SND_UI, 0.6f); return; }
        if (m.a || m.start) { AU.play(SND_UI_OK, 0.7f); if (G.pauseSel == 0) G.paused = false; else if (G.pauseSel == 1) startGame(); else { G.paused = false; G.state = ST_TITLE; G.menuSel = 0; G.stateT = 0; } }
    }
}
static void renderOverlay(Canvas& cv) {
    float u = G.H / 1080.f, cx = G.W * 0.5f;
    if (G.paused && !G.over) {
        cv.rect(0, 0, (float)G.W, (float)G.H, Col(0, 0, 0, 150));
        FONT.draw(cv, "PAUSED", V2(cx, G.H * 0.3f), 60 * u, C_PORT, 1, 0, 1.1f);
        const char* items[3] = { "RESUME", "RESTART", "QUIT TO TITLE" };
        menuList(cv, items, 3, G.pauseSel, cx, G.H * 0.48f, 30 * u, 60 * u);
    }
    if (G.over && G.overT > 0.8f) {
        float a = clampf((G.overT - 0.8f) * 2, 0, 1);
        cv.rect(0, 0, (float)G.W, (float)G.H, Col(0, 0, 0, (int)(185 * a)));
        FONT.draw(cv, "GAME OVER", V2(cx, G.H * 0.17f), 72 * u, withA(C_BAD, a), 1, 0, 1.2f);
        char b[96]; snprintf(b, sizeof b, "REACHED WAVE %d", G.wave); FONT.draw(cv, b, V2(cx, G.H * 0.17f + 100 * u), 24 * u, withA(C_WHITE, a), 1);
        float y = G.H * 0.38f;
        for (int i = 0; i < 4; i++) if (G.pl[i].joined) { Player& p = G.pl[i]; snprintf(b, sizeof b, "P%d   %07d   KILLS %d   FIREBALLS %d", i + 1, p.score, p.kills, p.ballsShot); FONT.draw(cv, b, V2(cx, y), 24 * u, withA(p.col, a), 1); y += 46 * u; }
        if (G.newBest) FONT.draw(cv, "NEW HIGH SCORE", V2(cx, y + 20 * u), 26 * u, withA(C_PORT, a * (0.6f + 0.4f * std::sin(G.overT * 8))), 1);
        if (G.overT > 2.0f) { const char* items[2] = { "PLAY AGAIN", "BACK TO SQUADRON" }; menuList(cv, items, 2, G.overSel, cx, G.H * 0.72f, 28 * u, 56 * u); }
    }
    cv.flush(G.ren);
}

// ---------------------------------------------------------------- art (rendered by the game)
static void renderIcon(Canvas& cv) {
    int S = std::min(G.W, G.H); SDL_Rect vp = { (G.W - S) / 2, (G.H - S) / 2, S, S };
    Cam c; c.set(V3(6, -16, 0), 0.05f, -0.02f, 0.18f, vp, 0.95f); LW = S / 1080.f * 1.9f; G.time = 0.8f;
    drawSky(cv, c, true);
    drawTrench(cv, c, 0, -300, 520, true);
    cv.flush(G.ren);
    drawModel(cv, c, M_FIGHTER, V3(-4, -9, 70), V3(0.3f, -0.1f, -1), 0.4f, 2.4f, C_ENEMY, 1.6f);
    Ball b; b.p = V3(9, -18, 40); b.spin = 0.4f; drawBall(cv, c, b);
    V3 g1 = c.p + c.r * 2.2f + c.u * -1.3f + c.f * 1, g2 = c.p + c.r * -2.2f + c.u * -1.3f + c.f * 1, tgt(0, TR_FLOOR, 520);
    line3(cv, c, g1 + (tgt - g1) * 0.3f, g1 + (tgt - g1) * 0.7f, 3, PCOL[0], 1.5f); line3(cv, c, g2 + (tgt - g2) * 0.25f, g2 + (tgt - g2) * 0.65f, 3, PCOL[0], 1.5f);
    cv.flush(G.ren);
    float m = S * 0.03f, w = S - 2 * m; cv.rectLine(vp.x + m, vp.y + m, w, w, S * 0.008f, withA(C_PORT, 0.85f), 1);
    cv.flush(G.ren);
}
static void renderLogoArt(Canvas& cv) { float sz = std::min(G.H * 0.36f, G.W * 0.92f / 13); drawLogo(cv, G.W * 0.5f, G.H * 0.5f - sz * 0.75f, sz); cv.flush(G.ren); }

static void saveShot() {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, G.useTarget ? G.W : G.outW, G.useTarget ? G.H : G.outH, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return;
    SDL_RenderReadPixels(G.ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, G.shotPath.c_str()); SDL_FreeSurface(s); printf("shot: saved %s\n", G.shotPath.c_str());
}
static void renderFrame(bool shotNow) {
    if (G.useTarget) SDL_SetRenderTarget(G.ren, G.target);
    SDL_SetRenderDrawColor(G.ren, 1, 2, 7, 255); SDL_RenderClear(G.ren);
    G.cv.glow = G.opt.glow; G.cv.px = G.H / 1080.f;
    if (G.state == ST_GAME) { renderGame(G.cv); renderOverlay(G.cv); }
    else if (G.state == ST_ICON) renderIcon(G.cv);
    else if (G.state == ST_LOGO) { SDL_SetRenderDrawColor(G.ren, 0, 0, 0, 255); SDL_RenderClear(G.ren); renderLogoArt(G.cv); }
    else renderMenus(G.cv);
    if (shotNow && G.useTarget) saveShot();
    if (G.useTarget) { SDL_SetRenderTarget(G.ren, nullptr); SDL_SetRenderDrawColor(G.ren, 0, 0, 0, 255); SDL_RenderClear(G.ren); SDL_RenderCopy(G.ren, G.target, nullptr, nullptr); }
    if (shotNow && !G.useTarget) saveShot();
    SDL_RenderPresent(G.ren);
}

// ---------------------------------------------------------------- test setup
static void testSetup() {
    const char* st = getenv("VS_STATE"); if (!st) return;
    std::string s = st;
    int n = getenv("VS_PLAYERS") ? atoi(getenv("VS_PLAYERS")) : 1;
    if (s == "title") return;
    if (s == "icon") { G.state = ST_ICON; return; }
    if (s == "logo") { G.state = ST_LOGO; return; }
    if (s == "options") { G.state = ST_OPTIONS; return; }
    if (s == "help") { G.state = ST_HELP; return; }
    for (int i = 0; i < n; i++) { joinPlayer(i == 0 ? -1 : 1000 + i); if (G.bots) G.pl[i].bot = true; }
    if (s == "lobby") { G.state = ST_LOBBY; return; }
    startGame();
    if (getenv("VS_PHASE")) { std::string ph = getenv("VS_PHASE"); if (ph == "surface") beginPhase(PH_SURFACE); else if (ph == "trench") beginPhase(PH_TRENCH); else if (ph == "tunnel") { G.level = 3; beginPhase(PH_TUNNEL); } else if (ph == "lightspeed") beginPhase(PH_LIGHTSPEED); else if (ph == "turret") { G.level = 2; beginPhase(PH_TURRET); } else if (ph == "tally") beginPhase(PH_TALLY); else if (ph == "escape") beginPhase(PH_ESCAPE); }
    if (getenv("VS_THIRD")) for (auto& p : G.pl) p.third = true;
    if (getenv("VS_SHIP")) { int k = atoi(getenv("VS_SHIP")); for (auto& p : G.pl) p.ship = k == 9 ? p.idx % 3 : k; }   // test: 0 fighter, 1 bomber, 2 heavy bomber, 9 mixed
    if (getenv("VS_SHOWALLY") && G.ally.on) { G.ally.joinT = 0; G.ally.shown = true; G.ally.p = G.pl[0].pos + G.pl[0].fwd * 90 + V3(25, 8, 0); G.ally.v = V3(0.6f, 0, 0.8f) * 175; }   // test: freighter parked ahead of P1
    if (getenv("VS_ACE")) { G.aceCd = 0; maybeSpawnAce(0.01f); if (!G.en.empty() && G.en.back().ace) { Enemy& e = G.en.back(); e.p = G.pl[0].pos + G.pl[0].fwd * 45 + V3(10, 4, 0); e.v = G.pl[0].fwd * 1.f; e.state = 2; e.cd = 99; } }   // test: the ace parked ahead of P1
    if (getenv("VS_SHOWENEMY")) { Enemy e; e.kind = EN_FIGHTER; e.tgt = 0; e.state = 2; float d = (float)atof(getenv("VS_SHOWENEMY")); e.p = G.pl[0].pos + G.pl[0].fwd * d + V3(d * 0.12f, d * 0.06f, 0); e.v = G.pl[0].fwd * -0.01f; e.cd = 99; G.en.push_back(e); }   // test: a fighter parked in front of P1 (model check)
    if (getenv("VS_TAILTEST")) { Enemy e; e.kind = EN_FIGHTER; e.tgt = 0; e.state = 3; e.p = G.pl[0].pos - G.pl[0].fwd * 150 + V3(10, 6, 0); e.v = G.pl[0].fwd * 120; e.cd = 5; G.en.push_back(e); }
    for (auto& p : G.pl) if (p.joined) setupCam(p);
    if (getenv("VS_ADVANCE")) {
        float secs = (float)atof(getenv("VS_ADVANCE")); float t = 0; int lastWave = G.wave;
        for (; t < secs && !G.over; t += 1 / 60.f) { for (auto& p : G.pl) if (p.joined) setupCam(p); stepGame(1 / 60.f); if (G.wave != lastWave) { lastWave = G.wave; } }
        for (auto& p : G.pl) if (p.joined) printf("test: P%d pos %.0f %.0f %.0f fwd %.2f %.2f %.2f speed %.0f\n", p.idx + 1, p.pos.x, p.pos.y, p.pos.z, p.fwd.x, p.fwd.y, p.fwd.z, p.speed);
        for (auto& e : G.en) if (e.kind == EN_PURSUER) printf("test: pursuer ace %d state %d p %.0f %.0f %.0f (G.z %.0f) dead %d\n", e.ace, e.state, e.p.x, e.p.y, e.p.z, G.z, e.dead);
        printf("test: z %.1f speed %.2f railMul %.3f trenchZ0 %.0f len %.0f\n", G.z, G.speed, G.railMul, G.trenchZ0, G.trenchLen);
        printf("test: advanced %.0fs: wave %d phase %d enemies %d balls %d over %d\n", t, G.wave, G.phase, (int)G.en.size(), (int)G.balls.size(), G.over);
        for (auto& p : G.pl) if (p.joined) printf("test: P%d score %d kills %d balls %d shields %d down %d\n", p.idx + 1, p.score, p.kills, p.ballsShot, p.shields, p.down);
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    printf("Vector Strike starting\n");
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) { printf("SDL_Init failed: %s\n", SDL_GetError()); return 1; }
    SDL_version lv; SDL_GetVersion(&lv); printf("sdl: %d.%d.%d, video driver %s\n", lv.major, lv.minor, lv.patch, SDL_GetCurrentVideoDriver());
    const char* home = getenv("VS_HOME"); char* base = SDL_GetBasePath();
    G.dir = home ? std::string(home) + "/" : (base ? base : "./"); if (base) SDL_free(base);
    G.cfgPath = G.dir + "vectorstrike.cfg";
    loadCfg(); buildModels();
    // optional model pack (your own wireframes made with tools/glb2vsm.py) - falls back to the built-in ships
    if (!getenv("VS_NOPACK")) {
        std::string md = G.dir + "modelpack/";
        if (loadVsm(md + "player.vsm", PK_PLAYER)) { MP = &PK_PLAYER; S_PLAYER = 5.5f; }
        if (loadVsm(md + "enemy.vsm", PK_ENEMY)) { ME = &PK_ENEMY; S_ENEMY = 5.5f; }   // about the X-wing's size
        loadVsm(md + "cockpit.vsm", PK_COCKPIT);
        if (loadVsm(md + "ywing.vsm", PK_YWING)) NSHIPS = 2;
        loadVsm(md + "arwing.vsm", PK_ARWING); loadVsm(md + "atat.vsm", PK_ATAT); loadVsm(md + "speeder.vsm", PK_SPEEDER); loadVsm(md + "probe.vsm", PK_PROBE); loadVsm(md + "hturret.vsm", PK_HTURRET);
        if (NSHIPS == 2 && loadVsm(md + "bwing.vsm", PK_BWING)) { NSHIPS = 3; loadVsm(md + "bwing_ck.vsm", PK_BWING_CK); }
        loadVsm(md + "falcon.vsm", PK_FALCON); loadVsm(md + "destroyer.vsm", PK_DESTROYER);
        loadVsm(md + "ace.vsm", PK_ACE);
        { const char* fn[7] = { "player_far", "enemy_far", "ywing_far", "bwing_far", "falcon_far", "destroyer_far", "ace_far" }; Model* full[7] = { &PK_PLAYER, &PK_ENEMY, &PK_YWING, &PK_BWING, &PK_FALCON, &PK_DESTROYER, &PK_ACE };
          for (int i = 0; i < 7; i++) if (!full[i]->v.empty() && !getenv("VS_NOFAR") && loadVsm(md + fn[i] + ".vsm", PK_FAR[i])) full[i]->far = &PK_FAR[i]; }
        if (loadVsm(md + "trench.vsm", PK_TRENCH)) { for (auto& v : PK_TRENCH.v) TRENCH_TILE = std::max(TRENCH_TILE, v.z); }
        if (loadVsm(md + "station.vsm", PK_STATION)) { MS = &PK_STATION; S_STATION = 1.0f; }
    }
    { Rng sr; sr.s = 31337; for (int i = 0; i < 420; i++) { V3 d(sr.range(-1, 1), sr.range(-1, 1), sr.range(-1, 1)); if (len3(d) > 1 || len3(d) < 0.1f) { i--; continue; } G.sky.push_back(norm3(d)); } }
    if (getenv("VS_TRLINES")) TRENCH_LINES = atoi(getenv("VS_TRLINES"));   // test: trench detail
    if (getenv("VS_STR")) ST_RADIUS = (float)atof(getenv("VS_STR"));   // test: battle station radius
    if (getenv("VS_WARP")) sscanf(getenv("VS_WARP"), "%f,%f", &SHIPS[0].warpY, &SHIPS[0].warpZ);   // test: cockpit wing warp
    int eyeShip = getenv("VS_SHIP") ? atoi(getenv("VS_SHIP")) : 0;
    if (getenv("VS_EYE")) sscanf(getenv("VS_EYE"), "%f,%f,%f", &SHIPS[eyeShip % 3].eye.x, &SHIPS[eyeShip % 3].eye.y, &SHIPS[eyeShip % 3].eye.z);   // test: cockpit seat position (model units)
    if (getenv("VS_SHOT")) G.shotFrame = atoi(getenv("VS_SHOT"));
    if (getenv("VS_SHOT_PATH")) G.shotPath = getenv("VS_SHOT_PATH");
    if (getenv("VS_BOTS")) G.bots = true;
    if (getenv("VS_RES")) G.opt.res = atoi(getenv("VS_RES"));
    Uint32 wf = SDL_WINDOW_FULLSCREEN_DESKTOP; int ww = 1280, wh = 720;
    if (getenv("VS_WINDOW")) { wf = 0; sscanf(getenv("VS_WINDOW"), "%dx%d", &ww, &wh); }
    G.win = SDL_CreateWindow("Vector Strike", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, wf | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!G.win) { printf("window failed: %s\n", SDL_GetError()); return 1; }
    G.ren = SDL_CreateRenderer(G.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!G.ren) G.ren = SDL_CreateRenderer(G.win, -1, SDL_RENDERER_TARGETTEXTURE);
    if (!G.ren) { printf("renderer failed: %s\n", SDL_GetError()); return 1; }
    SDL_RendererInfo ri; SDL_GetRendererInfo(G.ren, &ri); printf("renderer: %s\n", ri.name);
    SDL_ShowCursor(SDL_DISABLE);
    setupTarget();
    if (getenv("VS_EXPORT_SOUNDS")) { AU.build(); AU.exportWavs(getenv("VS_EXPORT_SOUNDS")); return 0; }
    if (!getenv("VS_MUTE")) AU.open(G.dir);
    applyAudioVol();
    Dev kb; kb.id = -1; kb.name = "KEYBOARD"; G.devs.push_back(kb);
    testSetup();
    Uint64 last = SDL_GetPerformanceCounter(); double acc = 0; float fpsT = 0; int fpsN = 0;
    while (!G.quit) {
        for (auto& d : G.devs) d.mouseMoved = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: G.quit = true; break;
            case SDL_CONTROLLERDEVICEADDED: openPad(e.cdevice.which); break;
            case SDL_CONTROLLERDEVICEREMOVED: closePad(e.cdevice.which); break;
            case SDL_MOUSEMOTION: { Dev* k = devById(-1); if (k) k->mouseMoved = true; break; }
            case SDL_KEYDOWN: if (e.key.keysym.sym == SDLK_F4 && (e.key.keysym.mod & KMOD_ALT)) G.quit = true; break;
            case SDL_WINDOWEVENT: if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) { setupTarget(); if (G.state == ST_GAME) layoutViews(); } break;
            }
        }
        Uint64 now = SDL_GetPerformanceCounter(); double dt = (double)(now - last) / SDL_GetPerformanceFrequency(); last = now;
        if (G.shotFrame >= 0) dt = 1 / 60.0;
        if (dt > 0.1) dt = 0.1;
        fpsT += (float)dt; fpsN++; if (fpsT > 30) { printf("fps: %.1f (state %d wave %d phase %d enemies %d balls %d frags %d)\n", fpsN / fpsT, G.state, G.wave, G.phase, (int)G.en.size(), (int)G.balls.size(), (int)G.frags.size()); fpsT = 0; fpsN = 0; }
        readDevs((float)dt, G.state == ST_GAME && !G.paused && !G.over);
        for (auto& d : G.devs) if (d.id != -1 && d.b[B_BACK] && d.b[B_START] && (pr(d, B_START) || pr(d, B_BACK))) { if (G.state == ST_GAME) { G.state = ST_TITLE; G.menuSel = 0; G.paused = false; } }
        if (G.state == ST_GAME) {
            if (G.paused || G.over) updateOverlay((float)dt);
            if (!G.paused && G.state == ST_GAME) {
                acc += dt;
                while (acc >= 1 / 60.0) { acc -= 1 / 60.0; for (auto& p : G.pl) if (p.joined) setupCam(p); stepGame(1 / 60.f); }
                if (!G.over) for (auto& p : G.pl) if (p.joined && !p.bot && !p.devLost) { Dev* d = devById(p.dev); if (d && pr(*d, B_START) && !d->b[B_BACK]) { G.paused = true; G.pauseSel = 0; AU.play(SND_UI_OK, 0.6f); break; } }
            }
        } else updateMenus((float)dt);
        G.frame++;
        static int benchN = getenv("VS_FRAMES") ? atoi(getenv("VS_FRAMES")) : 0; static Uint64 benchT0 = 0;
        if (benchN && G.frame == 10) benchT0 = SDL_GetPerformanceCounter();
        if (benchN && G.frame == 10 + benchN) { double s = (double)(SDL_GetPerformanceCounter() - benchT0) / SDL_GetPerformanceFrequency(); printf("bench: %d frames in %.2fs = %.1f fps\n", benchN, s, benchN / s); G.quit = true; }
        bool shot = G.shotFrame >= 0 && G.frame == G.shotFrame;
        renderFrame(shot);
        if (shot) break;
    }
    saveCfg();
    AU.close();
    SDL_DestroyRenderer(G.ren); SDL_DestroyWindow(G.win); SDL_Quit();
    printf("Vector Strike exit\n");
    return 0;
}
