// Vector Strike - STAR FOX MODE: an on-rails course (Chris's "Star Fox clone"), included once from main.cpp.
// The course flies forward on its own; the stick slides your ship round the screen; the camera sits behind and
// trails a little so the ship moves across the view. Lasers fire where the ship points (two-box reticle).
//   X (keyboard R) barrel roll - turns away shots      triggers / E,Q  boost / brake (squad average)
//   hold fire to charge, release for a homing bomb      Y  cockpit view
// Course: city blocks -> arches and rings -> pillar field -> the boss (a carrier that flies ahead of you).
// Enemies and the boss are generic wireframes built here; the player ship is Chris's low-poly SNES fighter (arwing.vsm).

static const float FOX_GROUND = -55, FOX_XW = 95, FOX_YTOP = 75;
enum { FO_BLOCK, FO_ARCH, FO_RING, FO_PILLAR };
struct FoxObj { int kind; V3 p; float w, h, d; bool hit = false; };
enum { FE_DRONE, FE_TANK, FE_POD, FE_CORE };
struct FoxEn { int kind; V3 p, v; float hp = 1, t = 0, cd = 2, flash = 0, ph = 0, ax = 0, ay = 0; int side = 0; bool dead = false; };
struct FoxBomb { V3 p, v; int owner, tgt; float life; };
static std::vector<FoxObj> FOXO;
static std::vector<FoxEn> FOXE;
static std::vector<FoxBomb> FOXB;
static Model M_DRONE, M_TANK;

static void buildFoxModels() {
    if (!M_DRONE.v.empty()) return;
    // drone: a flat ring-winged fighter (octagon hoop round a spike)
    for (int i = 0; i < 8; i++) { float a = i * PI / 4; M_DRONE.v.push_back(V3(std::cos(a) * 1.0f, std::sin(a) * 1.0f, -0.2f)); }
    M_DRONE.v.push_back(V3(0, 0, 1.4f)); M_DRONE.v.push_back(V3(0, 0, -0.8f));
    for (int i = 0; i < 8; i++) M_DRONE.E({ i, (i + 1) % 8 });
    for (int i = 0; i < 8; i += 2) M_DRONE.E({ i, 8, i, 9 });
    // tank: a wedge body on a box, with a barrel
    M_TANK.v = { V3(-1.2f, 0, -1.6f), V3(1.2f, 0, -1.6f), V3(1.2f, 0, 1.6f), V3(-1.2f, 0, 1.6f), V3(-0.9f, 0.9f, -1.0f), V3(0.9f, 0.9f, -1.0f), V3(0.9f, 0.9f, 0.9f), V3(-0.9f, 0.9f, 0.9f), V3(0, 1.1f, 0), V3(0, 1.8f, 2.4f) };
    M_TANK.E({ 0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7, 8, 9 });
}

