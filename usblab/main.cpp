// USB Lab - a Batocera "port" that diagnoses USB trouble as a game.
// Watches /sys/bus/usb (devices coming and going), /dev/kmsg (kernel USB errors) and SDL controllers
// (every button and axis), keeps a "clean for" timer like a high score, and writes a report to reports/.
#include <SDL.h>
#include "../src/gfx.h"
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <ctime>
#include <map>
#include <set>
#include <deque>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/utsname.h>
#include <sys/stat.h>

static const Col C_GREEN(80, 255, 140), C_CYAN(80, 220, 255), C_RED(255, 70, 60), C_YEL(255, 215, 70),
                 C_DIM(80, 100, 130), C_WHITE(225, 235, 255), C_ORANGE(255, 150, 60);
static const float VW = 1920, VH = 1080;

// ---------- small helpers ----------
static std::string readFile(const std::string& p) {
    FILE* f = fopen(p.c_str(), "r"); if (!f) return "";
    char buf[512]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
    std::string s(buf); while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}
static bool writeFile(const std::string& p, const char* v) {
    FILE* f = fopen(p.c_str(), "w"); if (!f) return false;
    bool ok = fputs(v, f) >= 0; ok = (fclose(f) == 0) && ok; return ok;
}
static std::string linkBase(const std::string& p) {
    char buf[512]; ssize_t n = readlink(p.c_str(), buf, sizeof buf - 1); if (n <= 0) return "";
    buf[n] = 0; const char* s = strrchr(buf, '/'); return s ? s + 1 : buf;
}
static std::string upper(std::string s) { for (auto& c : s) { if (c >= 'a' && c <= 'z') c -= 32; else if ((unsigned char)c >= 128) c = '?'; } return s; }
static std::string lower(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32; return s; }
static std::string clip(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 1) + "~"; }
static std::string fmtDur(double s) {
    int t = (int)s; char b[32]; snprintf(b, sizeof b, "%02d:%02d:%02d", t / 3600, t / 60 % 60, t % 60); return b;
}
static std::string speedName(const std::string& sp) {
    if (sp == "1.5") return "USB1 LOW"; if (sp == "12") return "USB1.1"; if (sp == "480") return "USB2";
    if (sp == "5000") return "USB3 5G"; if (sp == "10000") return "USB3 10G"; if (sp == "20000") return "USB3 20G";
    return sp.empty() ? "?" : sp + "M";
}
static std::string SYSROOT;   // USBLAB_SYSROOT: test against a fake /sys tree
static double nowS() { return SDL_GetTicks() / 1000.0; }

// ---------- sounds: tiny synth through SDL_QueueAudio ----------
struct Beeper {
    SDL_AudioDeviceID dev = 0; const int RATE = 44100;
    void init() {
        if (getenv("USBLAB_MUTE")) return;
        SDL_AudioSpec want{}, have{}; want.freq = RATE; want.format = AUDIO_F32SYS; want.channels = 1; want.samples = 1024;
        dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0); if (dev) SDL_PauseAudioDevice(dev, 0);
    }
    // notes: list of (freq Hz, seconds); wave 0 = sine, 1 = square buzz
    void play(std::initializer_list<std::pair<float, float>> notes, int wave = 0, float vol = 0.25f) {
        if (!dev || SDL_GetQueuedAudioSize(dev) > (Uint32)RATE * 4) return;
        std::vector<float> v; double ph = 0;
        for (auto& n : notes) {
            int cnt = (int)(n.second * RATE);
            for (int i = 0; i < cnt; i++) {
                float t = i / (float)RATE, env = std::min(1.f, t * 200) * std::min(1.f, (n.second - t) * 60);
                ph += 2 * PI * n.first / RATE; float s = wave ? (std::sin(ph) > 0 ? 0.6f : -0.6f) : std::sin((float)ph);
                v.push_back(n.first > 0 ? s * env * vol : 0);
            }
        }
        SDL_QueueAudio(dev, v.data(), (Uint32)(v.size() * sizeof(float)));
    }
    void plugged() { play({{660, .07f}, {990, .1f}}); }
    void lost() { play({{880, .12f}, {440, .12f}, {880, .12f}, {440, .2f}}, 1, 0.18f); }
    void kerr() { play({{140, .18f}}, 1, 0.2f); }
    void blip() { play({{1320, .05f}}); }
    void good() { play({{523, .08f}, {659, .08f}, {784, .08f}, {1046, .16f}}); }
} BEEP;

// ---------- report ----------
static std::string HOME = ".", REPDIR, STAMP, REPORT;
static FILE* REP = nullptr;
struct Toast { std::string text; Col c; double t; };
static std::deque<Toast> EVENTS;        // on-screen event log
static Toast BIG{ "", C_RED, -100 };    // centre-screen flash
static double T0 = 0;

static void logEv(Col c, bool big, const char* fmt, ...) {
    char buf[600]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    double t = nowS() - T0;
    if (REP) { fprintf(REP, "[%s.%d] %s\n", fmtDur(t).c_str(), (int)(t * 10) % 10, buf); fflush(REP); }
    EVENTS.push_back({ upper(buf), c, nowS() }); while (EVENTS.size() > 7) EVENTS.pop_front();
    if (big) BIG = { upper(buf), c, nowS() };
}
static void runSnapshot(const char* tag, bool wait) {
    std::string cmd = "sh '" + HOME + "/snapshot.sh' > '" + REPDIR + "/usblab-" + STAMP + "-" + tag + ".txt' 2>&1";
    if (!wait) cmd = "(" + cmd + ") &";
    int rc = system(cmd.c_str()); (void)rc;
}

// ---------- USB devices and ports from sysfs ----------
struct UDev { std::string name, vid, pid, prod, manu, speed, power, runtime, drivers; bool present = true, hid = false; double seen = 0, gone = 0; };
struct Bus { int num = 0, ports = 0; std::string speed, pci, driver; };
struct PortStat { bool used = false, now = false; int errs = 0, devs = 0; };
static std::map<std::string, UDev> DEVS;
static std::map<int, Bus> BUSES;
static std::map<std::pair<int, int>, PortStat> PORTS;   // (bus, top-level port)
static int DISCONNECTS = 0, CONNECTS = 0;
static double CLEAN_SINCE = 0, BEST_CLEAN = 0;
static int FAULTS = 0, DG_FWERR = 0, DG_ERR = 0;   // dongle kernel error counts
static std::string AUTOSUSPEND;

static void fault() { FAULTS++; BEST_CLEAN = std::max(BEST_CLEAN, nowS() - CLEAN_SINCE); CLEAN_SINCE = nowS(); }

static bool parsePort(const std::string& name, int& bus, int& port) {
    // "3-2.1" -> bus 3 port 2;  "usb3-port2" -> bus 3 port 2
    const char* s = name.c_str();
    if (!strncmp(s, "usb", 3) && strstr(s, "-port")) { bus = atoi(s + 3); port = atoi(strstr(s, "-port") + 5); return bus > 0 && port > 0; }
    if (*s < '0' || *s > '9') return false;
    const char* d = strchr(s, '-'); if (!d) return false;
    bus = atoi(s); port = atoi(d + 1); return bus > 0 && port > 0;
}

