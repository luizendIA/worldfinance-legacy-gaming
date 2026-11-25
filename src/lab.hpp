// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
inline void check(HRESULT hr, const char *operation) {
    if (FAILED(hr)) {
        std::ostringstream msg;
        msg << operation << " (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << ")";
        throw std::runtime_error(msg.str());
    }
}

struct FrameRun {
    unsigned width;
    unsigned height;
    std::vector<double> milliseconds;
    std::vector<uint8_t> rgba;
};

struct DynamicFrame {
    unsigned width = 0, height = 0, passes = 0;
    double nativeMs = 0, dynamicMs = 0, nativeWallMs = 0, dynamicWallMs = 0, rmse = 0;
};
struct DynamicRun {
    std::vector<DynamicFrame> frames;
    FrameRun nativePreview{}, dynamicPreview{};
};
double compareImages(const FrameRun &a, const FrameRun &b);

class Renderer {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> terrain, blit;
    ComPtr<ID3D11SamplerState> sampler;

  public:
    Renderer(IDXGIAdapter *adapter);
    DynamicRun dynamicResolution(bool dynamicFirst, unsigned budgetMs);
    FrameRun run(unsigned width, unsigned height, unsigned outputWidth, unsigned outputHeight,
                 unsigned warmups, unsigned samples);
};
