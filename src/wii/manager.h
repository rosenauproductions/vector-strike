// From Wii Lab v0.1 - unchanged.
// Finds, connects and looks after Wii remotes (Dolphin-style: our own Bluetooth inquiry + L2CAP channels, no pairing).
// Fast connect: a streaming inquiry connects the instant a remote answers (instead of waiting out the whole inquiry),
// remotes seen before are paged directly (no inquiry at all), init is ack-driven. Every step is timed.
#pragma once
#include "bt.h"
#include "wiimote.h"
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct MgrSettings {
    bool scan = true;            // search for new remotes
    int method = 0;              // 0 auto, 1 streaming raw HCI, 2 kernel inquiry, 3 bluetoothctl
    int inqLen = 2;              // inquiry length in 1.28 s units
    bool pageKnown = true;       // page remotes seen before directly (skips inquiry when they're in sync mode)
    bool autoPause = true;       // stop searching while any remote is connected (searching adds input lag)
    bool fastInit = true;        // ack-driven init vs Dead Center's fixed sleeps
    int mode = 0x33; bool cont = true; int irLevel = 3;
    bool blockSniff = true;      // keep links ACTIVE (sniff mode adds 10-50 ms of lag)
    bool wantMaster = true;      // ask to be master of each link
    int sndbuf = 0;              // 0 = kernel default, else bytes
    int pingHz = 2;              // round-trip probe rate
    int flushMs = 0;             // automatic flush timeout on the link (0 = off/infinite)
};

class Manager {
public:
    void start(int simCount, const std::string& dataDir, const std::string& cfgDir);
    void stop();
    MgrSettings settings() { std::lock_guard<std::mutex> g(m); return set; }
    void setSettings(const MgrSettings& s);       // mode/IR/sniff/sndbuf/flush changes are pushed to live remotes
    std::vector<std::shared_ptr<Wiimote>> remotes();
    std::string status();
    AdapterInfo adapter() { std::lock_guard<std::mutex> g(m); return ad; }
    LinkInfo link(const std::string& mac) { std::lock_guard<std::mutex> g(m); return links.count(mac) ? links[mac] : LinkInfo(); }
    std::vector<std::string> warnings() { std::lock_guard<std::mutex> g(m); return warn; }
    std::vector<std::string> known() { std::lock_guard<std::mutex> g(m); return knownMacs; }
    std::string lastMethod() { std::lock_guard<std::mutex> g(m); return methodUsed; }
    bool inquiring() const { return inq; }
    // actions (run on the service thread so the UI never blocks on Bluetooth)
    void act(const std::string& what, const std::string& mac = "", int arg = 0);
    void setCapture(bool on);
    bool capturing() { std::lock_guard<std::mutex> g(m); return cap != nullptr; }
    std::string capturePath() { std::lock_guard<std::mutex> g(m); return capPath; }
    double scanStartedAt = 0;

private:
    std::mutex m;
    MgrSettings set, applied;
    std::vector<std::shared_ptr<Wiimote>> live;
    std::map<std::string, int> playerOf;            // remembered player slot per remote (for reconnects)
    std::map<std::string, double> lostAt, lastTry;
    std::vector<std::string> knownMacs, warn;
    std::map<std::string, LinkInfo> links;
    std::map<std::string, double> rumbleUntil;
    std::deque<std::function<void()>> actions;
    AdapterInfo ad; std::string st, methodUsed, dataDir, cfgDir, capPath;
    std::thread disc, svc; std::atomic<bool> run{false}, inq{false};
    int simCount = 0, simMade = 0; int dev = 0; FILE* cap = nullptr;
    bool connect(const std::string& mac, const std::string& how, double foundAfter);
    void adopt(std::shared_ptr<Wiimote> w, bool sim);
    void discoveryLoop();
    void serviceLoop();
    void diag();
    void saveKnown();
    void setStatus(const std::string& s) { std::lock_guard<std::mutex> g(m); st = s; }
    void tune(std::shared_ptr<Wiimote> w, const MgrSettings& s, bool modeToo);
    void rumblePulse(std::shared_ptr<Wiimote> w, int ms);
};
