// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include "mesh_shader.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <d3dcompiler.h>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <windows.h>

namespace polaris {
void check(HRESULT result, const char *operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " failed (HRESULT 0x" << std::hex << static_cast<uint32_t>(result)
                << ')';
        throw std::runtime_error(message.str());
    }
}

namespace {
constexpr DXGI_FORMAT idFormat = DXGI_FORMAT_R32_UINT;
constexpr DXGI_FORMAT depthFormat = DXGI_FORMAT_D32_FLOAT;
constexpr UINT vertexBytes = vertexCount * sizeof(Vertex);
constexpr UINT indexBytes = indexCount * sizeof(uint32_t);

ComPtr<ID3DBlob> compile(const char *entry, const char *profile) {
    ComPtr<ID3DBlob> shader, messages;
    const auto hr = D3DCompile(meshShader, sizeof(meshShader) - 1, "meshlet.hlsl", nullptr, nullptr,
                               entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &messages);
    if (FAILED(hr) && messages)
        throw std::runtime_error(static_cast<const char *>(messages->GetBufferPointer()));
    check(hr, "Compile shader");
    return shader;
}

ComPtr<ID3D12RootSignature> root(ID3D12Device *device, D3D12_ROOT_SIGNATURE_DESC desc) {
    ComPtr<ID3DBlob> data, messages;
    const auto hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &data, &messages);
    if (FAILED(hr) && messages)
        throw std::runtime_error(static_cast<const char *>(messages->GetBufferPointer()));
    check(hr, "Serialize root signature");
    ComPtr<ID3D12RootSignature> signature;
    check(device->CreateRootSignature(0, data->GetBufferPointer(), data->GetBufferSize(),
                                      IID_PPV_ARGS(&signature)),
          "Create root signature");
    return signature;
}

ComPtr<ID3D12Resource> allocate(ID3D12Device *device, const D3D12_RESOURCE_DESC &desc,
                                D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    ComPtr<ID3D12Resource> result;
    check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                          IID_PPV_ARGS(&result)),
          "Allocate GPU resource");
    return result;
}

ComPtr<ID3D12Resource> buffer(ID3D12Device *device, UINT64 size, D3D12_HEAP_TYPE heap,
                              D3D12_RESOURCE_STATES state,
                              D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return allocate(device, desc, heap, state);
}

template <class T, size_t N> ComPtr<ID3D12Resource> upload(ID3D12Device *device, const T (&input)[N]) {
    auto resource =
        buffer(device, sizeof(input), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    const D3D12_RANGE noRead{};
    void *data = nullptr;
    check(resource->Map(0, &noRead, &data), "Map input");
    std::memcpy(data, input, sizeof(input));
    resource->Unmap(0, nullptr);
    return resource;
}

struct Attachment {
    ComPtr<ID3D12Resource> image;
    ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_CPU_DESCRIPTOR_HANDLE handle{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes{};
};

Attachment attachment(ID3D12Device *device, bool depth) {
    Attachment result;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = depth ? depthFormat : idFormat;
    desc.Flags =
        depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    const auto state = depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
    result.image = allocate(device, desc, D3D12_HEAP_TYPE_DEFAULT, state);
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.NumDescriptors = 1;
    heap.Type = depth ? D3D12_DESCRIPTOR_HEAP_TYPE_DSV : D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&result.heap)),
          "Create attachment descriptor");
    result.handle = result.heap->GetCPUDescriptorHandleForHeapStart();
    if (depth)
        device->CreateDepthStencilView(result.image.Get(), nullptr, result.handle);
    else
        device->CreateRenderTargetView(result.image.Get(), nullptr, result.handle);
    device->GetCopyableFootprints(&desc, 0, 1, 0, &result.footprint, nullptr, nullptr, &result.bytes);
    return result;
}

void transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &barrier);
}

