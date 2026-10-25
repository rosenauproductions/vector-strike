// From Wii Lab v0.1 (speaker decoding removed): a simulated Wii remote on the far end of a socketpair. Answers like the
// real thing (acks, memory reads, status, IR + accelerometer at ~100 Hz with jitter), so the driver and the game's
// aiming can be tested without hardware. Vector Strike: VS_WII_SIM=N.
#pragma once
#include <memory>
#include <string>
#include "transport.h"

std::unique_ptr<Transport> simConnect(const std::string& mac);
void simShutdownAll();
