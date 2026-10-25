// Vector Strike - ICE PLANET mode (Chris: "Empire Strikes Back" - snowspeeders against the walkers), included once
// from main.cpp. Models are Chris's: imperial_atat, the T-47 snowspeeder, the probe droid and the defence turret.
// Walkers march on the shield generator. Lasers bounce off their armour: fly in low, press X (keyboard R) to fire the
// tow cable at a walker, then circle it twice to wrap its legs - it trips and blows up. Probe droids can be shot.
// Lose the generator (walkers' guns, or one reaching it) and the game is over. All walkers down -> tally -> next wave.

static const float HW_S = 16, SPEEDER_S = 2.4f, PROBE_S = 3.0f, HTUR_S = 4.5f, GEN_R = 70;
struct Walker { V3 p, f; float t = 0, cd = 3, fallT = -1, flash = 0, msgT = 0; bool dead = false; };
struct Probe { V3 p, v; float hp = 2, cd = 3, t = 0, flash = 0; bool dead = false; };
struct HTurret { V3 p; float cd = 2, yaw = 0; bool dead = false; };
static std::vector<Walker> WK;
static std::vector<Probe> PRB;
static std::vector<HTurret> HTU;
static std::vector<int> ATAT_LEG;   // per model vertex: leg 0-3, -1 = body
static V3 ATAT_PIV[4]; static float ATAT_HIP = 0, ATAT_MINY = 0;

static void prepAtatLegs() {
    if (!ATAT_LEG.empty() || PK_ATAT.v.empty()) return;
    const auto& v = PK_ATAT.v; float mn = 1e9, mx = -1e9; for (auto& q : v) { mn = std::min(mn, q.y); mx = std::max(mx, q.y); }
    ATAT_MINY = mn; ATAT_HIP = mn + 0.6f * (mx - mn);
    float cx = 0, cz = 0; int n = 0; for (auto& q : v) if (q.y < ATAT_HIP) { cx += q.x; cz += q.z; n++; } if (n) { cx /= n; cz /= n; }
    V3 sum[4]; int cnt[4] = {};
    ATAT_LEG.resize(v.size());
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i].y >= ATAT_HIP) { ATAT_LEG[i] = -1; continue; }
        int L = (v[i].x > cx ? 1 : 0) + (v[i].z > cz ? 2 : 0); ATAT_LEG[i] = L; sum[L] += v[i]; cnt[L]++;
    }
    for (int L = 0; L < 4; L++) { ATAT_PIV[L] = cnt[L] ? sum[L] * (1.f / cnt[L]) : V3(); ATAT_PIV[L].y = ATAT_HIP; }
}
static void wkFrame(const Walker& w, V3& fw, V3& rt, V3& up) {
    fw = w.f; up = V3(0, 1, 0); rt = norm3(cross3(up, fw));
    if (w.fallT >= 0) { float a = std::min(1.5f, w.fallT * w.fallT * 1.4f), c = std::cos(a), s = std::sin(a); V3 u2 = up * c + rt * s, r2 = rt * c - up * s; up = u2; rt = r2; }
}
static float wkGait(const Walker& w) { return w.t * 0.9f; }
static V3 wkAnim(const Walker& w, size_t vi) {
    V3 m = PK_ATAT.v[vi]; int L = ATAT_LEG.empty() ? -1 : ATAT_LEG[vi];
    float g = wkGait(w), bob = 0.012f * std::sin(2 * g);
    if (L >= 0 && w.fallT < 0) {
        static const float off[4] = { 0, PI, PI, 0 };
        float ph = g + off[L], a = 0.22f * std::sin(ph); V3 pv = ATAT_PIV[L]; float y = m.y - pv.y, z = m.z - pv.z;
        float c = std::cos(a), s = std::sin(a); m.y = pv.y + y * c - z * s; m.z = pv.z + y * s + z * c;
        float k = clampf((ATAT_HIP - PK_ATAT.v[vi].y) / (ATAT_HIP - ATAT_MINY), 0, 1); m.y += std::max(0.f, std::cos(ph)) * 0.05f * k;   // lift the swinging foot
    }
    m.y += bob; return m;
}
static V3 wkWorld(const Walker& w, V3 m) { V3 fw, rt, up; wkFrame(w, fw, rt, up); return w.p + (rt * m.x + up * (m.y - ATAT_MINY) + fw * m.z) * HW_S; }
static V3 wkHead(const Walker& w) { return wkWorld(w, V3(0, ATAT_MINY + 1.15f, 0.85f)); }
static V3 wkLegsC(const Walker& w) { return w.p + V3(0, (ATAT_HIP - ATAT_MINY) * HW_S * 0.5f, 0); }
// hull test in walker space
static bool wkHit(const Walker& w, V3 q, float pad) {
    if (w.dead) return false;
    V3 fw, rt, up; wkFrame(w, fw, rt, up); V3 d = q - w.p; float x = dot3(d, rt) / HW_S, y = dot3(d, up) / HW_S + ATAT_MINY, z = dot3(d, fw) / HW_S, pm = pad / HW_S;
    if (y > ATAT_HIP - pm && y < ATAT_HIP + 0.75f + pm && std::fabs(x) < 0.32f + pm && z > -0.75f - pm && z < 0.45f + pm) return true;   // body
    if (y > ATAT_HIP && y < ATAT_HIP + 0.55f && std::fabs(x) < 0.2f + pm && z > 0.45f && z < 1.0f + pm) return true;              // neck + head
    for (int L = 0; L < 4; L++) if (y < ATAT_HIP + pm && len3(V3(x - ATAT_PIV[L].x, 0, z - ATAT_PIV[L].z)) < 0.09f + pm) return true;  // legs
    return false;
}

