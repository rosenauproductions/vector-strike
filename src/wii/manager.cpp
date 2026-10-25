// From Wii Lab v0.1. Changes for Vector Strike: no adapter -> discovery idles instead of spinning;
// diag() keeps only the kernel/root/adapter lines (no shell tools),
// the "leds" action takes the LED mask as its argument, the simulator has no speaker, warning text reworded.
#include "manager.h"
#include "log.h"
#include "sim.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int ledMask(int p) { static const int L[6] = {1, 2, 4, 8, 9, 10}; return p >= 0 && p < 6 ? L[p] : 0; }

void Manager::start(int sims, const std::string& dd, const std::string& cd) {
    simCount = sims; dataDir = dd; cfgDir = cd;
    { FILE* f = fopen((cfgDir + "/known_remotes.txt").c_str(), "r");
      if (f) { char b[64]; while (fgets(b, sizeof b, f)) { std::string s(b); while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back(); uint8_t a[6]; if (bt::macParse(s, a)) knownMacs.push_back(s); } fclose(f); } }
    run = true;
    if (!simCount) { diag(); bt::monitor.start(dev); }
    disc = std::thread([this] { discoveryLoop(); });
    svc = std::thread([this] { serviceLoop(); });
}

void Manager::stop() {
    run = false;
    if (disc.joinable()) disc.join();
    if (svc.joinable()) svc.join();
    std::vector<std::shared_ptr<Wiimote>> v; { std::lock_guard<std::mutex> g(m); v = live; live.clear(); }
    for (auto& w : v) { if (w->alive()) { w->rumble(false); w->setLeds(0); } w->stop(); }
    bt::monitor.stop();
    if (cap) { fclose(cap); cap = nullptr; }
}

void Manager::saveKnown() {
    FILE* f = fopen((cfgDir + "/known_remotes.txt").c_str(), "w"); if (!f) return;
    for (auto& k : knownMacs) fprintf(f, "%s\n", k.c_str());
    fclose(f);
}

std::vector<std::shared_ptr<Wiimote>> Manager::remotes() { std::lock_guard<std::mutex> g(m); return live; }
std::string Manager::status() { std::lock_guard<std::mutex> g(m); return st; }

void Manager::setSettings(const MgrSettings& s) { std::lock_guard<std::mutex> g(m); set = s; }

void Manager::act(const std::string& what, const std::string& mac, int arg) {
    std::lock_guard<std::mutex> g(m);
    actions.push_back([this, what, mac, arg] {
        std::vector<std::shared_ptr<Wiimote>> v = remotes();
        for (auto& w : v) {
            if (!mac.empty() && w->mac != mac) continue;
            if (what == "rumble") rumblePulse(w, arg ? arg : 300);
            else if (what == "leds") { w->setLeds(arg); }
            else if (what == "status") w->requestStatus();
            else if (what == "reset") w->resetStats();
            else if (what == "disconnect") {
                { std::lock_guard<std::mutex> g2(m); live.erase(std::remove(live.begin(), live.end(), w), live.end()); lostAt.erase(w->mac); links.erase(w->mac); }
                w->rumble(false); w->setLeds(0); w->stop(); logf("wiimote %s: disconnected by request", w->mac.c_str());
            }
        }
        if (what == "forget") { std::lock_guard<std::mutex> g2(m); knownMacs.clear(); saveKnown(); logf("manager: known remotes forgotten"); }
        if (what == "diag") diag();
    });
}

void Manager::setCapture(bool on) {
    std::lock_guard<std::mutex> g(m);
    if (on && !cap) {
        char name[64]; time_t t = time(nullptr); strftime(name, sizeof name, "capture-%Y%m%d-%H%M%S.csv", localtime(&t));
        capPath = dataDir + "/" + name; cap = fopen(capPath.c_str(), "w");
        if (cap) { fprintf(cap, "t_us,mac,report_hex\n"); logf("capture: recording every report to %s", capPath.c_str()); }
        else logf("capture: cannot write %s: %s", capPath.c_str(), strerror(errno));
    } else if (!on && cap) { for (auto& w : live) w->setCapture(nullptr); fclose(cap); cap = nullptr; logf("capture: stopped"); }
    for (auto& w : live) w->setCapture(cap);
}

void Manager::rumblePulse(std::shared_ptr<Wiimote> w, int ms) {
    w->rumble(true);
    std::lock_guard<std::mutex> g(m); rumbleUntil[w->mac] = nowS() + ms / 1000.0;
}