static void foxSetup() {
    buildFoxModels();
    FOXO.clear(); FOXE.clear(); FOXB.clear();
    G.foxLen = 9500 + 600.f * std::min(G.wave - 1, 6); G.foxBoss = false; G.foxBossT = 0; G.foxClearT = -1;
    G.speed = G.baseSpeed = 150 + 8.f * G.wave; G.z = 0; G.zStart = 0;
    float L = G.foxLen;
    // 1: city - blocks either side, some in the flight path
    for (float z = 300; z < L * 0.3f; z += R.range(70, 120)) {
        for (int s = -1; s <= 1; s += 2) { FoxObj o; o.kind = FO_BLOCK; o.w = R.range(18, 40); o.d = R.range(18, 45); o.h = R.range(25, 110); o.p = V3(s * R.range(110, 220), FOX_GROUND, z); FOXO.push_back(o); }
        if (R.f() < 0.35f) { FoxObj o; o.kind = FO_BLOCK; o.w = R.range(14, 26); o.d = R.range(14, 26); o.h = R.range(30, 95); o.p = V3(R.range(-70, 70), FOX_GROUND, z + 40); FOXO.push_back(o); }
    }
    // 2: arches across the course, rings through some of them
    for (float z = L * 0.32f; z < L * 0.58f; z += R.range(260, 380)) {
        FoxObj a; a.kind = FO_ARCH; a.w = R.range(70, 110); a.h = R.range(50, 80); a.d = 10; a.p = V3(R.range(-30, 30), FOX_GROUND, z); FOXO.push_back(a);
        if (R.f() < 0.6f) { FoxObj r; r.kind = FO_RING; r.w = 9; r.p = V3(a.p.x + R.range(-15, 15), FOX_GROUND + a.h * 0.55f, z); FOXO.push_back(r); }
    }
    // 3: a field of tall pillars to weave through
    for (float z = L * 0.6f; z < L * 0.88f; z += R.range(45, 85)) { FoxObj o; o.kind = FO_PILLAR; o.w = R.range(7, 12); o.h = R.range(90, 160); o.p = V3(R.range(-90, 90), FOX_GROUND, z); FOXO.push_back(o); }
    for (int i = 0; i < 6; i++) { FoxObj r; r.kind = FO_RING; r.w = 9; r.p = V3(R.range(-60, 60), R.range(-25, 40), L * (0.12f + 0.13f * i)); FOXO.push_back(r); }
    // ground tanks along the way
    for (float z = 600; z < L * 0.85f; z += R.range(250, 450)) { FoxEn e; e.kind = FE_TANK; e.p = V3(R.range(-80, 80), FOX_GROUND, z); e.hp = 2; e.cd = R.range(1, 3); FOXE.push_back(e); }
    G.foxWaveZ = 400;
    for (auto& p : G.pl) if (p.joined) { p.rollT = 0; p.charge = 0; p.zoom = -1; }
    setBanner("STAR FOX MODE - COURSE " + std::to_string(G.wave), "CLEAR THE WAY TO THE CARRIER", 3.5f);
}
// a flight of drones comes in from ahead in a V, or sweeps across the course
static void foxSpawnWave() {
    int n = 4 + std::min(4, G.wave / 2) + joinedCount() / 2, pat = R.i(3);
    float zc = G.z + 900, xc = R.range(-50, 50), yc = R.range(-15, 40);
    for (int i = 0; i < n; i++) {
        FoxEn e; e.kind = FE_DRONE; e.hp = G.wave >= 4 ? 2 : 1; e.cd = R.range(1.5f, 3.5f); e.ph = i * 0.6f; e.side = pat;
        if (pat == 0) { e.p = V3(xc + (i - n / 2) * 16.f, yc + std::abs(i - n / 2) * 6.f, zc + std::abs(i - n / 2) * 25.f); e.ax = 0; }
        else { e.p = V3((pat == 1 ? -1 : 1) * (140 + i * 22.f), yc + R.range(-10, 10), zc + i * 30.f); e.ax = pat == 1 ? 1.f : -1.f; }
        FOXE.push_back(e);
    }
}
static void foxSpawnBoss() {
    G.foxBoss = true; G.foxBossT = 0; G.foxBossX = 0; G.foxBossY = 20;
    for (int s = -1; s <= 1; s += 2) { FoxEn e; e.kind = FE_POD; e.side = s; e.hp = 14 + 2.f * G.wave; e.cd = R.range(1, 2); FOXE.push_back(e); }
    FoxEn c; c.kind = FE_CORE; c.hp = 26 + 4.f * G.wave; c.cd = 3; FOXE.push_back(c);
    setBanner("WARNING - CARRIER APPROACHING", "SHOOT OFF ITS GUN PODS, THEN HIT THE CORE", 3.0f); AU.play(SND_ALARM, 0.8f, 0, 0.8f);
    printf("game: fox boss\n");
}
static V3 foxBossC() { return V3(G.foxBossX, G.foxBossY, G.z + 330); }
static bool foxPodsUp() { for (auto& e : FOXE) if (!e.dead && e.kind == FE_POD) return true; return false; }

static float foxRoll(const Player& p) { return p.rollT > 0 ? -2 * PI * (1 - p.rollT / 0.55f) * (p.rollDir ? p.rollDir : 1) : 0; }
static V3 foxAimDir(const Player& p) { return norm3(V3(p.aim.x * 0.28f, p.aim.y * 0.22f, 1)); }

