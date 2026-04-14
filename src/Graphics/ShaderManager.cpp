//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// ShaderManager.cpp
// ================================================================================
#include "ShaderManager.h"
#include "../Core/Logger.h"
#include <algorithm>

#pragma comment(lib, "d3dcompiler.lib")

void ShaderManager::Initialize(ID3D11Device* device) {
    m_device = device;
}

ID3D11VertexShader* ShaderManager::GetVertexShader(const std::string& path, ID3DBlob** outBlob) {
    if (m_vsCache.count(path)) {
        if (outBlob) *outBlob = m_vsBlobCache[path].Get();
        return m_vsCache[path].Get();
    }

    ComPtr<ID3DBlob> blob, error;
    std::wstring wpath(path.begin(), path.end());

    // FIXME: Компиляция в рантайме! Должно быть чтение D3DReadFileToBlob для .cso файлов.
    HRESULT hr = D3DCompileFromFile(wpath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS", "vs_5_0", 0, 0, &blob, &error);

    if (FAILED(hr)) {
        if (error) GAMMA_LOG_ERROR(LogCategory::Render, reinterpret_cast<char*>(error->GetBufferPointer()));
        return nullptr;
    }

    ComPtr<ID3D11VertexShader> vs;
    m_device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs);

    m_vsCache[path] = vs;
    m_vsBlobCache[path] = blob;

    if (outBlob) *outBlob = blob.Get();
    return vs.Get();
}

ID3D11PixelShader* ShaderManager::GetPixelShader(const std::string& path, const std::vector<std::string>& defines) {
    std::string key = path;
    std::vector<D3D_SHADER_MACRO> macros;

    for (const auto& def : defines) {
        key += "|" + def;
        macros.push_back({ def.c_str(), "1" });
    }
    macros.push_back({ NULL, NULL }); // Terminator

    if (m_psCache.count(key)) return m_psCache[key].Get();

    ComPtr<ID3DBlob> blob, error;
    std::wstring wpath(path.begin(), path.end());

    HRESULT hr = D3DCompileFromFile(wpath.c_str(), macros.data(), D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PS", "ps_5_0", 0, 0, &blob, &error);

    if (FAILED(hr)) {
        if (error) {
            GAMMA_LOG_ERROR(LogCategory::Render, "PS Compile Error: " + std::string(reinterpret_cast<char*>(error->GetBufferPointer())));
        }
        else {
            GAMMA_LOG_ERROR(LogCategory::Render, "PS File not found: " + path);
        }
        return nullptr;
    }

    ComPtr<ID3D11PixelShader> ps;
    m_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ps);

    m_psCache[key] = ps;
    return ps.Get();
}