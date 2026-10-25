// From Wii Lab v0.1 - unchanged.
// Linux Bluetooth without libbluetooth: raw HCI sockets for inquiry, controller commands and link events, L2CAP for
// the remote's channels. Everything here is timed and logged so connection problems can be read from the log alone.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AdapterInfo {
    bool present = false, up = false;
    int devId = 0;
    std::string name, addr;
    uint32_t linkPolicy = 0, linkMode = 0;
    int aclMtu = 0, aclPkts = 0;
    uint32_t errRx = 0, errTx = 0, aclTx = 0, aclRx = 0, byteRx = 0, byteTx = 0;
};

struct LinkInfo {                   // one ACL link, as the controller sees it
    bool ok = false;
    int handle = -1;
    bool master = false;            // true = we (the adapter) are master; best for latency
    int rssi = 0; bool rssiOk = false;
    int quality = -1;               // 0-255 controller link quality
    int policy = -1;                // link policy bits: 1 role switch, 2 hold, 4 sniff, 8 park
    int mode = 0;                   // 0 active, 1 hold, 2 sniff (from Mode Change events)
    int sniffInterval = 0;          // slots (0.625 ms) when in sniff
};

namespace bt {
    std::string macStr(const uint8_t b[6]);
    bool macParse(const std::string& s, uint8_t b[6]);
    bool adapter(int dev, AdapterInfo& out);
    bool powerUp(int dev);                                           // rfkill unblock + HCIDEVUP + bluetoothctl power on
    std::string shell(const char* cmd);
    // controller command; returns the return-parameter bytes (first byte = status) or empty on timeout/refusal
    std::vector<uint8_t> cmd(int dev, int ogf, int ocf, const std::vector<uint8_t>& params, int timeoutMs = 500);
    int connHandle(int dev, const std::string& mac, bool* master = nullptr);
    void linkInfo(int dev, const std::string& mac, LinkInfo& li);    // handle, role, RSSI, quality, policy (+ last mode)
    bool setSniff(int dev, const std::string& mac, bool allow);      // write link policy (keeps role switch)
    bool requestMaster(int dev, const std::string& mac);
    bool setSupervisionTimeout(int dev, const std::string& mac, int ms);
    bool setFlushTimeout(int dev, const std::string& mac, int ms);   // 0 = infinite (default)
    // L2CAP connect with timeout; ms gets how long it took. -1 on failure (errno set)
    int l2connect(const std::string& mac, int psm, int timeoutMs, double* ms = nullptr);

    // streaming inquiry on a raw HCI socket: onFound fires the moment a Wii remote answers (no waiting for the
    // inquiry to finish). Returns 0 done, 1 stopped early by onFound returning true, -1 error (why set).
    int inquiry(int dev, int lengthUnits, const std::function<bool(const std::string&, int rssi)>& onFound,
                const std::atomic<bool>& cancel, std::string& why);
    int kernelInquiry(int dev, int lengthUnits, std::vector<std::string>& found, std::string& why);
    void bluezScan(std::vector<std::string>& found);

    // background watcher for link events (connect, disconnect, role change, sniff/active mode changes)
    struct Monitor {
        std::atomic<bool> run{false}; std::thread th; std::mutex m;
        std::map<int, int> modeByHandle, intervalByHandle;
        int dev = 0;
        void start(int d); void stop();
        int mode(int handle, int* interval);
    };
    extern Monitor monitor;
}
