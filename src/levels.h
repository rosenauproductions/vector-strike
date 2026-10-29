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

// turret fight: a fighter's shot at the freighter hits the crew member nearest it - the cockpit crew for a shot from
// ahead, otherwise the gunner on that side (top / bottom) - sprays a little (a lot while the pilot is jinking), and
// fighters fire less often
static bool turretShot(Enemy& e) {
    if (G.phase != PH_TURRET) return false;
    int side = e.p.y < G.falP.y ? 1 : 0; bool front = e.p.z > G.falP.z + 60;
    std::vector<int> cand; float spray = 1;
    for (auto& p : G.pl) if (p.joined && !p.down) {
        if (p.turret == 2 && len(p.aim) > 0.35f) spray = 2.6f;   // evasive flying
        if (front && p.turret >= 2) cand.push_back(p.idx);
    }
    if (cand.empty()) for (auto& p : G.pl) if (p.joined && !p.down && p.turret == side) cand.push_back(p.idx);
    int tgt = cand.empty() ? e.tgt : cand[R.i((int)cand.size())];
    e.cd *= 1.7f; fireBall(e.p + norm3(e.v) * 7, tgt, 0.9f, V3(R.range(-14, 14), R.range(-9, 9), R.range(-14, 14)) * spray);
    return true;
}
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
    pmsg(T, "DARTH VADER IS ON YOU"); printf("game: the ace arrives (hp %.0f)\n", e.hp);
}
// beaten: he never dies, he spins away and comes back later
static void aceSpinAway(Enemy& e, Player* by) {
    e.state = 4; e.hide = 3.5f; e.hp = 1; AU.play(SND_TIEFLY, 1.0f, 0, 0.55f, 900);
    explode3(e.p, 3, C_ENEMY, e.v * 0.5f);
    if (by) { by->score += 3000; pmsg(*by, "VADER HIT - HE SPINS AWAY  +3000"); }
    printf("game: the ace spins away\n");
}

// ---------------------------------------------------------------- rails pursuers (trench and tunnel)
static void spawnPursuers(bool withAce, int n) {
    int k = 0;
    for (int i = 0; i < n + (withAce ? 1 : 0); i++) {
        Enemy e; e.kind = EN_PURSUER; e.ace = withAce && i == 0 && !PK_ACE.v.empty(); e.tgt = randomAlive();
        Player& T = G.pl[e.tgt]; e.h = 38 + 16.f * k++; e.side = (i % 2) ? 1 : -1;
        e.p = V3(T.x, T.y + 20, G.z + T.zoff - 600 - 80.f * i); e.cd = R.range(1.5f, 2.5f); e.hp = e.ace ? 10.f + 2.f * G.wave : 1.f; e.passT = R.range(2.5f, 5.f) + 1.5f * i;
        G.en.push_back(e);
    }
    AU.play(SND_TIEFLY, 1.0f, 0, 0.8f, 900); AU.play(SND_ALARM, 0.6f, 0, 1.0f, 700);
    setBanner(withAce ? "DARTH VADER ON YOUR TAIL" : "FIGHTERS BEHIND YOU", "STAY ON TARGET", 2.5f);
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
        // every few seconds one surges past and hangs out in front of you for a moment: shoot it
        e.passT -= dt;
        if (e.state == 0 && e.passT <= 0 && e.p.z > G.z + T.zoff - e.h - 30) { e.state = 5; e.hide = e.ace ? 2.0f : 2.6f; AU.play(SND_TIEFLY, 0.9f, e.side * 0.3f, 1.1f, 250); printf("game: pursuer%s surges ahead of P%d\n", e.ace ? " (ace)" : "", T.idx + 1); }
        if (e.state == 5) {
            e.hide -= dt;
            if (e.hide <= 0) { e.state = 0; e.passT = R.range(5.f, 8.f); }
        }
        bool ahead = e.state == 5;
        float wx = ahead ? e.side * 9.f + std::sin(e.t * 2.1f) * 4 : e.side * (e.ace ? 0.f : 7.f);
        V3 want(T.x + wx, T.y + (ahead ? 4.f : 2.f), G.z + T.zoff + (ahead ? 55.f : -e.h));
        if (ahead && e.p.z > G.z + T.zoff + 30 && e.hide > 1.6f) pmsg(T, e.ace ? "VADER OVERSHOT - GET HIM" : "IT OVERSHOT - NOW GET IT");
        float lag = e.ace ? 2.2f : 1.2f;   // the ace flies your exact line; his wingmen lag (and can clip catwalks / girders)
        e.p.x += (want.x - e.p.x) * std::min(1.f, dt * lag); e.p.y += (want.y - e.p.y) * std::min(1.f, dt * lag);
        e.p.z += G.speed * dt + (want.z - e.p.z) * std::min(1.f, dt * (ahead ? 1.7f : 0.8f));   // keep pace with the run, then close in
        e.v = V3(0, 0, G.speed);
        if (!ahead && e.cd <= 0 && e.p.z > want.z - 60 && aliveCount() > 0) {
            e.cd = (e.ace ? 1.7f : 2.8f) * R.range(0.8f, 1.25f) * std::max(0.55f, 1 - 0.05f * G.wave);
            V3 miss = e.ace ? V3() : V3(R.range(-9, 9), R.range(-7, 7), 0);   // the wingmen spray, the ace is precise
            fireBall(e.p + V3(0, 0, 6), e.tgt, e.ace ? 1.5f : 1.25f, miss);
        }
    }
}

