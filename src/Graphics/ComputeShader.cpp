//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// ComputeShader.cpp
// ================================================================================
#include "ComputeShader.h"
#include "../Core/Logger.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

ComputeShader::ComputeShader(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
}

bool ComputeShader::Load(const std::wstring& filename, const std::string& entryPoint) {
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errorBlob;

    DWORD flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
#endif

    // Профиль cs_5_0 для DX11
    HRESULT hr = D3DCompileFromFile(
        filename.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint.c_str(), "cs_5_0",
        flags, 0, blob.GetAddressOf(), errorBlob.GetAddressOf()
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            Logger::Error(LogCategory::Render, "Compute Shader Compile Error: " + std::string((char*)errorBlob->GetBufferPointer()));
        }
        else {
            // Конвертация wide string to string (простая) для лога
            std::string fileStr(filename.begin(), filename.end());
            Logger::Error(LogCategory::Render, "Compute Shader File Not Found: " + fileStr);
        }
        return false;
    }

    hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, m_shader.GetAddressOf());
    if (FAILED(hr)) {
        Logger::Error(LogCategory::Render, "Failed to create Compute Shader object.");
        return false;
    }

    return true;
}

void ComputeShader::Bind() {
    if (m_shader) {
        m_context->CSSetShader(m_shader.Get(), nullptr, 0);
    }
}

void ComputeShader::Dispatch(UINT x, UINT y, UINT z) {
    m_context->Dispatch(x, y, z);
}

void ComputeShader::Unbind() {
    m_context->CSSetShader(nullptr, nullptr, 0);
}