static void scanUsb(bool first) {
    const std::string base = SYSROOT + "/sys/bus/usb/devices/";
    std::set<std::string> found;
    DIR* dir = opendir(base.c_str());
    for (auto& p : PORTS) p.second.now = false, p.second.devs = 0;
    if (dir) {
        while (dirent* e = readdir(dir)) {
            std::string n = e->d_name; if (n[0] == '.' || n.find(':') != std::string::npos) continue;
            std::string d = base + n + "/";
            if (!n.compare(0, 3, "usb")) {          // root hub = one bus of a controller
                Bus b; b.num = atoi(n.c_str() + 3); b.ports = atoi(readFile(d + "maxchild").c_str());
                b.speed = readFile(d + "speed");
                char lk[512]; ssize_t L = readlink((base + n).c_str(), lk, sizeof lk - 1);
                if (L > 0) { lk[L] = 0; std::string s(lk); size_t q = s.rfind('/'); if (q != std::string::npos) { s = s.substr(0, q); size_t r = s.rfind('/'); b.pci = s.substr(r + 1); } }
                b.driver = linkBase(d + "../driver");
                BUSES[b.num] = b; continue;
            }
            UDev u; u.name = n; u.vid = readFile(d + "idVendor"); u.pid = readFile(d + "idProduct");
            u.prod = readFile(d + "product"); u.manu = readFile(d + "manufacturer"); u.speed = readFile(d + "speed");
            u.power = readFile(d + "power/control"); u.runtime = readFile(d + "power/runtime_status");
            if (DIR* sd = opendir(d.c_str())) {           // interface drivers (usbhid, xpad, btusb...)
                std::set<std::string> drv;
                while (dirent* ie = readdir(sd)) if (strchr(ie->d_name, ':')) {
                    std::string dn = linkBase(d + ie->d_name + "/driver"); if (!dn.empty()) drv.insert(dn);
                    if (readFile(d + ie->d_name + "/bInterfaceClass") == "03") u.hid = true;
                }
                closedir(sd); for (auto& x : drv) u.drivers += (u.drivers.empty() ? "" : ",") + x;
            }
            found.insert(n);
            int bus, port; if (parsePort(n, bus, port)) { auto& ps = PORTS[{bus, port}]; ps.used = ps.now = true; ps.devs++; }
            auto it = DEVS.find(n);
            if (it != DEVS.end() && it->second.present && it->second.vid == u.vid && it->second.pid == u.pid) {
                u.seen = it->second.seen; it->second = u; continue;
            }
            if (it != DEVS.end() && it->second.present) {   // a different device sits there now
                logEv(C_RED, false, "USB %s swapped %s:%s -> %s:%s", n.c_str(), it->second.vid.c_str(), it->second.pid.c_str(), u.vid.c_str(), u.pid.c_str());
            }
            u.seen = first ? -100 : nowS(); DEVS[n] = u;
            if (!first) {
                CONNECTS++; BEEP.plugged();
                logEv(C_GREEN, false, "USB IN  %s  %s  %s:%s  %s", n.c_str(), speedName(u.speed).c_str(), u.vid.c_str(), u.pid.c_str(), (u.manu + " " + u.prod).c_str());
            } else if (REP) fprintf(REP, "  present: %-9s %-9s %s:%s %s %s [%s] power=%s\n", n.c_str(), speedName(u.speed).c_str(), u.vid.c_str(), u.pid.c_str(), u.manu.c_str(), u.prod.c_str(), u.drivers.c_str(), u.power.c_str());
        }
        closedir(dir);
    }
    for (auto& kv : DEVS) {
        UDev& u = kv.second;
        if (u.present && !found.count(kv.first)) {
            u.present = false; u.gone = nowS(); DISCONNECTS++; fault(); BEEP.lost();
            logEv(C_RED, true, "USB GONE  %s  %s:%s  %s", kv.first.c_str(), u.vid.c_str(), u.pid.c_str(), (u.manu + " " + u.prod).c_str());
        }
    }
    for (auto it = DEVS.begin(); it != DEVS.end();) {   // forget gone devices after 30 s
        if (!it->second.present && nowS() - it->second.gone > 30) it = DEVS.erase(it); else ++it;
    }
    AUTOSUSPEND = readFile(SYSROOT + "/sys/module/usbcore/parameters/autosuspend");
}

// ---------- kernel log ----------
enum KCat { K_INFO, K_NEW, K_DISC, K_RESET, K_ENUM, K_OVERCUR, K_HCDIED, K_FW, K_HIDERR, K_OTHERERR, K_N };
static const char* KNAMES[K_N] = { "info", "new device", "disconnect", "reset", "enumeration error", "over-current", "controller died", "firmware missing", "hid/input error", "other usb error" };
static int KBOOT[K_N], KLIVE[K_N];
struct KLine { std::string text; KCat cat; };
static std::deque<KLine> KLOG;
static std::map<std::string, int> HC_ERRS;   // pci address -> controller errors
static int KMSG_FD = -1;
static bool KMSG_OK = false;

