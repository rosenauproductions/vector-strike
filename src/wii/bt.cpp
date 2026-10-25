// From Wii Lab v0.1 - unchanged.
#include "bt.h"
#include "log.h"
#include "transport.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <endian.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

// ---- kernel ABI (no libbluetooth) ----
#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#define BTPROTO_L2CAP 0
#define BTPROTO_HCI 1
#define SOL_HCI 0
#define HCI_FILTER 2
#define SOL_L2CAP 6
#define L2CAP_OPTIONS 1
#define HCIDEVUP _IOW('H', 201, int)
#define HCIGETDEVINFO _IOR('H', 211, int)
#define HCIGETCONNINFO _IOR('H', 213, int)
#define HCIINQUIRY _IOR('H', 240, int)
#ifndef TIOCOUTQ
#define TIOCOUTQ 0x5411
#endif

struct bdaddr6 { uint8_t b[6]; };
struct sockaddr_l2x { sa_family_t l2_family; unsigned short l2_psm; bdaddr6 l2_bdaddr; unsigned short l2_cid; uint8_t l2_bdaddr_type; };
struct sockaddr_hcix { sa_family_t hci_family; unsigned short hci_dev; unsigned short hci_channel; };
struct hci_filterx { uint32_t type_mask; uint32_t event_mask[2]; uint16_t opcode; };
struct hci_dev_infox {
    uint16_t dev_id; char name[8]; bdaddr6 bdaddr; uint32_t flags; uint8_t type; uint8_t features[8];
    uint32_t pkt_type, link_policy, link_mode; uint16_t acl_mtu, acl_pkts, sco_mtu, sco_pkts;
    uint32_t err_rx, err_tx, cmd_tx, evt_rx, acl_tx, acl_rx, sco_tx, sco_rx, byte_rx, byte_tx;
};
struct hci_inquiry_reqx { uint16_t dev_id; uint16_t flags; uint8_t lap[3]; uint8_t length; uint8_t num_rsp; };
struct __attribute__((packed)) inquiry_infox { bdaddr6 bdaddr; uint8_t pscan_rep_mode, pscan_period_mode, pscan_mode; uint8_t dev_class[3]; uint16_t clock_offset; };
struct l2cap_optionsx { uint16_t omtu, imtu, flush_to; uint8_t mode, fcs, max_tx; uint16_t txwin_size; };

namespace bt {

Monitor monitor;

std::string macStr(const uint8_t b[6]) { char s[18]; snprintf(s, sizeof s, "%02X:%02X:%02X:%02X:%02X:%02X", b[5], b[4], b[3], b[2], b[1], b[0]); return s; }
bool macParse(const std::string& s, uint8_t b[6]) {
    unsigned v[6]; if (sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    for (int i = 0; i < 6; i++) b[5 - i] = (uint8_t)v[i];
    return true;
}

std::string shell(const char* c) {
    std::string out; FILE* f = popen(c, "r"); if (!f) return out;
    char b[512]; while (fgets(b, sizeof b, f)) out += b;
    pclose(f); return out;
}

static int openBound(int dev) {
    int s = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI); if (s < 0) return -1;
    sockaddr_hcix a{}; a.hci_family = AF_BLUETOOTH; a.hci_dev = (unsigned short)dev;
    if (bind(s, (sockaddr*)&a, sizeof a) < 0) { int e = errno; close(s); errno = e; return -1; }
    return s;
}

bool adapter(int dev, AdapterInfo& o) {
    o = AdapterInfo(); o.devId = dev;
    int s = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI); if (s < 0) return false;
    hci_dev_infox di{}; di.dev_id = (uint16_t)dev;
    bool ok = ioctl(s, HCIGETDEVINFO, &di) == 0; close(s);
    if (!ok) return false;
    o.present = true; o.up = di.flags & 1; o.name.assign(di.name, strnlen(di.name, 8)); o.addr = macStr(di.bdaddr.b);
    o.linkPolicy = di.link_policy; o.linkMode = di.link_mode; o.aclMtu = di.acl_mtu; o.aclPkts = di.acl_pkts;
    o.errRx = di.err_rx; o.errTx = di.err_tx; o.aclTx = di.acl_tx; o.aclRx = di.acl_rx; o.byteRx = di.byte_rx; o.byteTx = di.byte_tx;
    return true;
}

