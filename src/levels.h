// Vector Strike - levels 2 and 3, the enemy ace and the trench chase. Included once from main.cpp (after the
// shooting / enemy code, before the bots), so it can use everything above it.
//
//  Level 1  BATTLE STATION   the run in past the fleet -> surface -> trench (the ace and two wingmen chase you down
//                            the trench until the freighter turns up) -> exhaust port
//  Level 2  ASTEROID ESCAPE  everyone flies the freighter through an asteroid field with interceptors on your tail,
//                            then the hyperdrive charges and you jump to light speed
//  Level 3  REACTOR RUN      the run in, then into the station's superstructure: a long girder-filled tunnel to the
//                            reactor; shoot it and race the fireball back out

static const char* LEVELNAMES[4] = { "", "BATTLE STATION", "ASTEROID ESCAPE", "REACTOR RUN" };

// ---------------------------------------------------------------- the enemy ace
static int aceCount() { int n = 0; for (auto& e : G.en) if (!e.dead && e.ace) n++; return n; }
static void maybeSpawnAce(float dt) {
    if (PK_ACE.v.empty() || aliveCount() == 0) return;
    G.aceCd -= dt; if (G.aceCd > 0 || aceCount() > 0) return;
    Enemy e; e.kind = EN_FIGHTER; e.ace = true; e.hp = 5 + G.wave; e.cd = 2; e.weave = R.range(0, 6.28f);
    e.tgt = randomAlive(); Player& T = G.pl[e.tgt];
    e.p = T.pos - T.fwd * R.range(600, 800) + rndDir() * 80; e.v = T.fwd * 150; e.state = 0; e.t = 0;
    G.en.push_back(e); G.aceCd = 1e9;   // reset when he leaves
    AU.play(SND_TIEFLY, 1.0f, 0, 0.7f, 900); AU.play(SND_ALARM, 0.5f, 0, 0.8f, 700);
    pmsg(T, "THE ENEMY ACE IS ON YOU"); printf("game: the ace arrives (hp %.0f)\n", e.hp);
}
// beaten: he never dies, he spins away and comes back later
static void aceSpinAway(Enemy& e, Player* by) {
    e.state = 4; e.hide = 3.5f; e.hp = 1; AU.play(SND_TIEFLY, 1.0f, 0, 0.55f, 900);
    explode3(e.p, 3, C_ENEMY, e.v * 0.5f);
    if (by) { by->score += 3000; pmsg(*by, "ACE HIT - HE SPINS AWAY  +3000"); }
    printf("game: the ace spins away\n");
}

// ---------------------------------------------------------------- rails pursuers (trench and tunnel)
static void spawnPursuers(bool withAce, int n) {
    int k = 0;
    for (int i = 0; i < n + (withAce ? 1 : 0); i++) {
        Enemy e; e.kind = EN_PURSUER; e.ace = withAce && i == 0 && !PK_ACE.v.empty(); e.tgt = randomAlive();
        Player& T = G.pl[e.tgt]; e.h = 48 + 22.f * k++; e.side = (i % 2) ? 1 : -1;
        e.p = V3(T.x, T.y + 20, G.z + T.zoff - 600 - 80.f * i); e.cd = R.range(1.5f, 2.5f); e.hp = e.ace ? 99 : 1;
        G.en.push_back(e);
    }
    AU.play(SND_TIEFLY, 1.0f, 0, 0.8f, 900); AU.play(SND_ALARM, 0.6f, 0, 1.0f, 700);
    setBanner(withAce ? "ENEMY ACE ON YOUR TAIL" : "FIGHTERS BEHIND YOU", "STAY ON TARGET", 2.5f);
    printf("game: %d pursuers behind the squad%s\n", n, withAce ? " plus the ace" : "");
}
static void updatePursuers(float dt) {
    for (auto& e : G.en) {
        if (e.dead || e.kind != EN_PURSUER) continue;
        e.t += dt; e.cd -= dt; e.flash = std::max(0.f, e.flash - dt);
        if (e.state == 4) {   // spinning away out of the trench
            e.hide -= dt; e.p += V3(e.side * 30.f, 70, G.speed * 0.6f) * dt; if (e.hide <= 0) e.dead = true; continue;
        }
        if (!G.pl[e.tgt].joined || G.pl[e.tgt].down) e.tgt = randomAlive();
        Player& T = G.pl[e.tgt];
        V3 want(T.x + e.side * (e.ace ? 0.f : 7.f), T.y + 2, G.z + T.zoff - e.h);
        float lag = e.ace ? 2.2f : 1.2f;   // the ace flies your exact line; his wingmen lag (and can clip catwalks / girders)
        e.p.x += (want.x - e.p.x) * std::min(1.f, dt * lag); e.p.y += (want.y - e.p.y) * std::min(1.f, dt * lag);
        e.p.z += G.speed * dt + (want.z - e.p.z) * std::min(1.f, dt * 0.8f);   // keep pace with the run, then close in
        e.v = V3(0, 0, G.speed);
        if (e.cd <= 0 && e.p.z > want.z - 60 && aliveCount() > 0) {
            e.cd = (e.ace ? 1.7f : 2.8f) * R.range(0.8f, 1.25f) * std::max(0.55f, 1 - 0.05f * G.wave);
            V3 miss = e.ace ? V3() : V3(R.range(-9, 9), R.range(-7, 7), 0);   // the wingmen spray, the ace is precise
            fireBall(e.p + V3(0, 0, 6), e.tgt, e.ace ? 1.5f : 1.25f, miss);
        }
    }
}