static bool has(const std::string& s, const char* w) { return s.find(w) != std::string::npos; }
static KCat classify(const std::string& m) {
    if (has(m, "over-current")) return K_OVERCUR;
    if (has(m, "hc died") || has(m, "host controller not responding") || has(m, "host system error") || has(m, "assume dead")
        || has(m, "host halt") || has(m, "xhci host not responding") || has(m, "controller halted") || has(m, "hcd died")) return K_HCDIED;
    if (has(m, "error -71") || has(m, "error -110") || has(m, "error -32") || has(m, "descriptor read") || has(m, "unable to enumerate")
        || has(m, "not accepting address") || has(m, "cannot enable") || has(m, "device not responding") || has(m, "cannot reset")
        || has(m, "can't set config") || has(m, "string descriptor 0 read error")) return K_ENUM;
    if (has(m, "firmware") && (has(m, "fail") || has(m, "error") || has(m, "missing") || has(m, "not found") || has(m, "direct firmware load"))) return K_FW;
    if (has(m, "usb disconnect")) return K_DISC;
    if (has(m, "reset ") && has(m, "usb device")) return K_RESET;
    if (has(m, "new ") && has(m, "usb device number")) return K_NEW;
    bool usbish = has(m, "usb") || has(m, "xhci") || has(m, "ehci") || has(m, "ohci") || has(m, "hid") || has(m, "xpad") || has(m, "input");
    if (usbish && (has(m, "hid") || has(m, "input") || has(m, "xpad")) && (has(m, "error") || has(m, "fail") || has(m, "timeout") || has(m, "timed out"))) return K_HIDERR;
    if (usbish && (has(m, "error") || has(m, "fail") || has(m, "timeout") || has(m, "timed out") || has(m, "unable"))) return K_OTHERERR;
    return K_INFO;
}
static bool relevant(const std::string& m) {
    return has(m, "usb") || has(m, "xhci") || has(m, "ehci") || has(m, "ohci") || has(m, "hid") || has(m, "input:") || has(m, "xpad")
        || has(m, "over-current") || has(m, "firmware") || has(m, "bluetooth") || has(m, "btusb") || has(m, "xone") || has(m, "gip");
}
static void handleKLine(const std::string& msg, bool live) {
    std::string m = lower(msg);
    if (!relevant(m)) return;
    KCat c = classify(m);
    (live ? KLIVE : KBOOT)[c]++;
    // which port / controller is it about?
    size_t p = m.find("usb ");
    if (p != std::string::npos) {
        std::string tok = m.substr(p + 4); tok = tok.substr(0, tok.find_first_of(": "));
        int bus, port; if (parsePort(tok, bus, port) && c >= K_RESET) PORTS[{bus, port}].errs++;
    }
    if (c == K_HCDIED || c == K_OTHERERR) {
        for (auto& b : BUSES) if (!b.second.pci.empty() && has(m, b.second.pci.c_str())) { HC_ERRS[b.second.pci]++; break; }
    }
    if (has(m, "xone") || has(m, "gip") || has(m, "xow_dongle") || has(m, "mt76")) {
        if (c == K_FW) DG_FWERR++; else if (c >= K_RESET && c != K_DISC) DG_ERR++;
        if (live && c == K_INFO && (has(m, "error") || has(m, "fail"))) { DG_ERR++; fault(); logEv(C_ORANGE, true, "XBOX DONGLE ERROR"); }
    }
    if (live) {
        KLOG.push_back({ msg, c }); while (KLOG.size() > 9) KLOG.pop_front();
        if (REP) { fprintf(REP, "           kernel: %s\n", msg.c_str()); fflush(REP); }
        if (c >= K_RESET) {
            fault(); BEEP.kerr();
            if (c >= K_ENUM) logEv(C_ORANGE, c == K_HCDIED || c == K_OVERCUR, "KERNEL %s", KNAMES[c]);
        }
    }
}
static void openKmsg() {
    KMSG_FD = SYSROOT.empty() ? open("/dev/kmsg", O_RDONLY | O_NONBLOCK) : -1;
    KMSG_OK = KMSG_FD >= 0;
    if (REP) fprintf(REP, "kernel log: %s\n", KMSG_OK ? "/dev/kmsg open" : "cannot open /dev/kmsg (not root?)");
}
static void readKmsg(bool live) {
    if (KMSG_FD < 0) return;
    char buf[8192];
    for (int guard = 0; guard < 4000; guard++) {
        ssize_t n = read(KMSG_FD, buf, sizeof buf - 1);
        if (n < 0) { if (errno == EPIPE) continue; break; }   // EPIPE: records overwritten, keep reading
        buf[n] = 0; char* semi = strchr(buf, ';'); if (!semi) continue;
        std::string msg(semi + 1); size_t nl = msg.find('\n'); if (nl != std::string::npos) msg.resize(nl);
        handleKLine(msg, live);
    }
}

// ---------- controllers ----------
struct Pad {
    SDL_Joystick* js = nullptr; SDL_GameController* gc = nullptr; SDL_JoystickID id = -1;
    std::string name; int vid = 0, pid = 0, nb = 0, na = 0, nh = 0;
    std::vector<bool> everB; std::vector<int16_t> axMin, axMax, ax; std::vector<uint8_t> hat;
    double added = 0, lastIn = -100, lostAt = 0; int evCount = 0, evRate = 0; double rateT = 0;
    bool verified = false, lost = false, backHeld = false;
};
static std::vector<Pad> PADS;
static int PAD_LOSSES = 0, KEYS = 0; static std::string LAST_KEY;

static Pad* padById(SDL_JoystickID id) { for (auto& p : PADS) if (!p.lost && p.id == id) return &p; return nullptr; }
static void addPad(int idx) {
    Pad p; p.js = SDL_JoystickOpen(idx); if (!p.js) return;
    p.id = SDL_JoystickInstanceID(p.js);
    if (padById(p.id)) { SDL_JoystickClose(p.js); return; }
    if (SDL_IsGameController(idx)) p.gc = SDL_GameControllerOpen(idx);
    const char* nm = SDL_JoystickName(p.js); p.name = nm ? nm : "?";
    p.vid = SDL_JoystickGetVendor(p.js); p.pid = SDL_JoystickGetProduct(p.js);
    p.nb = SDL_JoystickNumButtons(p.js); p.na = SDL_JoystickNumAxes(p.js); p.nh = SDL_JoystickNumHats(p.js);
    p.everB.assign(std::max(0, p.nb), false); p.ax.assign(std::max(0, p.na), 0); p.hat.assign(std::max(0, p.nh), 0);
    for (int i = 0; i < p.na; i++) p.ax[i] = SDL_JoystickGetAxis(p.js, i);
    p.axMin = p.ax; p.axMax = p.ax; p.added = nowS();
    char guid[64]; SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(p.js), guid, sizeof guid);
    for (auto it = PADS.begin(); it != PADS.end();) if (it->lost) it = PADS.erase(it); else ++it;   // reuse slots of lost pads
    PADS.push_back(p);
    BEEP.plugged();
    logEv(C_GREEN, false, "PAD IN  %s  %04x:%04x  %d buttons %d axes %d hats %s", p.name.c_str(), p.vid, p.pid, p.nb, p.na, p.nh, p.gc ? "(mapped)" : "(no SDL mapping)");
    if (REP) fprintf(REP, "           guid %s\n", guid);
}
static void removePad(SDL_JoystickID id) {
    Pad* p = padById(id); if (!p) return;
    p->lost = true; p->lostAt = nowS(); PAD_LOSSES++; fault(); BEEP.lost();
    logEv(C_RED, true, "PAD LOST  %s  after %s", p->name.c_str(), fmtDur(nowS() - p->added).c_str());
    if (p->gc) SDL_GameControllerClose(p->gc); else SDL_JoystickClose(p->js);
    p->gc = nullptr; p->js = nullptr;
}
static void checkVerified(Pad& p) {
    if (p.verified) return;
    for (bool b : p.everB) if (!b) return;
    for (int i = 0; i < p.na; i++) { int r = (int)p.axMax[i] - p.axMin[i]; if (r > 4000 && r < 50000) return; }   // untouched dummy axes don't count
    p.verified = true; BEEP.good();
    logEv(C_GREEN, true, "PAD VERIFIED  %s  every button and axis works", p.name.c_str());
}

// ---------- actions ----------
static int MARKS = 0, SNAPS = 0; static double FIX_ARMED = -100; static bool FIXED = false;
static void actMark() { MARKS++; BEEP.blip(); logEv(C_YEL, true, "USER MARK #%d  (problem noticed here)", MARKS); }
static void actSnap() { SNAPS++; BEEP.blip(); char t[16]; snprintf(t, sizeof t, "snap%d", SNAPS); runSnapshot(t, false); logEv(C_CYAN, false, "SNAPSHOT %d saved", SNAPS); }
static void actFix() {
    if (nowS() - FIX_ARMED > 3) { FIX_ARMED = nowS(); BEEP.blip(); logEv(C_YEL, true, "PRESS AGAIN TO TURN USB POWER-SAVING OFF (UNTIL REBOOT)"); return; }
    FIX_ARMED = -100;
    bool ok = writeFile(SYSROOT + "/sys/module/usbcore/parameters/autosuspend", "-1"); int n = 0;
    for (auto& kv : DEVS) if (kv.second.present && writeFile(SYSROOT + "/sys/bus/usb/devices/" + kv.first + "/power/control", "on")) n++;
    for (auto& kv : BUSES) writeFile(SYSROOT + "/sys/bus/usb/devices/usb" + std::to_string(kv.first) + "/power/control", "on");
    FIXED = ok || n > 0; BEEP.good();
    logEv(FIXED ? C_GREEN : C_RED, true, "POWER-SAVING OFF: autosuspend %s, %d devices forced on", ok ? "-1" : "WRITE FAILED", n);
    CLEAN_SINCE = nowS();
}


