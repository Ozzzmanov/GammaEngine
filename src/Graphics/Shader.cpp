//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
#include "Shader.h"
#include "../Core/Logger.h"

Shader::Shader(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context)
{
}

bool Shader::Load(const std::wstring& filename) {
    ComPtr<ID3DBlob> vsBlob, psBlob;

    // 1. Компиляция Vertex Shader
    if (!CompileShader(filename, "VSMain", "vs_5_0", &vsBlob)) {
        Logger::Error("Failed to compile Vertex Shader.");
        return false;
    }
    HRESULT hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertexShader.GetAddressOf());
    HR_CHECK(hr, "Failed to create Vertex Shader");

    // 2. Компиляция Pixel Shader
    if (!CompileShader(filename, "PSMain", "ps_5_0", &psBlob)) {
        Logger::Error("Failed to compile Pixel Shader.");
        return false;
    }
    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixelShader.GetAddressOf());
    HR_CHECK(hr, "Failed to create Pixel Shader");

    // 3. Создание Input Layout (Описание формата вершин для GPU)
    // Должно совпадать со структурой SimpleVertex в Prerequisites.h
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = m_device->CreateInputLayout(layout, ARRAYSIZE(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.GetAddressOf());
    HR_CHECK(hr, "Failed to create Input Layout");

    // 4. Создание Sampler State (Настройка фильтрации текстур)
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; // Линейная фильтрация (сглаживание)
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;    // Тайлинг (повторение) текстур
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = m_device->CreateSamplerState(&sampDesc, m_samplerState.GetAddressOf());
    HR_CHECK(hr, "Failed to create Sampler State");

    Logger::Info("Shader Loaded Successfully.");
    return true;
}

void Shader::Bind() {
    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
}

bool Shader::CompileShader(const std::wstring& filename, const std::string& entryPoint, const std::string& profile, ID3DBlob** shaderBlob) {
    ComPtr<ID3DBlob> errorBlob;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG; // Включаем отладку шейдеров в Debug режиме
#endif

    HRESULT hr = D3DCompileFromFile(
        filename.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint.c_str(),
        profile.c_str(),
        flags,
        0,
        shaderBlob,
        errorBlob.GetAddressOf()
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            std::string err((char*)errorBlob->GetBufferPointer());
            Logger::Error("Shader Compile Error: " + err);
        }
        else {
            Logger::Error("Shader Compile Error (Unknown). Check filename.");
        }
        return false;
    }
    return true;
}