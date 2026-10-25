// From Wii Lab v0.1 - unchanged.
#include "wiimote.h"
#include "log.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace wiiproto {
// IR camera sensitivity, the Wii menu's levels 1-5 (wiibrew)
const uint8_t SENS1[5][9] = {{0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0x64, 0x00, 0xfe}, {0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0x96, 0x00, 0xb4},
                             {0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0xaa, 0x00, 0x64}, {0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0xc8, 0x00, 0x36},
                             {0x07, 0x00, 0x00, 0x71, 0x01, 0x00, 0x72, 0x00, 0x20}};
const uint8_t SENS2[5][2] = {{0xfd, 0x05}, {0xb3, 0x04}, {0x63, 0x03}, {0x35, 0x03}, {0x1f, 0x03}};
const char* modeName(int m) {
    switch (m) {
    case 0x30: return "0x30 BUTTONS";
    case 0x31: return "0x31 BUTTONS+ACCEL";
    case 0x33: return "0x33 +ACCEL+IR(12)";
    case 0x36: return "0x36 BUTTONS+IR(10)+EXT";
    case 0x37: return "0x37 +ACCEL+IR(10)+EXT";
    }
    return "?";
}
}

static const size_t IV_KEEP = 600, RTT_KEEP = 200, EVQ_KEEP = 256;

Wiimote::Wiimote(const std::string& m, std::unique_ptr<Transport> tr) : mac(m), t(std::move(tr)) { connectedAt = nowS(); }
Wiimote::~Wiimote() { stop(); }

void Wiimote::start() { if (run) return; run = true; live = true; th = std::thread([this] { loop(); }); }
void Wiimote::stop() {
    run = false;
    if (t) t->close();
    if (th.joinable()) th.join();
    live = false; cv.notify_all();
}

bool Wiimote::out(std::vector<uint8_t> rep, bool dontWait) {
    if (rep.size() < 2 || !live) return false;
    if (rep[0] != 0x10) rep[1] = (uint8_t)((rep[1] & ~1) | (rumbleOn ? 1 : 0));   // bit 0 of every output report is the motor
    bool ok;
    { std::lock_guard<std::mutex> g(sendM); ok = t->send(rep.data(), rep.size(), dontWait); }
    std::lock_guard<std::mutex> g(stM);
    if (ok) sx.sendOk++; else sx.sendFail++;
    return ok;
}

void Wiimote::rumble(bool on) { rumbleOn = on; out({0x10, (uint8_t)(on ? 1 : 0)}); }
void Wiimote::setLeds(int mask) { ledMask = mask & 15; out({0x11, (uint8_t)((mask & 15) << 4)}); }
void Wiimote::requestStatus() { out({0x15, 0x00}); }

bool Wiimote::writeReg(uint32_t addr, const uint8_t* d, int n, int ackMs) {
    std::lock_guard<std::mutex> w(wrM);
    std::vector<uint8_t> r(22, 0);
    r[0] = 0x16; r[1] = 0x04; r[2] = (uint8_t)(addr >> 16); r[3] = (uint8_t)(addr >> 8); r[4] = (uint8_t)addr; r[5] = (uint8_t)n;
    memcpy(&r[6], d, std::min(n, 16));
    { std::lock_guard<std::mutex> g(stM); ackWait = ackMs > 0; ackGot = false; ackErr = 0; }
    if (!out(r)) { std::lock_guard<std::mutex> g(stM); ackWait = false; return false; }
    if (ackMs <= 0) return true;
    std::unique_lock<std::mutex> g(stM);
    bool got = cv.wait_for(g, std::chrono::milliseconds(ackMs), [&] { return ackGot || !live; });
    ackWait = false;
    return got && ackGot && ackErr == 0;
}

bool Wiimote::readMem(uint32_t addr, int n, std::vector<uint8_t>& o, int timeoutMs, bool reg) {
    std::lock_guard<std::mutex> rd(rdM);
    { std::lock_guard<std::mutex> g(stM); rdWait = true; rdDone = false; rdAddr = addr; rdLen = n; rdBuf.clear(); rdErr = 0; }
    std::vector<uint8_t> r = {0x17, (uint8_t)(reg ? 0x04 : 0x00), (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)(n >> 8), (uint8_t)n};
    if (!out(r)) { std::lock_guard<std::mutex> g(stM); rdWait = false; return false; }
    std::unique_lock<std::mutex> g(stM);
    bool got = cv.wait_for(g, std::chrono::milliseconds(timeoutMs), [&] { return rdDone || !live; });
    rdWait = false;
    if (!got || !rdDone || rdErr) return false;
    o = rdBuf;
    return true;
}