// ---------- Xbox wireless dongle (045e:02e6 original, 02fe slim, 02f9 / 091e built-in) ----------
// Batocera drives it with the xone kernel driver (modules xone_dongle + xone_gip) and needs firmware xow_dongle*.bin.
struct Dongle {
    bool found = false, moduleLoaded = false, gipLoaded = false, fwFile = false, asleep = false, pairable = false;
    std::string port, pid, speed, driver, pairPath, fwName;
    int xboxPads = 0;
};
static Dongle DG; static int DG_LOSSES = 0, PAIRS = 0;
static bool isDongle(const UDev& u) { return u.vid == "045e" && (u.pid == "02e6" || u.pid == "02fe" || u.pid == "02f9" || u.pid == "091e"); }
static bool fileExists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }
static void scanDongle() {
    Dongle d;
    d.moduleLoaded = fileExists(SYSROOT + "/sys/module/xone_dongle") || fileExists(SYSROOT + "/sys/bus/usb/drivers/xone-dongle");
    d.gipLoaded = fileExists(SYSROOT + "/sys/module/xone_gip") || fileExists(SYSROOT + "/sys/bus/gip");
    for (const char* dir : { "/lib/firmware/", "/usr/lib/firmware/", "/lib/firmware/updates/" })   // xow_dongle*.bin (.xz / .zst too)
        if (DIR* fd = opendir((SYSROOT + dir).c_str())) {
            while (dirent* fe = readdir(fd)) if (!strncmp(fe->d_name, "xow_dongle", 10)) { d.fwFile = true; if (d.fwName.empty()) d.fwName = fe->d_name; }
            closedir(fd);
        }
    for (auto& kv : DEVS) {
        const UDev& u = kv.second; if (!u.present || !isDongle(u)) continue;
        d.found = true; d.port = kv.first; d.pid = u.pid; d.speed = u.speed; d.driver = u.drivers; d.asleep = u.runtime == "suspended";
        const std::string base = SYSROOT + "/sys/bus/usb/devices/";
        if (DIR* sd = opendir((base + kv.first).c_str())) {
            while (dirent* ie = readdir(sd)) if (strchr(ie->d_name, ':')) {
                std::string pp = base + kv.first + "/" + ie->d_name + "/pairing"; if (fileExists(pp)) { d.pairable = true; d.pairPath = pp; }
            }
            closedir(sd);
        }
        break;
    }
    for (auto& p : PADS) if (!p.lost && p.vid == 0x045e) d.xboxPads++;
    if (DG.found && !d.found) DG_LOSSES++;
    DG = d;
}
static void actPair() {
    if (!DG.found) { BEEP.kerr(); logEv(C_RED, true, "NO XBOX DONGLE ON USB - CANNOT PAIR"); return; }
    if (!DG.pairable) { BEEP.kerr(); logEv(C_RED, true, "DONGLE HAS NO PAIRING SWITCH - XONE DRIVER NOT ATTACHED"); return; }
    bool ok = writeFile(DG.pairPath, "1"); PAIRS++;
    if (ok) BEEP.good(); else BEEP.kerr();
    logEv(ok ? C_GREEN : C_RED, true, ok ? "DONGLE PAIRING ON - HOLD THE PAIR BUTTON ON THE PAD NOW" : "PAIRING WRITE FAILED (%s)", DG.pairPath.c_str());
}
static void dongleDiag(std::vector<std::pair<std::string, Col>>& d) {
    if (!DG.found) {
        if (DG_LOSSES) d.push_back({ "XBOX DONGLE DROPPED OFF USB - power/port problem. Try a USB2 port or a short extension cable.", C_RED });
        else if (DG.moduleLoaded) d.push_back({ "XBOX DONGLE NOT ON USB (driver is loaded). Not plugged in, dead port, or it fails to enumerate - check PORTS for red.", C_ORANGE });
        return;
    }
    if (!DG.moduleLoaded) d.push_back({ "XBOX DONGLE FOUND BUT XONE DRIVER NOT LOADED - the update may have dropped/disabled xone. See report.", C_RED });
    else if (DG.driver.find("xone") == std::string::npos) d.push_back({ "XBOX DONGLE: XONE LOADED BUT NOT ATTACHED (driver: " + (DG.driver.empty() ? std::string("none") : DG.driver) + "). Replug the dongle.", C_RED });
    if (DG_FWERR) d.push_back({ "XBOX DONGLE FIRMWARE FAILED TO LOAD - dongle cannot talk to pads. This is the likely cause.", C_RED });
    else if (!DG.fwFile && !DG.xboxPads) d.push_back({ "XBOX DONGLE FIRMWARE FILE NOT FOUND (xow_dongle*.bin in /lib/firmware) - likely why pads won't connect.", C_RED });
    if (DG.asleep) d.push_back({ "XBOX DONGLE IS ASLEEP (USB power-saving). Try BACK+X, then pair again.", C_ORANGE });
    if (DG.speed == "5000" || DG.speed == "10000") d.push_back({ "DONGLE ON A USB3 BUS - USB3 noise can block 2.4GHz. Move it to USB2 or use an extension cable.", C_YEL });
    if (DG_ERR) d.push_back({ "XBOX DONGLE/GIP KERNEL ERRORS (" + std::to_string(DG_ERR) + ") - see kernel log / report.", C_ORANGE });
    if (DG.moduleLoaded && DG.driver.find("xone") != std::string::npos && DG.fwFile && !DG_FWERR && !DG.xboxPads)
        d.push_back({ "DONGLE LOOKS HEALTHY BUT NO XBOX PAD CONNECTED - press BACK+B / P to pair, then the pad's pair button.", C_YEL });
}