bool powerUp(int dev) {
    shell("rfkill unblock bluetooth 2>&1");
    int s = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
    bool ok = false;
    if (s >= 0) { ok = ioctl(s, HCIDEVUP, dev) == 0 || errno == EALREADY; if (!ok) logf("bt: switching hci%d on failed: %s", dev, strerror(errno)); close(s); }
    shell("timeout 4 bluetoothctl power on >/dev/null 2>&1");
    usleep(300000);
    logf("bt: hci%d power-up %s", dev, ok ? "ok" : "failed");
    return ok;
}

std::vector<uint8_t> cmd(int dev, int ogf, int ocf, const std::vector<uint8_t>& p, int timeoutMs) {
    int s = openBound(dev); if (s < 0) return {};
    uint16_t op = (uint16_t)((ogf << 10) | ocf);
    hci_filterx f{}; f.type_mask = 1u << 4; f.event_mask[0] = (1u << 0x0E) | (1u << 0x0F); f.opcode = op;
    setsockopt(s, SOL_HCI, HCI_FILTER, &f, sizeof f);
    std::vector<uint8_t> pkt = {0x01, (uint8_t)op, (uint8_t)(op >> 8), (uint8_t)p.size()}; pkt.insert(pkt.end(), p.begin(), p.end());
    std::vector<uint8_t> out;
    if (write(s, pkt.data(), pkt.size()) == (ssize_t)pkt.size()) {
        double end = nowS() + timeoutMs / 1000.0;
        while (nowS() < end) {
            pollfd pf{s, POLLIN, 0}; if (poll(&pf, 1, 20) <= 0) continue;
            uint8_t b[260]; ssize_t n = read(s, b, sizeof b);
            if (n < 3 || b[0] != 0x04) continue;
            const uint8_t* q = b + 3; int len = b[2];
            if (b[1] == 0x0E && len >= 3 && (q[1] | q[2] << 8) == op) { out.assign(q + 3, q + len); break; }
            if (b[1] == 0x0F && len >= 4 && (q[2] | q[3] << 8) == op) { out.assign(q, q + 1); break; }
        }
    }
    close(s);
    return out;
}

int connHandle(int dev, const std::string& mac, bool* master) {
    uint8_t a[6]; if (!macParse(mac, a)) return -1;
    int s = openBound(dev); if (s < 0) return -1;
    uint8_t buf[8 + 16] = {0}; memcpy(buf, a, 6); buf[6] = 1;   // ACL link
    int h = -1;
    if (ioctl(s, HCIGETCONNINFO, buf) == 0) {
        h = buf[8] | buf[9] << 8;
        uint32_t lm; memcpy(&lm, buf + 20, 4);
        if (master) *master = lm & 1;
    }
    close(s);
    return h;
}

static std::vector<uint8_t> h16(int h) { return {(uint8_t)h, (uint8_t)(h >> 8)}; }

void linkInfo(int dev, const std::string& mac, LinkInfo& li) {
    bool m = false; int h = connHandle(dev, mac, &m);
    li.ok = h >= 0; li.handle = h; li.master = m;
    if (h < 0) return;
    auto r = cmd(dev, 0x05, 0x0005, h16(h), 200);
    li.rssiOk = r.size() >= 4 && r[0] == 0; if (li.rssiOk) li.rssi = (int8_t)r[3];
    r = cmd(dev, 0x05, 0x0003, h16(h), 200);
    if (r.size() >= 4 && r[0] == 0) li.quality = r[3];
    r = cmd(dev, 0x02, 0x000C, h16(h), 200);
    if (r.size() >= 5 && r[0] == 0) li.policy = r[3] | r[4] << 8;
    li.mode = monitor.mode(h, &li.sniffInterval);
}

bool setSniff(int dev, const std::string& mac, bool allow) {
    int h = connHandle(dev, mac); if (h < 0) return false;
    if (!allow) cmd(dev, 0x02, 0x0004, h16(h), 300);   // exit sniff now if we're in it
    std::vector<uint8_t> p = h16(h); uint16_t pol = allow ? 0x0005 : 0x0001; p.push_back((uint8_t)pol); p.push_back((uint8_t)(pol >> 8));
    auto r = cmd(dev, 0x02, 0x000D, p, 300);
    bool ok = !r.empty() && r[0] == 0;
    logf("bt: %s sniff %s on %s (handle 0x%03X)", ok ? "set" : "FAILED to set", allow ? "allowed" : "blocked", mac.c_str(), h);
    return ok;
}

