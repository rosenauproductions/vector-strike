// From Wii Lab v0.1. Change for Vector Strike: lines are prefixed "wii:" so they stand out in vectorstrike.log.
#include "log.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

static std::mutex g_m;
static std::deque<std::string> g_tail;
static FILE* g_file = nullptr;
static const double g_t0 = nowS();

double nowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
uint64_t nowUs() { return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void logInit(const std::string& path) {
    std::lock_guard<std::mutex> g(g_m);
    if (g_file) fclose(g_file);
    g_file = path.empty() ? nullptr : fopen(path.c_str(), "w");
}

void logf(const char* fmt, ...) {
    char body[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(body, sizeof body, fmt, ap); va_end(ap);
    char line[1100]; snprintf(line, sizeof line, "wii: %9.3f %s", nowS() - g_t0, body);
    std::lock_guard<std::mutex> g(g_m);
    fprintf(stdout, "%s\n", line); fflush(stdout);
    if (g_file) { fprintf(g_file, "%s\n", line); fflush(g_file); }
    g_tail.push_back(body);
    while (g_tail.size() > 200) g_tail.pop_front();
}

std::vector<std::string> logTail(int n) {
    std::lock_guard<std::mutex> g(g_m);
    std::vector<std::string> v;
    int s = (int)g_tail.size() - n; if (s < 0) s = 0;
    for (int i = s; i < (int)g_tail.size(); i++) v.push_back(g_tail[i]);
    return v;
}