// ---------- diagnosis ----------
static std::vector<std::pair<std::string, Col>> diagnose() {
    std::vector<std::pair<std::string, Col>> d;
    dongleDiag(d);
    int T[K_N]; for (int i = 0; i < K_N; i++) T[i] = KBOOT[i] + KLIVE[i];
    std::string badPorts; int portsWithErr = 0, portsUsed = 0;
    for (auto& p : PORTS) { if (p.second.used) portsUsed++; if (p.second.errs) { portsWithErr++; if (badPorts.size() < 30) badPorts += " " + std::to_string(p.first.first) + "-" + std::to_string(p.first.second); } }
    if (T[K_HCDIED]) d.push_back({ "USB CONTROLLER CRASHED - kernel/driver or BIOS. Try ports on another bus, BIOS update, or roll back the update.", C_RED });
    if (T[K_OVERCUR]) d.push_back({ "OVER-CURRENT - a device or hub draws too much power or is shorted. Remove it / use a powered hub.", C_RED });
    if (T[K_ENUM]) {
        if (portsWithErr >= 3 && portsWithErr * 2 >= portsUsed) d.push_back({ "ENUMERATION ERRORS ON MOST PORTS (" + badPorts + ") - points at the update (kernel), not one port or cable.", C_RED });
        else d.push_back({ "ENUMERATION ERRORS on port" + badPorts + " - cable, port or power. Swap cable, try another port.", C_ORANGE });
    }
    if (T[K_RESET] >= 3) d.push_back({ "DEVICES KEEP RESETTING (" + std::to_string(T[K_RESET]) + ") - flaky link or power-saving. Try BACK+X.", C_ORANGE });
    int susp = 0; for (auto& kv : DEVS) if (kv.second.present && kv.second.hid && kv.second.runtime == "suspended") susp++;
    if (susp) d.push_back({ std::to_string(susp) + " CONTROLLER/HID DEVICE(S) ASLEEP - USB power-saving is on them. Try BACK+X.", C_ORANGE });
    else if (!FIXED && AUTOSUSPEND != "-1" && !AUTOSUSPEND.empty() && (DISCONNECTS || T[K_RESET])) d.push_back({ "USB POWER-SAVING IS ON (autosuspend=" + AUTOSUSPEND + "s). If drops happen when idle, try BACK+X.", C_YEL });
    if (T[K_FW]) d.push_back({ "FIRMWARE MISSING for a USB device (dongle / bluetooth) - see report.", C_ORANGE });
    if (T[K_HIDERR]) d.push_back({ "HID/INPUT DRIVER ERRORS (" + std::to_string(T[K_HIDERR]) + ") - controller driver trouble after the update.", C_ORANGE });
    if (PAD_LOSSES && !T[K_DISC] && !DISCONNECTS) d.push_back({ "PAD DROPPED WITHOUT A USB DISCONNECT - wireless/bluetooth link or driver. Try it wired.", C_ORANGE });
    if (FIXED && FAULTS == 0) d.push_back({ "POWER-SAVING OFF AND CLEAN SO FAR - if it stays clean, autosuspend was the cause.", C_GREEN });
    if (d.empty()) d.push_back({ KMSG_OK ? "NO USB FAULTS YET. Plug each device into each port, play a while, watch the timer." : "NO KERNEL LOG ACCESS - only hot-plug and pad checks are running.", KMSG_OK ? C_GREEN : C_YEL });
    return d;
}

static void writeSummary() {
    if (!REP) return;
    double t = nowS() - T0;
    fprintf(REP, "\n===== SUMMARY after %s =====\n", fmtDur(t).c_str());
    fprintf(REP, "faults %d   usb connects %d   usb disconnects %d   pad losses %d   marks %d   best clean %s   power-saving fix %s\n",
            FAULTS, CONNECTS, DISCONNECTS, PAD_LOSSES, MARKS, fmtDur(std::max(BEST_CLEAN, nowS() - CLEAN_SINCE)).c_str(), FIXED ? "APPLIED" : "no");
    fprintf(REP, "autosuspend %s   keyboard keys %d\n", AUTOSUSPEND.c_str(), KEYS);
    fprintf(REP, "\nkernel events          since boot  during test\n");
    for (int i = 1; i < K_N; i++) fprintf(REP, "  %-20s %8d %10d\n", KNAMES[i], KBOOT[i], KLIVE[i]);
    fprintf(REP, "\nbuses\n");
    for (auto& b : BUSES) fprintf(REP, "  bus %d  %-9s %d ports  controller %s %s  errors %d\n", b.first, speedName(b.second.speed).c_str(), b.second.ports, b.second.pci.c_str(), b.second.driver.c_str(), HC_ERRS[b.second.pci]);
    fprintf(REP, "\nports used / errors\n");
    for (auto& p : PORTS) fprintf(REP, "  %d-%d  used %s  now %s  errors %d\n", p.first.first, p.first.second, p.second.used ? "yes" : "no", p.second.now ? "yes" : "no", p.second.errs);
    fprintf(REP, "\npads\n");
    for (auto& p : PADS) {
        int nbOk = 0; for (bool b : p.everB) nbOk += b;
        fprintf(REP, "  %s %04x:%04x  buttons %d/%d  %s%s\n", p.name.c_str(), p.vid, p.pid, nbOk, p.nb, p.verified ? "VERIFIED" : "not fully tested", p.lost ? "  LOST" : "");
    }
    fprintf(REP, "\nxbox dongle: %s port %s pid %s speed %s driver [%s] xone module %s gip %s firmware %s %s asleep %s pairing-switch %s xbox pads %d dongle drops %d fw errors %d other errors %d pair attempts %d\n",
            DG.found ? "FOUND" : "not found", DG.port.c_str(), DG.pid.c_str(), DG.speed.c_str(), DG.driver.c_str(), DG.moduleLoaded ? "yes" : "NO", DG.gipLoaded ? "yes" : "NO",
            DG.fwFile ? "present" : "MISSING", DG.fwName.c_str(), DG.asleep ? "yes" : "no", DG.pairable ? "yes" : "no", DG.xboxPads, DG_LOSSES, DG_FWERR, DG_ERR, PAIRS);
    fprintf(REP, "\ndiagnosis\n"); for (auto& d : diagnose()) fprintf(REP, "  - %s\n", d.first.c_str());
    fflush(REP);
}

// ---------- drawing ----------
static Canvas CV; static Font FONT;
static void txt(const std::string& s, float x, float y, float size, Col c, int align = 0) { FONT.draw(CV, upper(s).c_str(), V2(x, y), size, c, align); }

static void drawPad(const Pad& p, float x, float y, float w, float h, double t) {
    Col frame = p.lost ? C_RED : p.verified ? C_GREEN : C_CYAN;
    CV.rectLine(x, y, w, h, 2, withA(frame, 0.8f), 0.5f);
    txt(clip(p.name, 26), x + 12, y + 10, 15, C_WHITE);
    char b[128]; double idle = t - p.lastIn;
    snprintf(b, sizeof b, "%04X:%04X  B%d A%d H%d  %d EV/S  %s", p.vid, p.pid, p.nb, p.na, p.nh, p.evRate, p.lastIn < 0 ? "NO INPUT YET" : idle < 1 ? "LIVE" : ("IDLE " + std::to_string((int)idle) + "S").c_str());
    txt(b, x + 12, y + 34, 11, C_DIM);
    // buttons
    int perRow = 14; float bx = x + 22, by = y + 66, step = (w - 40) / perRow;
    for (int i = 0; i < p.nb && i < 42; i++) {
        V2 c(bx + (i % perRow) * step, by + (i / perRow) * 26);
        bool down = !p.lost && p.js && SDL_JoystickGetButton(p.js, i);
        if (down) CV.glowDisc(c, 14, withA(C_YEL, 0.9f));
        CV.circle(c, 8, 1.5f, p.everB[i] ? C_GREEN : C_DIM, p.everB[i] ? 0.6f : 0);
    }
    // axes
    float ay = y + 66 + ((std::min(p.nb, 42) + perRow - 1) / perRow) * 26 + 10;
    float aw = (w - 40) / 4 - 10;
    for (int i = 0; i < p.na && i < 8; i++) {
        float ax0 = x + 20 + (i % 4) * (aw + 10), ay0 = ay + (i / 4) * 20;
        bool full = (int)p.axMax[i] - p.axMin[i] >= 50000;
        CV.rectLine(ax0, ay0, aw, 10, 1, full ? C_GREEN : C_DIM, 0);
        float v = (p.ax[i] + 32768) / 65535.f;
        CV.rect(ax0 + (p.axMin[i] + 32768) / 65535.f * aw, ay0 + 3, (p.axMax[i] - p.axMin[i]) / 65535.f * aw, 4, withA(full ? C_GREEN : C_CYAN, 0.35f));
        CV.line(V2(ax0 + v * aw, ay0 - 3), V2(ax0 + v * aw, ay0 + 13), 2, C_YEL);
    }
    // hats
    for (int i = 0; i < p.nh && i < 2; i++) {
        V2 c(x + w - 40 - i * 50, y + h - 34); uint8_t hv = p.lost ? 0 : p.hat[i];
        CV.line(c + V2(-14, 0), c + V2(14, 0), 1, C_DIM, 0); CV.line(c + V2(0, -14), c + V2(0, 14), 1, C_DIM, 0);
        V2 d((hv & SDL_HAT_RIGHT) ? 1.f : (hv & SDL_HAT_LEFT) ? -1.f : 0.f, (hv & SDL_HAT_DOWN) ? 1.f : (hv & SDL_HAT_UP) ? -1.f : 0.f);
        if (hv) CV.glowDisc(c + d * 12, 9, C_YEL);
    }
    if (p.lost) txt("LOST", x + w / 2, y + h / 2 - 20, 40, withA(C_RED, 0.5f + 0.5f * (float)std::fabs(std::sin(t * 6))), 1);
    else if (p.verified) txt("VERIFIED", x + 12, y + h - 26, 16, C_GREEN);
    else { int ok = 0; for (bool e : p.everB) ok += e; snprintf(b, sizeof b, "PRESS EVERY BUTTON  %d/%d", ok, p.nb); txt(b, x + 12, y + h - 24, 12, C_YEL); }
}