ComPtr<ID3D12Resource> readAttachment(ID3D12Device *device, ID3D12GraphicsCommandList *list,
                                      const Attachment &source, D3D12_RESOURCE_STATES before) {
    auto destination =
        buffer(device, source.bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    transition(list, source.image.Get(), before, D3D12_RESOURCE_STATE_COPY_SOURCE);
    const D3D12_TEXTURE_COPY_LOCATION from{source.image.Get(),
                                           D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION to{destination.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    to.PlacedFootprint = source.footprint;
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    return destination;
}

template <class T> std::vector<T> readRows(ID3D12Resource *resource, const Attachment &source) {
    std::vector<T> out(static_cast<size_t>(width) * height);
    const D3D12_RANGE read{0, static_cast<SIZE_T>(source.bytes)}, noWrite{};
    void *data = nullptr;
    check(resource->Map(0, &read, &data), "Map attachment readback");
    for (unsigned y = 0; y < height; ++y)
        std::memcpy(out.data() + size_t(y) * width,
                    static_cast<const char *>(data) + size_t(y) * source.footprint.Footprint.RowPitch,
                    size_t(width) * sizeof(T));
    resource->Unmap(0, &noWrite);
    return out;
}

ComPtr<ID3D12PipelineState> graphicsPipeline(ID3D12Device *device, ID3D12RootSignature *signature) {
    const auto vs = compile("vertex", "vs_5_1"), ps = compile("pixel", "ps_5_1");
    const D3D12_INPUT_ELEMENT_DESC position{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                            0,          0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                                            0};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = signature;
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.InputLayout = {&position, 1};
    auto &rt = desc.BlendState.RenderTarget[0];
    rt.SrcBlend = rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlend = rt.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt.BlendOp = rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.LogicOp = D3D12_LOGIC_OP_NOOP;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = idFormat;
    desc.DSVFormat = depthFormat;
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    check(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline)),
          "Create graphics pipeline");
    return pipeline;
}

void waitForQueue(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *list) {
    ComPtr<ID3D12Fence> fence;
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Create fence");
    std::unique_ptr<void, decltype(&CloseHandle)> event(CreateEventW(nullptr, FALSE, FALSE, nullptr),
                                                        &CloseHandle);
    if (!event)
        throw std::runtime_error("Could not create fence event");
    check(fence->SetEventOnCompletion(1, event.get()), "Set fence event");
    ID3D12CommandList *lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    check(queue->Signal(fence.Get(), 1), "Signal fence");
    if (WaitForSingleObject(event.get(), 15000) != WAIT_OBJECT_0)
        throw std::runtime_error("GPU fence wait failed or timed out");
    check(device->GetDeviceRemovedReason(), "Check device after draw");
}
} // namespace

