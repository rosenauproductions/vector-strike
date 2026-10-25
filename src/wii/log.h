// From Wii Lab v0.1 - logging: stderr (the launcher sends it to vectorstrike.log) + optional file + an in-memory tail.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

void logInit(const std::string& path);
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::vector<std::string> logTail(int n);
double nowS();          // steady clock, seconds
uint64_t nowUs();       // steady clock, microseconds
