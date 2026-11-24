// SPDX-License-Identifier: GPL-3.0-or-later
#include "gpu-query.hpp"
#include "lab.hpp"
#include <array>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>

namespace {
constexpr unsigned side = 256;
struct Vertex {
    float x, y, depth;
};
constexpr Vertex front(float x, float y) { return {x, y, .125f + y / 2048}; }
constexpr Vertex floorVertex(float x, float y) { return {x, y, .5f + x / 1024}; }
constexpr Vertex vertices[] = {floorVertex(32, 32),  floorVertex(224, 32), floorVertex(224, 224),
                               floorVertex(32, 224), front(56, 88),        front(216, 56),
                               front(144, 216),      {230, 12, .0625f},    {180, 20, .0625f},
                               {232, 84, .0625f}};
// Meshlet local-to-global vertex table and local triangle list. Submit near geometry first.
constexpr uint16_t remap[] = {4, 5, 6, 0, 1, 2, 3, 7, 8, 9};
constexpr std::array<uint8_t, 12> localIndices = {0, 1, 2, 3, 4, 5, 3, 5, 6, 7, 8, 9};
constexpr char source[] = R"(
float4 vs(float3 v : POSITION) : SV_Position {
    return float4(v.x/128-1,1-v.y/128,v.z,1);
}
uint ps(uint primitive : SV_PrimitiveID) : SV_Target { return primitive+1; }
)";

ComPtr<ID3DBlob> shader(const char *name, const char *profile) {
    ComPtr<ID3DBlob> code, errors;
    check(D3DCompile(source, sizeof(source) - 1, nullptr, nullptr, nullptr, name, profile,
                     D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors),
          "Compile indexed shader");
    return code;
}
ComPtr<ID3D11Buffer> buffer(ID3D11Device *dev, const void *data, UINT bytes, UINT bind) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = bytes;
    desc.BindFlags = bind;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    D3D11_SUBRESOURCE_DATA initial{data, 0, 0};
    ComPtr<ID3D11Buffer> result;
    check(dev->CreateBuffer(&desc, &initial, &result), "Create geometry buffer");
    return result;
}
ComPtr<ID3D11Texture2D> texture(ID3D11Device *dev, DXGI_FORMAT format, UINT bind) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = side;
    desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = format;
    desc.BindFlags = bind;
    ComPtr<ID3D11Texture2D> result;
    check(dev->CreateTexture2D(&desc, nullptr, &result), "Create indexed attachment");
    return result;
}
void saveAttachment(ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11Texture2D *attachment,
                    unsigned bytesPerPixel, const std::filesystem::path &path) {
    D3D11_TEXTURE2D_DESC desc{};
    attachment->GetDesc(&desc);
    desc.BindFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    check(dev->CreateTexture2D(&desc, nullptr, &staging), "Create indexed readback");
    ctx->CopyResource(staging.Get(), attachment);
    std::ofstream out(path, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map indexed readback");
    for (unsigned row = 0; row < side; ++row)
        out.write(static_cast<const char *>(mapped.pData) + row * mapped.RowPitch, side * bytesPerPixel);
    ctx->Unmap(staging.Get(), 0);
    out.close();
}
} // namespace

std::vector<double> indexedFixture(IDXGIAdapter *adapter, const wchar_t *directory, bool smoke) {
    const std::filesystem::path folder(directory);
    if (!std::filesystem::create_directory(folder))
        throw std::runtime_error("Choose a new capture directory.");
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    check(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1, D3D11_SDK_VERSION,
                            &device, nullptr, &context),
          "Create indexed device");
    auto dev = device.Get();
    auto ctx = context.Get();
    std::array<uint16_t, localIndices.size()> indices{};
    for (size_t i = 0; i < indices.size(); ++i)
        indices[i] = remap[localIndices[i]];
    auto vb = buffer(dev, vertices, sizeof(vertices), D3D11_BIND_VERTEX_BUFFER);
    auto ib = buffer(dev, indices.data(), sizeof(indices), D3D11_BIND_INDEX_BUFFER);
    auto v = shader("vs", "vs_5_0"), p = shader("ps", "ps_5_0");
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    check(dev->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr, &vs),
          "Create indexed VS");
    check(dev->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &ps),
          "Create indexed PS");
    D3D11_INPUT_ELEMENT_DESC pos{
        "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    ComPtr<ID3D11InputLayout> layout;
    check(dev->CreateInputLayout(&pos, 1, v->GetBufferPointer(), v->GetBufferSize(), &layout),
          "Create indexed layout");
    auto ids = texture(dev, DXGI_FORMAT_R8_UINT, D3D11_BIND_RENDER_TARGET);
    auto depth = texture(dev, DXGI_FORMAT_R32_TYPELESS, D3D11_BIND_DEPTH_STENCIL);
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> z;
    check(dev->CreateRenderTargetView(ids.Get(), nullptr, &target), "Create ID target");
    D3D11_DEPTH_STENCIL_VIEW_DESC ds{};
    ds.Format = DXGI_FORMAT_D32_FLOAT;
    ds.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    check(dev->CreateDepthStencilView(depth.Get(), &ds, &z), "Create depth view");
    ctx->OMSetRenderTargets(1, target.GetAddressOf(), z.Get());
    D3D11_VIEWPORT viewport{0, 0, side, side, 0, 1};
    ctx->RSSetViewports(1, &viewport);
    ctx->IASetInputLayout(layout.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(Vertex), offset = 0;
    ctx->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &offset);
    ctx->IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R16_UINT, 0);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->PSSetShader(ps.Get(), nullptr, 0);
    auto draw = [&] {
        const float clear[4] = {};
        ctx->ClearRenderTargetView(target.Get(), clear);
        ctx->ClearDepthStencilView(z.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        ctx->DrawIndexed(static_cast<UINT>(indices.size()), 0, 0);
    };
    for (int i = 0; i < 8; ++i)
        draw();
    std::vector<double> times;
    if (!smoke) {
        auto clock = query(dev, D3D11_QUERY_TIMESTAMP_DISJOINT),
             begin = query(dev, D3D11_QUERY_TIMESTAMP), end = query(dev, D3D11_QUERY_TIMESTAMP);
        for (int i = 0; i < 32; ++i) {
            ctx->Begin(clock.Get());
            ctx->End(begin.Get());
            for (int j = 0; j < 32; ++j)
                draw();
            ctx->End(end.Get());
            ctx->End(clock.Get());
            ctx->Flush();
            auto rate = readQuery<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT>(ctx, clock.Get());
            auto a = readQuery<UINT64>(ctx, begin.Get()), b = readQuery<UINT64>(ctx, end.Get());
            if (rate.Disjoint || !rate.Frequency || b <= a)
                throw std::runtime_error("Invalid indexed timing");
            times.push_back(1000. * static_cast<double>(b - a) / rate.Frequency);
        }
    }
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    saveAttachment(dev, ctx, ids.Get(), 1, folder / "ids.u8");
    saveAttachment(dev, ctx, depth.Get(), 4, folder / "depth.f32le");
    return times;
}