static void foxInput(Player& p, bool& fire, float dt) {   // barrel roll, charge shot
    bool roll = false;
    if (p.bot) roll = p.botRoll; else { Dev* d = devById(p.dev); if (d) roll = pr(*d, B_X) || (d->id == -1 && d->b[B_X] && !d->pb[B_X]); }
    p.rollT = std::max(0.f, p.rollT - dt);
    if (roll && p.rollT <= 0) { p.rollT = 0.55f; p.rollDir = p.aim.x < 0 ? -1 : 1; AU.play(SND_TIEFLY, 0.4f, 0, 1.6f, 300); }
    if (fire) p.charge = std::min(1.5f, p.charge + dt);
    else {
        if (p.charge >= 0.8f) {   // release a charged shot: a homing bomb at the nearest enemy ahead
            int best = -1; float bd = 1e9; V3 me = ppos(p), dir = foxAimDir(p);
            for (int i = 0; i < (int)FOXE.size(); i++) { auto& e = FOXE[i]; if (e.dead || (e.kind == FE_CORE && foxPodsUp())) continue; V3 d = e.p - me; float a = dot3(norm3(d), dir); if (a > 0.8f && len3(d) < 900 && len3(d) * (2 - a) < bd) { bd = len3(d) * (2 - a); best = i; } }
            FOXB.push_back({ me + dir * 6, dir * (G.speed + 260), p.idx, best, 3.0f });
            AU.play(SND_PORT, 0.5f, 0, 1.6f, 100);
        }
        p.charge = 0;
    }
}
static void foxDamage(FoxEn& e, float dmg, Player& p) {
    if (e.dead) return;
    if (e.kind == FE_CORE && foxPodsUp()) { G.flashes.push_back({ e.p, 0.15f, 0.15f, 6, C_WHITE, false }); return; }   // shielded until the pods are gone
    e.hp -= dmg; e.flash = 0.1f;
    if (e.hp > 0) { AU.play(SND_BALLHIT, 0.35f); return; }
    e.dead = true; p.kills++; p.foxHits++;
    int pts = e.kind == FE_DRONE ? 100 : e.kind == FE_TANK ? 150 : e.kind == FE_POD ? 2000 : 10000; p.score += pts;
    explode3(e.p, e.kind >= FE_POD ? 14 : 5, e.kind == FE_TANK ? C_TOWER : C_ENEMY, V3(0, 0, G.speed)); AU.play(e.kind >= FE_POD ? SND_BOOM_L : SND_BOOM_S, 0.7f);
    if (e.kind == FE_POD) setBanner("GUN POD DOWN", foxPodsUp() ? "ONE MORE" : "THE CORE IS OPEN - HIT IT", 2.0f);
    if (e.kind == FE_CORE) {
        G.portBy = p.idx; G.foxClearT = 0; V3 c = foxBossC();
        for (int k = 0; k < 8; k++) explode3(c + rndDir() * 50, 16, k % 2 ? C_PORT : C_BALL, V3(0, 0, G.speed));
        for (auto& q : FOXE) if (q.kind == FE_DRONE) q.dead = true;
        shakeAll(26); setBanner("CARRIER DESTROYED", "COURSE CLEAR", 3.0f); printf("game: P%d destroyed the fox boss\n", p.idx + 1);
    }
}
static void foxFire(Player& p) {
    p.fireCd = 0.14f;
    V3 me = ppos(p), dir = foxAimDir(p); float bestT = 900; int hitE = -1, hitB = -1;
    auto test = [&](V3 c, float r, int e, int b) { V3 oc = c - me; float t = dot3(oc, dir); if (t < 2 || t > bestT) return; float d2 = dot3(oc, oc) - t * t; if (d2 < r * r) { bestT = t; hitE = e; hitB = b; } };
    for (int i = 0; i < (int)FOXE.size(); i++) { auto& e = FOXE[i]; if (e.dead) continue; test(e.p, e.kind == FE_DRONE ? 7 : e.kind == FE_TANK ? 8 : e.kind == FE_POD ? 20 : 26, i, -1); }
    for (int i = 0; i < (int)G.balls.size(); i++) if (!G.balls[i].dead) test(G.balls[i].p, 4.5f, -1, i);
    for (auto& o : FOXO) if (o.kind == FO_BLOCK || o.kind == FO_PILLAR) {   // buildings stop shots
        V3 oc = V3(o.p.x, FOX_GROUND + o.h * 0.5f, o.p.z) - me; float t = oc.z / std::max(0.05f, dir.z); if (t < 2 || t > bestT) continue;
        V3 at = me + dir * t; if (std::fabs(at.x - o.p.x) < o.w * 0.5f + 1 && at.y < FOX_GROUND + o.h) { bestT = t; hitE = -1; hitB = -1; }
    }
    V3 end = me + dir * bestT;
    V3 sp, sf, sr, su; shipFrame(p, sp, sf, sr, su); float S = S_PLAYER;
    for (int s = -1; s <= 1; s += 2) { V3 tip = sp + sr * (s * 0.9f * S) + sf * (-0.6f * S); G.bolts.push_back({ tip, end, 0.14f, 0.14f, Col(120, 255, 140), p.idx, s, 1 }); }
    AU.play(SND_LASER, 0.35f, 0, 1.25f, 40);
    if (hitE >= 0) foxDamage(FOXE[hitE], 1, p);
    if (hitB >= 0) { Ball& b = G.balls[hitB]; b.dead = true; p.score += 30; G.flashes.push_back({ b.p, 0.2f, 0.2f, 5, C_BALL, false }); }
}

