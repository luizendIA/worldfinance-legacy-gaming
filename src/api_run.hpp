// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace api_test {
constexpr unsigned width = 1280, height = 720, frames = 72, warmups = 8;
inline unsigned passes(unsigned frame) { return frame >= 24 && frame < 48 ? 3 : 1; }
inline float camera(unsigned frame) { return static_cast<float>(frame) / 32.0f; }
inline void save(const std::filesystem::path &dir, unsigned frame, const void *rgba) {
    std::ofstream out(dir / (std::to_string(frame) + ".rgba"), std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.write(static_cast<const char *>(rgba), width * height * 4);
    out.close();
}
struct Time {
    double gpu, wall;
};
inline void print(const char *api, const std::string &quotedDevice, const std::vector<Time> &times) {
    std::cout << std::fixed << std::setprecision(6) << "{\"api\":\"" << api
              << "\",\"adapter\":" << quotedDevice << ",\"frames\":[";
    for (size_t i = 0; i < times.size(); ++i) {
        if (i)
            std::cout << ',';
        std::cout << "{\"frame\":" << i << ",\"gpu_ms\":" << times[i].gpu
                  << ",\"wall_ms\":" << times[i].wall << '}';
    }
    std::cout << "]}\n";
}
} // namespace api_test
