// SPDX-License-Identifier: GPL-3.0-or-later
#include "lab.hpp"
#include <algorithm>
#include <cmath>
#include <d3d12.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>

namespace {
std::string jsonString(const wchar_t *wide) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        throw std::runtime_error("Invalid adapter name");
    std::string utf8(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), n, nullptr, nullptr);
    utf8.pop_back();
    std::ostringstream out;
    out << '"';
    for (unsigned char c : utf8) {
        if (c == '"' || c == '\\')
            out << '\\' << c;
        else if (c < 32)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else
            out << c;
    }
    out << '"';
    return out.str();
}

ComPtr<IDXGIAdapter1> selectAdapter(bool warp) {
    ComPtr<IDXGIFactory4> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
    ComPtr<IDXGIAdapter1> adapter;
    if (warp) {
        check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter");
        return adapter;
    }
    for (UINT i = 0;; ++i) {
        HRESULT hr = factory->EnumAdapters1(i, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND)
            throw std::runtime_error("No hardware adapter found");
        check(hr, "EnumAdapters1");
        DXGI_ADAPTER_DESC1 d{};
        check(adapter->GetDesc1(&d), "GetDesc1");
        if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            return adapter;
        adapter.Reset();
    }
}

std::string capabilities(IDXGIAdapter1 *adapter) {
    ComPtr<ID3D12Device> device;
    HRESULT hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    if (FAILED(hr))
        return "{\"device\":false,\"mesh_tier\":null,\"shader_model\":null}";
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 caps{};
    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &caps, sizeof(caps));
    std::string tier = SUCCEEDED(hr) ? std::to_string(caps.MeshShaderTier) : "null";
    std::string model = "null";
    for (int id : {0x69, 0x68, 0x67, 0x66, 0x65, 0x64, 0x63, 0x62, 0x61, 0x60, 0x51}) {
        D3D12_FEATURE_DATA_SHADER_MODEL sm{static_cast<D3D_SHADER_MODEL>(id)};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm)))) {
            int value = sm.HighestShaderModel;
            model = "\"" + std::to_string(value >> 4) + "." + std::to_string(value & 15) + "\"";
            break;
        }
    }
    return "{\"device\":true,\"mesh_tier\":" + tier + ",\"shader_model\":" + model + "}";
}

std::string budget(IDXGIAdapter1 *adapter) {
    ComPtr<IDXGIAdapter3> a;
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(adapter->QueryInterface(IID_PPV_ARGS(&a))) ||
        FAILED(a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
        return "null";
    return std::to_string(info.Budget);
}

void printRun(const FrameRun &r) {
    std::cout << "{\"width\":" << r.width << ",\"height\":" << r.height
              << ",\"scene_color_bytes\":" << uint64_t(r.width) * r.height * 4 << ",\"gpu_ms\":[";
    for (size_t i = 0; i < r.milliseconds.size(); ++i) {
        if (i)
            std::cout << ',';
        std::cout << r.milliseconds[i];
    }
    std::cout << "]}";
}

double compare(const FrameRun &a, const FrameRun &b) {
    auto validate = [](const FrameRun &f) {
        uint8_t lo = 255, hi = 0;
        for (size_t i = 0; i < f.rgba.size(); i += 4) {
            if (f.rgba[i + 3] != 255)
                throw std::runtime_error("Invalid alpha in readback");
            lo = std::min(lo, f.rgba[i + 1]);
            hi = std::max(hi, f.rgba[i + 1]);
        }
        if (hi - lo < 24)
            throw std::runtime_error("Scene readback lacks expected contrast");
    };
    validate(a);
    validate(b);
    double error = 0;
    for (size_t i = 0; i < a.rgba.size(); ++i) {
        if (i % 4 == 3)
            continue;
        const int diff = int(a.rgba[i]) - int(b.rgba[i]);
        error += double(diff * diff);
    }
    return std::sqrt(error / (a.rgba.size() / 4 * 3));
}

void capture(const wchar_t *path, const FrameRun &r) {
    std::ofstream out(std::filesystem::path(path), std::ios::binary);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out << "P6\n" << r.width << ' ' << r.height << "\n255\n";
    for (size_t i = 0; i < r.rgba.size(); i += 4)
        out.write(reinterpret_cast<const char *>(&r.rgba[i]), 3);
    out.close();
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    try {
        bool warp = false, smoke = false, reverse = false;
        const wchar_t *indexedDirectory = nullptr;
        const wchar_t *capturePath = nullptr;
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg = argv[i];
            if (arg == L"--warp")
                warp = true;
            else if (arg == L"--smoke")
                smoke = true;
            else if (arg == L"--reverse")
                reverse = true;
            else if (arg == L"--indexed" && !indexedDirectory && i + 1 < argc)
                indexedDirectory = argv[++i];
            else if (arg == L"--capture" && i + 1 < argc)
                capturePath = argv[++i];
            else
                throw std::runtime_error("Usage: legacy-lab [--warp] [--smoke] [--reverse] [--capture "
                                         "frame.ppm] or --indexed NEW_DIRECTORY [--warp] [--smoke]");
        }
        const unsigned ow = smoke ? 160 : 1280, oh = smoke ? 90 : 720;
        const unsigned warmups = smoke ? 1 : 8, count = smoke ? 0 : 32;
        const auto adapter = selectAdapter(warp);
        DXGI_ADAPTER_DESC1 d{};
        check(adapter->GetDesc1(&d), "GetDesc1");
        const auto caps = capabilities(adapter.Get());
        if (indexedDirectory) {
            if (reverse || capturePath)
                throw std::runtime_error("Terrain switches supplied for indexed fixture");
            const auto samples = indexedFixture(adapter.Get(), indexedDirectory, smoke);
            std::cout << "{\"schema\":1,\"workload\":\"indexed-v1\",\"adapter\":"
                      << jsonString(d.Description) << ",\"software\":" << (warp ? "true" : "false")
                      << ",\"d3d12\":" << caps << ",\"draws_per_batch\":32,\"gpu_batch_ms\":[";
            for (size_t i = 0; i < samples.size(); ++i) {
                if (i)
                    std::cout << ',';
                std::cout << std::fixed << std::setprecision(7) << samples[i];
            }
            std::cout << "]}\n";
            return 0;
        }
        Renderer renderer(adapter.Get());
        const auto localBudget = budget(adapter.Get());
        auto profile = [&](bool low) {
            return renderer.run(low ? ow / 2 : ow, low ? oh / 2 : oh, ow, oh, warmups, count);
        };
        FrameRun first = profile(reverse), second = profile(!reverse);
        const FrameRun &full = reverse ? second : first;
        const FrameRun &low = reverse ? first : second;
        double rmse = compare(full, low);
        if (capturePath)
            capture(capturePath, full);
        std::cout << std::fixed << std::setprecision(6)
                  << "{\"schema\":1,\"workload\":\"terrain-v1\",\"adapter\":"
                  << jsonString(d.Description) << ",\"software\":" << (warp ? "true" : "false")
                  << ",\"dedicated_bytes\":" << d.DedicatedVideoMemory
                  << ",\"local_budget_bytes_before_profiles\":" << localBudget << ",\"d3d12\":" << caps
                  << ",\"warmups_per_profile\":" << warmups
                  << ",\"lower_resolution_first\":" << (reverse ? "true" : "false")
                  << ",\"rgb_rmse_8bit\":" << rmse << ",\"native\":";
        printRun(full);
        std::cout << ",\"scaled\":";
        printRun(low);
        std::cout << "}\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