void Manager::diag() {
    std::string k = bt::shell("uname -r"); while (!k.empty() && k.back() == '\n') k.pop_back();
    logf("diag: kernel %s, running as uid %d%s", k.c_str(), (int)geteuid(), geteuid() ? " (NOT ROOT: direct Bluetooth will fail)" : "");
    AdapterInfo a; bt::adapter(dev, a);
    if (a.present) logf("diag: hci%d %s %s, ACL MTU %d x %d buffers, link policy 0x%X%s", dev, a.addr.c_str(), a.up ? "UP" : "DOWN", a.aclMtu, a.aclPkts, a.linkPolicy, (a.linkPolicy & 4) ? " (sniff allowed by default)" : "");
    else logf("diag: no hci%d adapter", dev);
    std::lock_guard<std::mutex> g(m); ad = a;
}

bool Manager::connect(const std::string& mac, const std::string& how, double foundAfter) {
    { std::lock_guard<std::mutex> g(m); for (auto& w : live) if (w->mac == mac && w->alive()) return true; }
    setStatus("CONNECTING " + mac + "...");
    double t0 = nowS(), ctlMs = 0, intrMs = 0;
    int tmo = how == "page" ? 1400 : 4000;
    int ctl = bt::l2connect(mac, 0x11, tmo, &ctlMs);
    if (ctl < 0) { if (how != "page") logf("connect %s: control channel failed after %.0f ms: %s", mac.c_str(), (nowS() - t0) * 1000, strerror(errno)); return false; }
    int intr = bt::l2connect(mac, 0x13, 4000, &intrMs);
    if (intr < 0) { logf("connect %s: data channel failed: %s", mac.c_str(), strerror(errno)); close(ctl); return false; }
    auto w = std::make_shared<Wiimote>(mac, std::unique_ptr<Transport>(new L2capTransport(ctl, intr)));
    w->timing.found = foundAfter; w->timing.ctlMs = ctlMs; w->timing.intrMs = intrMs; w->timing.method = how;
    adopt(w, false);
    return true;
}

void Manager::adopt(std::shared_ptr<Wiimote> w, bool sim) {
    double t0 = nowS();
    MgrSettings s = settings();
    w->sim = sim;
    w->start();
    int p;
    { std::lock_guard<std::mutex> g(m);
      std::vector<bool> used(6, false); for (auto& o : live) if (o->alive() && o->player >= 0 && o->player < 6) used[o->player] = true;
      p = playerOf.count(w->mac) && !used[playerOf[w->mac]] ? playerOf[w->mac] : -1;
      for (int i = 0; p < 0 && i < 6; i++) if (!used[i]) p = i;
      playerOf[w->mac] = p; w->player = p; }
    w->setLeds(ledMask(p));
    w->readAccelCal();
    w->applyMode((uint8_t)s.mode, s.cont, s.irLevel, s.fastInit);
    w->timing.initMs = (nowS() - t0) * 1000;
    w->timing.totalMs = w->timing.found * 1000 + w->timing.ctlMs + w->timing.intrMs + w->timing.initMs;
    if (!sim) tune(w, s, false);
    rumblePulse(w, 200);
    w->requestStatus();   // battery + flags (the reader re-sends the mode afterwards)
    logf("connect %s: P%d via %s | found %.2f s, control %.0f ms, data %.0f ms, init %.0f ms | %s", w->mac.c_str(), p + 1, w->timing.method.c_str(),
         w->timing.found, w->timing.ctlMs, w->timing.intrMs, w->timing.initMs, w->tx()->describe().c_str());
    std::lock_guard<std::mutex> g(m);
    if (cap) w->setCapture(cap);
    live.erase(std::remove_if(live.begin(), live.end(), [&](const std::shared_ptr<Wiimote>& o) { return o->mac == w->mac; }), live.end());
    live.push_back(w);
    lostAt.erase(w->mac);
    if (!sim && std::find(knownMacs.begin(), knownMacs.end(), w->mac) == knownMacs.end()) { knownMacs.push_back(w->mac); saveKnown(); }
}

void Manager::tune(std::shared_ptr<Wiimote> w, const MgrSettings& s, bool modeToo) {
    if (modeToo) w->applyMode((uint8_t)s.mode, s.cont, s.irLevel, s.fastInit);
    if (w->sim) return;
    bt::setSniff(dev, w->mac, !s.blockSniff);
    if (s.wantMaster) { bool master = false; if (bt::connHandle(dev, w->mac, &master) >= 0 && !master) bt::requestMaster(dev, w->mac); }
    if (s.sndbuf > 0) { w->tx()->setSendBuf(s.sndbuf); logf("wiimote %s: send buffer now %d bytes", w->mac.c_str(), w->tx()->sendBuf()); }
    if (s.flushMs > 0) bt::setFlushTimeout(dev, w->mac, s.flushMs);
}

