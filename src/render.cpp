// SPDX-License-Identifier: GPL-3.0-or-later
#include "lab.hpp"
#include "shaders.hpp"
#include <algorithm>
#include <chrono>
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

ComPtr<ID3D11Query> query(ID3D11Device *device, D3D11_QUERY type) {
    D3D11_QUERY_DESC desc{type, 0};
    ComPtr<ID3D11Query> value;
    check(device->CreateQuery(&desc, &value), "CreateQuery");
    return value;
}

template <typename T> T readQuery(ID3D11DeviceContext *ctx, ID3D11Query *q) {
    T value{};
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
        HRESULT hr = ctx->GetData(q, &value, sizeof(value), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (hr == S_OK)
            return value;
        check(hr, "GetData");
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("GPU query timed out");
        Sleep(1);
    }
}

struct Surface {
    unsigned width, height;
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
    Surface s{};
    s.width = width;
    s.height = height;
    check(dev->CreateTexture2D(&d, nullptr, &s.image), "CreateTexture2D");
    check(dev->CreateRenderTargetView(s.image.Get(), nullptr, &s.target), "CreateRenderTargetView");
    if (sampled)
        check(dev->CreateShaderResourceView(s.image.Get(), nullptr, &s.view),
              "CreateShaderResourceView");
    return s;
}
void drawScene(ID3D11DeviceContext *context, Surface &scene, Surface &output, ID3D11PixelShader *terrain,
               ID3D11PixelShader *blit, unsigned terrainPasses) {
    auto pass = [&](Surface &s, unsigned width, unsigned height, ID3D11PixelShader *shader) {
        D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->OMSetRenderTargets(1, s.target.GetAddressOf(), nullptr);
        context->PSSetShader(shader, nullptr, 0);
        context->Draw(3, 0);
    };
    ID3D11ShaderResourceView *none = nullptr;
    context->PSSetShaderResources(0, 1, &none);
    for (unsigned i = 0; i < terrainPasses; ++i)
        pass(scene, scene.width, scene.height, terrain);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    context->PSSetShaderResources(0, 1, scene.view.GetAddressOf());
    pass(output, output.width, output.height, blit);
}

class GpuTimer {
    ComPtr<ID3D11Query> disjoint, start, end;
    ID3D11DeviceContext *context;

  public:
    GpuTimer(ID3D11Device *device, ID3D11DeviceContext *ctx)
        : disjoint(query(device, D3D11_QUERY_TIMESTAMP_DISJOINT)),
          start(query(device, D3D11_QUERY_TIMESTAMP)), end(query(device, D3D11_QUERY_TIMESTAMP)),
          context(ctx) {}
    template <class F> double measure(F &&draw) {
        context->Begin(disjoint.Get());
        context->End(start.Get());
        draw();
        context->End(end.Get());
        context->End(disjoint.Get());
        context->Flush();
        const auto clock = readQuery<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT>(context, disjoint.Get());
        const auto a = readQuery<UINT64>(context, start.Get()),
                   b = readQuery<UINT64>(context, end.Get());
        if (clock.Disjoint || !clock.Frequency || b <= a)
            throw std::runtime_error("Invalid timestamp interval; repeat this run.");
        return 1000.0 * static_cast<double>(b - a) / static_cast<double>(clock.Frequency);
    }
};

std::vector<uint8_t> readPixels(ID3D11Device *device, ID3D11DeviceContext *context, Surface &output) {
    const unsigned ow = output.width, oh = output.height;
    std::vector<uint8_t> rgba;
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
    rgba.resize(static_cast<size_t>(ow) * oh * 4);
    D3D11_MAPPED_SUBRESOURCE map{};
    check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "Map readback");
    for (unsigned y = 0; y < oh; ++y)
        std::memcpy(&rgba[size_t(y) * ow * 4],
                    static_cast<const uint8_t *>(map.pData) + size_t(y) * map.RowPitch, ow * 4);
    context->Unmap(staging.Get(), 0);
    return rgba;
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
    auto draw = [&] { drawScene(context.Get(), scene, output, terrain.Get(), blit.Get(), 1); };
    for (unsigned i = 0; i < warmups; ++i)
        draw();
    FrameRun result{w, h, {}, {}};
    GpuTimer timer(device.Get(), context.Get());
    for (unsigned i = 0; i < samples; ++i)
        result.milliseconds.push_back(timer.measure(draw));
    result.rgba = readPixels(device.Get(), context.Get(), output);
    return result;
}

