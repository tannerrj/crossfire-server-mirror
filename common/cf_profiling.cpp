#include <cstdint>
#include <map>
#include <sstream>
#include <iomanip>
#include "global.h"

static uint64_t ticks = 0;

struct ProfilingStatistics {
    uint64_t totalCalls = 0;
    std::chrono::microseconds totalTime = std::chrono::microseconds::zero();

    bool isRunning = false;
    std::chrono::time_point<std::chrono::steady_clock> start;
    uint64_t currentCalls = 0;
    std::chrono::microseconds currentTime = std::chrono::microseconds::zero();
};

static std::map<std::string, ProfilingStatistics> profiling;

void profiler_start(const std::string &name) {
    auto &stats = profiling[name];
    if (!stats.isRunning) {
        stats.start = std::chrono::steady_clock::now();
        stats.isRunning = true;
    }
}
void profiler_stop(const std::string &name) {
    auto &stats = profiling[name];
    if (stats.isRunning) {
        stats.isRunning = false;
        auto diff = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - stats.start);
        stats.currentCalls++;
        stats.currentTime += diff;
    }
}
void profiler_end_tick(bool display) {
    ++ticks;
    std::string forced(display ? " (forced)" : "");
    display |= (ticks % 1000 == 0);
    std::ostringstream out;
    out
        << "Profiling statistics after " << std::setw(9) << ticks << " ticks" << forced << ":"
        << std::endl;
    for (auto &block : profiling) {
        if (block.second.isRunning) {
            LOG(llevError, "profiler %s still running\n", block.first.c_str());
        }
        if (display && block.second.currentCalls > 0) {
            out
                << std::setw(40) << block.first << ": "
                << std::setw(6) << block.second.currentCalls << " calls, "
                << std::setw(6) << block.second.currentTime.count() << " us, "
                << std::setw(6) << block.second.currentTime.count() / block.second.currentCalls << " us avr"
                ;
            if (block.second.totalCalls > 0) {
                out
                    << ", "
                    << std::setw(6) << block.second.totalCalls << " calls total, "
                    << std::setw(6) << block.second.totalCalls / ticks << " calls per tick, "
                    << std::setw(15) << block.second.totalTime.count() << " us total, "
                    << std::setw(8) << block.second.totalTime.count() / block.second.totalCalls << " us avr";
            }
            out << std::endl;
        }
        block.second.totalCalls += block.second.currentCalls;
        block.second.totalTime += block.second.currentTime;
        block.second.currentCalls = 0;
        block.second.currentTime = std::chrono::microseconds::zero();
    }
    if (display)
        LOG(llevInfo, "%s", out.str().c_str());
}
