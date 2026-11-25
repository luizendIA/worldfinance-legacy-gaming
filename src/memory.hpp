// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lab.hpp"
#include <chrono>
#include <ostream>

class MemoryTrace {
    struct Sample {
        const char *stage;
        int64_t elapsedUs;
        DXGI_QUERY_VIDEO_MEMORY_INFO local, nonlocal;
    };
    using Clock = std::chrono::steady_clock;
    Clock::time_point start = Clock::now();
    ComPtr<IDXGIAdapter3> adapter;
    std::vector<Sample> samples;

  public:
    explicit MemoryTrace(IDXGIAdapter1 *selected);
    void record(const char *stage);
    void requireHeadroom(uint64_t upcomingBytes) const;
    void write(std::ostream &out) const;
};

void measureMemory(IDXGIAdapter1 *adapter, const std::string &quotedName, bool scaled);