static void drawRadar(V2 c, float r, double t) {
    CV.circle(c, r, 1.5f, C_DIM, 0.4f); CV.circle(c, r * 0.5f, 1, C_DIM, 0);
    float a = (float)t * 2.2f; CV.line(c, c + fromAng(a) * r, 2, withA(C_GREEN, 0.8f));
    for (auto& kv : DEVS) {
        unsigned h = 0; for (char ch : kv.first) h = h * 31 + ch;
        float ang = (h % 360) * PI / 180, rr = r * (0.3f + 0.6f * ((h / 360) % 100) / 100.f);
        V2 p = c + fromAng(ang) * rr;
        float since = std::fmod(a - ang + 100 * PI, 2 * PI);
        float f = 0.35f + 0.65f * std::exp(-since * 1.5f);
        Col col = !kv.second.present ? C_RED : (t - kv.second.seen < 3 ? C_YEL : C_GREEN);
        CV.glowDisc(p, 7, withA(col, f));
    }
}

static std::string VERSION;
static void draw(double t) {
    double clean = t - CLEAN_SINCE, best = std::max(BEST_CLEAN, clean);
    float flash = std::max(0.f, 1 - (float)(t - BIG.t) / 2.5f);
    // header
    txt("USB LAB", 40, 26, 46, C_CYAN);
    txt(VERSION, 40, 86, 15, C_DIM);
    drawRadar(V2(800, 62), 46, t);
    bool recent = clean < 3;
    txt("CLEAN " + fmtDur(clean), 1880, 22, 40, recent ? C_RED : C_GREEN, 2);
    char b[256];
    snprintf(b, sizeof b, "BEST %s   FAULTS %d   DROPS %d   PAD LOSSES %d", fmtDur(best).c_str(), FAULTS, DISCONNECTS, PAD_LOSSES);
    txt(b, 1880, 78, 16, C_WHITE, 2);
    CV.line(V2(30, 116), V2(1890, 116), 1.5f, withA(C_CYAN, 0.5f), 0.3f);

    // USB device list
    txt("USB DEVICES", 40, 132, 20, C_CYAN);
    snprintf(b, sizeof b, "POWER-SAVING %s", AUTOSUSPEND == "-1" ? "OFF" : AUTOSUSPEND.empty() ? "?" : ("ON " + AUTOSUSPEND + "S").c_str());
    txt(b, 930, 136, 13, AUTOSUSPEND == "-1" ? C_GREEN : C_YEL, 2);
    float y = 166; int rows = 0;
    for (auto& kv : DEVS) {
        if (rows++ >= 8) { txt("...", 40, y, 14, C_DIM); break; }
        const UDev& u = kv.second;
        Col c = !u.present ? C_RED : (t - u.seen < 4 ? C_GREEN : C_WHITE);
        std::string nm = u.manu.empty() ? u.prod : (u.prod.empty() ? u.manu : u.prod);
        snprintf(b, sizeof b, "%-8s %-8s %s:%s %-24s", kv.first.c_str(), speedName(u.speed).c_str(), u.vid.c_str(), u.pid.c_str(), clip(nm, 24).c_str());
        txt(b, 40, y, 14, c);
        std::string st = !u.present ? "GONE" : u.runtime == "suspended" ? "ASLEEP" : clip(u.drivers, 12);
        txt(st, 930, y, 12, !u.present ? C_RED : u.runtime == "suspended" ? C_ORANGE : C_DIM, 2);
        y += 25;
    }
    if (DEVS.empty()) txt("NO USB DEVICES FOUND IN /SYS/BUS/USB", 40, y, 14, C_RED);

    // Xbox dongle box
    {
        float bx = 40, by = 398, bw = 890;
        Col fc = !DG.found ? C_DIM : (DG.moduleLoaded && DG.driver.find("xone") != std::string::npos && DG.fwFile && !DG_FWERR) ? C_GREEN : C_RED;
        CV.rectLine(bx - 8, by - 6, bw + 16, 74, 1.5f, withA(fc, 0.7f), 0.3f);
        txt("XBOX WIRELESS DONGLE", bx, by + 2, 15, C_CYAN);
        snprintf(b, sizeof b, "%s", DG.found ? ("ON " + DG.port + "  " + speedName(DG.speed) + "  045E:" + DG.pid).c_str() : "NOT ON USB");
        txt(b, bx + bw, by + 4, 13, DG.found ? C_WHITE : C_ORANGE, 2);
        struct Chk { const char* n; int st; } ck[5] = {   // st: 1 ok, 0 bad, 2 unknown
            { "USB", DG.found ? 1 : 0 }, { "DRIVER", !DG.moduleLoaded ? 0 : !DG.found ? 2 : DG.driver.find("xone") != std::string::npos ? 1 : 0 },
            { "FIRMWARE", DG_FWERR ? 0 : (DG.fwFile || DG.xboxPads) ? 1 : 0 }, { "AWAKE", !DG.found ? 2 : DG.asleep ? 0 : 1 }, { "PADS", DG.xboxPads ? 1 : DG.found ? 0 : 2 } };
        for (int i = 0; i < 5; i++) {
            float cx = bx + i * 150; Col c = ck[i].st == 1 ? C_GREEN : ck[i].st == 0 ? C_RED : C_DIM;
            CV.rectLine(cx, by + 30, 22, 22, 1.5f, c, 0.5f);
            if (ck[i].st == 1) { CV.line(V2(cx + 4, by + 41), V2(cx + 9, by + 47), 2, c); CV.line(V2(cx + 9, by + 47), V2(cx + 19, by + 34), 2, c); }
            else if (ck[i].st == 0) { CV.line(V2(cx + 5, by + 35), V2(cx + 17, by + 47), 2, c); CV.line(V2(cx + 17, by + 35), V2(cx + 5, by + 47), 2, c); }
            std::string lab = ck[i].n; if (i == 4) lab += " " + std::to_string(DG.xboxPads);
            txt(lab, cx + 30, by + 35, 12, c);
        }
        if (DG.pairable) txt("BACK+B / P = PAIR", bx + 330, by + 6, 11, C_YEL);
    }

    // port map
    txt("PORTS", 40, 488, 20, C_CYAN);
    txt("GREEN IN USE   CYAN USED BEFORE   RED ERRORS   NUMBER = PORT", 930, 492, 11, C_DIM, 2);
    y = 524; int bi = 0;
    for (auto& kv : BUSES) {
        if (bi++ >= 6) break;
        const Bus& bs = kv.second; int herr = HC_ERRS[bs.pci];
        snprintf(b, sizeof b, "BUS %d %s", bs.num, speedName(bs.speed).c_str());
        txt(b, 40, y + 8, 13, herr ? C_RED : C_WHITE);
        txt(clip(bs.pci, 13), 40, y + 26, 9, C_DIM);
        for (int p = 1; p <= bs.ports && p <= 16; p++) {
            float px = 230 + (p - 1) * 44;
            auto it = PORTS.find({ bs.num, p }); PortStat ps = it != PORTS.end() ? it->second : PortStat();
            Col c = ps.errs ? C_RED : ps.now ? C_GREEN : ps.used ? C_CYAN : C_DIM;
            if (ps.now) CV.rect(px, y, 36, 36, withA(c, 0.18f));
            CV.rectLine(px, y, 36, 36, ps.now || ps.errs ? 2.f : 1.f, c, ps.now ? 0.6f : 0);
            txt(std::to_string(p), px + 18, y + 11, 13, c, 1);
            if (ps.errs) txt(std::to_string(std::min(ps.errs, 99)), px + 18, y + 40, 9, C_RED, 1);
            if (ps.devs > 1) txt("HUB", px + 18, y - 11, 8, C_YEL, 1);
        }
        y += 44 + 6;
    }
    if (BUSES.empty()) txt("NO USB CONTROLLERS FOUND", 40, y, 14, C_RED);

    // controllers
    txt("CONTROLLERS (SDL)", 980, 132, 20, C_CYAN);
    if (KEYS) { snprintf(b, sizeof b, "KEYBOARD OK  %d KEYS  LAST %s", KEYS, LAST_KEY.c_str()); txt(clip(b, 40), 1880, 136, 12, C_GREEN, 2); }
    int shown = 0;
    for (auto& p : PADS) {
        if (shown >= 4) break;
        drawPad(p, 980 + (shown % 2) * 455, 166 + (shown / 2) * 232, 445, 222, t); shown++;
    }
    if (PADS.empty()) {
        txt("NO CONTROLLERS DETECTED", 1430, 260, 26, withA(C_RED, 0.6f + 0.4f * (float)std::sin(t * 4)), 1);
        txt("PLUG ONE IN - IT SHOULD APPEAR HERE", 1430, 304, 15, C_DIM, 1);
    }

    // diagnosis
    txt("DIAGNOSIS", 980, 640, 20, C_CYAN);
    y = 672;
    for (auto& d : diagnose()) {
        if (y > 760) break;
        std::string s = d.first;   // wrap at ~74 chars
        while (!s.empty() && y <= 760) {
            size_t cut = s.size() <= 74 ? s.size() : s.rfind(' ', 74); if (cut == std::string::npos || cut == 0) cut = std::min<size_t>(74, s.size());
            txt(s.substr(0, cut), 980, y, 12, d.second); y += 19; s = cut < s.size() ? s.substr(cut + 1) : "";
        }
        y += 6;
    }

    // event log + kernel log
    CV.line(V2(30, 786), V2(1890, 786), 1.5f, withA(C_CYAN, 0.5f), 0.3f);
    txt("EVENTS", 40, 798, 16, C_CYAN);
    y = 824;
    for (auto& e : EVENTS) { txt(clip(e.text, 62), 40, y, 12, withA(e.c, t - e.t < 10 ? 1.f : 0.55f)); y += 20; }
    if (EVENTS.empty()) txt("WAITING...", 40, y, 12, C_DIM);
    txt(KMSG_OK ? "KERNEL USB LOG (LIVE)" : "KERNEL LOG NOT READABLE", 980, 798, 16, KMSG_OK ? C_CYAN : C_RED);
    y = 824;
    for (auto& k : KLOG) {
        Col c = k.cat >= K_ENUM ? C_RED : k.cat >= K_RESET ? C_ORANGE : k.cat == K_NEW ? C_GREEN : k.cat == K_DISC ? C_YEL : C_DIM;
        txt(clip(k.text, 70), 980, y, 11, c); y += 19;
    }
    if (KLOG.empty() && KMSG_OK) {
        int e = 0; for (int i = K_RESET; i < K_N; i++) e += KBOOT[i];
        snprintf(b, sizeof b, "QUIET. SINCE BOOT: %d USB ERRORS, %d DISCONNECTS, %d NEW DEVICES", e, KBOOT[K_DISC], KBOOT[K_NEW]);
        txt(b, 980, y, 11, e ? C_ORANGE : C_DIM);
    }

    // footer
    txt("BACK+A / M  MARK PROBLEM   BACK+B / P  PAIR DONGLE   BACK+X / F  POWER-SAVE OFF   BACK+Y / S  SNAPSHOT   BACK+START / ESC  QUIT", 40, 1042, 11, C_DIM);
    txt("REPORT reports/usblab-" + STAMP + ".txt", 1880, 1062, 10, C_DIM, 2);

    // centre flash
    if (flash > 0) {
        CV.rectLine(6, 6, VW - 12, VH - 12, 6, withA(BIG.c, flash), 1);
        float w = std::min(1800.f, Font::width(clip(BIG.text, 60).c_str(), 34) + 80);
        CV.rect(VW / 2 - w / 2, 455, w, 90, Col(0, 0, 0, (int)(220 * flash)));
        CV.rectLine(VW / 2 - w / 2, 455, w, 90, 3, withA(BIG.c, flash));
        txt(clip(BIG.text, 60), VW / 2, 482, 34, withA(BIG.c, flash), 1);
    }
}