static void hothSetup() {
    prepAtatLegs();
    WK.clear(); PRB.clear(); G.balls.clear(); G.genHp = 100;
    int nw = std::min(5, 1 + G.wave);
    for (int i = 0; i < nw; i++) { Walker w; w.p = V3((i - (nw - 1) * 0.5f) * 260 + R.range(-40, 40), 0, 2600 + R.range(0, 900) + i * 120); w.f = V3(0, 0, -1); w.t = R.range(0, 6); w.cd = R.range(2, 5); WK.push_back(w); }
    for (int i = 0; i < 2 + G.wave; i++) { Probe p; p.p = V3(R.range(-500, 500), R.range(15, 45), R.range(1200, 2600)); p.t = R.range(0, 6); PRB.push_back(p); }
    if (HTU.empty() || G.wave == STARTWAVE[G.startSel]) { HTU.clear(); for (int i = 0; i < 6; i++) { HTurret t; t.p = V3((i - 2.5f) * 120, 0, 260 + (i % 2) * 70); HTU.push_back(t); } }
    int k = 0;
    for (auto& p : G.pl) if (p.joined) { p.pos = V3((k - 1.5f) * 25, 18, 120); p.fwd = V3(0, 0, 1); p.up = V3(0, 1, 0); p.speed = 90; p.cable = -1; p.zoom = -1; k++; }
    setBanner("ICE PLANET - WAVE " + std::to_string(G.wave), "STOP THE WALKERS - X FIRES THE TOW CABLE", 4.0f);
}
static void hothGenLost() {
    if (G.over) return;
    G.genHp = 0; explode3(V3(0, 30, 0), 30, C_PORT); AU.play(SND_BOOM_L, 1.0f, 0, 0.5f); shakeAll(30);
    setBanner("SHIELD GENERATOR LOST", "", 4.0f);
    for (auto& p : G.pl) if (p.joined && !p.down) { p.inv = 0; while (!p.down) { p.inv = 0; hitPlayer(p, "generator lost"); } }
}
static void hothPlayer(Player& p, float dt, bool fire) {
    // flight: like open space, but low over the snow
    p.speed = clampf(p.speed + p.thr * 110 * dt, 55, 175);
    V3 right = norm3(cross3(p.up, p.fwd)); float tm = turnMul();
    p.fwd = norm3(p.fwd + right * (p.aim.x * 1.7f * tm * dt) + p.up * (p.aim.y * 1.3f * tm * dt));
    V3 up = norm3(p.up - p.fwd * dot3(p.up, p.fwd)), lv = V3(0, 1, 0) - p.fwd * dot3(V3(0, 1, 0), p.fwd);
    if (len3(lv) > 0.15f) up = norm3(up + norm3(lv) * (1.6f * dt));
    p.up = up; p.pos += p.fwd * (p.speed * dt);
    if (p.pos.y < 4) { if (p.fwd.y < -0.35f) { hitPlayer(p, "the ground"); } p.pos.y = 4; p.fwd = norm3(V3(p.fwd.x, std::max(p.fwd.y, 0.08f), p.fwd.z)); }
    if (p.pos.y > 170) { p.pos.y = 170; p.fwd = norm3(V3(p.fwd.x, std::min(p.fwd.y, 0.f), p.fwd.z)); }
    if (len3(V3(p.pos.x, 0, p.pos.z)) > 4500) p.fwd = norm3(p.fwd - V3(p.pos.x, 0, p.pos.z) * (0.4f * dt / 4500));   // keep to the battle
    if (len3(p.pos) < GEN_R + 4) { p.pos = norm3(p.pos + V3(0, 0.1f, 0)) * (GEN_R + 6); hitPlayer(p, "the generator"); }
    for (auto& w : WK) if (wkHit(w, p.pos, 3)) { hitPlayer(p, "a walker"); V3 n = norm3(p.pos - wkLegsC(w)); p.pos += n * 12; p.fwd = norm3(p.fwd + n); }
    p.bank += (p.aim.x * 0.6f - p.bank) * std::min(1.f, dt * 5);
    // tow cable
    bool cab = false;
    if (p.bot) cab = p.botRoll; else { Dev* d = devById(p.dev); if (d) cab = pr(*d, B_X); }
    if (cab && p.cable < 0) {
        int best = -1; float bd = 130;
        for (int i = 0; i < (int)WK.size(); i++) { auto& w = WK[i]; if (w.dead || w.fallT >= 0) continue; V3 d = wkLegsC(w) - p.pos; float hd = len3(V3(d.x, 0, d.z)); if (hd < bd && p.pos.y < (ATAT_HIP - ATAT_MINY) * HW_S + 10) { bd = hd; best = i; } }
        if (best >= 0) { p.cable = best; V3 d = p.pos - WK[best].p; p.cableAng = std::atan2(d.x, d.z); p.cableSum = 0; AU.play(SND_LASER, 0.6f, 0, 0.5f); pmsg(p, "CABLE ON - CIRCLE IT TWICE"); }
        else { AU.play(SND_UI, 0.4f, 0, 0.6f); pmsg(p, "GET IN LOW AND CLOSE TO A WALKER"); }
    }
    if (p.cable >= 0) {
        if (p.cable >= (int)WK.size() || WK[p.cable].dead || WK[p.cable].fallT >= 0) p.cable = -1;
        else {
            Walker& w = WK[p.cable]; V3 d = p.pos - w.p; float hd = len3(V3(d.x, 0, d.z)), a = std::atan2(d.x, d.z);
            p.cableSum += std::remainder(a - p.cableAng, 2 * PI); p.cableAng = a;
            if (hd > 190) { p.cable = -1; pmsg(p, "CABLE SNAPPED - TOO FAR"); AU.play(SND_BALLHIT, 0.6f, 0, 0.5f); }
            else if (std::fabs(p.cableSum) >= 4 * PI) {
                w.fallT = 0; p.cable = -1; p.score += 5000; p.kills++; G.portBy = p.idx; pmsg(p, "IT'S GOING DOWN  +5000");
                AU.play(SND_PORT, 0.8f, 0, 0.6f); printf("game: P%d tripped a walker\n", p.idx + 1);
            }
        }
    }
    if (fire && p.fireCd <= 0 && !G.over) {   // twin lasers: probes die, walkers shrug them off
        p.fireCd = 0.15f; V3 me = p.pos, dir = p.fwd; float bestT = 800; int hp = -1, hw = -1;
        for (int i = 0; i < (int)PRB.size(); i++) { auto& q = PRB[i]; if (q.dead) continue; V3 oc = q.p - me; float t = dot3(oc, dir); if (t > 2 && t < bestT && dot3(oc, oc) - t * t < 7.5f * 7.5f) { bestT = t; hp = i; hw = -1; } }
        for (int i = 0; i < (int)WK.size(); i++) { if (WK[i].dead) continue; for (float t = 10; t < bestT; t += 6) if (wkHit(WK[i], me + dir * t, 0)) { bestT = t; hw = i; hp = -1; break; } }
        for (auto& b : G.balls) if (!b.dead) { V3 oc = b.p - me; float t = dot3(oc, dir); if (t > 2 && t < bestT && dot3(oc, oc) - t * t < 25) { b.dead = true; p.score += 30; G.flashes.push_back({ b.p, 0.2f, 0.2f, 5, C_BALL, false }); } }
        V3 end = me + dir * bestT, rt = norm3(cross3(p.up, p.fwd));
        for (int s = -1; s <= 1; s += 2) G.bolts.push_back({ me + rt * (s * 2.2f) + p.fwd * 1.5f, end, 0.15f, 0.15f, Col(255, 90, 70), 9, s, 1 });
        AU.play(SND_LASER, 0.35f, 0, 1.1f, 40);
        if (hp >= 0) { auto& q = PRB[hp]; q.hp -= 1; q.flash = 0.1f; if (q.hp <= 0) { q.dead = true; p.score += 500; p.kills++; explode3(q.p, 5, C_ENEMY); AU.play(SND_BOOM_S, 0.6f); } }
        if (hw >= 0) { G.flashes.push_back({ end, 0.15f, 0.15f, 4, C_WHITE, false }); if (WK[hw].msgT <= 0) { pmsg(p, "ARMOUR TOO STRONG - USE THE TOW CABLE (X)"); WK[hw].msgT = 6; } }
    }
}
static void hothUpdate(float dt) {
    int left = 0;
    for (auto& w : WK) {
        if (w.dead) continue;
        w.msgT -= dt; w.flash = std::max(0.f, w.flash - dt);
        if (w.fallT >= 0) {
            w.fallT += dt;
            if (w.fallT > 1.5f) { w.dead = true; V3 c = wkLegsC(w) + V3(0, 10, 0); for (int k = 0; k < 4; k++) explode3(c + rndDir() * 15, 14, k % 2 ? C_PORT : C_BALL); AU.play(SND_BOOM_L, 1.0f, 0, 0.7f); shakeAll(14); for (auto& p : G.pl) if (p.joined) pmsg(p, "WALKER DOWN"); }
            continue;
        }
        left++;
        w.t += dt;
        float dz = len3(V3(w.p.x, 0, w.p.z));
        if (dz > 520) w.p += w.f * ((7 + 0.6f * G.wave) * dt);
        else hothGenLost();
        w.f = norm3(V3(-w.p.x, 0, -w.p.z) * 0.02f + w.f);
        w.cd -= dt;
        if (w.cd <= 0) {   // head guns: speeders close by, else the generator / turrets
            w.cd = R.range(1.8f, 3.0f) * std::max(0.55f, 1 - 0.05f * G.wave); V3 h = wkHead(w); int tgt = -1; float bd = 650;
            for (auto& p : G.pl) if (p.joined && !p.down) { V3 d = p.pos - h; if (len3(d) < bd && dot3(norm3(d), w.f) > 0.3f) { bd = len3(d); tgt = p.idx; } }
            if (tgt >= 0) fireBall(h, tgt, 1.2f);
            else if (dz < 1500) { G.genHp -= 4; G.bolts.push_back({ h, V3(R.range(-30, 30), R.range(10, 50), R.range(-30, 30)), 0.3f, 0.3f, Col(255, 90, 70), 9, 0, 0 }); G.flashes.push_back({ V3(0, 40, 0), 0.3f, 0.3f, 20, C_BALL, false }); if (G.genHp <= 0) hothGenLost(); }
            else for (auto& t : HTU) if (!t.dead && len3(t.p - w.p) < 1500 && R.f() < 0.25f) { t.dead = true; G.bolts.push_back({ h, t.p, 0.3f, 0.3f, Col(255, 90, 70), 9, 0, 0 }); explode3(t.p + V3(0, 6, 0), 8, C_BALL); AU.play(SND_BOOM_S, 0.6f); break; }
        }
    }
    for (auto& q : PRB) {
        if (q.dead) continue;
        q.t += dt; q.cd -= dt; q.flash = std::max(0.f, q.flash - dt);
        int tg = randomAlive(); V3 to = G.pl[tg].pos - q.p; float d = len3(to);
        V3 want = d > 250 ? norm3(to) * 25 : V3(std::sin(q.t), 0, std::cos(q.t)) * 15; q.v = q.v + (want - q.v) * std::min(1.f, dt); q.p += q.v * dt; q.p.y = 20 + 12 * std::sin(q.t * 0.7f);
        if (q.cd <= 0 && d < 500 && aliveCount() > 0) { q.cd = R.range(2.5f, 4); fireBall(q.p, tg, 0.9f); }
    }
    for (auto& t : HTU) {   // the rebel guns plink at the walkers (they can't hurt them either)
        if (t.dead) continue; t.cd -= dt; const Walker* best = nullptr; float bd = 1600;
        for (auto& w : WK) if (!w.dead && w.fallT < 0 && len3(w.p - t.p) < bd) { bd = len3(w.p - t.p); best = &w; }
        if (best) { V3 d = wkLegsC(*best) + V3(0, 20, 0) - t.p; t.yaw = std::atan2(d.x, d.z); if (t.cd <= 0) { t.cd = R.range(1.5f, 2.5f); G.bolts.push_back({ t.p + V3(0, 9, 0), wkLegsC(*best) + V3(R.range(-10, 10), 25, 0), 0.25f, 0.25f, Col(255, 90, 70), 9, 0, 0 }); } }
    }
    PRB.erase(std::remove_if(PRB.begin(), PRB.end(), [](const Probe& q) { return q.dead; }), PRB.end());
    bool any = false; for (auto& w : WK) if (!w.dead) any = true;
    if (!any && G.phaseT > 2 && !G.over) beginPhase(PH_TALLY);
    (void)left;
}
static void hothCam(Player& p, SDL_Rect vp, float vfov) {
    V3 rt = norm3(cross3(p.up, p.fwd)), up = cross3(p.fwd, rt);
    Cam c0, c1; c0.setBasis(p.pos + up * 0.8f, p.fwd, p.up, p.bank, vp, vfov);
    float back = 15 * chaseMul(); V3 cp = p.pos - p.fwd * back + V3(0, 4.5f, 0); if (cp.y < 2) cp.y = 2;
    c1.setBasis(cp, p.pos + p.fwd * 25 - cp, V3(0, 1, 0), p.bank * 0.3f, vp, vfov);
    float want = p.third ? 0.f : 1.f; if (p.zoom < 0) p.zoom = want; else p.zoom += clampf(want - p.zoom, -0.022f, 0.022f);
    float t = p.zoom * p.zoom * (3 - 2 * p.zoom);
    p.cam = c1; p.cam.p = lerp3(c0.p, c1.p, t); V3 f = norm3(lerp3(c0.f, c1.f, t)), u = lerp3(c0.u, c1.u, t); V3 r = norm3(cross3(u, f)); p.cam.f = f; p.cam.r = r; p.cam.u = cross3(f, r);
    p.cam.fogNear = 1800; p.cam.fogFar = 3400; p.ck = c0;
}
static void drawWalker(Canvas& cv, const Cam& cam, const Walker& w) {
    if (PK_ATAT.v.empty()) return;
    V3 cc = cam.toCam(wkLegsC(w)); if (cc.z < -40 || cc.z > cam.fogFar + 60) return;
    float rpx = cc.z > 1 ? HW_S * cam.foc / cc.z : 1e4f;
    const Model& m = PK_ATAT; size_t ne = std::min(m.e.size(), (size_t)std::max(160.f, rpx * 22) * 2);
    static std::vector<V3> wv; static std::vector<char> done; wv.resize(m.v.size()); done.assign(m.v.size(), 0);
    Col c = w.flash > 0 ? C_WHITE : Col(215, 225, 245); V3 fw, rt, up; wkFrame(w, fw, rt, up);
    for (size_t i = 0; i + 1 < ne; i += 2) {
        int a = m.e[i], b = m.e[i + 1];
        if (!done[a]) { wv[a] = wkWorld(w, wkAnim(w, a)); done[a] = 1; } if (!done[b]) { wv[b] = wkWorld(w, wkAnim(w, b)); done[b] = 1; }
        if (backEdge(m, i, cam, (wv[a] + wv[b]) * 0.5f, rt, up, fw)) { if (ne < m.e.size()) ne += 2; continue; }
        line3(cv, cam, wv[a], wv[b], 1.2f, c, 0.4f);
    }
}
static void drawHothWorld(Canvas& cv, const Cam& cam, const Player* me) {
    Col sn(200, 225, 255), sd = withA(sn, 0.22f);
    Cam far = cam; far.p = V3(); far.fogNear = 1e9f; far.fogFar = 2e9f;
    auto hz = [&](float a, float h) { return V3(std::sin(a), h, std::cos(a)) * 1e5f; };
    for (int i = 0; i < 72; i++) { float a0 = i * 2 * PI / 72, a1 = (i + 1) * 2 * PI / 72;
        float h0 = 0.02f + 0.035f * std::fabs(std::sin(a0 * 3 + 2)) * std::fabs(std::sin(a0 * 7)), h1 = 0.02f + 0.035f * std::fabs(std::sin(a1 * 3 + 2)) * std::fabs(std::sin(a1 * 7));
        line3(cv, far, hz(a0, h0), hz(a1, h1), 1.2f, withA(sn, 0.7f), 0.3f); }
    float gx = std::floor(cam.p.x / 80) * 80, gz = std::floor(cam.p.z / 80) * 80;   // snowfield grid round the camera
    for (int k = -18; k <= 18; k++) { line3(cv, cam, V3(gx + k * 80.f, 0, gz - 1440), V3(gx + k * 80.f, 0, gz + 1440), 1.0f, sd, 0); line3(cv, cam, V3(gx - 1440, 0, gz + k * 80.f), V3(gx + 1440, 0, gz + k * 80.f), 1.0f, sd, 0); }
    for (int s = -1; s <= 1; s += 2) line3(cv, cam, V3(-900, 0.5f, 360 + s * 12.f), V3(900, 0.5f, 360 + s * 12.f), 1.2f, withA(sn, 0.5f), 0.2f);   // the trench line
    // the shield generator: a dome on a collar
    Col gc = G.genHp > 30 ? C_PORT : C_BAD;
    for (int r = 0; r < 4; r++) { float la = r * PI / 8; V3 pr; for (int i = 0; i <= 24; i++) { float a = i * 2 * PI / 24; V3 q(std::cos(a) * GEN_R * std::cos(la), GEN_R * std::sin(la) + 8, std::sin(a) * GEN_R * std::cos(la)); if (i) line3(cv, cam, pr, q, 1.3f, gc, 0.5f); pr = q; } }
    for (int i = 0; i < 12; i++) { float a = i * PI / 6; V3 pr; for (int k = 0; k <= 8; k++) { float la = k * PI / 16; V3 q(std::cos(a) * GEN_R * std::cos(la), GEN_R * std::sin(la) + 8, std::sin(a) * GEN_R * std::cos(la)); if (k) line3(cv, cam, pr, q, 1.1f, withA(gc, 0.7f), 0.3f); pr = q; } }
    for (auto& t : HTU) if (!t.dead && !PK_HTURRET.v.empty()) drawModel(cv, cam, PK_HTURRET, t.p + V3(0, HTUR_S * 0.85f, 0), V3(std::sin(t.yaw), 0, std::cos(t.yaw)), 0, HTUR_S, Col(180, 210, 255), 1.2f);
    for (auto& w : WK) if (!w.dead) drawWalker(cv, cam, w);
    for (auto& q : PRB) if (!q.dead && !PK_PROBE.v.empty()) drawModel(cv, cam, PK_PROBE, q.p, V3(std::sin(q.t * 0.3f), 0, std::cos(q.t * 0.3f)), 0, PROBE_S, q.flash > 0 ? C_WHITE : C_ENEMY, 1.3f);
    for (auto& o : G.pl) if (o.joined && !o.down) {
        if (&o != me || viewThird(o)) { V3 rt = norm3(cross3(o.up, o.fwd)), up = cross3(o.fwd, rt); float cs = std::cos(o.bank * 1.2f), sn2 = std::sin(o.bank * 1.2f); V3 r2 = rt * cs - up * sn2, u2 = up * cs + rt * sn2;
            if (!PK_SPEEDER.v.empty()) drawModelRU(cv, cam, PK_SPEEDER, o.pos, o.fwd, r2, u2, SPEEDER_S * (&o == me ? 1 : visScale(cam.p, o.pos, 120, 800, 1.2f)), o.col, 1.3f, 1); }
        if (o.cable >= 0 && o.cable < (int)WK.size()) {   // the tow cable and its wraps round the legs
            const Walker& w = WK[o.cable]; V3 lc = wkLegsC(w); line3(cv, cam, o.pos - o.fwd * 2, lc + norm3(o.pos - lc) * 18, 1.4f, C_WHITE, 0.8f);
            int wraps = (int)(std::fabs(o.cableSum) / (2 * PI) * 6);
            for (int k = 0; k < wraps; k++) { float y = 8 + k * 3.f; V3 pr; for (int i = 0; i <= 16; i++) { float a = i * 2 * PI / 16; V3 q = V3(lc.x, y, lc.z) + V3(std::cos(a) * 18, 0, std::sin(a) * 22); if (i) line3(cv, cam, pr, q, 1.2f, withA(C_WHITE, 0.8f), 0.4f); pr = q; } }
        }
    }
}
static void drawHothHud(Canvas& cv, Player& p, float u) {
    const SDL_Rect& vp = p.vp; float cx = vp.x + vp.w * 0.5f, cy = vp.y + vp.h * 0.5f; char buf[64];
    if (!viewThird(p) || true) { V2 c(cx, cy); float r = 20 * u; cv.circle(c, r, 1.8f * u, withA(p.col, 0.8f), 0.4f, 24); cv.line(c - V2(r * 1.6f, 0), c - V2(r * 0.6f, 0), 1.8f * u, p.col, 0.3f); cv.line(c + V2(r * 0.6f, 0), c + V2(r * 1.6f, 0), 1.8f * u, p.col, 0.3f); }
    int left = 0; float nearZ = 1e9; for (auto& w : WK) if (!w.dead) { left++; nearZ = std::min(nearZ, len3(V3(w.p.x, 0, w.p.z))); }
    snprintf(buf, sizeof buf, "WALKERS %d   NEAREST %d", left, (int)std::max(0.f, nearZ - 520) / 10 * 10); FONT.draw(cv, buf, V2(cx, vp.y + 22 * u), 18 * u, withA(C_PORT, 0.9f), 1);
    float bw = vp.w * 0.3f; FONT.draw(cv, "SHIELD GENERATOR", V2(cx, vp.y + 46 * u), 12 * u, withA(C_WHITE, 0.7f), 1);
    cv.rectLine(cx - bw / 2, vp.y + 56 * u, bw, 9 * u, 1.2f * u, withA(C_DIM, 0.8f), 0); cv.rect(cx - bw / 2 + 2 * u, vp.y + 58 * u, (bw - 4 * u) * clampf(G.genHp / 100, 0, 1), 5 * u, withA(G.genHp > 30 ? C_GOOD : C_BAD, 0.85f));
    if (p.cable >= 0) { snprintf(buf, sizeof buf, "TOW CABLE  %.1f / 2 LOOPS", std::fabs(p.cableSum) / (2 * PI)); FONT.draw(cv, buf, V2(cx, cy + 90 * u), 22 * u, withA(C_WHITE, 0.7f + 0.3f * std::sin(G.time * 8)), 1); }
    else for (auto& w : WK) if (!w.dead && w.fallT < 0) { V3 d = wkLegsC(w) - p.pos; if (len3(V3(d.x, 0, d.z)) < 130 && p.pos.y < (ATAT_HIP - ATAT_MINY) * HW_S + 10) { FONT.draw(cv, "X - FIRE TOW CABLE", V2(cx, cy + 90 * u), 22 * u, withA(C_PORT, 0.6f + 0.4f * std::sin(G.time * 10)), 1); break; } }
    for (auto& w : WK) {   // walker markers (arrows at the edge when off screen)
        if (w.dead) continue; V3 c = p.cam.toCam(wkLegsC(w) + V3(0, 15, 0)); V2 sp; bool on = false;
        if (c.z > 1) { sp = p.cam.scr(c); on = std::fabs(sp.x - cx) < vp.w * 0.45f && std::fabs(sp.y - cy) < vp.h * 0.42f; }
        if (on) { FONT.draw(cv, "WALKER", sp + V2(0, -40 * u), 13 * u, withA(C_PORT, 0.7f), 1); continue; }
        V2 d(c.x, -c.y); if (len(d) < 1e-3f) d = V2(0, 1); d = norm(d); float mx = vp.w * 0.45f, my = vp.h * 0.42f;
        float tt = std::min(mx / std::max(std::fabs(d.x), 1e-3f), my / std::max(std::fabs(d.y), 1e-3f)); V2 e = V2(cx, cy) + d * tt, n = perp(d); float sz = 16 * u;
        V2 tri[3] = { e + d * sz, e - d * sz * 0.6f + n * sz * 0.75f, e - d * sz * 0.6f - n * sz * 0.75f }; cv.fillPoly(tri, 3, withA(C_PORT, 0.5f)); cv.poly(tri, 3, 1.6f * u, C_PORT, 0.6f);
    }
}
static void hothBot(Player& p, V2& desired, bool& fire) {
    p.botRoll = false; V3 tgt; bool has = false;
    if (p.cable >= 0 && p.cable < (int)WK.size()) {   // circle the walker
        V3 c = wkLegsC(WK[p.cable]); V3 d = p.pos - c; d.y = 0; float a = std::atan2(d.x, d.z) + 0.7f; tgt = V3(c.x + std::sin(a) * 75, 16, c.z + std::cos(a) * 75); has = true;
    } else {
        float bd = 1e9; const Walker* bw = nullptr; for (auto& w : WK) if (!w.dead && w.fallT < 0) { float d = len3(w.p - p.pos); if (d < bd) { bd = d; bw = &w; } }
        for (auto& q : PRB) if (!q.dead && len3(q.p - p.pos) < 350) { tgt = q.p; has = true; break; }
        if (!has && bw) { V3 c = wkLegsC(*bw); V3 d = p.pos - c; d.y = 0; float hd = len3(d); tgt = hd > 200 ? c + norm3(d) * 90 + V3(0, 16 - c.y, 0) : V3(c.x + d.x * 0.6f, 16, c.z + d.z * 0.6f); has = true; if (hd < 115 && p.pos.y < 30) p.botRoll = R.f() < 0.2f; }
    }
    desired = V2(0, 0); fire = false; if (!has) return;
    V3 rt = norm3(cross3(p.up, p.fwd)), up = cross3(p.fwd, rt), d = tgt - p.pos;
    float x = dot3(d, rt), y = dot3(d, up), z = dot3(d, p.fwd);
    desired = V2(clampf(std::atan2(x, z) * 2, -1, 1), clampf(std::atan2(y, std::max(1.f, std::fabs(z))) * 2.5f, -1, 1));
    if (p.pos.y < 10) desired.y = std::max(desired.y, 0.4f);
    for (auto& q : PRB) if (!q.dead) { V3 o = q.p - p.pos; float t = dot3(o, p.fwd); if (t > 0 && t < 600 && dot3(o, o) - t * t < 40) fire = true; }
    p.thr = p.cable >= 0 ? -0.3f : 0.5f;
}