void Manager::discoveryLoop() {
    bool triedUp = false; int method = 0, cycle = 0;
    const char* names[4] = {"auto", "streaming HCI inquiry", "kernel inquiry", "bluetoothctl scan"};
    while (run) {
        MgrSettings s = settings();
        // simulated remotes: "found" a second apart
        if (simCount) {
            if (simMade < simCount && nowS() > scanStartedAt + 1.0 + simMade * 0.8) {
                char mac[18]; snprintf(mac, sizeof mac, "00:5A:51:00:00:%02X", simMade + 1);
                auto t = simConnect(mac);
                auto w = std::make_shared<Wiimote>(mac, std::move(t)); w->timing.method = "simulator"; w->timing.found = nowS() - scanStartedAt;
                adopt(w, true); simMade++;
            }
            setStatus(simMade < simCount ? "SIMULATOR: CONNECTING..." : "SIMULATOR: " + std::to_string(simMade) + " REMOTES");
            usleep(50000); continue;
        }
        // Vector Strike: no adapter (e.g. a PC without Bluetooth) -> don't spin on failing inquiries; look again every 10 s
        if (!adapter().present) {
            static double nextDiag = 0; setStatus("NO BLUETOOTH ADAPTER");
            if (nowS() > nextDiag) { nextDiag = nowS() + 10; AdapterInfo a; bt::adapter(dev, a); std::lock_guard<std::mutex> g(m); ad = a; }
            for (int i = 0; i < 5 && run; i++) usleep(100000);
            continue;
        }
        // remotes that dropped: call them straight back for two minutes
        std::vector<std::string> retry;
        { std::lock_guard<std::mutex> g(m); double t = nowS();
          for (auto& kv : lostAt) if (t - kv.second < 120 && t - lastTry[kv.first] > 3) { retry.push_back(kv.first); lastTry[kv.first] = t; } }
        for (auto& mac : retry) if (run) connect(mac, "reconnect", 0);

        size_t n = remotes().size();
        if (!s.scan || (s.autoPause && n > 0)) {
            setStatus(n ? std::to_string(n) + " CONNECTED - SEARCH PAUSED" : "SEARCH OFF");
            for (int i = 0; i < 3 && run; i++) usleep(100000);
            continue;
        }
        if (scanStartedAt == 0) scanStartedAt = nowS();
        setStatus("TO CONNECT: PRESS 1+2 (OR RED SYNC) ON A WII REMOTE");
        // known remotes first: a remote in sync mode answers a direct page in well under a second, no inquiry needed
        if (s.pageKnown && (cycle++ % 2 == 0)) {
            std::vector<std::string> k = known();
            for (auto& mac : k) {
                bool have = false; for (auto& w : remotes()) if (w->mac == mac) have = true;
                if (have || !run) continue;
                double t0 = nowS();
                if (connect(mac, "page", 0)) { logf("connect %s: direct page answered in %.0f ms (no inquiry)", mac.c_str(), (nowS() - t0) * 1000); }
            }
        }
        if (!run) break;
        int want = s.method ? s.method : (method ? method : 1);
        std::string why; double t0 = nowS(); inq = true;
        std::string foundMac; double foundAt = 0;
        if (want == 1) {
            std::atomic<bool> cancel{false};
            int r = bt::inquiry(dev, s.inqLen, [&](const std::string& mac, int rssi) {
                for (auto& w : remotes()) if (w->mac == mac) return false;
                foundMac = mac; foundAt = nowS() - t0;
                logf("inquiry: found %s after %.2f s (RSSI %d) -> connecting now", mac.c_str(), foundAt, rssi);
                return true;   // stop the inquiry and connect right away
            }, cancel, why);
            inq = false;
            if (r < 0) {
                if (why.find("Network is down") != std::string::npos && !triedUp) { logf("inquiry: hci%d is off -> switching it on", dev); triedUp = true; bt::powerUp(dev); diag(); continue; }
                logf("inquiry (%s) failed: %s%s", names[1], why.c_str(), s.method ? "" : " -> trying kernel inquiry");
                if (!s.method) method = 2; else usleep(500000);
                continue;
            }
            { std::lock_guard<std::mutex> g(m); methodUsed = names[1]; }
            if (!foundMac.empty()) connect(foundMac, "inquiry", foundAt);
        } else {
            std::vector<std::string> found;
            if (want == 2) {
                if (bt::kernelInquiry(dev, s.inqLen, found, why) < 0) {
                    inq = false;
                    logf("inquiry (%s) failed: %s%s", names[2], why.c_str(), s.method ? "" : " -> trying bluetoothctl");
                    if (!s.method) method = 3; else usleep(500000);
                    continue;
                }
            } else bt::bluezScan(found);
            inq = false;
            { std::lock_guard<std::mutex> g(m); methodUsed = names[want]; }
            for (auto& mac : found) connect(mac, want == 2 ? "kernel inquiry" : "bluetoothctl", nowS() - t0);
        }
    }
}

