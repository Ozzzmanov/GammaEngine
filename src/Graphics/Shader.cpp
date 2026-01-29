//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Shader.cpp
// ================================================================================

#include "Shader.h"
#include "../Core/Logger.h"

Shader::Shader(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
}

bool Shader::Load(const std::wstring& filename) {
    ComPtr<ID3DBlob> psBlob;

    // Vertex Shader
    if (!CompileShader(filename, "VSMain", "vs_5_0", &m_vsBlob)) {
        return false;
    }
    HR_CHECK(m_device->CreateVertexShader(m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(), nullptr, m_vertexShader.GetAddressOf()), "Create Vertex Shader");

    // Pixel Shader
    if (!CompileShader(filename, "PSMain", "ps_5_0", &psBlob)) {
        return false;
    }
    HR_CHECK(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixelShader.GetAddressOf()), "Create Pixel Shader");

    // Input Layout (SimpleVertex)
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    HRESULT hr = m_device->CreateInputLayout(layout, ARRAYSIZE(layout), m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(), m_inputLayout.GetAddressOf());
    if (FAILED(hr)) {
        Logger::Warn(LogCategory::Render, "Default Layout mismatch. Shader might use custom layout.");
    }

    // Sampler State
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HR_CHECK(m_device->CreateSamplerState(&sampDesc, m_samplerState.GetAddressOf()), "Create Sampler State");

    return true;
}

void Shader::Bind() {
    if (m_inputLayout) {
        m_context->IASetInputLayout(m_inputLayout.Get());
    }
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
}

bool Shader::CompileShader(const std::wstring& filename, const std::string& entryPoint, const std::string& profile, ID3DBlob** shaderBlob) {
    ComPtr<ID3DBlob> errorBlob;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
#endif

    HRESULT hr = D3DCompileFromFile(
        filename.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint.c_str(), profile.c_str(),
        flags, 0, shaderBlob, errorBlob.GetAddressOf()
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            std::string err((char*)errorBlob->GetBufferPointer());
            Logger::Error(LogCategory::Render, "Shader Compile Error: " + err);
        }
        else {
            Logger::Error(LogCategory::Render, "Shader file not found: " + std::string(filename.begin(), filename.end()));
        }
        return false;
    }
    return true;
}