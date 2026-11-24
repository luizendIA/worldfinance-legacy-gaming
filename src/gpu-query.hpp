// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lab.hpp"
#include <chrono>

inline ComPtr<ID3D11Query> query(ID3D11Device *device, D3D11_QUERY type) {
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
