#include <windows.h>
#include <oleauto.h>
#include <unknwn.h>
#include <dxcapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
void check(HRESULT hr, const char* message) {
    if (FAILED(hr)) throw std::runtime_error(message);
}
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc != 4) throw std::runtime_error("Usage: sign SDK_BIN INPUT.dxil OUTPUT.dxil");
        const std::filesystem::path sdk(argv[1]);
        auto compiler = LoadLibraryExW((sdk / L"dxcompiler.dll").c_str(), nullptr,
                                      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        auto validator = LoadLibraryExW((sdk / L"dxil.dll").c_str(), nullptr,
                                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!compiler || !validator) throw std::runtime_error("Could not load SDK DXIL components");
        const auto createLibrary = reinterpret_cast<DxcCreateInstanceProc>(
            GetProcAddress(compiler, "DxcCreateInstance"));
        const auto createValidator = reinterpret_cast<DxcCreateInstanceProc>(
            GetProcAddress(validator, "DxcCreateInstance"));
        if (!createLibrary || !createValidator) throw std::runtime_error("Missing DxcCreateInstance");
        ComPtr<IDxcLibrary> library;
        ComPtr<IDxcValidator> valid;
        check(createLibrary(CLSID_DxcLibrary, IID_PPV_ARGS(&library)), "Create DXC library");
        check(createValidator(CLSID_DxcValidator, IID_PPV_ARGS(&valid)), "Create DXIL validator");
        std::ifstream file(std::filesystem::path(argv[2]), std::ios::binary);
        if (!file) throw std::runtime_error("Cannot read shader");
        std::vector<char> data((std::istreambuf_iterator<char>(file)), {});
        ComPtr<IDxcBlobEncoding> blob;
        check(library->CreateBlobWithEncodingFromPinned(data.data(), static_cast<UINT32>(data.size()),
                                                       DXC_CP_ACP, &blob), "Create blob");
        ComPtr<IDxcOperationResult> result;
        check(valid->Validate(blob.Get(), DxcValidatorFlags_InPlaceEdit, &result), "Start validation");
        HRESULT status;
        check(result->GetStatus(&status), "Read status");
        if (FAILED(status)) {
            ComPtr<IDxcBlobEncoding> errors;
            if (SUCCEEDED(result->GetErrorBuffer(&errors)) && errors)
                std::cerr << static_cast<const char*>(errors->GetBufferPointer()) << '\n';
            throw std::runtime_error("DXIL validation failed");
        }
        std::ofstream out(std::filesystem::path(argv[3]), std::ios::binary);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out.write(data.data(), data.size());
        out.close();
        std::cout << "DXIL validated and signed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
