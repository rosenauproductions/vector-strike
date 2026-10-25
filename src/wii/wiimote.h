// From Wii Lab v0.1 - unchanged.
// One Wii remote, spoken to directly (protocol per wiibrew). Built for low latency:
//  * a dedicated reader thread blocks on the data channel and decodes every report the moment it lands, stamped with
//    a microsecond clock (the game reads the newest snapshot instead of polling once per frame)
//  * button edges go into a timestamped queue, so taps shorter than a frame are never lost
//  * register writes wait for the remote's own acknowledgement instead of fixed sleeps (fast init)
//  * the rumble bit lives here and is OR'd into every output report, so speaker/LED/mode reports never cancel rumble
#pragma once
#include "transport.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum WiiBtn : uint32_t { WB_UP = 1, WB_DOWN = 2, WB_LEFT = 4, WB_RIGHT = 8, WB_A = 16, WB_B = 32, WB_1 = 64, WB_2 = 128, WB_PLUS = 256, WB_MINUS = 512, WB_HOME = 1024 };

struct WiiState {
    uint32_t buttons = 0;
    int accRaw[3] = {512, 512, 512}; float acc[3] = {0, 0, 1}; bool hasAcc = false;   // acc in g, from the remote's own calibration
    int ir[4][3];                    // x 0-1023, y 0-767, size (1023 = no dot)
    int irCount = 0; bool hasIr = false;
    uint64_t tUs = 0;                // arrival time of the newest data report
    uint32_t seq = 0;
    uint8_t lastId = 0;
    int battery = -1; bool ext = false, spkFlag = false, irFlag = false; int ledFlags = 0;
    WiiState() { for (auto& d : ir) d[0] = d[1] = d[2] = 1023; }
};

struct BtnEvent { uint32_t bit; bool down; uint64_t tUs; };

struct WiiStats {
    uint64_t reports = 0, dataReports = 0, status = 0, modeResends = 0, sendOk = 0, sendFail = 0, missedTaps = 0;
    uint64_t perId[64] = {0};
    std::vector<float> intervals;    // ms, oldest first (last ~600)
    float rateHz = 0, avg = 0, mn = 0, mx = 0, p50 = 0, p99 = 0, sd = 0;
    std::vector<float> rtt;          // ping round trips, ms (last ~200)
    float rttAvg = 0, rttP95 = 0; uint64_t pings = 0, pingLost = 0;
    int outq = -1, outqMax = 0;
};

struct ConnectTiming { double found = 0, ctlMs = 0, intrMs = 0, initMs = 0, totalMs = 0; std::string method; };

class Wiimote {
public:
    std::string mac;
    bool sim = false;
    ConnectTiming timing;
    double connectedAt = 0;
    int player = -1;
    std::atomic<uint8_t> mode{0x33};
    std::atomic<bool> continuous{true};
    std::atomic<int> irLevel{3};

    Wiimote(const std::string& mac, std::unique_ptr<Transport> t);
    ~Wiimote();
    void start();
    void stop();
    bool alive() const { return live; }
    Transport* tx() { return t.get(); }

    // output
    bool out(std::vector<uint8_t> rep, bool dontWait = false);
    void rumble(bool on);
    bool rumbling() const { return rumbleOn; }
    void setLeds(int mask);
    int leds() const { return ledMask; }
    bool writeReg(uint32_t addr, const uint8_t* d, int n, int ackMs = 300);
    bool readMem(uint32_t addr, int n, std::vector<uint8_t>& out, int timeoutMs = 300, bool reg = false);
    bool ping(double& ms);
    void requestStatus();

    // configure: report mode, continuous flag, IR camera (switched on/off/re-moded as the report mode needs)
    bool applyMode(uint8_t mode, bool continuous, int irLevel, bool fastInit);
    bool readAccelCal();

    WiiState state();
    WiiStats stats();
    void resetStats();
    std::vector<BtnEvent> takeEvents();
    void setCapture(FILE* f);
    void noteFrameSample(uint32_t sampledButtons);   // called once per UI frame: counts taps a per-frame poll would miss

private:
    std::unique_ptr<Transport> t;
    std::thread th; std::atomic<bool> run{false}, live{true};
    std::atomic<bool> rumbleOn{false}; std::atomic<int> ledMask{0};
    std::mutex sendM, stM, rdM, wrM, capM;
    std::condition_variable cv;
    WiiState st; WiiStats sx;
    std::deque<float> iv, rtt;
    std::deque<BtnEvent> evq;
    uint64_t lastData = 0;
    uint32_t edgesSinceFrame = 0, lastFrameBtn = 0;
    int irModeCur = 0, irLevelCur = 0;
    int calZero[3] = {512, 512, 512}, calOne[3] = {616, 616, 616};
    // pending request state (guarded by stM, signalled with cv)
    bool ackWait = false, ackGot = false; int ackErr = 0;
    bool rdWait = false; uint32_t rdAddr = 0; int rdLen = 0; std::vector<uint8_t> rdBuf; int rdErr = 0; bool rdDone = false;
    FILE* cap = nullptr;
    void loop();
    void handle(const uint8_t* b, int n, uint64_t tUs);
};

namespace wiiproto {
    extern const uint8_t SENS1[5][9], SENS2[5][2];
    const char* modeName(int mode);
}
