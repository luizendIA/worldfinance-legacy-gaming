// SPDX-License-Identifier: GPL-3.0-or-later
#include "memory.hpp"
#include <algorithm>
#include <iostream>

MemoryTrace::MemoryTrace(IDXGIAdapter1 *selected) {
    check(selected->QueryInterface(IID_PPV_ARGS(&adapter)), "IDXGIAdapter3");
}
void MemoryTrace::record(const char *stage) {
    Sample s{};
    s.stage = stage;
    s.elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
    check(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &s.local),
          "DXGI local usage");
    check(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &s.nonlocal),
          "DXGI non-local usage");
    samples.push_back(s);
}
void MemoryTrace::requireHeadroom(uint64_t upcomingBytes) const {
    constexpr uint64_t reserve = 64 * 1024 * 1024;
    const auto &info = samples.back().local;
    if (info.CurrentUsage >= info.Budget || info.Budget - info.CurrentUsage < reserve + upcomingBytes)
        throw std::runtime_error("Insufficient local budget for the experiment");
}
void MemoryTrace::write(std::ostream &out) const {
    bool first = true;
    for (const auto &s : samples) {
        if (!first)
            out << ',';
        first = false;
        out << "{\"stage\":\"" << s.stage << "\",\"elapsed_us\":" << s.elapsedUs
            << ",\"local_usage\":" << s.local.CurrentUsage << ",\"local_budget\":" << s.local.Budget
            << ",\"nonlocal_usage\":" << s.nonlocal.CurrentUsage
            << ",\"nonlocal_budget\":" << s.nonlocal.Budget << '}';
    }
}
void measureMemory(IDXGIAdapter1 *adapter, const std::string &quotedName, bool scaled) {
    constexpr unsigned ow = 1280, oh = 720;
    const unsigned w = scaled ? ow / 2 : ow, h = scaled ? oh / 2 : oh;
    MemoryTrace trace(adapter);
    {
        Renderer renderer(adapter);
        trace.record("before_allocation");
        trace.requireHeadroom((uint64_t(w) * h + 2 * ow * oh) * 4);
        const FrameRun frame = renderer.run(w, h, ow, oh, 32, 0, &trace);
        renderer.flush();
        trace.record("after_release");
        for (int i = 0; i < 10; ++i) {
            Sleep(25);
            trace.record("release_poll");
        }
        uint8_t lo = 255, hi = 0;
        for (size_t i = 1; i < frame.rgba.size(); i += 4) {
            lo = std::min(lo, frame.rgba[i]);
            hi = std::max(hi, frame.rgba[i]);
        }
        if (hi - lo < 24)
            throw std::runtime_error("Invalid terrain readback");
    }
    trace.record("after_device_release");
    std::cout << "{\"schema\":1,\"workload\":\"terrain-vram-v1\",\"adapter\":" << quotedName
              << ",\"scene_width\":" << w << ",\"scene_height\":" << h
              << ",\"output_width\":1280,\"output_height\":720,\"frames\":32,\"samples\":[";
    trace.write(std::cout);
    std::cout << "]}\n";
}