// ---------- main ----------
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    if (const char* h = getenv("USBLAB_HOME")) HOME = h;
    if (const char* r = getenv("USBLAB_SYSROOT")) SYSROOT = r;
    REPDIR = HOME + "/reports"; mkdir(REPDIR.c_str(), 0755);
    { time_t tt = time(nullptr); char b[32]; strftime(b, sizeof b, "%Y%m%d-%H%M%S", localtime(&tt)); STAMP = b; }
    REPORT = REPDIR + "/usblab-" + STAMP + ".txt";
    REP = fopen(REPORT.c_str(), "w");
    {
        std::string bv = readFile("/usr/share/batocera/batocera.version"); utsname un{}; uname(&un);
        VERSION = "BATOCERA " + (bv.empty() ? std::string("?") : bv.substr(0, bv.find('\n'))) + "   KERNEL " + un.release;
        if (REP) fprintf(REP, "USB Lab report %s\n%s\n", STAMP.c_str(), VERSION.c_str());
    }
    BEST_CLEAN = atof(readFile(HOME + "/usblab.cfg").c_str());

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        if (REP) fprintf(REP, "SDL_Init failed: %s\n", SDL_GetError());
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) BEEP.init();
    SDL_GameControllerAddMappingsFromFile((HOME + "/gamecontrollerdb.txt").c_str());
    int ww = 1280, wh = 720; Uint32 wf = SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (const char* w = getenv("USBLAB_WINDOW")) { if (sscanf(w, "%dx%d", &ww, &wh) == 2) wf = 0; }
    SDL_Window* win = SDL_CreateWindow("USB Lab", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, wf);
    if (!win) { if (REP) fprintf(REP, "window failed: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { if (REP) fprintf(REP, "renderer failed: %s\n", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(ren, (int)VW, (int)VH);
    { SDL_RendererInfo ri; SDL_GetRendererInfo(ren, &ri); if (REP) fprintf(REP, "renderer %s, SDL %d.%d.%d\n", ri.name, SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL); }
    SDL_ShowCursor(SDL_DISABLE);
    CV.px = 1; CV.glow = true;

    T0 = nowS(); CLEAN_SINCE = T0;
    if (REP) fprintf(REP, "\n--- at start ---\n");
    scanUsb(true);
    openKmsg(); readKmsg(false); scanDongle();
    if (REP) {
        fprintf(REP, "kernel events since boot:"); for (int i = 1; i < K_N; i++) if (KBOOT[i]) fprintf(REP, "  %s %d", KNAMES[i], KBOOT[i]);
        fprintf(REP, "\nautosuspend %s\n\n--- events ---\n", AUTOSUSPEND.c_str()); fflush(REP);
    }
    runSnapshot("start", false);

    // demo / test switches
    if (getenv("USBLAB_DEMO")) {
        const char* demo[] = { "usb 3-2: new full-speed USB device number 7 using xhci_hcd", "usb 3-2: reset full-speed USB device number 7 using xhci_hcd",
                               "usb 3-4: device descriptor read/64, error -71", "usb 3-4: device not accepting address 9, error -71", "usb 3-2: USB disconnect, device number 7" };
        for (auto d : demo) handleKLine(d, true);
        logEv(C_YEL, true, "USER MARK #1  (problem noticed here)");
    }
    SDL_Joystick* vjs = nullptr;   // USBLAB_VPAD=1: a virtual pad to test the controller panels
    if (getenv("USBLAB_VPAD")) {
        int vi = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 11, 1);
        if (vi >= 0) vjs = SDL_JoystickOpen(vi);
    }
    double quitAt = getenv("USBLAB_SECS") ? atof(getenv("USBLAB_SECS")) : 0;
    const char* shot = getenv("USBLAB_SHOT"); int frames = 0;

    bool run = true; double lastScan = 0, lastK = 0, holdQuit = -1;
    while (run) {
        double t = nowS();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: run = false; break;
            case SDL_KEYDOWN: {
                KEYS++; LAST_KEY = upper(SDL_GetKeyName(e.key.keysym.sym));
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) run = false; else if (k == SDLK_m) actMark(); else if (k == SDLK_f) actFix(); else if (k == SDLK_s) actSnap(); else if (k == SDLK_p) actPair();
                break; }
            case SDL_JOYDEVICEADDED: addPad(e.jdevice.which); break;
            case SDL_JOYDEVICEREMOVED: removePad(e.jdevice.which); break;
            case SDL_JOYBUTTONDOWN: case SDL_JOYBUTTONUP:
                if (Pad* p = padById(e.jbutton.which)) {
                    p->lastIn = t; p->evCount++;
                    if (e.type == SDL_JOYBUTTONDOWN && e.jbutton.button < p->nb && !p->everB[e.jbutton.button]) { p->everB[e.jbutton.button] = true; checkVerified(*p); }
                } break;
            case SDL_JOYAXISMOTION:
                if (Pad* p = padById(e.jaxis.which)) if (e.jaxis.axis < p->na) {
                    int i = e.jaxis.axis; p->ax[i] = e.jaxis.value; p->axMin[i] = std::min(p->axMin[i], e.jaxis.value); p->axMax[i] = std::max(p->axMax[i], e.jaxis.value);
                    p->lastIn = t; p->evCount++; checkVerified(*p);
                }
                break;
            case SDL_JOYHATMOTION:
                if (Pad* p = padById(e.jhat.which)) if (e.jhat.hat < p->nh) { p->hat[e.jhat.hat] = e.jhat.value; p->lastIn = t; p->evCount++; }
                break;
            case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP:
                if (Pad* p = padById(e.cbutton.which)) {
                    bool down = e.type == SDL_CONTROLLERBUTTONDOWN; int bt = e.cbutton.button;
                    if (bt == SDL_CONTROLLER_BUTTON_BACK) p->backHeld = down;
                    else if (down && p->backHeld) {
                        if (bt == SDL_CONTROLLER_BUTTON_A) actMark(); else if (bt == SDL_CONTROLLER_BUTTON_X) actFix(); else if (bt == SDL_CONTROLLER_BUTTON_B) actPair();
                        else if (bt == SDL_CONTROLLER_BUTTON_Y) actSnap(); else if (bt == SDL_CONTROLLER_BUTTON_START) holdQuit = t;
                    }
                    if (bt == SDL_CONTROLLER_BUTTON_START && !down) holdQuit = -1;
                } break;
            }
        }
        if (vjs) { SDL_JoystickSetVirtualButton(vjs, frames % 11, 1); SDL_JoystickSetVirtualButton(vjs, (frames + 10) % 11, 0);
                   SDL_JoystickSetVirtualAxis(vjs, frames % 6, (Sint16)(frames % 2 ? 32767 : -32768)); SDL_JoystickSetVirtualHat(vjs, 0, SDL_HAT_UP); }
        if (holdQuit >= 0 && t - holdQuit > 0.6) run = false;
        if (t - lastScan > 0.5) { lastScan = t; scanUsb(false); scanDongle(); }
        if (t - lastK > 0.1) { lastK = t; readKmsg(true); }
        for (auto& p : PADS) if (t - p.rateT >= 1) { p.evRate = p.evCount; p.evCount = 0; p.rateT = t; }

        SDL_SetRenderDrawColor(ren, 0, 0, 6, 255); SDL_RenderClear(ren);
        draw(t); CV.flush(ren);
        frames++;
        if (shot && frames == 20) {
            int w, h; SDL_GetRendererOutputSize(ren, &w, &h);
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch); SDL_SaveBMP(s, shot); SDL_FreeSurface(s); run = false;
        }
        SDL_RenderPresent(ren);
        if (quitAt > 0 && t - T0 > quitAt) run = false;
    }
    double t = nowS();
    BEST_CLEAN = std::max(BEST_CLEAN, t - CLEAN_SINCE);
    { char b[32]; snprintf(b, sizeof b, "%.0f\n", BEST_CLEAN); writeFile(HOME + "/usblab.cfg", b); }
    writeSummary();
    if (REP) { fprintf(REP, "\nfull system snapshots: usblab-%s-start.txt / -end.txt\n", STAMP.c_str()); fclose(REP); REP = nullptr; }
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    runSnapshot("end", true);
    SDL_Quit();
    return 0;
}