static float railChaseBack(const Player& p) {
    if (G.phase != PH_TRENCH && G.phase != PH_TUNNEL) return 0;
    float far = 0; for (auto& e : G.en) if (!e.dead && e.kind == EN_PURSUER && e.state != 4 && e.tgt == p.idx) far = std::max(far, G.z + p.zoff - e.p.z);
    return far > 0 ? std::min(far + 35, 140.f) : 0;
}
// ---------------------------------------------------------------- level 1: the trench chase and the rescue
static void updateTrenchChase(float dt) {
    float prog = (G.z - G.trenchZ0) / std::max(1.f, G.trenchLen);
    if (!G.chaseOn && !G.chaseDone && prog > 0.42f) { G.chaseOn = true; G.chaseT = 0; spawnPursuers(true, 2); }
    if (!G.chaseOn) return;
    G.chaseT += dt;
    auto& A = G.ally;
    bool trouble = false; for (auto& p : G.pl) if (p.joined && !p.down && p.shields <= 2) trouble = true;
    if (!A.shown && !PK_FALCON.v.empty() && G.chaseT > 5 && (prog > 0.8f || trouble)) {
        A.shown = true; A.t = 0; A.p = V3(0, 160, G.z + 900);
        setBanner("FREIGHTER INBOUND", "YOU'RE ALL CLEAR - NOW HIT THE PORT", 3.0f); AU.play(SND_TIEFLY, 1.0f, 0, 0.6f, 900);
        printf("game: the freighter comes in over the trench\n");
    }
    if (A.shown) {   // swoops in head-on over the trench, guns the wingmen, one clips the ace and he spins off
        A.t += dt; float t = A.t;
        A.p = V3(std::sin(t * 1.3f) * 10, std::max(28.f, 130 - t * 75), G.z + 650 - t * 300);
        A.v = V3(0, t < 1.4f ? -75.f : 0.f, -300);
        A.cd -= dt;
        for (auto& e : G.en) if (!e.dead && e.kind == EN_PURSUER && e.state != 4) {
            if (!e.ace && A.cd <= 0 && t > 1.2f) {
                A.cd = 0.7f; G.bolts.push_back({ A.p, e.p, 0.25f, 0.25f, Col(255, 120, 70), 9, 0, 0 });
                e.dead = true; explode3(e.p, 6, C_ENEMY, V3(0, 0, G.speed)); AU.play(SND_BOOM_S, 0.8f); break;
            }
        }
        bool wing = false; for (auto& e : G.en) if (!e.dead && e.kind == EN_PURSUER && !e.ace) wing = true;
        if (!wing) for (auto& e : G.en) if (!e.dead && e.kind == EN_PURSUER && e.ace && e.state != 4) { e.state = 4; e.hide = 3; explode3(e.p, 3, C_ENEMY, V3(0, 0, G.speed)); printf("game: the ace spins out of the trench\n"); }
        if (t > 4.5f) { G.chaseOn = false; G.chaseDone = true; A.shown = false; }
    }
}