bool Wiimote::ping(double& ms) {
    uint64_t t0 = nowUs(); std::vector<uint8_t> d;
    bool ok = readMem(0x0016, 1, d, 250);
    ms = (nowUs() - t0) / 1000.0;
    std::lock_guard<std::mutex> g(stM);
    sx.pings++;
    if (!ok) { sx.pingLost++; return false; }
    rtt.push_back((float)ms); while (rtt.size() > RTT_KEEP) rtt.pop_front();
    return true;
}

bool Wiimote::readAccelCal() {
    std::vector<uint8_t> d;
    if (!readMem(0x0016, 10, d, 300) || d.size() < 8) { logf("wiimote %s: accelerometer calibration not readable, using defaults", mac.c_str()); return false; }
    int z[3] = {d[0] << 2 | ((d[3] >> 4) & 3), d[1] << 2 | ((d[3] >> 2) & 3), d[2] << 2 | (d[3] & 3)};
    int o[3] = {d[4] << 2 | ((d[7] >> 4) & 3), d[5] << 2 | ((d[7] >> 2) & 3), d[6] << 2 | (d[7] & 3)};
    for (int k = 0; k < 3; k++) if (o[k] - z[k] < 40 || o[k] - z[k] > 200) { logf("wiimote %s: odd accelerometer calibration, using defaults", mac.c_str()); return false; }
    std::lock_guard<std::mutex> g(stM);
    memcpy(calZero, z, sizeof z); memcpy(calOne, o, sizeof o);
    logf("wiimote %s: accelerometer zero %d/%d/%d, 1g %d/%d/%d", mac.c_str(), z[0], z[1], z[2], o[0], o[1], o[2]);
    return true;
}

bool Wiimote::applyMode(uint8_t m, bool cont, int level, bool fast) {
    double t0 = nowS();
    int irMode = (m == 0x33) ? 3 : (m == 0x36 || m == 0x37) ? 1 : 0;
    level = std::max(1, std::min(5, level));
    bool ok = true;
    if (irMode && (irModeCur != irMode || irLevelCur != level)) {
        uint8_t eight = 0x08, im = (uint8_t)irMode;
        int ackMs = fast ? 120 : 250;
        out({0x13, 0x04}); if (!fast) std::this_thread::sleep_for(std::chrono::milliseconds(30));
        out({0x1a, 0x04}); if (!fast) std::this_thread::sleep_for(std::chrono::milliseconds(30));
        ok &= writeReg(0xb00030, &eight, 1, ackMs);
        ok &= writeReg(0xb00000, wiiproto::SENS1[level - 1], 9, ackMs);
        ok &= writeReg(0xb0001a, wiiproto::SENS2[level - 1], 2, ackMs);
        ok &= writeReg(0xb00033, &im, 1, ackMs);
        ok &= writeReg(0xb00030, &eight, 1, ackMs);
        irModeCur = irMode; irLevelCur = level;
    } else if (!irMode && irModeCur) {
        out({0x13, 0x00}); out({0x1a, 0x00}); irModeCur = 0;
    }
    mode = m; continuous = cont; irLevel = level;
    out({0x12, (uint8_t)(cont ? 0x04 : 0x00), m});
    logf("wiimote %s: mode %s%s, IR level %d, %.0f ms%s", mac.c_str(), wiiproto::modeName(m), cont ? " continuous" : " on-change", level, (nowS() - t0) * 1000, ok ? "" : " (some camera writes not acknowledged)");
    return ok;
}

WiiState Wiimote::state() { std::lock_guard<std::mutex> g(stM); return st; }

static float pct(std::vector<float> v, float p) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, (size_t)(p * (v.size() - 1) + 0.5f))]; }