bool requestMaster(int dev, const std::string& mac) {
    uint8_t a[6]; if (!macParse(mac, a)) return false;
    std::vector<uint8_t> p(a, a + 6); p.push_back(0x00);
    auto r = cmd(dev, 0x02, 0x000B, p, 500);
    bool ok = !r.empty() && r[0] == 0;
    logf("bt: role switch to master on %s: %s", mac.c_str(), r.empty() ? "no answer" : ok ? "accepted" : ("refused, status " + std::to_string(r[0])).c_str());
    return ok;
}

bool setSupervisionTimeout(int dev, const std::string& mac, int ms) {
    int h = connHandle(dev, mac); if (h < 0) return false;
    int slots = ms * 1000 / 625; std::vector<uint8_t> p = h16(h); p.push_back((uint8_t)slots); p.push_back((uint8_t)(slots >> 8));
    auto r = cmd(dev, 0x03, 0x0037, p, 300);
    return !r.empty() && r[0] == 0;
}

bool setFlushTimeout(int dev, const std::string& mac, int ms) {
    int h = connHandle(dev, mac); if (h < 0) return false;
    int u = ms * 1000 / 625; if (u > 0x07FF) u = 0x07FF;
    std::vector<uint8_t> p = h16(h); p.push_back((uint8_t)u); p.push_back((uint8_t)(u >> 8));
    auto r = cmd(dev, 0x03, 0x0028, p, 300);
    bool ok = !r.empty() && r[0] == 0;
    logf("bt: automatic flush timeout %d ms on %s: %s", ms, mac.c_str(), ok ? "set" : "failed");
    return ok;
}

int l2connect(const std::string& mac, int psm, int timeoutMs, double* ms) {
    double t0 = nowS();
    bdaddr6 a; if (!macParse(mac, a.b)) { errno = EINVAL; return -1; }
    int s = socket(AF_BLUETOOTH, SOCK_SEQPACKET | SOCK_CLOEXEC, BTPROTO_L2CAP); if (s < 0) return -1;
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    sockaddr_l2x addr; memset(&addr, 0, sizeof addr); addr.l2_family = AF_BLUETOOTH; addr.l2_psm = htole16((unsigned short)psm); addr.l2_bdaddr = a;
    int r = connect(s, (sockaddr*)&addr, sizeof addr);
    if (r < 0 && errno != EINPROGRESS) { int e = errno; close(s); errno = e; return -1; }
    if (r < 0) {
        pollfd p{s, POLLOUT, 0};
        if (poll(&p, 1, timeoutMs) <= 0) { close(s); errno = ETIMEDOUT; return -1; }
        int err = 0; socklen_t l = sizeof err; getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &l);
        if (err) { close(s); errno = err; return -1; }
    }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK);   // blocking from here; sends that must not block use MSG_DONTWAIT
    if (ms) *ms = (nowS() - t0) * 1000;
    return s;
}

static bool isWiiClass(const uint8_t* c) { return (c[0] == 0x04 && c[1] == 0x25 && c[2] == 0x00) || (c[0] == 0x08 && c[1] == 0x05 && c[2] == 0x00); }