static float railChaseBack(const Player& p) {
    if (G.phase != PH_TRENCH && G.phase != PH_TUNNEL) return 0;
    float far = 0; for (auto& e : G.en) if (!e.dead && e.kind == EN_PURSUER && e.state != 4 && e.tgt == p.idx) far = std::max(far, G.z + p.zoff - e.p.z);
    return far > 0 ? std::min(far * 0.8f + 18, 95.f) : 0;   // like the chase view, a bit further out
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
static void drawRocks(Canvas& cv, const Cam& cam0) {
    Cam cam = cam0; float maxD = 2500;
    if (G.phase == PH_TURRET) { cam.fogNear = 9000; cam.fogFar = 13000; maxD = 12000; }   // the field, far ahead: only the big rocks show
    for (auto& k : ROCKS) {
        V3 c = cam.toCam(k.p); if (c.z < -k.r || c.z > maxD + k.r) continue;
        if (c.z > 2500 && k.r < 40) continue;
        if (c.z > 1 && k.r * cam.foc / c.z < 1.2f) continue;
        V3 f, r, u; rockSpin(k, f, r, u);
        drawModelRU(cv, cam, ROCKM[k.shape], k.p, f, r, u, k.r, COLS[CI_ROCK], 1.1f, 0.15f);
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
    int nt = foeN(10 + 3 * G.wave);
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
        V3 q = s.p * 1.0f; V3 c(q.x - 650, q.y - 650, 1302 - std::fmod(q.z + t * 2600, 1300.f));   // stars rush toward you...
        V2 a = cam.scr(c); V3 c2 = c - V3(0, 0, std::min(c.z - 1.5f, 30 + k * 700)); V2 b = cam.scr(c2);       // ...and stretch out toward the screen edges
        cv.line(a, b, (1 + k) * LW, Col(220, 235, 255, (int)(220 * s.b)), 0.6f);
    }
    if (t > 1.2f) { float f = clampf(1 - (t - 1.2f) / 1.5f, 0, 1); cv.rect((float)cam.vp.x, (float)cam.vp.y, (float)cam.vp.w, (float)cam.vp.h, Col(255, 255, 255, (int)(255 * f))); }
}

// ---------------------------------------------------------------- level 2 opening: the freighter's gun turrets
// Everyone mans a different part of the freighter while it cruises toward the asteroid field, still far ahead:
//   1st player TOP GUN, 2nd BOTTOM GUN - stick swings the turret; the quad cannon fires its top pair and bottom pair in turn
//   3rd PILOT    - in the cockpit: stick jinks the whole freighter (shots from ahead spray wide), fire = the forward guns
//   4th CO-PILOT - in the cockpit: stick / pointer moves a sight, fire = concussion missiles (slow, wide lock, blast)
static const float FAL_S = 15.3f;   // freighter draw scale (34.75 m)
static const char* STATION_NAME[4] = { "TOP GUN", "BOTTOM GUN", "PILOT", "CO-PILOT" };
static V3 cockpitEye(const Player& p) { return G.falP + V3(p.turret == 3 ? 0.618f : 0.582f, 0.075f, 0.42f) * FAL_S; }   // two seats, side by side
static V3 turretEye(const Player& p) { if (p.turret >= 2) return cockpitEye(p); return G.falP + V3(0, (p.turret ? -1.f : 1.f) * 0.27f * FAL_S, 0); }
static void turretBasis(const Player& p, V3& f, V3& up) {
    if (p.turret >= 2) { f = V3(0, 0, 1); up = V3(std::sin(G.falRoll), std::cos(G.falRoll), 0); return; }
    float s = p.turret ? -1.f : 1.f, cy = std::cos(p.tPitch);
    f = norm3(V3(std::sin(p.tYaw) * cy, s * std::sin(p.tPitch), std::cos(p.tYaw) * cy)); up = V3(0, s, 0);
}
static void setupTurret() {
    setupAsteroids();
    G.falP = V3(0, 0, -7500); G.falSpeed = 95; G.turretDoneT = -1;
    int np = std::max(1, joinedCount());
    G.killsDone = 0; G.killsNeeded = foeN((5 + 2 * G.wave) * (1 + 0.35f * (np - 1))); G.spawnT = 2.5f; G.aceCd = 1e9f;
    G.falRoll = 0; int k = 0;
    for (auto& p : G.pl) if (p.joined) {
        p.turret = std::min(k++, 3); p.tYaw = 0; p.tPitch = 0.3f; p.aim = V2(); p.pos = G.falP; p.fwd = V3(0, 0, 1); p.up = V3(0, 1, 0); p.speed = G.falSpeed; p.zoom = 0;
        static const char* hint[4] = { "TOP GUN - SWING ONTO THEM", "BOTTOM GUN - SWING ONTO THEM", "PILOT - STICK JINKS, FIRE FORWARD GUNS", "CO-PILOT - AIM AND FIRE MISSILES" };
        pmsg(p, hint[p.turret]); p.msgT = 6;
        printf("game: P%d mans the %s\n", p.idx + 1, STATION_NAME[p.turret]);
    }
    char b[64]; snprintf(b, sizeof b, "MAN THE GUNS - %d FIGHTERS INCOMING", G.killsNeeded);
    setBanner("LEVEL 2 - WAVE " + std::to_string(G.wave), b, 3.5f);
}
static void turretAim(Player& p, float dt) {   // gunners: p.aim (stick) is a turn rate; pilot: jinks the ship; co-pilot: the sight
    if (p.turret == 2) {
        G.falP.x = clampf(G.falP.x + p.aim.x * 75 * dt, -170, 170); G.falP.y = clampf(G.falP.y + p.aim.y * 55 * dt, -95, 95);
        G.falRoll += (-p.aim.x * 0.5f - G.falRoll) * std::min(1.f, dt * 3);
    } else if (p.turret < 2) {
    p.tYaw += p.aim.x * 1.9f * turnMul() * dt; p.tPitch = clampf(p.tPitch + p.aim.y * 1.4f * turnMul() * dt, -0.08f, 1.5f);
    }
    p.pos = G.falP; p.fwd = V3(0, 0, 1); p.up = V3(0, 1, 0); p.speed = G.falSpeed;
}
static void updateTurretPhase(float dt) {
    if (G.falP.z > -1800) G.falSpeed = std::max(35.f, G.falSpeed - 20 * dt);   // ease off before the field
    G.falP.z += G.falSpeed * dt;
    bool pilot = false; for (auto& q : G.pl) if (q.joined && !q.down && q.turret == 2) pilot = true;
    if (!pilot) { G.falRoll *= std::pow(0.3f, dt); G.falP.x *= std::pow(0.7f, dt); G.falP.y *= std::pow(0.7f, dt); }   // nobody flying: settle back on course
    int alive = 0; for (auto& e : G.en) if (!e.dead && e.kind == EN_FIGHTER) alive++;
    if (G.turretDoneT < 0 && G.killsDone >= G.killsNeeded) {
        G.turretDoneT = 0; for (auto& e : G.en) if (e.kind == EN_FIGHTER && !e.dead) { e.state = 1; e.hide = 99; }   // the rest break off
        setBanner("GREAT SHOOTING - THEY'RE BREAKING OFF", "TAKE THE CONTROLS - INTO THE ASTEROIDS", 3.0f); AU.play(SND_WAVE, 0.8f);
        printf("game: turret defence done (%d kills)\n", G.killsDone);
    }
    if (G.turretDoneT >= 0) { G.turretDoneT += dt; if (G.turretDoneT > 3) beginPhase(PH_ASTEROIDS); }
    (void)alive;
}
static void drawFalconFromTurret(Canvas& cv, const Cam& cam) {   // the hull round you, minus the turret dome you sit in
    const Model& m = PK_FALCON; if (m.v.empty()) return;
    V3 f(0, 0, 1), r(1, 0, 0), u(0, 1, 0); Col c = withA(COLS[CI_FREIGHTER], 0.59f);
    for (size_t i = 0; i + 1 < m.e.size(); i += 2) {
        V3 a = G.falP + m.v[m.e[i]] * FAL_S, b = G.falP + m.v[m.e[i + 1]] * FAL_S, mid = (a + b) * 0.5f;
        if (len3(mid - cam.p) < 3.2f) continue;
        if (backEdge(m, i, cam, mid, r, u, f)) continue;
        line3(cv, cam, a, b, 1.0f, c, 0.25f);
    }
}
static V3 tcp(const Cam& c, V3 q) { return c.p + c.r * q.x + c.u * q.y + c.f * q.z; }   // camera space -> world
// quad cannon in camera space: barrels 0,1 = top pair, 2,3 = bottom pair
static V3 turretBarrel(int i, float along) { float x = (i & 1) ? 0.075f : -0.075f, y = (i < 2) ? -0.2f : -0.31f; return V3(x * (1 - along * 0.25f), y + along * 0.05f, 1.25f + along * 1.6f); }
// the freighter's cockpit, from the pilot's / co-pilot's seat: the round canopy's struts, the console along the bottom
static void drawCockpitOverlay(Canvas& cv, const Player& p) {
    const SDL_Rect& vp = p.vp; float u = vp.h / 1080.f, cx = vp.x + vp.w * 0.5f, cy = vp.y + vp.h * 0.5f;
    Col frame(150, 160, 185), dark(5, 7, 12, 255);
    float sx = p.turret == 3 ? -1.f : 1.f;   // the canopy's nose sits between the two seats
    V2 hub(cx + sx * vp.w * 0.06f, cy - vp.h * 0.05f); float big = (float)(vp.w + vp.h);
    const int N = 10; V2 in[N], mid[N];
    for (int i = 0; i < N; i++) {
        float a = (i + 0.5f) * 2 * PI / N; V2 d(std::cos(a), std::sin(a) * 0.85f);
        in[i] = hub + d * (vp.h * 0.045f); mid[i] = hub + d * (vp.h * 0.42f);
        cv.line(mid[i], hub + d * big, 4.5f * u, frame, 0.3f); cv.line(in[i], mid[i], 1.6f * u, withA(frame, 0.45f), 0.1f);   // thin near the middle: keep the view clear
    }
    for (int i = 0; i < N; i++) { cv.line(in[i], in[(i + 1) % N], 2.0f * u, withA(frame, 0.6f), 0.2f); cv.line(mid[i], mid[(i + 1) % N], 3.0f * u, frame, 0.25f); }
    float y0 = vp.y + vp.h * 0.83f;   // console
    cv.rect((float)vp.x, y0, (float)vp.w, vp.y + vp.h - y0, dark); cv.line(V2((float)vp.x, y0), V2((float)(vp.x + vp.w), y0), 3 * u, frame, 0.3f);
    for (int i = 0; i < 7; i++) { float gx = vp.x + vp.w * (0.12f + i * 0.127f); cv.circle(V2(gx, y0 + vp.h * 0.06f), vp.h * 0.025f, 1.5f * u, withA(frame, 0.7f), 0.2f, 20);
        float a = G.time * (0.7f + i * 0.31f) + i; cv.line(V2(gx, y0 + vp.h * 0.06f), V2(gx + std::cos(a) * vp.h * 0.02f, y0 + vp.h * 0.06f + std::sin(a) * vp.h * 0.02f), 1.5f * u, withA(p.col, 0.8f), 0.3f); }
    if (p.turret == 3) {   // missile reload
        float k = clampf(1 - p.fireCd / 0.8f, 0, 1), w = vp.w * 0.22f;
        cv.rectLine(cx - w / 2, y0 + vp.h * 0.105f, w, 10 * u, 1.2f * u, withA(frame, 0.8f), 0.2f);
        cv.rect(cx - w / 2, y0 + vp.h * 0.105f, w * k, 10 * u, k >= 1 ? p.col : withA(p.col, 0.5f));
    }
    FONT.draw(cv, p.turret == 3 ? "CO-PILOT  -  MISSILES" : "PILOT  -  STICK JINKS, FORWARD GUNS", V2(cx, vp.y + vp.h - 24 * u), 16 * u, withA(p.col, 0.85f), 1);
}
static void drawTurretOverlay(Canvas& cv, const Player& p) {
    if (p.turret >= 2) { drawCockpitOverlay(cv, p); return; }
    const SDL_Rect& vp = p.vp; float u = vp.h / 1080.f, cx = vp.x + vp.w * 0.5f, cy = vp.y + vp.h * 0.5f, R = vp.h * 0.47f;
    const Cam& c = p.cam; Col frame(150, 160, 185), dark(5, 7, 12, 255);
    // outside the round window: the dark turret interior
    float big = (float)(vp.w + vp.h);
    for (int i = 0; i < 48; i++) {
        float a0 = i * 2 * PI / 48, a1 = (i + 1) * 2 * PI / 48;
        V2 p0(cx + std::cos(a0) * R, cy + std::sin(a0) * R), p1(cx + std::cos(a1) * R, cy + std::sin(a1) * R), q0(cx + std::cos(a0) * big, cy + std::sin(a0) * big), q1(cx + std::cos(a1) * big, cy + std::sin(a1) * big);
        cv.quad(cv.fillV, p0, p1, q1, q0, dark, dark, dark, dark);
    }
    cv.circle(V2(cx, cy), R, 5 * u, frame, 0.4f, 64); cv.circle(V2(cx, cy), R * 0.985f, 1.5f * u, withA(frame, 0.6f), 0.2f, 64);
    // octagon framing in the middle and the braces out to the rim
    float ro = R * 0.2f; V2 oct[8];
    for (int i = 0; i < 8; i++) { float a = PI / 8 + i * PI / 4; oct[i] = V2(cx + std::cos(a) * ro, cy + std::sin(a) * ro); }
    for (int i = 0; i < 8; i++) {
        cv.line(oct[i], oct[(i + 1) % 8], 3.5f * u, frame, 0.3f);
        float a = PI / 8 + i * PI / 4; cv.line(oct[i], V2(cx + std::cos(a) * R, cy + std::sin(a) * R), 3.0f * u, frame, 0.3f);
    }
    // the quad cannon in front of you (kicks back when its pair fires)
    Col body = mixc(Col(150, 165, 195), p.col, 0.3f);
    for (int i = 0; i < 4; i++) {
        float kick = 0; for (auto& b : G.bolts) if (b.owner == p.idx && b.sy == (i < 2 ? 1 : -1)) kick = std::max(kick, b.t / b.max);
        V3 back(0, 0, -0.25f * kick * kick);
        V3 a0 = turretBarrel(i, 0) + back, a1 = turretBarrel(i, 1) + back;
        for (int k = 0; k < 4; k++) { float an = k * PI / 2; V3 o(std::cos(an) * 0.018f, std::sin(an) * 0.018f, 0); line3(cv, c, tcp(c, a0 + o), tcp(c, a1 + o), 1.2f, body, 0.2f); }
        line3(cv, c, tcp(c, a1 + V3(-0.03f, 0, 0)), tcp(c, a1 + V3(0.03f, 0, 0)), 2.0f, body, 0.5f);   // muzzle
    }
    V3 hb[8]; for (int i = 0; i < 8; i++) hb[i] = V3((i & 1) ? 0.11f : -0.11f, (i & 2) ? -0.17f : -0.36f, (i & 4) ? 1.42f : 1.18f);   // gun housing
    int ed[24] = { 0,1, 2,3, 4,5, 6,7, 0,2, 1,3, 4,6, 5,7, 0,4, 1,5, 2,6, 3,7 };
    for (int i = 0; i < 24; i += 2) line3(cv, c, tcp(c, hb[ed[i]]), tcp(c, hb[ed[i + 1]]), 1.4f, withA(body, 0.8f), 0.2f);
    FONT.draw(cv, p.turret ? "BOTTOM GUN" : "TOP GUN", V2(cx, vp.y + vp.h - 40 * u), 16 * u, withA(p.col, 0.8f), 1);
}

// ---------------------------------------------------------------- level 1 opening: escort the bombers
// Two bombers fly a winding S through the Star Destroyer fleet, hopping from destroyer to destroyer: at every bend
// they skim right over (or under) a destroyer lying broadside across the route, and its deck guns put up green flak
// all round them. Interceptors launch off the destroyers and slot in on the bombers' tails - between you and them. Near the station they break off for their run; you hold off one more
// group, then the report comes in: both bombers are down - it's up to you. (stage 0 escort, 1 cover, 2 report, 3 attack)
static bool capInside1(const Game::Capital& c, V3 w, float margin);
static V3 escortPath(float z) {
    float lam = std::max(1200.f, (G.runLen - 2600) / 5.f) / PI;   // about two and a half S bends
    float t = std::max(0.f, z - 250);   // starts dead ahead of you
    return V3(900 * std::sin(t / lam), 140 * std::sin(t / (lam * 1.7f)), z);
}
static void setupEscortRun() {
    G.escStage = 0; G.escT = 0; G.esc.clear();
    if (PK_YWING.v.empty()) { G.escStage = 3; return; }
    G.caps.clear();
    if (!PK_DESTROYER.v.empty()) {   // a destroyer on the outside of every bend
        float lam = std::max(1200.f, (G.runLen - 2600) / 5.f) / PI;
        int k = 0;
        for (float z = 250 + lam * PI * 0.5f; z < G.runLen - 1800; z += lam * PI, k++) {
            V3 q = escortPath(z); float sd = q.x > 0 ? 1.f : -1.f;
            // broadside across the route at the top of the bend, nose toward the inside; the bombers' path crosses it a
            // little ahead of the middle (clear of the bridge tower at the back), skimming just over the deck - every
            // third one they dive underneath instead
            Game::Capital c; float L = c.L;
            c.f = norm3(V3(-sd, 0, R.range(-0.2f, 0.2f))); c.r = norm3(cross3(V3(0, 1, 0), c.f)); c.u = cross3(c.f, c.r);
            bool under = k % 3 == 2; float qz = R.range(-0.05f, 0.35f), yl = under ? -(0.24f * L + 40) : 0.07f * L + 34;
            c.p = q - c.f * (qz * L) - c.u * yl; c.gunCd = R.range(1, 3); c.flakCd = R.range(0.3f, 1.0f); G.caps.push_back(c);
        }
    }
    for (int i = 0; i < 2; i++) { Game::Escort e; e.z = 250 + 40.f * i; e.off = V3(i ? 26.f : -26.f, i ? -8.f : 8.f, 0); e.hp = 14 + G.wave; e.p = escortPath(e.z) + e.off; e.f = V3(0, 0, 1); G.esc.push_back(e); }
    G.approach = false; G.escWaveT = 5; G.escWaveLeft = 0; G.aceCd = 1e9f;   // Vader waits for the cover fight
    setBanner("LEVEL 1 - WAVE " + std::to_string(G.wave), "FOLLOW THE BOMBERS IN - KEEP THE FIGHTERS OFF THEM", 3.5f);
}
static int escortsAlive() { int n = 0; for (auto& e : G.esc) if (e.alive && !e.gone) n++; return n; }
static int ESC_HITS = 0;
static void escortHit(int i) {
    auto& e = G.esc[i]; if (!e.alive) return; e.hp -= 1; e.flash = 0.15f; ESC_HITS++;
    if (e.hp <= 0) { e.alive = false; explode3(e.p, 8, Col(255, 230, 160), e.f * 60); AU.play(SND_BOOM_L, 0.8f); setBanner("WE LOST A BOMBER", escortsAlive() ? "PROTECT THE OTHER ONE" : "BOTH BOMBERS GONE", 2.5f); printf("game: bomber %d shot down\n", i + 1); }
}
// fighters that go after a bomber instead of you: returns the bomber index, or -1
static int escortTargetOf(const Enemy& e) {
    if (G.level != 1 || G.escStage != 0 || e.ace || e.weave < 1.6f) return -1;   // about 3 in 4 go for the bombers
    int i = (int)(e.weave * 10) % 2; if (G.esc.size() > (size_t)i && G.esc[i].alive && !G.esc[i].gone) return i;
    for (int k = 0; k < (int)G.esc.size(); k++) if (G.esc[k].alive && !G.esc[k].gone) return k;
    return -1;
}
static void updateEscortRun(float dt) {
    if (G.level != 1) return;
    G.escT += dt;
    V3 sc = squadCenter();
    for (int i = 0; i < (int)G.esc.size(); i++) {
        auto& e = G.esc[i]; if (!e.alive || e.gone) continue;
        e.flash = std::max(0.f, e.flash - dt); e.cd -= dt;
        if (G.escStage == 0) {   // fly the path, waiting for the squad if it falls behind
            float lag = e.z - sc.z; float spd = clampf(140 - (lag - 220) * 0.4f, 40, 200);   // stay just ahead of the squad: you follow them in
            e.z += spd * dt; V3 np = escortPath(e.z) + e.off;
            V3 want;   // skim: never into a hull (the destroyers drift) - lift over the deck / drop below the keel
            for (auto& c : G.caps) if (len3(np - c.p) < c.L * 1.3f + 60) {
                float s = dot3(np + e.lift - c.p, c.u) > -0.08f * c.L ? 1.f : -1.f; V3 t = np;
                for (int k = 0; k < 60 && capInside1(c, t, 26); k++) t += c.u * (s * 4);
                if (len3(t - np) > len3(want)) want = t - np;
            }
            e.lift += (want - e.lift) * std::min(1.f, dt * (len3(want) > len3(e.lift) ? 6.f : 1.5f)); np += e.lift;
            e.f = norm3(np - e.p + V3(0, 0, 0.01f)); e.p = np;
        } else {   // diving for the station: gone once well ahead
            e.p += e.f * (260 * dt); e.f = norm3(e.f + (norm3(G.stC - e.p) - e.f) * std::min(1.f, dt * 0.8f));
            if (e.p.z > sc.z + 2500 || len3(e.p - G.stC) < G.stR + 300) e.gone = true;
        }
        if (e.cd <= 0) {   // tail gunners: shoot at the nearest interceptor
            e.cd = R.range(0.6f, 1.1f); float bd = 500; Enemy* tg = nullptr;
            for (auto& f : G.en) if (!f.dead && f.kind == EN_FIGHTER && !f.ace) { float d = len3(f.p - e.p); if (d < bd) { bd = d; tg = &f; } }
            if (tg) { G.bolts.push_back({ e.p, tg->p, 0.2f, 0.2f, Col(255, 120, 70), 9, 0, 0 }); if (R.f() < 0.1f) { tg->dead = true; G.killsDone++; explode3(tg->p, 5, C_ENEMY, tg->v * 0.5f); AU.play(SND_BOOM_S, 0.5f); } }
        }
    }
    if (G.escStage == 0) {   // the interceptors come in waves
        int alive = 0; for (auto& f : G.en) if (!f.dead && f.kind == EN_FIGHTER && !f.ace) alive++;
        G.escWaveT -= dt;
        if (G.escWaveT <= 0 && alive < 2 && G.escWaveLeft <= 0) {
            G.escWaveLeft = foeN(3 + G.wave / 2 + joinedCount() / 2); G.escWaveT = 14; G.spawnT = 0.3f;
            setBanner("FIGHTERS INBOUND", "PICK THEM OFF THE BOMBERS", 2.0f); AU.play(SND_ALARM, 0.4f, 0, 1.2f, 700);
        }
    }
    float dropZ = G.runLen - 2400;
    if (G.escStage == 0 && (escortsAlive() == 0 || (G.esc.size() && std::max(G.esc[0].z, G.esc.size() > 1 ? G.esc[1].z : 0.f) > dropZ))) {
        G.escStage = 1; G.escT = 0; G.killsDone = 0; G.killsNeeded = foeN(4 + G.wave + joinedCount()); G.aceCd = 6;
        if (escortsAlive()) setBanner("BOMBERS: GOING IN FOR OUR RUN", "COVER US - HOLD OFF THOSE FIGHTERS", 3.0f);
        else setBanner("NO BOMBERS LEFT", "HOLD OFF THOSE FIGHTERS", 3.0f);
        printf("game: bombers break off (%d left, %d hits taken), cover fight\n", escortsAlive(), ESC_HITS);
    }
    if (G.escStage == 1 && (G.killsDone >= G.killsNeeded || G.escT > 45)) {
        G.escStage = 2; G.escT = 0; for (auto& e : G.esc) if (e.alive) { e.alive = false; e.gone = true; }
        setBanner("BOMBER GROUP - NO RESPONSE", "BOTH BOMBERS WERE DESTROYED", 3.5f); AU.play(SND_ALARM, 0.5f, 0, 0.7f);
        printf("game: report - both bombers destroyed\n");
    }
    if (G.escStage == 2 && G.escT > 3.6f) {
        G.escStage = 3; G.approach = true; setBanner("IT'S UP TO YOU", "GO IN AND HIT THE TRENCH", 3.0f); AU.play(SND_BANNER, 0.8f);
    }
}
static void drawEscorts(Canvas& cv, const Cam& cam) {
    if (PK_YWING.v.empty()) return;
    for (auto& e : G.esc) {
        if (!e.alive || e.gone) continue;
        V3 f = e.f, r = norm3(cross3(V3(0, 1, 0), f)), u = cross3(f, r);
        drawModelRU(cv, cam, PK_YWING, e.p, f, r, u, S_PLAYER * 1.3f * visScale(cam.p, e.p, 150, 900, 0.7f), e.flash > 0 ? C_WHITE : COLS[CI_BOMBER], 1.3f, 1);
    }
}