static void foxUpdate(float dt) {
    // enemy flights
    if (!G.foxBoss) { while (G.z + 900 > G.foxWaveZ && G.foxWaveZ < G.foxLen - 400) { foxSpawnWave(); G.foxWaveZ += R.range(700, 1050) / std::min(1.6f, 0.9f + 0.07f * G.wave); } }
    if (!G.foxBoss && G.z > G.foxLen) foxSpawnBoss();
    if (G.foxBoss && G.foxClearT < 0) { G.foxBossT += dt; G.foxBossX = 45 * std::sin(G.foxBossT * 0.5f); G.foxBossY = 18 + 18 * std::sin(G.foxBossT * 0.37f);
        if ((int)(G.foxBossT / 7) != (int)((G.foxBossT - dt) / 7)) foxSpawnWave(); }
    if (G.foxClearT >= 0) { G.foxClearT += dt; if (G.foxClearT > 3.5f) { G.foxClearT = -2; beginPhase(PH_TALLY); return; } }
    for (auto& e : FOXE) {
        if (e.dead) continue;
        e.t += dt; e.cd -= dt; e.flash = std::max(0.f, e.flash - dt);
        float relz = e.p.z - G.z;
        if (e.kind == FE_DRONE) {
            if (e.side == 0) { e.p.x += std::sin(e.t * 1.4f + e.ph) * 30 * dt; e.p.y += std::cos(e.t * 1.1f + e.ph) * 14 * dt; e.p.z += (G.speed * 0.55f) * dt; }
            else { e.p.x += e.ax * 75 * dt; e.p.y += std::sin(e.t * 2 + e.ph) * 18 * dt; e.p.z += G.speed * 0.75f * dt; }
            if (relz < 100 && e.t > 2) { e.p.y += 50 * dt; e.p.z += G.speed * 0.4f * dt; }   // peel off over you
            if (relz < -60 || std::fabs(e.p.x) > 400) { e.dead = true; continue; }
        } else if (e.kind == FE_TANK) {
            if (relz < -60) { e.dead = true; continue; }
        } else if (e.kind == FE_POD) { e.p = foxBossC() + V3(e.side * 78.f, -6, -10); }
        else if (e.kind == FE_CORE) { e.p = foxBossC() + V3(0, 0, -62); }
        bool canFire = relz > 120 && relz < 750 && aliveCount() > 0 && G.foxClearT < 0;
        if (e.kind == FE_CORE && foxPodsUp()) canFire = false;
        if (canFire && e.cd <= 0) {
            float base = e.kind == FE_DRONE ? 3.2f : e.kind == FE_TANK ? 2.6f : e.kind == FE_POD ? 1.3f : 2.4f;
            e.cd = base * R.range(0.8f, 1.25f) * std::max(0.55f, 1 - 0.05f * G.wave);
            if (e.kind == FE_CORE) for (int k = -2; k <= 2; k++) fireBall(e.p, randomAlive(), 0.85f, V3(k * 14.f, R.range(-6, 6), 0));
            else fireBall(e.p, randomAlive(), e.kind == FE_POD ? 1.0f : 0.9f, V3(R.range(-6, 6), R.range(-4, 4), 0));
        }
    }
    FOXE.erase(std::remove_if(FOXE.begin(), FOXE.end(), [](const FoxEn& e) { return e.dead && e.kind != FE_POD && e.kind != FE_CORE; }), FOXE.end());
    // homing bombs
    for (auto& b : FOXB) {
        b.life -= dt;
        if (b.tgt >= 0 && b.tgt < (int)FOXE.size() && !FOXE[b.tgt].dead) { V3 want = norm3(FOXE[b.tgt].p - b.p) * (G.speed + 280); b.v = b.v + (want - b.v) * std::min(1.f, dt * 4); }
        b.p += b.v * dt;
        bool boom = b.life <= 0;
        for (auto& e : FOXE) if (!e.dead && len3(e.p - b.p) < (e.kind >= FE_POD ? 24 : 9)) boom = true;
        if (boom) {
            b.life = -1; explode3(b.p, 10, C_PORT, V3(0, 0, G.speed)); AU.play(SND_BOOM_L, 0.6f, 0, 1.3f);
            for (auto& e : FOXE) if (!e.dead && len3(e.p - b.p) < 45) foxDamage(e, e.kind >= FE_POD ? 6 : 3, G.pl[b.owner]);
            for (auto& q : G.balls) if (!q.dead && len3(q.p - b.p) < 45) q.dead = true;
        }
    }
    FOXB.erase(std::remove_if(FOXB.begin(), FOXB.end(), [](const FoxBomb& b) { return b.life < 0; }), FOXB.end());
    // the squad hits things
    for (auto& p : G.pl) {
        if (!p.joined || p.down) continue; V3 me = ppos(p);
        for (auto& o : FOXO) {
            if (std::fabs(o.p.z - me.z) > (o.kind == FO_BLOCK ? o.d * 0.5f + 3 : 6)) continue;
            if (o.kind == FO_RING) { if (!o.hit && len3(V3(me.x - o.p.x, me.y - o.p.y, 0)) < o.w + 2) { o.hit = true; p.shields = std::min(MAXSH, p.shields + 1); p.score += 500; pmsg(p, "RING - SHIELD +1"); AU.play(SND_UI_OK, 0.8f); } continue; }
            bool bump = false;
            if (o.kind == FO_BLOCK || o.kind == FO_PILLAR) bump = std::fabs(me.x - o.p.x) < o.w * 0.5f + 3 && me.y < FOX_GROUND + o.h + 2;
            if (o.kind == FO_ARCH) { float lx = std::fabs(me.x - o.p.x); bump = (std::fabs(lx - o.w * 0.5f) < 7 && me.y < FOX_GROUND + o.h) || (lx < o.w * 0.5f + 7 && std::fabs(me.y - (FOX_GROUND + o.h + 4)) < 6); if (!bump && !o.hit && lx < o.w * 0.5f && me.y < FOX_GROUND + o.h) { o.hit = true; p.score += 100; } }
            if (bump && p.inv <= 0) { hitPlayer(p, o.kind == FO_ARCH ? "arch" : "building"); p.y += 10; G.flashes.push_back({ me, 0.3f, 0.3f, 8, C_WHITE, false }); }
        }
        if (G.foxBoss && G.foxClearT < 0 && len3(me - foxBossC()) < 50 && p.inv <= 0) { hitPlayer(p, "carrier"); p.y += 20; }
    }
}