int inquiry(int dev, int len, const std::function<bool(const std::string&, int)>& onFound, const std::atomic<bool>& cancel, std::string& why) {
    int s = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
    if (s < 0) { why = std::string("socket: ") + strerror(errno); return -1; }
    sockaddr_hcix a{}; a.hci_family = AF_BLUETOOTH; a.hci_dev = (unsigned short)dev;
    if (bind(s, (sockaddr*)&a, sizeof a) < 0) { why = std::string("bind: ") + strerror(errno); close(s); return -1; }
    hci_filterx f{}; f.type_mask = 1u << 4; f.event_mask[0] = f.event_mask[1] = 0xffffffffu;
    setsockopt(s, SOL_HCI, HCI_FILTER, &f, sizeof f);
    uint8_t c[9] = {0x01, 0x01, 0x04, 0x05, 0x33, 0x8b, 0x9e, (uint8_t)len, 0x00};
    if (write(s, c, sizeof c) != (ssize_t)sizeof c) { why = std::string("send: ") + strerror(errno); close(s); return -1; }
    double end = nowS() + len * 1.28 + 2.0; bool started = false; int rc = 0;
    auto hit = [&](const uint8_t* addr, int rssi) {
        bdaddr6 b; memcpy(b.b, addr, 6);
        if (onFound(macStr(b.b), rssi)) { rc = 1; return true; }
        return false;
    };
    while (nowS() < end && rc == 0) {
        if (cancel) { rc = 1; break; }
        pollfd p{s, POLLIN, 0}; if (poll(&p, 1, 50) <= 0) continue;
        uint8_t b[300]; ssize_t n = read(s, b, sizeof b);
        if (n < 3 || b[0] != 0x04) continue;
        uint8_t ev = b[1]; const uint8_t* q = b + 3; int l = b[2];
        if (ev == 0x0F && l >= 4 && q[2] == 0x01 && q[3] == 0x04) {
            if (q[0]) { char t[64]; snprintf(t, sizeof t, "controller refused inquiry, status 0x%02X", q[0]); why = t; rc = -1; break; }
            started = true;
        } else if (ev == 0x01) break;
        else if (ev == 0x02 && l >= 1) { for (int i = 0; i < q[0] && 1 + (i + 1) * 14 <= l; i++) { const uint8_t* e = q + 1 + i * 14; if (isWiiClass(e + 9) && hit(e, 0)) break; } }
        else if (ev == 0x22 && l >= 1) { for (int i = 0; i < q[0] && 1 + (i + 1) * 14 <= l; i++) { const uint8_t* e = q + 1 + i * 14; if (isWiiClass(e + 8) && hit(e, (int8_t)e[13])) break; } }
        else if (ev == 0x2F && l >= 15) { if (isWiiClass(q + 9)) hit(q + 1, (int8_t)q[14]); }
    }
    if (rc == 0 && !started && why.empty()) { why = "no answer from controller"; rc = -1; }
    if (rc == 1) { uint8_t cc[4] = {0x01, 0x02, 0x04, 0x00}; if (write(s, cc, 4) < 0) {} usleep(20000); }   // HCI_Inquiry_Cancel
    close(s);
    return rc;
}

int kernelInquiry(int dev, int len, std::vector<std::string>& found, std::string& why) {
    int s = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
    if (s < 0) { why = strerror(errno); return -1; }
    std::vector<uint8_t> buf(sizeof(hci_inquiry_reqx) + 255 * sizeof(inquiry_infox));
    auto* req = (hci_inquiry_reqx*)buf.data();
    req->dev_id = (uint16_t)dev; req->flags = 1; req->lap[0] = 0x33; req->lap[1] = 0x8b; req->lap[2] = 0x9e; req->length = (uint8_t)len; req->num_rsp = 255;
    int r = ioctl(s, HCIINQUIRY, buf.data());
    if (r < 0) { why = strerror(errno); close(s); return -1; }
    auto* info = (inquiry_infox*)(buf.data() + sizeof(hci_inquiry_reqx));
    for (int i = 0; i < req->num_rsp; i++) if (isWiiClass(info[i].dev_class)) found.push_back(macStr(info[i].bdaddr.b));
    close(s);
    return 0;
}

void bluezScan(std::vector<std::string>& found) {
    std::string seen = shell("timeout 7 bluetoothctl --timeout 5 scan on 2>&1");
    std::string devs = shell("timeout 3 bluetoothctl devices 2>&1");
    size_t pos = 0;
    while ((pos = devs.find("Device ", pos)) != std::string::npos) {
        size_t eol = devs.find('\n', pos); std::string line = devs.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos += 7;
        if (line.size() < 24) continue;
        std::string mac = line.substr(7, 17);
        bool wii = line.find("RVL-CNT") != std::string::npos || line.find("Nintendo") != std::string::npos;
        if (wii && seen.find(mac) != std::string::npos) found.push_back(mac);
    }
}