Capture render(ID3D12Device *device, D3D12_SHADER_BYTECODE shader, bool culled) {
    constexpr auto uav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    constexpr auto writable = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto vertexBuffer = buffer(device, vertexBytes, D3D12_HEAP_TYPE_DEFAULT, uav, writable);
    auto indexBuffer = buffer(device, indexBytes, D3D12_HEAP_TYPE_DEFAULT, uav, writable);
    const D3D12_DRAW_INDEXED_ARGUMENTS draw[] = {{indexCount, 1, 0, 0, 0}};
    auto args = upload(device, draw);
    auto commandRead = buffer(device, vertexBytes + indexBytes, D3D12_HEAP_TYPE_READBACK,
                              D3D12_RESOURCE_STATE_COPY_DEST);
    auto id = attachment(device, false), depth = attachment(device, true);

    std::array<D3D12_ROOT_PARAMETER, 2> params{};
    for (UINT i = 0; i < params.size(); ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[i].Descriptor.ShaderRegister = i;
        params[i].Descriptor.RegisterSpace = 1;
    }
    D3D12_ROOT_SIGNATURE_DESC computeRootDesc{};
    computeRootDesc.NumParameters = static_cast<UINT>(params.size());
    computeRootDesc.pParameters = params.data();
    auto computeRoot = root(device, computeRootDesc);
    D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc{};
    computeDesc.pRootSignature = computeRoot.Get();
    computeDesc.CS = shader;
    ComPtr<ID3D12PipelineState> compute;
    check(device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&compute)),
          "Create translated compute pipeline");

    D3D12_ROOT_SIGNATURE_DESC graphicsRootDesc{};
    graphicsRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    auto graphicsRoot = root(device, graphicsRootDesc);
    auto graphics = graphicsPipeline(device, graphicsRoot.Get());
    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    D3D12_COMMAND_SIGNATURE_DESC signatureDesc{sizeof(draw[0]), 1, &argument, 0};
    ComPtr<ID3D12CommandSignature> signature;
    check(device->CreateCommandSignature(&signatureDesc, nullptr, IID_PPV_ARGS(&signature)),
          "Create indirect signature");

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "Create direct queue");
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
          "Create command allocator");
    check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&list)),
          "Create command list");

    list->SetPipelineState(compute.Get());
    list->SetComputeRootSignature(computeRoot.Get());
    list->SetComputeRootUnorderedAccessView(0, vertexBuffer->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(1, indexBuffer->GetGPUVirtualAddress());
    list->Dispatch(1, 1, 1);
    transition(list.Get(), vertexBuffer.Get(), uav, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    transition(list.Get(), indexBuffer.Get(), uav, D3D12_RESOURCE_STATE_INDEX_BUFFER);

    list->SetPipelineState(graphics.Get());
    list->SetGraphicsRootSignature(graphicsRoot.Get());
    const D3D12_VIEWPORT viewport{0, 0, width, height, 0, 1};
    const D3D12_RECT rectangle{0, 0, width, height};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &rectangle);
    const float black[4]{};
    list->ClearRenderTargetView(id.handle, black, 0, nullptr);
    list->ClearDepthStencilView(depth.handle, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
    list->OMSetRenderTargets(1, &id.handle, FALSE, &depth.handle);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VERTEX_BUFFER_VIEW vb{vertexBuffer->GetGPUVirtualAddress(), vertexBytes,
                                      sizeof(Vertex)};
    const D3D12_INDEX_BUFFER_VIEW ib{indexBuffer->GetGPUVirtualAddress(), indexBytes,
                                     DXGI_FORMAT_R32_UINT};
    list->IASetVertexBuffers(0, 1, &vb);
    list->IASetIndexBuffer(&ib);
    list->ExecuteIndirect(signature.Get(), 1, args.Get(), 0, nullptr, 0);

    transition(list.Get(), indexBuffer.Get(), D3D12_RESOURCE_STATE_INDEX_BUFFER,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(list.Get(), vertexBuffer.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(commandRead.Get(), 0, vertexBuffer.Get(), 0, vertexBytes);
    list->CopyBufferRegion(commandRead.Get(), vertexBytes, indexBuffer.Get(), 0, indexBytes);
    auto ids = readAttachment(device, list.Get(), id, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto depths = readAttachment(device, list.Get(), depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    check(list->Close(), "Close command list");
    waitForQueue(device, queue.Get(), list.Get());

    std::array<Vertex, vertexCount> expectedPositions{};
    std::array<uint32_t, indexCount> expectedIndices{};
    for (UINT i = 0; i < vertexCount; ++i)
        expectedPositions[i] = {(i & 1) ? 3.f : -1.f, (i & 2) ? 3.f : -1.f, .5f, 1.f};
    if (!culled) {
        for (UINT i = 0; i < indexCount; ++i)
            expectedIndices[i] = i % 3;
    }
    const D3D12_RANGE commands{0, vertexBytes + indexBytes}, noWrite{};
    void *commandBytes = nullptr;
    check(commandRead->Map(0, &commands, &commandBytes), "Map geometry buffers");
    const bool positionsMatch = std::memcmp(commandBytes, expectedPositions.data(), vertexBytes) == 0;
    const bool indicesMatch = std::memcmp(static_cast<const char *>(commandBytes) + vertexBytes,
                                          expectedIndices.data(), indexBytes) == 0;
    commandRead->Unmap(0, &noWrite);
    if (!positionsMatch || !indicesMatch)
        throw std::runtime_error(positionsMatch ? "Translated index buffer differs from CPU reference"
                                                : "Translated vertex buffer differs from CPU reference");
    return {readRows<uint32_t>(ids.Get(), id), readRows<float>(depths.Get(), depth)};
}
} // namespace polaris