void Manager::serviceLoop() {
    double nextLink = 0, nextAd = 0, nextWarn = 0, nextStatus = nowS() + 60; std::map<std::string, double> nextPing;
    while (run) {
        // queued actions
        std::deque<std::function<void()>> q; { std::lock_guard<std::mutex> g(m); q.swap(actions); }
        for (auto& f : q) f();
        MgrSettings s = settings(), old; { std::lock_guard<std::mutex> g(m); old = applied; applied = s; }
        std::vector<std::shared_ptr<Wiimote>> v = remotes();
        // drop dead links, remember them for reconnect
        for (auto& w : v) if (!w->alive()) {
            std::lock_guard<std::mutex> g(m);
            live.erase(std::remove(live.begin(), live.end(), w), live.end());
            if (!w->sim) { lostAt[w->mac] = nowS(); lastTry[w->mac] = nowS() - 2.5; }
            links.erase(w->mac);
            logf("wiimote %s: P%d gone%s", w->mac.c_str(), w->player + 1, w->sim ? "" : " - will call it back for 2 minutes");
        }
        v = remotes();
        // push setting changes to live remotes
        bool modeCh = s.mode != old.mode || s.cont != old.cont || s.irLevel != old.irLevel;
        bool linkCh = s.blockSniff != old.blockSniff || s.wantMaster != old.wantMaster || s.sndbuf != old.sndbuf || s.flushMs != old.flushMs;
        if (modeCh || linkCh) for (auto& w : v) tune(w, s, modeCh);
        double t = nowS();
        // rumble timers
        { std::vector<std::shared_ptr<Wiimote>> off;
          { std::lock_guard<std::mutex> g(m); for (auto& w : v) { auto it = rumbleUntil.find(w->mac); if (it != rumbleUntil.end() && t >= it->second) { off.push_back(w); rumbleUntil.erase(it); } } }
          for (auto& w : off) w->rumble(false); }
        // latency probe
        if (s.pingHz > 0) for (auto& w : v) { double& np = nextPing[w->mac]; if (t >= np) { np = t + 1.0 / s.pingHz; double ms; w->ping(ms); } }
        // link quality
        if (t >= nextLink) {
            nextLink = t + 1.0;
            for (auto& w : v) {
                LinkInfo li;
                if (w->sim) { li.ok = true; li.handle = 0; li.master = true; li.rssiOk = true; li.rssi = -4; li.quality = 255; li.policy = 1; }
                else bt::linkInfo(dev, w->mac, li);
                std::lock_guard<std::mutex> g(m); links[w->mac] = li;
            }
        }
        if (t >= nextStatus) { nextStatus = t + 60; for (auto& w : v) w->requestStatus(); }
        if (!simCount && t >= nextAd) { nextAd = t + 2.0; AdapterInfo a; bt::adapter(dev, a); std::lock_guard<std::mutex> g(m); ad = a; }
        if (t >= nextWarn) {
            nextWarn = t + 3.0;
            std::vector<std::string> w;
            if (!simCount && geteuid() != 0) w.push_back("NOT RUNNING AS ROOT - DIRECT BLUETOOTH NEEDS ROOT");
            AdapterInfo a = adapter();
            if (!simCount && !a.present) w.push_back("NO BLUETOOTH ADAPTER (hci0) FOUND");
            else if (!simCount && !a.up) w.push_back("ADAPTER IS SWITCHED OFF - WILL TRY TO SWITCH IT ON");
            DIR* d = opendir("/dev/input");
            if (d) {
                int paired = 0;
                while (dirent* e = readdir(d)) {
                    if (strncmp(e->d_name, "event", 5)) continue;
                    int fd = open((std::string("/dev/input/") + e->d_name).c_str(), O_RDONLY | O_NONBLOCK); if (fd < 0) continue;
                    char name[128] = {0}; ioctl(fd, EVIOCGNAME(sizeof name - 1), name); close(fd);
                    if (!strcmp(name, "Nintendo Wii Remote")) paired++;
                }
                closedir(d);
                if (paired) w.push_back(std::to_string(paired) + " REMOTE(S) PAIRED THROUGH BATOCERA - FORGET THEM THERE FOR WII AIMING");
            }
            std::lock_guard<std::mutex> g(m); warn = w;
        }
        usleep(5000);
    }
}