// ---------------------------------------------------------------- level 2: asteroids
struct Rock { V3 p, v, ax; float r, ang, spin; int shape; bool dead; };
static std::vector<Rock> ROCKS;
static Model ROCKM[6], ROCKF[6];   // ROCKF: plain icosahedron stand-ins once small on screen
static void buildRockModels() {   // lumpy subdivided icosahedra, with face normals so their far sides are hidden
    const float t = (1 + std::sqrt(5.f)) / 2;
    for (int s = 0; s < 6; s++) {
        std::vector<V3> v = { V3(-1, t, 0), V3(1, t, 0), V3(-1, -t, 0), V3(1, -t, 0), V3(0, -1, t), V3(0, 1, t), V3(0, -1, -t), V3(0, 1, -t), V3(t, 0, -1), V3(t, 0, 1), V3(-t, 0, -1), V3(-t, 0, 1) };
        std::vector<int> f = { 0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11, 1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8, 3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9, 4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1 };
        std::vector<V3> v0 = v; std::vector<int> f0 = f;
        std::map<std::pair<int, int>, int> mid; std::vector<int> f2;
        auto midp = [&](int a, int b) { auto k = std::make_pair(std::min(a, b), std::max(a, b)); auto it = mid.find(k); if (it != mid.end()) return it->second; v.push_back((v[a] + v[b]) * 0.5f); mid[k] = (int)v.size() - 1; return (int)v.size() - 1; };
        for (size_t i = 0; i < f.size(); i += 3) { int a = f[i], b = f[i + 1], c = f[i + 2], ab = midp(a, b), bc = midp(b, c), ca = midp(c, a); int q[12] = { a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca }; f2.insert(f2.end(), q, q + 12); }
        uint32_t h = 1234567u * (s + 1);
        for (size_t i = 0; i < v.size(); i++) { h = h * 1664525u + 1013904223u; float k = 0.72f + 0.5f * ((h >> 8) & 1023) / 1023.f; v[i] = norm3(v[i]) * k; }
        float sq = 0.75f + 0.08f * s; for (auto& p : v) p.y *= sq;   // a bit squashed
        Model& m = ROCKM[s]; m = Model(); m.v = v;
        std::map<std::pair<int, int>, std::vector<int>> ef;
        for (size_t i = 0; i < f2.size(); i += 3) for (int k = 0; k < 3; k++) { int a = f2[i + k], b = f2[i + (k + 1) % 3]; ef[std::make_pair(std::min(a, b), std::max(a, b))].push_back((int)i / 3); }
        auto fn = [&](int fi) { V3 a = v[f2[fi * 3]], b = v[f2[fi * 3 + 1]], c = v[f2[fi * 3 + 2]]; V3 n = norm3(cross3(b - a, c - a)); if (dot3(n, a + b + c) < 0) n = n * -1.f; return n; };
        std::vector<std::pair<float, int>> L; std::vector<std::array<int, 2>> E; std::vector<std::array<V3, 2>> N;
        for (auto& kv : ef) { if (kv.second.size() < 2) continue; E.push_back({ kv.first.first, kv.first.second }); N.push_back({ fn(kv.second[0]), fn(kv.second[1]) }); L.push_back({ -len3(v[kv.first.first] - v[kv.first.second]), (int)E.size() - 1 }); }
        std::sort(L.begin(), L.end());
        for (auto& q : L) { m.e.push_back(E[q.second][0]); m.e.push_back(E[q.second][1]); m.n.push_back(N[q.second][0]); m.n.push_back(N[q.second][1]); }
        m.sorted = true;
        Model& m2 = ROCKF[s]; m2 = Model(); for (size_t i = 0; i < v0.size(); i++) m2.v.push_back(v[i]);   // first 12 vertices = the icosahedron, same jitter
        std::set<std::pair<int, int>> seen;
        for (size_t i = 0; i < f0.size(); i += 3) for (int k = 0; k < 3; k++) { int a = f0[i + k], b = f0[i + (k + 1) % 3]; auto key = std::make_pair(std::min(a, b), std::max(a, b)); if (seen.insert(key).second) { m2.e.push_back(a); m2.e.push_back(b); } }
        m.far = &m2;
    }
}
static void setupAsteroids() {
    if (ROCKM[0].v.empty()) buildRockModels();
    ROCKS.clear(); G.fieldLen = 10000 + 1200.f * std::min(G.wave - 1, 6); G.hyperT = -1;
    int n = (int)(G.fieldLen / 8);
    for (int i = 0; i < n; i++) {
        Rock k; float a = R.range(0, 6.283f), rr = 1100 * std::sqrt(R.f());
        k.p = V3(std::cos(a) * rr, std::sin(a) * rr * 0.8f, R.range(450, G.fieldLen));
        float sz = R.f(); k.r = sz < 0.7f ? R.range(4, 18) : sz < 0.95f ? R.range(20, 60) : R.range(80, 230);
        k.v = rndDir() * R.range(0, 10); k.ax = rndDir(); k.ang = R.range(0, 6.28f); k.spin = R.range(-0.8f, 0.8f) * (30 / (k.r + 20)); k.shape = R.i(6); k.dead = false;
        ROCKS.push_back(k);
    }
    G.caps.clear();
    if (!PK_DESTROYER.v.empty()) for (int i = 0; i < 2; i++) {   // the pursuing Star Destroyers, behind you
        Game::Capital c; c.p = V3((i ? 1 : -1) * R.range(500, 900), R.range(-200, 200), -1600 - 900.f * i); c.f = norm3(V3(0, 0, 1)); c.r = norm3(cross3(V3(0, 1, 0), c.f)); c.u = cross3(c.f, c.r); c.gunCd = 3; G.caps.push_back(c);
    }
    G.approach = false; G.ally = Game::Ally();
    printf("game: asteroid field %.0f long, %d rocks\n", G.fieldLen, n);
}
static V3 rockSpin(const Rock& k, V3& f, V3& r, V3& u) {   // rotation frame from axis + angle
    V3 a = k.ax, b = norm3(cross3(a, std::fabs(a.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0))), c = cross3(a, b);
    float cs = std::cos(k.ang), sn = std::sin(k.ang);
    r = b * cs + c * sn; u = c * cs - b * sn; f = a; return k.p;
}
static void breakRock(Rock& k, V3 vel) {
    k.dead = true; for (int i = 0; i < 10 + (int)k.r; i++) { Frag f; f.p = k.p + rndDir() * k.r * 0.5f; f.v = vel * 0.3f + rndDir() * R.range(20, 80); f.d = rndDir() * R.range(0.5f, 2.5f) * (k.r / 10); f.life = f.max = R.range(0.6f, 1.4f); f.c = Col(190, 170, 150); G.frags.push_back(f); }
    AU.play(SND_BOOM_S, 0.5f, 0, 0.7f, 60);
}
static void updateRocks(float dt) {
    for (auto& k : ROCKS) { if (k.dead) continue; k.ang += k.spin * dt; k.p += k.v * dt; }
    for (auto& p : G.pl) {
        if (!p.joined || p.down) continue; float pr = 4 + 3 * shipOf(p).k;
        for (auto& k : ROCKS) {
            if (k.dead || std::fabs(k.p.z - p.pos.z) > k.r + 40) continue;
            V3 d = p.pos - k.p; float L = len3(d); if (L > k.r * 0.92f + pr) continue;
            V3 n = d * (1 / std::max(0.01f, L));
            if (k.r < 16) { breakRock(k, pvel(p)); if (p.inv <= 0) hitPlayer(p, "small rock"); }
            else { hitPlayer(p, "asteroid"); p.pos = k.p + n * (k.r * 0.92f + pr + 2); p.fwd = norm3(p.fwd + n * 0.8f); p.speed *= 0.7f; }
        }
    }
    for (auto& e : G.en) {   // interceptors smash into rocks (the ace dodges)
        if (e.dead || e.kind != EN_FIGHTER) continue;
        for (auto& k : ROCKS) {
            if (k.dead || std::fabs(k.p.z - e.p.z) > k.r + 30) continue;
            V3 d = e.p - k.p; float L = len3(d);
            if (e.ace) { if (L < k.r + 50) e.v = norm3(e.v + d * (1 / std::max(1.f, L)) * 2) * len3(e.v); continue; }
            if (e.weave > 3.5f && L < k.r + 35) { e.v = norm3(e.v + d * (1 / std::max(1.f, L))) * len3(e.v); continue; }   // the careful ones swerve
            if (L < k.r * 0.9f + 4) {
                e.dead = true; G.killsDone++; explode3(e.p, 5, C_ENEMY, k.v); AU.play(SND_BOOM_S, 0.7f);
                if (e.state == 3 && G.pl[e.tgt].joined) pmsg(G.pl[e.tgt], "IT HIT A ROCK");
                printf("game: an interceptor hit an asteroid\n"); break;
            }
        }
    }
    ROCKS.erase(std::remove_if(ROCKS.begin(), ROCKS.end(), [](const Rock& k) { return k.dead; }), ROCKS.end());
}
static void updateAsteroidStage(float dt) {
    float sz = squadCenter().z;
    if (G.hyperT < 0 && sz > G.fieldLen) { G.hyperT = 4.5f; setBanner("CLEAR OF THE FIELD", "HYPERDRIVE CHARGING - HOLD ON", 3.0f); AU.play(SND_BANNER, 0.8f); printf("game: out of the field, hyperdrive charging\n"); }
    if (G.hyperT > 0) { G.hyperT -= dt; if (G.hyperT <= 0) beginPhase(PH_LIGHTSPEED); }
}
static void drawRocks(Canvas& cv, const Cam& cam) {
    for (auto& k : ROCKS) {
        V3 c = cam.toCam(k.p); if (c.z < -k.r || c.z > 2500 + k.r) continue;
        if (c.z > 1 && k.r * cam.foc / c.z < 1.2f) continue;
        V3 f, r, u; rockSpin(k, f, r, u);
        drawModelRU(cv, cam, ROCKM[k.shape], k.p, f, r, u, k.r, Col(195, 175, 150), 1.1f, 0.15f);
    }
}