namespace {
class ResolutionController {
    // Eighths of the output width and height. Start at native resolution.
    unsigned scale = 8;
    unsigned overBudget = 0, canRaise = 0;

  public:
    unsigned current() const { return scale; }
    void observe(double gpuMs, unsigned budgetMs) {
        double ratio = double(scale + 1) / scale;
        overBudget = gpuMs > budgetMs ? overBudget + 1 : 0;
        canRaise = gpuMs * ratio * ratio < 0.9 * budgetMs ? canRaise + 1 : 0;
        if (overBudget >= 2 && scale > 4) {
            --scale;
        } else if (canRaise >= 4 && scale < 8) {
            ++scale;
        } else {
            return;
        }
        overBudget = canRaise = 0;
    }
};
} // namespace

DynamicRun Renderer::dynamicResolution(bool dynamicFirst, unsigned budgetMs) {
    constexpr unsigned ow = 1280, oh = 720, count = 72;
    auto code = compile("movingTerrain", "ps_5_0");
    ComPtr<ID3D11PixelShader> moving;
    check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &moving),
          "Create moving terrain shader");
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = 16;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> camera;
    check(device->CreateBuffer(&desc, nullptr, &camera), "Create camera buffer");
    context->PSSetConstantBuffers(0, 1, camera.GetAddressOf());
    std::vector<Surface> scenes;
    for (unsigned scale = 4; scale <= 8; ++scale)
        scenes.push_back(surface(device.Get(), ow * scale / 8, oh * scale / 8, true));
    Surface output = surface(device.Get(), ow, oh, false);
    GpuTimer timer(device.Get(), context.Get());
    DynamicRun run;
    run.frames.resize(count);
    auto position = [&](unsigned frame) {
        const float values[4] = {float(frame) / 32.0f, 0, 0, 0};
        context->UpdateSubresource(camera.Get(), 0, nullptr, values, 0, 0);
    };
    auto draw = [&](unsigned scale, unsigned passes) {
        drawScene(context.Get(), scenes.at(scale - 4), output, moving.Get(), blit.Get(), passes);
    };
    auto sequence = [&](bool adaptive) {
        ResolutionController controller;
        position(0);
        for (unsigned i = 0; i < 8; ++i)
            draw(8, 1);
        for (unsigned i = 0; i < count; ++i) {
            auto &f = run.frames[i];
            f.passes = (i >= 24 && i < 48) ? 3 : 1;
            const unsigned scale = adaptive ? controller.current() : 8;
            const auto begin = std::chrono::steady_clock::now();
            position(i);
            const double gpu = timer.measure([&] { draw(scale, f.passes); });
            if (adaptive)
                controller.observe(gpu, budgetMs);
            const double wall =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                    .count();
            if (adaptive) {
                f.width = ow * scale / 8;
                f.height = oh * scale / 8;
                f.dynamicMs = gpu;
                f.dynamicWallMs = wall;
            } else {
                f.nativeMs = gpu;
                f.nativeWallMs = wall;
            }
        }
    };
    sequence(dynamicFirst);
    sequence(!dynamicFirst);
    // Replay the recorded scales and exact camera positions. No readback in the timed sequences.
    for (unsigned i = 0; i < count; ++i) {
        auto &f = run.frames[i];
        position(i);
        draw(8, f.passes);
        FrameRun reference{ow, oh, {}, readPixels(device.Get(), context.Get(), output)};
        draw(f.width * 8 / ow, f.passes);
        FrameRun actual{ow, oh, {}, readPixels(device.Get(), context.Get(), output)};
        f.rmse = compareImages(reference, actual);
        if (i == 36) {
            run.nativePreview = std::move(reference);
            run.dynamicPreview = std::move(actual);
        }
    }
    ID3D11Buffer *none = nullptr;
    context->PSSetConstantBuffers(0, 1, &none);
    return run;
}