// the camera: behind the ship, trailing it (so the ship slides round the screen)
static void foxCam(const Player& p, Cam& c, SDL_Rect vp, float vfov) {
    float back = S_PLAYER * 6.5f * chaseMul();
    V3 cp(p.x * 0.82f, p.y * 0.82f + back * 0.15f + 3, G.z + p.zoff - back);
    V3 look = cp + V3(0, -back * 0.06f, 120);   // level look: the ship sits just below the middle and slides round with the stick
    c.setBasis(cp, look - cp, V3(0, 1, 0), -p.bank * 0.25f, vp, vfov);
    c.fogNear = 900; c.fogFar = 1600;
}

static void drawFoxWorld(Canvas& cv, const Cam& cam) {
    float zc = G.z; Col gc = withA(Col(70, 200, 120), 0.6f), gd = withA(Col(70, 200, 120), 0.25f);
    for (float z = std::floor((zc - 60) / 60) * 60; z < zc + 1500; z += 60) line3(cv, cam, V3(-420, FOX_GROUND, z), V3(420, FOX_GROUND, z), 1.0f, ((int)(z / 60) % 4) ? gd : gc, 0.2f);
    for (int k = -7; k <= 7; k++) line3(cv, cam, V3(k * 60.f, FOX_GROUND, zc - 60), V3(k * 60.f, FOX_GROUND, zc + 1500), 1.0f, k % 2 ? gd : gc, 0.2f);
    for (int k = 0; k < 9; k++) { float x = -1400 + k * 350.f, h = 120 + 90 * std::sin(k * 1.7f); line3(cv, cam, V3(x, FOX_GROUND, zc + 2200), V3(x + 175, FOX_GROUND + h, zc + 2200), 1, withA(gc, 0.5f), 0); line3(cv, cam, V3(x + 175, FOX_GROUND + h, zc + 2200), V3(x + 350, FOX_GROUND, zc + 2200), 1, withA(gc, 0.5f), 0); }   // far hills
    for (auto& o : FOXO) {
        float rz = o.p.z - zc; if (rz < -80 || rz > 1500) continue;
        if (o.kind == FO_BLOCK) box3(cv, cam, V3(o.p.x - o.w / 2, FOX_GROUND, o.p.z - o.d / 2), V3(o.p.x + o.w / 2, FOX_GROUND + o.h, o.p.z + o.d / 2), Col(110, 170, 255), 1.1f);
        else if (o.kind == FO_PILLAR) box3(cv, cam, V3(o.p.x - o.w / 2, FOX_GROUND, o.p.z - o.w / 2), V3(o.p.x + o.w / 2, FOX_GROUND + o.h, o.p.z + o.w / 2), Col(200, 160, 255), 1.1f);
        else if (o.kind == FO_ARCH) {
            float x0 = o.p.x - o.w / 2, x1 = o.p.x + o.w / 2, t = 6; Col ac(255, 200, 90);
            box3(cv, cam, V3(x0 - t, FOX_GROUND, o.p.z - t), V3(x0 + t, FOX_GROUND + o.h, o.p.z + t), ac, 1.2f);
            box3(cv, cam, V3(x1 - t, FOX_GROUND, o.p.z - t), V3(x1 + t, FOX_GROUND + o.h, o.p.z + t), ac, 1.2f);
            box3(cv, cam, V3(x0 - t, FOX_GROUND + o.h, o.p.z - t), V3(x1 + t, FOX_GROUND + o.h + 2 * t, o.p.z + t), ac, 1.2f);
        } else if (o.kind == FO_RING && !o.hit) {
            V3 pr; float sp = G.time * 2;
            for (int i = 0; i <= 20; i++) { float a = i * 2 * PI / 20; V3 q = o.p + V3(std::cos(a) * o.w * std::cos(sp), std::sin(a) * o.w, std::cos(a) * o.w * std::sin(sp) * 0.3f); if (i) line3(cv, cam, pr, q, 2.0f, C_PORT, 1.2f); pr = q; }
        }
    }
    for (auto& e : FOXE) {
        if (e.dead) continue; Col c = e.flash > 0 ? C_WHITE : C_ENEMY;
        if (e.kind == FE_DRONE) drawModel(cv, cam, M_DRONE, e.p, V3(e.ax * 0.5f, 0, -1), e.t * 2, 5.0f, c, 1.5f);
        else if (e.kind == FE_TANK) drawModel(cv, cam, M_TANK, e.p, V3(0, 0, -1), 0, 4.0f, e.flash > 0 ? C_WHITE : C_TOWER, 1.3f);
        else if (e.kind == FE_POD) { box3(cv, cam, e.p - V3(16, 12, 26), e.p + V3(16, 12, 26), c, 1.5f, true); line3(cv, cam, e.p, foxBossC() + V3(0, -6, 0), 2.0f, Col(180, 190, 220), 0.4f); }
    }
    if (G.foxBoss && G.foxClearT < 0) {   // the carrier: a long hexagonal hull with a caged core at the front
        V3 c = foxBossC(); Col hc(180, 190, 220); V3 pr, f0[6], f1[6];
        for (int i = 0; i < 6; i++) { float a = i * PI / 3 + PI / 6; f0[i] = c + V3(std::cos(a) * 44, std::sin(a) * 28, -50); f1[i] = c + V3(std::cos(a) * 30, std::sin(a) * 20, 110); }
        for (int i = 0; i < 6; i++) { line3(cv, cam, f0[i], f0[(i + 1) % 6], 1.5f, hc, 0.5f); line3(cv, cam, f1[i], f1[(i + 1) % 6], 1.5f, hc, 0.5f); line3(cv, cam, f0[i], f1[i], 1.3f, hc, 0.5f); }
        for (auto& e : FOXE) if (e.kind == FE_CORE && !e.dead) {
            bool sh = foxPodsUp(); Col cc = e.flash > 0 ? C_WHITE : sh ? withA(C_CIT, 0.7f) : C_BAD; float r = 16 + (sh ? 0 : 3 * std::sin(G.time * 9));
            V3 q[6] = { e.p + V3(0, r, 0), e.p + V3(-r, 0, 0), e.p + V3(0, 0, -r), e.p + V3(r, 0, 0), e.p + V3(0, 0, r), e.p + V3(0, -r, 0) };
            for (int i = 1; i <= 4; i++) { int j = i % 4 + 1; line3(cv, cam, q[0], q[i], 1.6f, cc); line3(cv, cam, q[5], q[i], 1.6f, cc); line3(cv, cam, q[i], q[j], 1.6f, cc); }
            if (sh) for (int i = 0; i < 6; i++) line3(cv, cam, e.p + V3(std::cos(i * PI / 3) * 26, std::sin(i * PI / 3) * 26, -4), f0[i], 1.0f, withA(C_CIT, 0.5f), 0.3f);   // the cage
            else blob3(cv, cam, e.p, 10, withA(C_BAD, 0.6f));
        }
    }
    for (auto& b : FOXB) { blob3(cv, cam, b.p, 4, withA(C_PORT, 0.9f)); line3(cv, cam, b.p, b.p - norm3(b.v) * 14, 2, C_PORT, 1); }
}
// HUD: the two-box reticle where your guns point, charge ring, hits, boss bar
static void drawFoxHud(Canvas& cv, Player& p, float u) {
    const Cam& cam = p.cam; V3 me = ppos(p), dir = foxAimDir(p);
    for (int k = 0; k < 2; k++) {
        V2 s; float z; if (!cam.proj(me + dir * (k ? 140.f : 60.f), s, z)) continue;
        float r = (k ? 14 : 22) * u; Col c = withA(p.col, k ? 0.7f : 0.95f);
        cv.line(s + V2(-r, -r), s + V2(r, -r), 2 * u, c, 0.4f); cv.line(s + V2(r, -r), s + V2(r, r), 2 * u, c, 0.4f);
        cv.line(s + V2(r, r), s + V2(-r, r), 2 * u, c, 0.4f); cv.line(s + V2(-r, r), s + V2(-r, -r), 2 * u, c, 0.4f);
        if (k == 0 && p.charge > 0.25f) cv.circle(s, r * 1.5f, 2.5f * u, withA(p.charge >= 0.8f ? C_PORT : p.col, 0.4f + 0.5f * std::min(1.f, p.charge)), 0.8f);
    }
}
static void foxBot(Player& p, V2& desired, bool& fire) {
    V3 me = ppos(p); float bd = 1e9; V3 tg; bool has = false;
    for (auto& e : FOXE) { if (e.dead || (e.kind == FE_CORE && foxPodsUp())) continue; float d = e.p.z - me.z; if (d > 40 && d < 650 && d < bd) { bd = d; tg = e.p; has = true; } }
    float wantX = has ? tg.x : 0, wantY = has ? std::max(FOX_GROUND + 25, tg.y) : 10;
    for (auto& o : FOXO) { float d = o.p.z - me.z; if (d < 0 || d > 160) continue;
        if (o.kind == FO_RING && !o.hit) { wantX = o.p.x; wantY = o.p.y; }
        if ((o.kind == FO_BLOCK || o.kind == FO_PILLAR) && std::fabs(o.p.x - me.x) < o.w * 0.5f + 12) wantX = o.p.x + (me.x < o.p.x ? -1 : 1) * (o.w * 0.5f + 20);
        if (o.kind == FO_ARCH) { wantX = o.p.x; wantY = std::min(wantY, FOX_GROUND + o.h * 0.5f); } }
    desired = V2(clampf((wantX - me.x) * 0.05f, -1, 1), clampf((wantY - me.y) * 0.06f, -1, 1));
    fire = has && std::fabs(tg.x - me.x) < 10 + bd * 0.05f && std::fabs(tg.y - me.y) < 10 + bd * 0.05f;
    p.botRoll = false; for (auto& b : G.balls) if (!b.dead && len3(b.p - me) < 30 && b.p.z > me.z) p.botRoll = R.f() < 0.1f;
}