// ---------------------------------------------------------------- level 3: the superstructure tunnel
struct Beam { float z; V2 a, b; };
static std::vector<Beam> BEAMS;
static void tunFrame(float z, V3& c, float& hw, float& hh) {
    float s = z - G.tunZ0;
    c = V3(55 * std::sin(s * 0.0014f) + 25 * std::sin(s * 0.0033f + 1), 30 * std::sin(s * 0.0019f + 2), z);
    hw = 34 + 10 * std::sin(s * 0.0025f); hh = 28 + 8 * std::sin(s * 0.0021f + 1);
    float e = std::fabs(s - G.tunLen); float w = clampf(1 - e / 420, 0, 1); w = w * w * (3 - 2 * w);   // the reactor chamber
    hw += 230 * w; hh += 170 * w;
}
static float tunExitZ() { return G.tunZ0 + G.tunLen + 320 + G.outLen; }
static V3 reactorPos() { V3 c; float hw, hh; tunFrame(G.tunZ0 + G.tunLen, c, hw, hh); return c + V3(0, -75, 0); }
static void addBeams(float z0, float z1, float gapMin, float gapMax) {
    for (float z = z0; z < z1; z += R.range(gapMin, gapMax)) {
        int t = R.i(4); Beam b; b.z = z;
        if (t == 0) { float y = R.range(-0.7f, 0.7f); b.a = V2(-1, y); b.b = V2(1, y); }
        else if (t == 1) { float x = R.range(-0.7f, 0.7f); b.a = V2(x, -1); b.b = V2(x, 1); }
        else { float y1 = R.range(-1, 1), y2 = R.range(-1, 1); if (std::fabs(y1 - y2) < 0.6f) y2 = -y1; b.a = V2(-1, y1); b.b = V2(1, y2); }
        BEAMS.push_back(b);
        if (G.wave >= 3 && R.f() < 0.3f) { Beam b2 = b; b2.z += 14; std::swap(b2.a.x, b2.a.y); std::swap(b2.b.x, b2.b.y); BEAMS.push_back(b2); }
    }
}
static void beamWorld(const Beam& b, V3& A, V3& B) { V3 c; float hw, hh; tunFrame(b.z, c, hw, hh); A = c + V3(b.a.x * hw, b.a.y * hh, 0); B = c + V3(b.b.x * hw, b.b.y * hh, 0); }
static float segDist2(V2 p, V2 a, V2 b) { V2 ab = b - a; float t = clampf(dot(p - a, ab) / std::max(1e-4f, dot(ab, ab)), 0, 1); V2 q = a + ab * t; return dot(p - q, p - q); }
static void setupTunnel() {
    G.speed = G.baseSpeed = 160 + 9.f * G.wave; G.tunZ0 = 0; G.z = -150;
    G.tunLen = 6500 + 500.f * std::min(G.wave, 8); G.outLen = 4200; G.reactorHit = false; G.fireOn = false; G.portBy = -1;
    BEAMS.clear(); float gap = std::max(110.f, 230 - 12.f * G.wave);
    addBeams(500, G.tunLen - 520, gap, gap * 1.9f);
    addBeams(G.tunLen + 520, G.tunLen + 320 + G.outLen - 250, gap * 1.3f, gap * 2.4f);
    V3 c0; float hw, hh; tunFrame(0, c0, hw, hh);
    for (auto& p : G.pl) if (p.joined) { p.x = c0.x + clampf(p.x, -16, 16); p.y = c0.y + clampf(p.y, -12, 12); p.zoff = -12.f * p.idx; }
    int nt = 10 + 3 * G.wave;
    for (int i = 0; i < nt; i++) {
        Enemy e; e.kind = EN_TURRET; e.side = R.f() < 0.5f ? -1 : 1; float z = R.range(400, G.tunLen - 600); V3 c; tunFrame(z, c, hw, hh);
        e.p = c + V3(e.side * (hw - 1.5f), R.range(-0.6f, 0.6f) * hh, 0); e.cd = R.range(0.5f, 2.5f); G.en.push_back(e);
    }
    Enemy r; r.kind = EN_REACTOR; r.p = reactorPos(); r.hp = 12 + 2.f * G.wave; r.cd = 0; G.en.push_back(r);
    G.chaseOn = false; G.chaseDone = false;
    setBanner(G.runs ? "COME ROUND AGAIN" : "INTO THE SUPERSTRUCTURE", "FIND THE REACTOR - MIND THE GIRDERS", 3.5f);
}
static void updateTunnel(float dt) {
    if (!G.chaseOn && !G.chaseDone && G.z > G.tunZ0 + G.tunLen * 0.3f) { G.chaseOn = true; spawnPursuers(true, 2); }
    // pursuers that clip a girder go up in flames
    for (auto& e : G.en) if (!e.dead && e.kind == EN_PURSUER && !e.ace) for (auto& b : BEAMS) if (std::fabs(b.z - e.p.z) < 3) {
        V3 A, B; beamWorld(b, A, B); if (segDist2(V2(e.p.x, e.p.y), V2(A.x, A.y), V2(B.x, B.y)) < 25) { e.dead = true; explode3(e.p, 6, C_ENEMY, V3(0, 0, G.speed)); AU.play(SND_BOOM_S, 0.7f); for (auto& q : G.pl) if (q.joined) pmsg(q, "A FIGHTER HIT A GIRDER"); break; }
    }
    float rz = G.tunZ0 + G.tunLen;
    if (!G.reactorHit && G.z > rz + 220) {   // flew past it: round again
        G.runs++; printf("game: missed the reactor, run %d\n", G.runs); AU.play(SND_ALARM, 0.7f);
        G.z = rz - 1600; for (auto& e : G.en) if (e.kind == EN_PURSUER) e.dead = true;
        setBanner("MISSED THE REACTOR", "COME ROUND AGAIN", 2.5f);
    }
    if (G.reactorHit) {
        G.fireT += dt;
        if (!G.fireOn && G.fireT > 2.2f) { G.fireOn = true; G.fireZ = rz; G.fireSpeed = G.baseSpeed * 0.95f; setBanner("THE FIREBALL IS COMING", "FULL THROTTLE - GET OUT", 2.5f); AU.play(SND_BOOM_L, 1.0f, 0, 0.5f); shakeAll(20); }
        if (G.fireOn) {
            G.fireSpeed = std::min(G.baseSpeed * 1.25f, G.fireSpeed + G.baseSpeed * 0.035f * dt); G.fireZ += G.fireSpeed * dt;
            for (auto& p : G.pl) if (p.joined && !p.down && G.fireZ > G.z + p.zoff - 4) { hitPlayer(p, "fireball"); }
            for (auto& e : G.en) if (!e.dead && e.kind == EN_PURSUER && G.fireZ > e.p.z) { e.dead = true; explode3(e.p, 6, C_BALL, V3(0, 0, G.speed)); }
        }
        if (G.z > tunExitZ()) {   // out! watch it go from outside
            printf("game: out of the superstructure\n");
            beginPhase(PH_ESCAPE); G.phaseT = 2.21f; setBanner("YOU MADE IT OUT", "", 2.0f);
        }
    }
}
static void reactorDestroyed(Player& p) {
    G.reactorHit = true; G.fireT = 0; G.portBy = p.idx; p.score += 8000;
    V3 rp = reactorPos(); for (int k = 0; k < 6; k++) explode3(rp + rndDir() * 20, 14, k % 2 ? C_PORT : C_BALL);
    AU.play(SND_PORT, 1.0f); shakeAll(26); setBanner("REACTOR HIT", "GET OUT BEFORE IT BLOWS", 2.2f);
    printf("game: P%d hit the reactor on wave %d\n", p.idx + 1, G.wave);
}
static void drawTunnel(Canvas& cv, const Cam& cam, float zc) {
    float zEnd = tunExitZ();
    float zf = zc - 40, zt = std::min(zc + 1400, zEnd);
    Col st = withA(C_CIT, 0.75f), dim = withA(C_CIT, 0.35f);
    V3 pc; float phw = 0, phh = 0; bool has = false; int k = 0;
    for (float z = std::floor(zf / 40) * 40; z <= zt; z += 40, k++) {
        V3 c; float hw, hh; tunFrame(z, c, hw, hh);
        V3 q[4] = { c + V3(-hw, -hh, 0), c + V3(hw, -hh, 0), c + V3(hw, hh, 0), c + V3(-hw, hh, 0) };
        bool major = ((int)(z / 40) % 3) == 0;
        float heat = G.fireOn ? clampf(1 - (z - G.fireZ) / 350, 0, 1) : 0;   // walls glow ahead of the fireball
        Col rc = heat > 0 ? mixc(major ? st : dim, C_BALL, heat) : (major ? st : dim);
        for (int i = 0; i < 4; i++) line3(cv, cam, q[i], q[(i + 1) % 4], major ? 1.2f : 0.9f, rc, major ? 0.3f : 0.1f);
        if (has) {
            V3 p4[4] = { pc + V3(-phw, -phh, 0), pc + V3(phw, -phh, 0), pc + V3(phw, phh, 0), pc + V3(-phw, phh, 0) };
            for (int i = 0; i < 4; i++) line3(cv, cam, p4[i], q[i], 1.1f, st, 0.2f);
            for (int s = -1; s <= 1; s += 2) {   // wall mid-lines
                line3(cv, cam, pc + V3(s * phw, 0, 0), c + V3(s * hw, 0, 0), 0.8f, dim, 0.2f);
                line3(cv, cam, pc + V3(0, s * phh, 0), c + V3(0, s * hh, 0), 0.8f, dim, 0.2f);
            }
            if (major) {   // girder braces on the side walls
                line3(cv, cam, pc + V3(-phw, -phh, 0), c + V3(-hw, hh, 0), 0.8f, dim, 0.2f); line3(cv, cam, pc + V3(-phw, phh, 0), c + V3(-hw, -hh, 0), 0.8f, dim, 0.2f);
                line3(cv, cam, pc + V3(phw, -phh, 0), c + V3(hw, hh, 0), 0.8f, dim, 0.2f); line3(cv, cam, pc + V3(phw, phh, 0), c + V3(hw, -hh, 0), 0.8f, dim, 0.2f);
            }
        }
        pc = c; phw = hw; phh = hh; has = true;
    }
    for (auto& b : BEAMS) {
        if (b.z < zf || b.z > zt) continue;
        V3 A, B; beamWorld(b, A, B); Col bc = C_CAT;
        for (int i = -1; i <= 1; i += 2) line3(cv, cam, A + V3(0, 0, i * 2.f), B + V3(0, 0, i * 2.f), 1.4f, bc, 0.8f);
        line3(cv, cam, A, B, 0.9f, withA(bc, 0.6f), 0.3f);
    }
    if (G.fireOn && G.fireZ > zc - 300) {   // the fireball: a glowing wall filling the tunnel behind you
        V3 c; float hw, hh; tunFrame(G.fireZ, c, hw, hh); float fl = 0.8f + 0.2f * std::sin(G.time * 23);
        for (int i = 0; i < 5; i++) blob3(cv, cam, c + V3(R.range(-hw, hw) * 0.6f, R.range(-hh, hh) * 0.6f, R.range(-10, 10)), std::max(hw, hh) * (0.5f + 0.15f * i) * fl, withA(i % 2 ? C_BALL : C_PORT, 0.55f));
        for (int i = 0; i < 24; i++) { V3 a = c + V3(R.range(-hw, hw), R.range(-hh, hh), 0); line3(cv, cam, a, a + V3(R.range(-3, 3), R.range(-3, 3), R.range(10, 60)), 1.4f, C_BALL, 1); }
    }
}
static void drawReactor(Canvas& cv, const Cam& cam, const Enemy& e) {
    float sp = G.time * 0.6f, R0 = 26; Col c = e.flash > 0 ? C_WHITE : Col(255, 120, 90);
    for (int i = -2; i <= 2; i++) {   // latitude rings
        float la = i * 0.5f, rr = R0 * std::cos(la), y = R0 * std::sin(la); V3 pr;
        for (int j = 0; j <= 24; j++) { float a = j * 2 * PI / 24 + sp; V3 p = e.p + V3(std::cos(a) * rr, y, std::sin(a) * rr); if (j) line3(cv, cam, pr, p, 1.3f, c, 0.8f); pr = p; }
    }
    for (int j = 0; j < 8; j++) { float a = j * PI / 8 + sp; V3 pr; for (int i = 0; i <= 16; i++) { float la = -PI / 2 + i * PI / 16; V3 p = e.p + V3(std::cos(a) * std::cos(la), std::sin(la), std::sin(a) * std::cos(la)) * R0; if (i) line3(cv, cam, pr, p, 1.0f, withA(c, 0.7f), 0.5f); pr = p; } }
    for (int k = 0; k < 2; k++) { float tl = 0.4f + k * 0.8f; V3 pr; for (int j = 0; j <= 32; j++) { float a = j * 2 * PI / 32 - sp * 1.5f; V3 p = e.p + V3(std::cos(a) * 52, std::sin(a) * 52 * std::sin(tl), std::sin(a) * 52 * std::cos(tl)); if (j) line3(cv, cam, pr, p, 1.2f, withA(C_PORT, 0.8f), 0.7f); pr = p; } }
    V3 cc; float hw, hh; tunFrame(e.p.z, cc, hw, hh);
    for (int k = 0; k < 6; k++) { float a = k * PI / 3; line3(cv, cam, e.p + V3(std::cos(a) * 30, std::sin(a) * 30, 0), cc + V3(std::cos(a) * hw, std::sin(a) * hh, 0), 1.0f, withA(C_CIT, 0.6f), 0.3f); }
    blob3(cv, cam, e.p, 14 + 4 * std::sin(G.time * 8), withA(C_PORT, 0.7f));
}

// ---------------------------------------------------------------- level 2 ending: light speed
static void drawLightspeed(Canvas& cv, const Cam& cam) {
    float t = G.phaseT; float k = clampf(t / 1.2f, 0, 1);
    for (auto& s : G.stars) {   // star streaks pouring past
        V3 q = s.p * 1.0f; V3 c(q.x - 650, q.y - 650, std::fmod(q.z + t * 2600, 1300.f) + 2);
        V2 a = cam.scr(c); V3 c2 = c + V3(0, 0, 40 + k * 900); V2 b = cam.scr(c2);
        cv.line(a, b, (1 + k) * LW, Col(220, 235, 255, (int)(220 * s.b)), 0.6f);
    }
    if (t > 1.2f) { float f = clampf(1 - (t - 1.2f) / 1.5f, 0, 1); cv.rect((float)cam.vp.x, (float)cam.vp.y, (float)cam.vp.w, (float)cam.vp.h, Col(255, 255, 255, (int)(255 * f))); }
}