WiiStats Wiimote::stats() {
    std::lock_guard<std::mutex> g(stM);
    WiiStats s = sx;
    s.intervals.assign(iv.begin(), iv.end());
    s.rtt.assign(rtt.begin(), rtt.end());
    if (!s.intervals.empty()) {
        double sum = 0, sq = 0; s.mn = 1e9f; s.mx = 0;
        size_t from = s.intervals.size() > 200 ? s.intervals.size() - 200 : 0;
        std::vector<float> w(s.intervals.begin() + from, s.intervals.end());
        for (float x : w) { sum += x; sq += x * x; s.mn = std::min(s.mn, x); s.mx = std::max(s.mx, x); }
        s.avg = (float)(sum / w.size()); s.sd = (float)std::sqrt(std::max(0.0, sq / w.size() - s.avg * (double)s.avg));
        s.p50 = pct(w, 0.5f); s.p99 = pct(w, 0.99f); s.rateHz = s.avg > 0 ? 1000 / s.avg : 0;
        if (lastData && nowUs() - lastData > 500000) s.rateHz = 0;
    }
    if (!s.rtt.empty()) { double sum = 0; for (float x : s.rtt) sum += x; s.rttAvg = (float)(sum / s.rtt.size()); s.rttP95 = pct(s.rtt, 0.95f); }
    int q = t ? t->outq() : -1; s.outq = q;
    if (q > sx.outqMax) sx.outqMax = q;
    s.outqMax = sx.outqMax;
    return s;
}

void Wiimote::resetStats() {
    std::lock_guard<std::mutex> g(stM);
    WiiStats z; sx = z; iv.clear(); rtt.clear();
}

std::vector<BtnEvent> Wiimote::takeEvents() { std::lock_guard<std::mutex> g(stM); std::vector<BtnEvent> v(evq.begin(), evq.end()); evq.clear(); return v; }
void Wiimote::setCapture(FILE* f) { std::lock_guard<std::mutex> g(capM); cap = f; }

void Wiimote::noteFrameSample(uint32_t) {
    std::lock_guard<std::mutex> g(stM);
    uint32_t missed = edgesSinceFrame & ~st.buttons & ~lastFrameBtn;
    sx.missedTaps += __builtin_popcount(missed);
    edgesSinceFrame = 0; lastFrameBtn = st.buttons;
}

void Wiimote::loop() {
    uint8_t b[96];
    while (run) {
        int n = t->recv(b, sizeof b, 200);
        if (n == 0) continue;
        if (n < 0) {
            if (run) logf("wiimote %s: link lost", mac.c_str());
            live = false; cv.notify_all();
            break;
        }
        uint64_t now = nowUs();
        { std::lock_guard<std::mutex> g(capM);
          if (cap) { fprintf(cap, "%llu,%s", (unsigned long long)now, mac.c_str()); for (int i = 0; i < n; i++) fprintf(cap, i ? " %02x" : ",%02x", b[i]); fputc('\n', cap); } }
        handle(b, n, now);
    }
}

static uint32_t decodeButtons(const uint8_t* p) {
    uint32_t w = 0;
    if (p[0] & 0x01) w |= WB_LEFT; if (p[0] & 0x02) w |= WB_RIGHT; if (p[0] & 0x04) w |= WB_DOWN; if (p[0] & 0x08) w |= WB_UP; if (p[0] & 0x10) w |= WB_PLUS;
    if (p[1] & 0x01) w |= WB_2; if (p[1] & 0x02) w |= WB_1; if (p[1] & 0x04) w |= WB_B; if (p[1] & 0x08) w |= WB_A; if (p[1] & 0x10) w |= WB_MINUS; if (p[1] & 0x80) w |= WB_HOME;
    return w;
}

