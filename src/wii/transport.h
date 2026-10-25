// From Wii Lab v0.1 (Controller Lab sandbox) - unchanged.
// A Wii remote's data channel. Linux: an L2CAP socket on PSM 0x13 (plus the control channel on 0x11 held open).
// The simulator uses a socketpair with the same semantics.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

struct Transport {
    virtual ~Transport() {}
    // one output report, WITHOUT the 0xA2 header (the transport adds whatever its platform needs). false = not sent.
    virtual bool send(const uint8_t* rep, size_t n, bool dontWait) = 0;
    // one input report, WITHOUT the 0xA1 header. returns length, 0 on timeout, -1 when the link is gone.
    virtual int recv(uint8_t* buf, size_t cap, int timeoutMs) = 0;
    virtual int outq() { return -1; }              // bytes waiting in the send queue (-1 unknown)
    virtual bool setSendBuf(int bytes) { (void)bytes; return false; }
    virtual int sendBuf() { return -1; }
    virtual std::string describe() { return ""; }
    virtual void close() = 0;
};

// Linux L2CAP pair (control + data sockets already connected)
struct L2capTransport : Transport {
    int ctl = -1, intr = -1;
    L2capTransport(int c, int i) : ctl(c), intr(i) {}
    ~L2capTransport() override;
    bool send(const uint8_t* rep, size_t n, bool dontWait) override;
    int recv(uint8_t* buf, size_t cap, int timeoutMs) override;
    int outq() override;
    bool setSendBuf(int bytes) override;
    int sendBuf() override;
    std::string describe() override;
    void close() override;
};

// Simulator (sim.cpp): fd is one end of a SOCK_SEQPACKET socketpair; frames carry the same 0xA2/0xA1 headers.
struct SimTransport : L2capTransport {
    explicit SimTransport(int fd) : L2capTransport(-1, fd) {}
    std::string describe() override { return "simulated remote (socketpair)"; }
};
