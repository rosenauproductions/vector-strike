// Vector Strike - TANK BATTLE mode (a 1980 Battlezone-style tank game), included once from main.cpp.
// Endless wireframe plain with blocks and pyramids, mountains + volcano + moon on the horizon. Drive with the stick
// (up/down = forward/back, left/right = turn), fire a shell (one in flight per player). Y = chase view.
// Enemies: tanks, fast super tanks (wave 3+), ground-skimming missiles (wave 2+), a harmless saucer for points.
// Destroy the wave's quota -> tally -> next wave. 1-4 players share the battlefield.

static const float TK_TILE = 4000, TK_EYE = 3.6f;
enum { TO_CUBE, TO_PYR, TO_WIDE };
struct TankObj { int kind; V3 p; float s; };
enum { TE_TANK, TE_SUPER, TE_MISSILE, TE_SAUCER };
struct TankEn { int kind; V3 p, f; float hp = 1, cd = 2, t = 0, flash = 0, strafe = 0; int tgt = 0; bool dead = false; };
struct Shell { V3 p, v; int owner; float life; };
static std::vector<TankObj> TKO;
static std::vector<TankEn> TKE;
static std::vector<Shell> TKS;
static Model M_BZTANK, M_BZMISSILE, M_SAUCER;

static void buildTankModels() {
    if (!M_BZTANK.v.empty()) return;
    // the classic outline: a low hull, a sloped turret, a long barrel (model units ~ metres / 4, nose +z)
    M_BZTANK.v = { V3(-1.3f, 0, -2.0f), V3(1.3f, 0, -2.0f), V3(1.3f, 0, 2.2f), V3(-1.3f, 0, 2.2f),          // 0-3 track base
                   V3(-1.5f, 0.7f, -2.3f), V3(1.5f, 0.7f, -2.3f), V3(1.5f, 0.7f, 2.6f), V3(-1.5f, 0.7f, 2.6f), // 4-7 hull top
                   V3(-0.9f, 0.7f, -1.4f), V3(0.9f, 0.7f, -1.4f), V3(0.6f, 1.5f, -0.9f), V3(-0.6f, 1.5f, -0.9f), V3(0, 1.3f, 0.9f),   // 8-12 turret
                   V3(0, 1.15f, 0.9f), V3(0, 1.15f, 3.6f), V3(0, 1.05f, 3.6f) };                                   // 13-15 barrel
    M_BZTANK.E({ 0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7, 8, 9, 9, 10, 10, 11, 11, 8, 8, 12, 9, 12, 10, 12, 11, 12, 12, 14, 13, 14, 14, 15, 15, 13 });
    M_BZMISSILE.v = { V3(0, 0.5f, 3), V3(-0.5f, 0, -2), V3(0.5f, 0, -2), V3(0, 1.0f, -2), V3(0, 0.5f, -2.5f), V3(-1.2f, 0.4f, -1.6f), V3(1.2f, 0.4f, -1.6f) };
    M_BZMISSILE.E({ 0, 1, 0, 2, 0, 3, 1, 2, 2, 3, 3, 1, 1, 4, 2, 4, 3, 4, 1, 5, 2, 6, 5, 3, 6, 3 });
    for (int i = 0; i < 8; i++) { float a = i * PI / 4; M_SAUCER.v.push_back(V3(std::cos(a) * 2, 0, std::sin(a) * 2)); }
    for (int i = 0; i < 8; i++) { float a = i * PI / 4; M_SAUCER.v.push_back(V3(std::cos(a) * 0.8f, 0.7f, std::sin(a) * 0.8f)); }
    M_SAUCER.v.push_back(V3(0, -0.5f, 0));
    for (int i = 0; i < 8; i++) M_SAUCER.E({ i, (i + 1) % 8, 8 + i, 8 + (i + 1) % 8, i, 8 + i, i, 16 });
}
static V3 tkWrap(V3 p, V3 ref) { p.x += std::floor((ref.x - p.x) / TK_TILE + 0.5f) * TK_TILE; p.z += std::floor((ref.z - p.z) / TK_TILE + 0.5f) * TK_TILE; return p; }
static float tkObjR(const TankObj& o) { return o.kind == TO_WIDE ? o.s * 1.4f : o.s; }
// is a ground point inside an obstacle (wrapped to the point)? (height check for shells)
static bool tkBlocked(V3 q, float pad) {
    for (auto& o : TKO) {
        V3 c = tkWrap(o.p, q); float r = tkObjR(o) + pad;
        if (std::fabs(q.x - c.x) > r || std::fabs(q.z - c.z) > r) continue;
        float h = o.kind == TO_CUBE ? o.s * 1.6f : o.kind == TO_PYR ? o.s * 2.4f : o.s * 1.0f;
        if (o.kind != TO_CUBE) { float m = std::max(std::fabs(q.x - c.x), std::fabs(q.z - c.z)) / r; h *= 1 - m; }
        if (q.y < h + pad * 0.3f) return true;
    }
    return false;
}
static void tankSetup() {
    buildTankModels();
    if (TKO.empty()) for (int i = 0; i < 70; i++) { TankObj o; o.kind = R.i(3); o.s = R.range(14, 30); o.p = V3(R.range(-TK_TILE / 2, TK_TILE / 2), 0, R.range(-TK_TILE / 2, TK_TILE / 2)); if (len3(o.p) > 150) TKO.push_back(o); }
    TKE.clear(); TKS.clear();
    G.killsDone = 0; G.killsNeeded = foeN(6 + 2 * G.wave + 2 * (joinedCount() - 1)); G.spawnT = 2; G.tkSaucerT = R.range(25, 45);
    int k = 0;
    for (auto& p : G.pl) if (p.joined) {
        if (p.pos.y != 0 || G.wave == STARTWAVE[G.startSel]) { p.pos = V3((k - 1.5f) * 40, 0, 0); p.fwd = V3(0, 0, 1); }
        p.up = V3(0, 1, 0); p.speed = 0; p.tkShell = false; p.crackT = 0; k++;
    }
    setBanner("TANK BATTLE - WAVE " + std::to_string(G.wave), "DESTROY " + std::to_string(G.killsNeeded) + " ENEMIES", 3.5f);
}
static void tankSpawn(int kind) {
    TankEn e; e.kind = kind; e.tgt = randomAlive(); Player& T = G.pl[e.tgt];
    float a = R.range(0, 2 * PI), d = kind == TE_SAUCER ? 500 : R.range(700, 1000);
    if (kind != TE_SAUCER && R.f() < 0.6f) a = std::atan2(T.fwd.x, T.fwd.z) + R.range(-1.2f, 1.2f);   // mostly somewhere ahead-ish, so you can find them
    e.p = T.pos + V3(std::sin(a), 0, std::cos(a)) * d; for (int i = 0; i < 8 && tkBlocked(e.p, 8); i++) e.p += V3(30, 0, 30);
    e.f = norm3(T.pos - e.p); e.hp = kind == TE_SUPER ? 1 : 1; e.cd = R.range(2, 4);
    if (kind == TE_SAUCER) e.p.y = 12;
    TKE.push_back(e);
    if (kind == TE_MISSILE) { AU.play(SND_ALARM, 0.5f, 0, 1.4f, 500); for (auto& p : G.pl) if (p.joined) pmsg(p, "MISSILE INBOUND"); }
}
static void tankFire(Player& p) {
    if (p.tkShell) return;
    TKS.push_back({ p.pos + p.fwd * 9 + V3(0, 2.6f, 0), p.fwd * 300, p.idx, 2.6f });
    p.tkShell = true; p.fireCd = 0.3f; AU.play(SND_LASER, 0.5f, 0, 0.6f, 50);
}
static void tankKill(TankEn& e, Player* by) {
    e.dead = true; int pts = e.kind == TE_TANK ? 1000 : e.kind == TE_SUPER ? 3000 : e.kind == TE_MISSILE ? 2000 : 5000;
    if (by) { by->score += pts; by->kills++; pmsg(*by, (e.kind == TE_SAUCER ? "SAUCER  +" : "HIT  +") + std::to_string(pts)); }
    if (e.kind != TE_SAUCER) G.killsDone++;
    explode3(e.p + V3(0, 3, 0), e.kind == TE_SAUCER ? 6 : 8, e.kind == TE_SAUCER ? C_PORT : C_ENEMY); AU.play(SND_BOOM_L, 0.7f);
    printf("game: tank mode kill kind %d (%d/%d)\n", e.kind, G.killsDone, G.killsNeeded);
}
static void tankPlayer(Player& p, float dt, bool fire) {
    p.crackT = std::max(0.f, p.crackT - dt);
    float fwdIn = p.aim.y, turn = p.aim.x;   // stick up = forward
    float want = fwdIn > 0 ? fwdIn * 45 : fwdIn * 28;
    p.speed += clampf(want - p.speed, -60 * dt, 60 * dt);
    float yaw = turn * 1.35f * turnMul() * dt * (p.speed < -1 ? -1 : 1);
    float c = std::cos(yaw), s = std::sin(yaw); p.fwd = norm3(V3(p.fwd.x * c + p.fwd.z * s, 0, -p.fwd.x * s + p.fwd.z * c)); p.up = V3(0, 1, 0);
    V3 np = p.pos + p.fwd * (p.speed * dt);
    if (tkBlocked(np + V3(0, 1, 0), 7)) { p.speed = 0; AU.play(SND_BALLHIT, 0.3f, 0, 0.5f, 400); } else p.pos = np;
    for (auto& e : TKE) if (!e.dead && e.kind != TE_SAUCER && len3(e.p - p.pos) < 12) { V3 d = norm3(p.pos - e.p + V3(0.01f, 0, 0)); p.pos = e.p + d * 12; p.speed = 0; }
    p.pos.y = 0;
    p.bank = turn * 0.05f;
    if (fire && p.fireCd <= 0 && !G.over) tankFire(p);
}
static void tankUpdate(float dt) {
    int alive = 0; for (auto& e : TKE) if (!e.dead && e.kind != TE_SAUCER) alive++;
    int maxSim = std::min(8, foeN(std::min(6, 1 + G.wave / 2 + joinedCount() / 2)));
    G.spawnT -= dt;
    if (G.spawnT <= 0 && alive < maxSim && G.killsDone + alive < G.killsNeeded) {
        G.spawnT = R.range(2, 4);
        int k = TE_TANK; float r = R.f();
        if (G.wave >= 2 && r < 0.15f) k = TE_MISSILE; else if (G.wave >= 3 && r < 0.4f) k = TE_SUPER;
        tankSpawn(k);
    }
    G.tkSaucerT -= dt; if (G.tkSaucerT <= 0) { G.tkSaucerT = R.range(40, 70); tankSpawn(TE_SAUCER); }
    for (auto& e : TKE) {
        if (e.dead) continue;
        e.t += dt; e.cd -= dt; e.flash = std::max(0.f, e.flash - dt);
        if (!G.pl[e.tgt].joined || G.pl[e.tgt].down) e.tgt = randomAlive();
        Player& T = G.pl[e.tgt]; V3 to = T.pos - e.p; to.y = 0; float d = len3(to); V3 dir = d > 0.1f ? to * (1 / d) : e.f;
        if (e.kind == TE_SAUCER) { e.p += V3(std::sin(e.t * 0.7f), 0, std::cos(e.t * 0.5f)) * (40 * dt); e.p.y = 12 + 3 * std::sin(e.t * 2); if (e.t > 25) e.dead = true; continue; }
        if (e.kind == TE_MISSILE) {   // skims the ground in a zigzag, hops over obstacles
            V3 side(dir.z, 0, -dir.x); e.f = norm3(dir + side * std::sin(e.t * 3) * 0.6f);
            e.p += e.f * (130 + 6.f * G.wave) * dt; e.p.y = tkBlocked(e.p + e.f * 20, 4) ? std::min(e.p.y + 60 * dt, 40.f) : std::max(1.5f, e.p.y - 40 * dt);
            if (d < 8) { e.dead = true; hitPlayer(T, "missile"); hitPlayer(T, "missile"); T.crackT = 1.2f; explode3(e.p, 8, C_ENEMY); AU.play(SND_BOOM_L, 0.8f); }
            if (d > 2500) e.dead = true;
            continue;
        }
        float spd = e.kind == TE_SUPER ? 42 : 22, tr = e.kind == TE_SUPER ? 1.6f : 0.9f;
        V3 goal = dir; if (d < 300) { V3 side(dir.z, 0, -dir.x); goal = norm3(side * (std::sin(e.t * 0.4f + e.strafe) > 0 ? 1.f : -1.f) + dir * 0.2f); }
        float cr = e.f.x * goal.z - e.f.z * goal.x, turn = clampf(-cr * 3, -1, 1) * tr * dt;
        float c = std::cos(turn), s = std::sin(turn); e.f = norm3(V3(e.f.x * c + e.f.z * s, 0, -e.f.x * s + e.f.z * c));
        bool aimed = dot3(e.f, dir) > 0.985f;
        if (!(aimed && d < 900)) { V3 np = e.p + e.f * (spd * dt); if (!tkBlocked(np + V3(0, 1, 0), 8)) e.p = np; else e.strafe += 1.3f; }
        if (aimed && d < 900 && e.cd <= 0 && aliveCount() > 0) {
            e.cd = (e.kind == TE_SUPER ? 2.2f : 3.5f) * R.range(0.8f, 1.2f) * std::max(0.55f, 1 - 0.04f * G.wave);
            fireBall(e.p + e.f * 9 + V3(0, 2.6f, 0), e.tgt, 1.0f);
            AU.play(SND_TIELASER, clampf(1.1f - d / 900, 0.2f, 0.8f), 0, 0.6f, 60);
        }
    }
    // player shells
    for (auto& sh : TKS) {
        sh.life -= dt; V3 a = sh.p; sh.p += sh.v * dt;
        bool hit = sh.life <= 0 || tkBlocked(sh.p, 0);
        for (auto& e : TKE) if (!hit && !e.dead) {
            V3 c = e.p + V3(0, e.kind == TE_SAUCER ? 0 : 2.5f, 0), ab = sh.p - a; float t = clampf(dot3(c - a, ab) / std::max(1e-4f, dot3(ab, ab)), 0, 1);
            if (len3(a + ab * t - c) < (e.kind == TE_SAUCER ? 9 : 8)) { hit = true; tankKill(e, &G.pl[sh.owner]); }
        }
        for (auto& b : G.balls) if (!hit && !b.dead && len3(b.p - sh.p) < 6) { hit = true; b.dead = true; G.flashes.push_back({ b.p, 0.25f, 0.25f, 6, C_BALL, false }); }
        if (hit) { sh.life = -1; G.pl[sh.owner].tkShell = false; if (!(sh.life > 0)) G.flashes.push_back({ sh.p, 0.2f, 0.2f, 4, C_WHITE, false }); }
    }
    TKS.erase(std::remove_if(TKS.begin(), TKS.end(), [](const Shell& s) { return s.life < 0; }), TKS.end());
    for (auto& b : G.balls) if (!b.dead && tkBlocked(b.p, 0)) { b.dead = true; G.flashes.push_back({ b.p, 0.2f, 0.2f, 5, C_BALL, false }); }
    for (auto& p : G.pl) if (p.joined && p.hitT > 0.45f) p.crackT = 1.2f;
    TKE.erase(std::remove_if(TKE.begin(), TKE.end(), [](const TankEn& e) { return e.dead; }), TKE.end());
    if (G.killsDone >= G.killsNeeded && G.phaseT > 2) { G.portBy = -1; beginPhase(PH_TALLY); }
}
static void tankCam(Player& p, SDL_Rect vp, float vfov) {
    Cam c0, c1; V3 eye = p.pos + V3(0, TK_EYE, 0) + p.fwd * 1.0f;
    c0.setBasis(eye, p.fwd, V3(0, 1, 0), 0, vp, vfov);
    V3 cp = p.pos - p.fwd * 32 + V3(0, 12, 0); c1.setBasis(cp, p.pos + p.fwd * 40 + V3(0, 3, 0) - cp, V3(0, 1, 0), 0, vp, vfov);
    float want = p.third ? 1.f : 0.f; if (p.zoom < 0) p.zoom = want; else p.zoom += clampf(want - p.zoom, -0.022f, 0.022f);
    float t = p.zoom * p.zoom * (3 - 2 * p.zoom);
    p.cam = c1; p.cam.p = lerp3(c0.p, c1.p, t); V3 f = norm3(lerp3(c0.f, c1.f, t)); V3 r = norm3(cross3(V3(0, 1, 0), f)); p.cam.f = f; p.cam.r = r; p.cam.u = cross3(f, r);
    p.cam.fogNear = 900; p.cam.fogFar = 1700; p.ck = c0;
}
static void drawTankWorld(Canvas& cv, const Cam& cam, const Player* me) {
    Col g(90, 255, 130); Col gd = withA(g, 0.35f);
    // horizon: mountains, a volcano and the moon, fixed at infinity (they only turn with you)
    Cam far = cam; far.p = V3(); far.fogNear = 1e9f; far.fogFar = 2e9f;
    auto hz = [&](float a, float h) { return V3(std::sin(a), h, std::cos(a)) * 1e5f; };
    for (int i = 0; i < 64; i++) {
        float a0 = i * 2 * PI / 64, a1 = (i + 1) * 2 * PI / 64;
        float h0 = 0.03f + 0.025f * std::fabs(std::sin(a0 * 5 + 1)) + 0.02f * std::fabs(std::sin(a0 * 13)), h1 = 0.03f + 0.025f * std::fabs(std::sin(a1 * 5 + 1)) + 0.02f * std::fabs(std::sin(a1 * 13));
        line3(cv, far, hz(a0, h0), hz(a1, h1), 1.2f, withA(g, 0.8f), 0.4f);
        if (i % 3 == 0) line3(cv, far, hz(a0, h0), hz(a0 + 0.04f, 0.0f), 1.0f, withA(g, 0.4f), 0.2f);
    }
    line3(cv, far, hz(0, 0), hz(2 * PI - 0.001f, 0), 1, gd, 0);
    float va = 0.6f; V3 v0 = hz(va - 0.08f, 0.0f), v1 = hz(va - 0.02f, 0.11f), v2 = hz(va + 0.02f, 0.11f), v3 = hz(va + 0.08f, 0.0f);
    line3(cv, far, v0, v1, 1.4f, g, 0.5f); line3(cv, far, v1, v2, 1.4f, g, 0.5f); line3(cv, far, v2, v3, 1.4f, g, 0.5f);
    for (int k = 0; k < 6; k++) { float t = std::fmod(G.time * 0.6f + k / 6.f, 1.f); V3 q = hz(va + std::sin(k * 2.3f) * 0.05f * t, 0.11f + 0.08f * t - 0.1f * t * t); line3(cv, far, q, q + V3(0, -400, 0), 1.6f, withA(C_BAD, 1 - t), 0.8f); }   // volcano spitting rocks
    V3 mc = hz(2.4f, 0.22f); for (int i = 0; i < 12; i++) { float a0 = -PI / 2 + i * PI / 12, a1 = a0 + PI / 12; V3 r(std::cos(a0) * 3000, std::sin(a0) * 3000, 0), r2(std::cos(a1) * 3000, std::sin(a1) * 3000, 0); line3(cv, far, mc + r, mc + r2, 1.3f, withA(C_WHITE, 0.8f), 0.4f); line3(cv, far, mc + r * 0.55f, mc + r2 * 0.55f, 1.3f, withA(C_WHITE, 0.8f), 0.4f); }
    // obstacles, wrapped round the camera
    for (auto& o : TKO) {
        V3 c = tkWrap(o.p, cam.p); V3 cc = cam.toCam(c); if (cc.z < -60 || cc.z > 1700) continue; float s = o.s;
        if (o.kind == TO_CUBE) box3(cv, cam, c - V3(s, 0, s), c + V3(s, s * 1.6f, s), g, 1.2f);
        else { float w = tkObjR(o), h = o.kind == TO_PYR ? s * 2.4f : s * 1.0f; V3 top = c + V3(0, h, 0), b[4] = { c + V3(-w, 0, -w), c + V3(w, 0, -w), c + V3(w, 0, w), c + V3(-w, 0, w) };
            for (int i = 0; i < 4; i++) { line3(cv, cam, b[i], b[(i + 1) % 4], 1.2f, g); line3(cv, cam, b[i], top, 1.2f, g); } }
    }
    for (auto& e : TKE) {
        if (e.dead) continue; Col ec = e.flash > 0 ? C_WHITE : e.kind == TE_SAUCER ? C_PORT : C_ENEMY;
        if (e.kind == TE_SAUCER) drawModel(cv, cam, M_SAUCER, e.p, V3(std::sin(e.t * 3), 0, std::cos(e.t * 3)), 0, 4, ec, 1.4f);
        else if (e.kind == TE_MISSILE) drawModel(cv, cam, M_BZMISSILE, e.p, e.f, std::sin(e.t * 6) * 0.3f, 2.2f, ec, 1.4f);
        else drawModel(cv, cam, M_BZTANK, e.p, e.f, 0, e.kind == TE_SUPER ? 2.4f : 2.8f, ec, 1.4f);
    }
    for (auto& o : G.pl) if (o.joined && !o.down && (&o != me || viewThird(o))) drawModel(cv, cam, M_BZTANK, o.pos, o.fwd, 0, 2.8f, o.col, 1.4f);
    for (auto& sh : TKS) { blob3(cv, cam, sh.p, 1.6f, withA(C_PORT, 0.9f)); line3(cv, cam, sh.p, sh.p - norm3(sh.v) * 10, 1.6f, C_PORT, 1); }
}
static void drawTankHud(Canvas& cv, Player& p, float u) {
    const SDL_Rect& vp = p.vp; float cx = vp.x + vp.w * 0.5f, cy = vp.y + vp.h * 0.5f; Col g(90, 255, 130); char buf[64];
    // periscope sight
    if (!viewThird(p) && len(p.glance) < 0.15f) { float s = 26 * u; cv.line(V2(cx - s * 2, cy), V2(cx - s * 0.6f, cy), 2 * u, g, 0.4f); cv.line(V2(cx + s * 0.6f, cy), V2(cx + s * 2, cy), 2 * u, g, 0.4f);
        cv.line(V2(cx, cy - s * 1.6f), V2(cx, cy - s * 0.6f), 2 * u, g, 0.4f); cv.line(V2(cx - s * 0.6f, cy - s * 0.6f), V2(cx + s * 0.6f, cy - s * 0.6f), 2 * u, g, 0.4f);
        cv.line(V2(cx - s * 0.6f, cy + s * 0.6f), V2(cx + s * 0.6f, cy + s * 0.6f), 2 * u, g, 0.4f); }
    // radar: a sweeping scope at the top, enemies relative to where you face
    float R = std::min(vp.h * 0.1f, 80 * u), rx = cx, ry = vp.y + R + 46 * u;
    cv.circle(V2(rx, ry), R, 1.5f * u, withA(g, 0.7f), 0.3f, 40);
    float sw = G.time * 3; cv.line(V2(rx, ry), V2(rx + std::sin(sw) * R, ry - std::cos(sw) * R), 1.5f * u, withA(g, 0.6f), 0.4f);
    V3 rt(p.fwd.z, 0, -p.fwd.x); const TankEn* near = nullptr; float nd = 1e9;
    for (auto& e : TKE) {
        if (e.dead) continue; V3 d = e.p - p.pos; float x = dot3(d, rt), z = dot3(d, p.fwd), L = len3(d);
        if (e.kind != TE_SAUCER && L < nd) { nd = L; near = &e; }
        if (L > 1500) continue; V2 q(rx + x / 1500 * R, ry - z / 1500 * R);
        float ang = std::atan2(x, z), da = std::fabs(std::remainder(ang - sw, 2 * PI)); float br = clampf(1 - da / 2.5f, 0.3f, 1);
        cv.circle(q, 3 * u, 3 * u, withA(e.kind == TE_SAUCER ? C_PORT : C_ENEMY, br), 0.8f, 8);
    }
    for (auto& o : G.pl) if (o.joined && !o.down && &o != &p) { V3 d = o.pos - p.pos; if (len3(d) < 1500) cv.circle(V2(rx + dot3(d, rt) / 1500 * R, ry - dot3(d, p.fwd) / 1500 * R), 3 * u, 2 * u, o.col, 0.5f, 6); }
    if (near) {
        V3 d = near->p - p.pos; float ang = std::atan2(dot3(d, rt), dot3(d, p.fwd));
        const char* msg = std::fabs(ang) < 0.35f && nd < 900 ? "ENEMY IN RANGE" : std::fabs(ang) > 2.2f ? "ENEMY TO REAR" : ang < 0 ? "ENEMY TO LEFT" : "ENEMY TO RIGHT";
        FONT.draw(cv, msg, V2(cx, ry + R + 20 * u), 18 * u, withA(C_BAD, 0.6f + 0.4f * std::sin(G.time * 8)), 1);
    }
    snprintf(buf, sizeof buf, "DESTROY %d", std::max(0, G.killsNeeded - G.killsDone)); FONT.draw(cv, buf, V2(cx, vp.y + 22 * u), 18 * u, withA(g, 0.9f), 1);
    if (p.crackT > 0) {   // cracked periscope glass
        uint32_t h = 977u * (p.idx + 1) + (uint32_t)(p.shields * 31); V2 o(cx + (float)((h % 200) - 100) * u, cy + (float)(((h / 7) % 160) - 80) * u);
        for (int i = 0; i < 9; i++) { h = h * 1664525u + 1013904223u; float a = (h % 628) / 100.f; V2 q = o, dq = fromAng(a);
            for (int k = 0; k < 4; k++) { V2 n = q + (dq + fromAng(a + (k % 2 ? 0.5f : -0.5f)) * 0.4f) * (vp.h * 0.12f); cv.line(q, n, 1.6f * u, withA(C_WHITE, 0.8f * std::min(1.f, p.crackT * 2)), 0.3f); q = n; } }
    }
}
static void tankBot(Player& p, V2& desired, bool& fire) {
    const TankEn* t = nullptr; float bd = 1e9;
    for (auto& e : TKE) if (!e.dead) { float d = len3(e.p - p.pos) * (e.kind == TE_MISSILE ? 0.3f : 1); if (d < bd) { bd = d; t = &e; } }
    desired = V2(0, 0.6f); fire = false;
    if (!t) return;
    V3 d = t->p - p.pos; V3 rt(p.fwd.z, 0, -p.fwd.x); float ang = std::atan2(dot3(d, rt), dot3(d, p.fwd));
    desired = V2(clampf(ang * 2.5f, -1, 1), std::fabs(ang) < 0.4f && len3(d) > 350 ? 0.8f : 0.f);
    if (tkBlocked(p.pos + p.fwd * 25 + V3(0, 1, 0), 7)) desired = V2(1, 0.3f);
    fire = std::fabs(ang) < 0.04f && len3(d) < 950;
}
