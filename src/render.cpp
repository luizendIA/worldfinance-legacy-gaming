// SPDX-License-Identifier: GPL-3.0-or-later
#include "lab.hpp"
#include "shaders.hpp"
#include <algorithm>
#include "gpu-query.hpp"
#include <cstring>
#include <d3dcompiler.h>

namespace {
ComPtr<ID3DBlob> compile(const char *entry, const char *target) {
    ComPtr<ID3DBlob> code, errors;
    HRESULT hr = D3DCompile(shaderSource, sizeof(shaderSource) - 1, nullptr, nullptr, nullptr, entry,
                            target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr) && errors)
        throw std::runtime_error(static_cast<const char *>(errors->GetBufferPointer()));
    check(hr, "D3DCompile");
    return code;
}

struct Surface {
    ComPtr<ID3D11Texture2D> image;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11ShaderResourceView> view;
};
Surface surface(ID3D11Device *dev, unsigned width, unsigned height, bool sampled) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = width;
    d.Height = height;
    d.ArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | (sampled ? D3D11_BIND_SHADER_RESOURCE : 0);
    Surface s;
    check(dev->CreateTexture2D(&d, nullptr, &s.image), "CreateTexture2D");
    check(dev->CreateRenderTargetView(s.image.Get(), nullptr, &s.target), "CreateRenderTargetView");
    if (sampled)
        check(dev->CreateShaderResourceView(s.image.Get(), nullptr, &s.view),
              "CreateShaderResourceView");
    return s;
}
} // namespace

Renderer::Renderer(IDXGIAdapter *adapter) {
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    check(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1, D3D11_SDK_VERSION,
                            &device, nullptr, &context),
          "D3D11CreateDevice FL11_0");
    const auto vs = compile("screen", "vs_5_0"), ps = compile("landscape", "ps_5_0"),
               copy = compile("composite", "ps_5_0");
    check(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex),
          "CreateVertexShader");
    check(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &terrain),
          "CreatePixelShader terrain");
    check(device->CreatePixelShader(copy->GetBufferPointer(), copy->GetBufferSize(), nullptr, &blit),
          "CreatePixelShader composite");
    D3D11_SAMPLER_DESC d{};
    d.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    d.AddressU = d.AddressV = d.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    d.MaxLOD = D3D11_FLOAT32_MAX;
    d.MaxAnisotropy = 1;
    d.ComparisonFunc = D3D11_COMPARISON_NEVER;
    check(device->CreateSamplerState(&d, &sampler), "CreateSamplerState");
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex.Get(), nullptr, 0);
    context->PSSetSamplers(0, 1, sampler.GetAddressOf());
}

FrameRun Renderer::run(unsigned w, unsigned h, unsigned ow, unsigned oh, unsigned warmups,
                       unsigned samples) {
    Surface scene = surface(device.Get(), w, h, true), output = surface(device.Get(), ow, oh, false);
    auto pass = [&](Surface &s, unsigned width, unsigned height, ID3D11PixelShader *shader) {
        D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->OMSetRenderTargets(1, s.target.GetAddressOf(), nullptr);
        context->PSSetShader(shader, nullptr, 0);
        context->Draw(3, 0);
    };
    auto draw = [&]() {
        ID3D11ShaderResourceView *none = nullptr;
        context->PSSetShaderResources(0, 1, &none);
        pass(scene, w, h, terrain.Get());
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->PSSetShaderResources(0, 1, scene.view.GetAddressOf());
        pass(output, ow, oh, blit.Get());
    };
    for (unsigned i = 0; i < warmups; ++i)
        draw();
    FrameRun result{w, h, {}, {}};
    auto disjoint = query(device.Get(), D3D11_QUERY_TIMESTAMP_DISJOINT);
    auto start = query(device.Get(), D3D11_QUERY_TIMESTAMP),
         end = query(device.Get(), D3D11_QUERY_TIMESTAMP);
    for (unsigned i = 0; i < samples; ++i) {
        context->Begin(disjoint.Get());
        context->End(start.Get());
        draw();
        context->End(end.Get());
        context->End(disjoint.Get());
        context->Flush();
        const auto clock = readQuery<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT>(context.Get(), disjoint.Get());
        const auto a = readQuery<UINT64>(context.Get(), start.Get()),
                   b = readQuery<UINT64>(context.Get(), end.Get());
        if (clock.Disjoint || !clock.Frequency || b <= a)
            throw std::runtime_error("Invalid timestamp interval; repeat this run.");
        result.milliseconds.push_back(1000.0 * static_cast<double>(b - a) /
                                      static_cast<double>(clock.Frequency));
    }
    // Readback occurs after all timestamps. Copy rows separately to respect driver pitch.
    context->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView *none = nullptr;
    context->PSSetShaderResources(0, 1, &none);
    D3D11_TEXTURE2D_DESC d{};
    output.image->GetDesc(&d);
    d.BindFlags = 0;
    d.Usage = D3D11_USAGE_STAGING;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    check(device->CreateTexture2D(&d, nullptr, &staging), "CreateTexture2D staging");
    context->CopyResource(staging.Get(), output.image.Get());
    result.rgba.resize(static_cast<size_t>(ow) * oh * 4);
    D3D11_MAPPED_SUBRESOURCE map{};
    check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "Map readback");
    for (unsigned y = 0; y < oh; ++y)
        std::memcpy(&result.rgba[size_t(y) * ow * 4],
                    static_cast<const uint8_t *>(map.pData) + size_t(y) * map.RowPitch, ow * 4);
    context->Unmap(staging.Get(), 0);
    return result;
}