// ---- link event monitor ----
static const char* reason(int r) {
    switch (r) { case 0x08: return "link timeout (out of range / interference)"; case 0x13: return "remote ended it (power button / battery)";
        case 0x16: return "we ended it"; case 0x15: return "remote low on power"; case 0x05: return "authentication failure"; case 0x22: return "LMP response timeout"; }
    return "other";
}
void Monitor::start(int d) {
    if (run) return; dev = d; run = true;
    th = std::thread([this] {
        int s = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI); if (s < 0) { run = false; return; }
        sockaddr_hcix a{}; a.hci_family = AF_BLUETOOTH; a.hci_dev = (unsigned short)dev;
        if (bind(s, (sockaddr*)&a, sizeof a) < 0) { close(s); run = false; return; }
        hci_filterx f{}; f.type_mask = 1u << 4; f.event_mask[0] = (1u << 0x03) | (1u << 0x05) | (1u << 0x12) | (1u << 0x14);
        setsockopt(s, SOL_HCI, HCI_FILTER, &f, sizeof f);
        while (run) {
            pollfd p{s, POLLIN, 0}; if (poll(&p, 1, 200) <= 0) continue;
            uint8_t b[260]; ssize_t n = read(s, b, sizeof b);
            if (n < 3 || b[0] != 0x04) continue;
            const uint8_t* q = b + 3; int l = b[2];
            if (b[1] == 0x03 && l >= 9) logf("hci: connection %s to %s, handle 0x%03X", q[0] ? "FAILED" : "up", macStr(q + 3).c_str(), q[1] | (q[2] & 0x0f) << 8);
            else if (b[1] == 0x05 && l >= 4) { int h = q[1] | (q[2] & 0x0f) << 8; logf("hci: handle 0x%03X disconnected, reason 0x%02X: %s", h, q[3], reason(q[3])); std::lock_guard<std::mutex> g(m); modeByHandle.erase(h); }
            else if (b[1] == 0x12 && l >= 8) logf("hci: role change with %s -> we are %s%s", macStr(q + 1).c_str(), q[7] == 0 ? "MASTER" : "slave", q[0] ? " (failed)" : "");
            else if (b[1] == 0x14 && l >= 6) {
                int h = q[1] | (q[2] & 0x0f) << 8, md = q[3], iv = q[4] | q[5] << 8;
                { std::lock_guard<std::mutex> g(m); modeByHandle[h] = md; intervalByHandle[h] = iv; }
                char extra[64] = ""; if (md == 2) snprintf(extra, sizeof extra, " interval %.2f ms (adds input lag!)", iv * 0.625);
                logf("hci: handle 0x%03X mode -> %s%s", h, md == 0 ? "ACTIVE" : md == 1 ? "HOLD" : md == 2 ? "SNIFF" : "PARK", extra);
            }
        }
        close(s);
    });
}
void Monitor::stop() { run = false; if (th.joinable()) th.join(); }
int Monitor::mode(int h, int* iv) { std::lock_guard<std::mutex> g(m); auto it = modeByHandle.find(h); if (iv) *iv = intervalByHandle.count(h) ? intervalByHandle[h] : 0; return it == modeByHandle.end() ? 0 : it->second; }

}

// ---- L2CAP transport ----
bool L2capTransport::send(const uint8_t* rep, size_t n, bool dontWait) {
    uint8_t b[64]; if (n + 1 > sizeof b || intr < 0) return false;
    b[0] = 0xA2; memcpy(b + 1, rep, n);
    return ::send(intr, b, n + 1, MSG_NOSIGNAL | (dontWait ? MSG_DONTWAIT : 0)) == (ssize_t)(n + 1);
}
int L2capTransport::recv(uint8_t* buf, size_t cap, int timeoutMs) {
    if (intr < 0) return -1;
    pollfd p{intr, POLLIN, 0}; int r = poll(&p, 1, timeoutMs);
    if (r == 0) return 0;
    if (r < 0) return errno == EINTR ? 0 : -1;
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL) && !(p.revents & POLLIN)) return -1;
    uint8_t b[96]; ssize_t n = ::recv(intr, b, sizeof b, MSG_DONTWAIT);
    if (n < 0) return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    if (n == 0) return -1;
    if (b[0] != 0xA1 || n < 2) return 0;
    size_t m = std::min((size_t)n - 1, cap); memcpy(buf, b + 1, m);
    return (int)m;
}
int L2capTransport::outq() { int q = -1; if (intr >= 0 && ioctl(intr, TIOCOUTQ, &q) < 0) q = -1; return q; }
bool L2capTransport::setSendBuf(int bytes) { return intr >= 0 && setsockopt(intr, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof bytes) == 0; }
int L2capTransport::sendBuf() { int v = -1; socklen_t l = sizeof v; if (intr >= 0) getsockopt(intr, SOL_SOCKET, SO_SNDBUF, &v, &l); return v; }
std::string L2capTransport::describe() {
    l2cap_optionsx o{}; socklen_t l = sizeof o;
    if (intr < 0 || getsockopt(intr, SOL_L2CAP, L2CAP_OPTIONS, &o, &l) < 0) return "L2CAP";
    char s[96]; snprintf(s, sizeof s, "L2CAP out MTU %u, in MTU %u, flush %u", o.omtu, o.imtu, o.flush_to); return s;
}
L2capTransport::~L2capTransport() { close(); if (intr >= 0) ::close(intr); if (ctl >= 0) ::close(ctl); }
void L2capTransport::close() {
    if (intr >= 0) ::shutdown(intr, SHUT_RDWR);
    if (ctl >= 0) ::shutdown(ctl, SHUT_RDWR);
}
