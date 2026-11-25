// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <wrl/client.h>

namespace polaris {
using Microsoft::WRL::ComPtr;
inline constexpr unsigned width = 1280, height = 720, vertexCount = 32, indexCount = 96;
struct Vertex {
    float x, y, z, w;
};
struct Capture {
    std::vector<uint32_t> ids;
    std::vector<float> depth;
};
void check(HRESULT result, const char *operation);
Capture render(ID3D12Device *device, D3D12_SHADER_BYTECODE shader, bool culled);
} // namespace polaris
