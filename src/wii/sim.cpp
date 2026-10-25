// From Wii Lab v0.1, speaker decoding removed; for the game the B trigger is held for half a second every 4 s (fire)
// and the sensor-bar dots sweep in a slow circle (aim).
#include "sim.h"
#include "log.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <poll.h>
#include <random>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

struct SimRemote {
    int fd; std::string mac; std::thread th; std::atomic<bool> run{true};
    uint8_t mode = 0x30; bool cont = false, rumble = false; int leds = 0;
    void send(const std::vector<uint8_t>& r) { std::vector<uint8_t> b(1, 0xA1); b.insert(b.end(), r.begin(), r.end()); if (::send(fd, b.data(), b.size(), MSG_NOSIGNAL | MSG_DONTWAIT) < 0) {} }
    void loop();
};

static std::mutex g_m; static std::vector<std::shared_ptr<SimRemote>> g_sims;

void SimRemote::loop() {
    std::mt19937 rng((unsigned)std::hash<std::string>()(mac)); std::uniform_real_distribution<float> J(-1.2f, 1.2f);
    double t0 = nowS(), nextRep = t0;
    uint8_t eeprom[64] = {0}; eeprom[0x16] = 0x80; eeprom[0x17] = 0x80; eeprom[0x18] = 0x80; eeprom[0x19] = 0x00; eeprom[0x1a] = 0x9a; eeprom[0x1b] = 0x9a; eeprom[0x1c] = 0x9a; eeprom[0x1d] = 0x00;
    while (run) {
        double now = nowS(); int wait = (int)std::max(0.0, (nextRep - now) * 1000);
        pollfd p{fd, POLLIN, 0}; int pr = poll(&p, 1, cont || mode != 0x30 ? std::min(wait, 20) : 50);
        if (pr > 0) {
            uint8_t b[64]; ssize_t n = recv(fd, b, sizeof b, MSG_DONTWAIT);
            if (n <= 0) break;
            if (b[0] != 0xA2 || n < 3) continue;
            uint8_t id = b[1]; const uint8_t* q = b + 2;
            if (id != 0x10) rumble = q[0] & 1;
            switch (id) {
            case 0x10: rumble = q[0] & 1; break;
            case 0x11: leds = q[0] >> 4; break;
            case 0x12: cont = q[0] & 4; mode = q[1]; break;
            case 0x15: send({0x20, 0x00, 0x00, (uint8_t)(leds << 4), 0x00, 0x00, 0xC8}); mode = 0x30; cont = false; break;   // status stops streaming
            case 0x16: {
                uint32_t a = q[1] << 16 | q[2] << 8 | q[3]; int len = q[4];
                if (!(q[0] & 4) && a + len <= sizeof eeprom) memcpy(eeprom + a, q + 5, len);
                send({0x22, 0x00, 0x00, 0x16, 0x00});
                break;
            }
            case 0x17: {
                uint32_t a = q[1] << 16 | q[2] << 8 | q[3]; int len = q[4] << 8 | q[5];
                for (int off = 0; off < len; off += 16) {
                    int sz = std::min(16, len - off); std::vector<uint8_t> r = {0x21, 0x00, 0x00, (uint8_t)((sz - 1) << 4), (uint8_t)((a + off) >> 8), (uint8_t)(a + off)};
                    for (int i = 0; i < 16; i++) r.push_back(i < sz && a + off + i < sizeof eeprom ? eeprom[a + off + i] : 0);
                    send(r);
                }
                break;
            }
            }
        }
        now = nowS();
        if (now >= nextRep && mode >= 0x30) {
            nextRep = now + 0.010 + J(rng) * 0.001;
            if (!cont && mode == 0x30) continue;
            double t = now - t0;
            uint8_t b0 = 0, b1 = 0;
            double ph = std::fmod(t, 4.0);
            if (ph > 1.0 && ph < 1.15) b1 |= 0x08;     // A tap
            if (ph > 2.0 && ph < 2.5) b1 |= 0x04;      // B held half a second
            int ax = 512 + (int)(40 * std::sin(t * 1.3)), ay = 512 + (int)(30 * std::cos(t * 0.9)), az = 616;
            std::vector<uint8_t> r = {mode, (uint8_t)(b0 | ((ax & 3) << 5)), (uint8_t)(b1 | ((ay & 2) << 4) | ((az & 2) << 5))};
            if (mode == 0x31 || mode == 0x33 || mode == 0x37) { r.push_back((uint8_t)(ax >> 2)); r.push_back((uint8_t)(ay >> 2)); r.push_back((uint8_t)(az >> 2)); }
            if (mode == 0x33) {
                double cx = 512 + 200 * std::cos(t * 0.7), cy = 384 + 120 * std::sin(t * 0.9);
                int d[2][2] = {{(int)(cx - 110), (int)cy}, {(int)(cx + 110), (int)cy}};
                for (int k = 0; k < 4; k++) {
                    if (k < 2) { int x = d[k][0], y = d[k][1]; r.push_back((uint8_t)x); r.push_back((uint8_t)y); r.push_back((uint8_t)(((y >> 8) & 3) << 6 | ((x >> 8) & 3) << 4 | 3)); }
                    else { r.push_back(0xff); r.push_back(0xff); r.push_back(0xff); }
                }
            } else if (mode == 0x37) { for (int i = 0; i < 10; i++) r.push_back(0xff); for (int i = 0; i < 6; i++) r.push_back(0); }
            send(r);
        }
    }
    close(fd);
}

std::unique_ptr<Transport> simConnect(const std::string& mac) {
    int sv[2]; if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) < 0) return nullptr;
    auto s = std::make_shared<SimRemote>(); s->fd = sv[1]; s->mac = mac;
    s->th = std::thread([s] { s->loop(); });
    { std::lock_guard<std::mutex> g(g_m); g_sims.push_back(s); }
    return std::unique_ptr<Transport>(new SimTransport(sv[0]));
}

void simShutdownAll() {
    std::lock_guard<std::mutex> g(g_m);
    for (auto& s : g_sims) { s->run = false; if (s->th.joinable()) s->th.join(); }
    g_sims.clear();
}
