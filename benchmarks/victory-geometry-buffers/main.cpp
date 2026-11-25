// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include "generated_dxil.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <windows.h>

namespace {
using namespace polaris;
constexpr double tolerance = 1e-6;

ComPtr<IDXGIAdapter1> adapter(bool warp) {
    ComPtr<IDXGIFactory4> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Create DXGI factory");
    ComPtr<IDXGIAdapter1> result;
    if (warp) {
        check(factory->EnumWarpAdapter(IID_PPV_ARGS(&result)), "Find software adapter");
        return result;
    }
    for (UINT index = 0;; ++index) {
        auto hr = factory->EnumAdapters1(index, &result);
        if (hr == DXGI_ERROR_NOT_FOUND)
            throw std::runtime_error("No D3D12 hardware adapter found");
        check(hr, "Enumerate DXGI adapter");
        DXGI_ADAPTER_DESC1 info{};
        check(result->GetDesc1(&info), "Read adapter descriptor");
        if (!(info.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            SUCCEEDED(D3D12CreateDevice(result.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                        nullptr)))
            return result;
        result.Reset();
    }
}

std::string name(const wchar_t *description) {
    int chars = WideCharToMultiByte(CP_UTF8, 0, description, -1, nullptr, 0, nullptr, nullptr);
    if (chars < 1)
        throw std::runtime_error("Cannot encode adapter description");
    std::string result(chars, '\0');
    WideCharToMultiByte(CP_UTF8, 0, description, -1, result.data(), chars, nullptr, nullptr);
    result.pop_back();
    return result;
}

unsigned shaderModel(ID3D12Device *device) {
    for (int model : {0x69, 0x68, 0x67, 0x66, 0x65, 0x64, 0x63, 0x62, 0x61, 0x60, 0x51}) {
        D3D12_FEATURE_DATA_SHADER_MODEL info{static_cast<D3D_SHADER_MODEL>(model)};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &info, sizeof(info))))
            return static_cast<unsigned>(info.HighestShaderModel);
    }
    return 0;
}

struct Result {
    size_t pixels{}, wrongIds{}, wrongDepth{};
    double maximumDepthError{};
};

Result compare(const Capture &image, bool culled) {
    Result result;
    result.pixels = image.ids.size();
    const uint32_t id = culled ? 0 : 1;
    const double depth = culled ? 1.0 : .5;
    for (size_t i = 0; i < image.ids.size(); ++i) {
        result.wrongIds += image.ids[i] != id;
        if (!std::isfinite(image.depth[i]))
            throw std::runtime_error("Nonfinite depth in readback");
        const double error = std::abs(image.depth[i] - depth);
        result.wrongDepth += error > tolerance;
        result.maximumDepthError = std::max(result.maximumDepthError, error);
    }
    return result;
}

void printCase(std::ostream &json, const Result &r) {
    json << R"({"pixels":)" << r.pixels << R"(,"id_mismatches":)" << r.wrongIds
         << R"(,"depth_mismatches":)" << r.wrongDepth << R"(,"max_abs_depth_error":)"
         << r.maximumDepthError << '}';
}

std::wstring wide(const std::string &utf8) {
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0)
        return L"Erro no teste gráfico.";
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), size);
    return result;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    const bool dialog = argc == 1;
    try {
        bool warp = false;
        for (int i = 1; i < argc; ++i) {
            const std::wstring option(argv[i]);
            if (option == L"--warp")
                warp = true;
            else if (option != L"--cli")
                throw std::runtime_error("Usage: polaris-720p [--cli] [--warp]");
        }
        auto selected = adapter(warp);
        DXGI_ADAPTER_DESC1 info{};
        check(selected->GetDesc1(&info), "Read adapter identity");
        ComPtr<ID3D12Device> device;
        check(D3D12CreateDevice(selected.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)),
              "Create device");
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options{};
        const auto mesh = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options,
                                                                sizeof(options)))
                              ? std::to_string(options.MeshShaderTier)
                              : "null";
        const auto model = shaderModel(device.Get());
        if (model < D3D_SHADER_MODEL_6_5)
            throw std::runtime_error("Este teste requer Direct3D 12 Shader Model 6.5.");
        const auto visible =
            compare(render(device.Get(), {visibleDxil, sizeof(visibleDxil)}, false), false);
        const auto culled = compare(render(device.Get(), {culledDxil, sizeof(culledDxil)}, true), true);
        const bool ok =
            !(visible.wrongIds || visible.wrongDepth || culled.wrongIds || culled.wrongDepth);
        std::ostringstream json;
        json << std::setprecision(12) << R"({"workload":"victory-geometry-capture-v2","adapter":)"
             << std::quoted(name(info.Description)) << R"(,"software":)" << (warp ? "true" : "false")
             << R"(,"shader_model":)"
             << std::quoted(std::to_string(model >> 4) + "." + std::to_string(model & 15))
             << R"(,"mesh_shader_tier":)" << mesh << R"(,"width":)" << width << R"(,"height":)" << height
             << R"(,"vertices":)" << vertexCount << R"(,"indices":)" << indexCount << R"(,"visible":)";
        printCase(json, visible);
        json << R"(,"culled":)";
        printCase(json, culled);
        json << "}\n";
        std::cout << json.str() << std::flush;
        if (dialog) {
            std::wstring summary =
                ok ? L"Teste gráfico aprovado em 1280 × 720.\n" : L"Falha na comparação gráfica.\n";
            summary += info.Description;
            summary += L"\n\nO tradutor gravou vértices e índices no GPU.";
            if (ok)
                summary += L" A imagem bateu com a referência da CPU.";
            summary += L"\nCenário sintético. GTA VI ainda não foi executado.";
            MessageBoxW(nullptr, summary.c_str(), L"WordFinance · VIctory Polaris",
                        MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
        }
        return ok ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        if (dialog)
            MessageBoxW(nullptr, wide(error.what()).c_str(), L"WordFinance · Falha no teste",
                        MB_OK | MB_ICONERROR);
        return 1;
    }
}