void Wiimote::handle(const uint8_t* b, int n, uint64_t now) {
    uint8_t id = b[0]; const uint8_t* p = b + 1; int pl = n - 1;
    bool resendMode = false;
    {
        std::lock_guard<std::mutex> g(stM);
        sx.reports++; sx.perId[id & 63]++;
        bool hasBtn = (id >= 0x20 && id <= 0x22) || (id >= 0x30 && id <= 0x37) || id == 0x3e || id == 0x3f;
        if (hasBtn && pl >= 2) {
            uint32_t w = decodeButtons(p), ch = w ^ st.buttons;
            for (uint32_t bit = 1; ch && bit <= WB_HOME; bit <<= 1) if (ch & bit) {
                evq.push_back({bit, (w & bit) != 0, now}); if (evq.size() > EVQ_KEEP) evq.pop_front();
                if (w & bit) edgesSinceFrame |= bit;
            }
            st.buttons = w;
        }
        if (id >= 0x30 && id <= 0x3f) {
            sx.dataReports++;
            if (lastData) { iv.push_back((now - lastData) / 1000.0f); if (iv.size() > IV_KEEP) iv.pop_front(); }
            lastData = now; st.tUs = now; st.seq++; st.lastId = id;
        }
        if ((id == 0x31 || id == 0x33 || id == 0x35 || id == 0x37) && pl >= 5) {
            st.accRaw[0] = p[2] << 2 | ((p[0] >> 5) & 3); st.accRaw[1] = p[3] << 2 | ((p[1] >> 4) & 2); st.accRaw[2] = p[4] << 2 | ((p[1] >> 5) & 2);
            for (int k = 0; k < 3; k++) st.acc[k] = (st.accRaw[k] - calZero[k]) / (float)std::max(1, calOne[k] - calZero[k]);
            st.hasAcc = true;
        }
        if (id == 0x33 && pl >= 17) {
            int c = 0;
            for (int k = 0; k < 4; k++) {
                const uint8_t* e = p + 5 + k * 3;
                if (e[0] == 0xff && e[1] == 0xff && e[2] == 0xff) { st.ir[k][0] = st.ir[k][1] = st.ir[k][2] = 1023; continue; }
                st.ir[k][0] = e[0] | ((e[2] >> 4) & 3) << 8; st.ir[k][1] = e[1] | ((e[2] >> 6) & 3) << 8; st.ir[k][2] = e[2] & 15; c++;
            }
            st.irCount = c; st.hasIr = true;
        } else if ((id == 0x36 && pl >= 12) || (id == 0x37 && pl >= 15)) {
            const uint8_t* e = p + (id == 0x36 ? 2 : 5); int c = 0;
            for (int k = 0; k < 2; k++, e += 5) {
                int x1 = e[0] | ((e[2] >> 4) & 3) << 8, y1 = e[1] | ((e[2] >> 6) & 3) << 8;
                int x2 = e[3] | (e[2] & 3) << 8, y2 = e[4] | ((e[2] >> 2) & 3) << 8;
                st.ir[k * 2][0] = x1; st.ir[k * 2][1] = y1; st.ir[k * 2][2] = 2;
                st.ir[k * 2 + 1][0] = x2; st.ir[k * 2 + 1][1] = y2; st.ir[k * 2 + 1][2] = 2;
                c += (x1 < 1023 && y1 < 1023) + (x2 < 1023 && y2 < 1023);
            }
            st.irCount = c; st.hasIr = true;
        }
        if (id == 0x20 && pl >= 6) {
            st.battery = p[5]; st.ext = p[2] & 2; st.spkFlag = p[2] & 4; st.irFlag = p[2] & 8; st.ledFlags = p[2] >> 4;
            sx.status++; sx.modeResends++; resendMode = true;
        }
        if (id == 0x22 && pl >= 4 && ackWait && p[2] == 0x16) { ackGot = true; ackErr = p[3]; cv.notify_all(); }
        if (id == 0x21 && pl >= 5 && rdWait) {
            int se = p[2], err = se & 15, size = (se >> 4) + 1; uint16_t off = (uint16_t)(p[3] << 8 | p[4]);
            if (err) { rdErr = err; rdDone = true; cv.notify_all(); }
            else {
                size_t at = (uint16_t)(off - (uint16_t)rdAddr);
                if (rdBuf.size() < at + size) rdBuf.resize(at + size);
                for (int i = 0; i < size && 5 + i < pl; i++) rdBuf[at + i] = p[5 + i];
                if ((int)rdBuf.size() >= rdLen) { rdBuf.resize(rdLen); rdDone = true; cv.notify_all(); }
            }
        }
    }
    // after any status report the remote stops streaming until the reporting mode is set again
    if (resendMode) out({0x12, (uint8_t)(continuous ? 0x04 : 0x00), mode.load()}, true);
}
