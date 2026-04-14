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

bool Shader::Load(const std::wstring& filename, const D3D_SHADER_MACRO* defines, bool isTreeShader, bool isWaterShader, bool useTessellation) {
    ComPtr<ID3DBlob> psBlob;

    if (!CompileShader(filename, "VSMain", "vs_5_0", defines, &m_vsBlob)) return false;
    HR_CHECK(m_device->CreateVertexShader(m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(), nullptr, m_vertexShader.GetAddressOf()), "Create Vertex Shader");

    if (!CompileShader(filename, "PSMain", "ps_5_0", defines, &psBlob)) return false;
    HR_CHECK(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixelShader.GetAddressOf()), "Create Pixel Shader");

    if (useTessellation) {
        ComPtr<ID3DBlob> hsBlob, dsBlob;
        if (!CompileShader(filename, "HSMain", "hs_5_0", defines, &hsBlob)) return false;
        HR_CHECK(m_device->CreateHullShader(hsBlob->GetBufferPointer(), hsBlob->GetBufferSize(), nullptr, m_hullShader.GetAddressOf()), "Create Hull Shader");

        if (!CompileShader(filename, "DSMain", "ds_5_0", defines, &dsBlob)) return false;
        HR_CHECK(m_device->CreateDomainShader(dsBlob->GetBufferPointer(), dsBlob->GetBufferSize(), nullptr, m_domainShader.GetAddressOf()), "Create Domain Shader");
    }

    // FIXME: Жесткий хардкод InputLayout на основе bool флагов. 
    // Нужно использовать D3DReflect для автоматической сборки Layout'а по сигнатуре шейдера.
    HRESULT hr = S_OK;
    if (isWaterShader) {
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 } //было R32G32, а отступ 12 байт на R32G32B32. Оставим пока твой вариант в будущем пофикситб.
        };
        hr = m_device->CreateInputLayout(layout, ARRAYSIZE(layout), m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(), m_inputLayout.GetAddressOf());
    }
    else if (isTreeShader) {
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION",        0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",          0, DXGI_FORMAT_R10G10B10A2_UNORM,  0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",        0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",           0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",           1, DXGI_FORMAT_R32_UINT,           0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "INSTANCE_OFFSET", 0, DXGI_FORMAT_R32_UINT,           1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1 }
        };
        hr = m_device->CreateInputLayout(layout, ARRAYSIZE(layout), m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(), m_inputLayout.GetAddressOf());
    }
    else {
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION",        0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",           0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",          0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",        0, DXGI_FORMAT_R32G32_FLOAT,       0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",         0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "INSTANCE_OFFSET", 0, DXGI_FORMAT_R32_UINT,           1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1 }
        };
        hr = m_device->CreateInputLayout(layout, ARRAYSIZE(layout), m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(), m_inputLayout.GetAddressOf());
    }

    if (FAILED(hr)) {
        GAMMA_LOG_WARN(LogCategory::Render, "Default Layout mismatch. Shader might use custom layout.");
    }

    // FIXME: Изолированный сэмплер. Каждый Shader объект владеет своим. Перенести в CommonStates.
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.MipLODBias = -0.5f;
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

    m_context->HSSetShader(m_hullShader.Get(), nullptr, 0);
    m_context->DSSetShader(m_domainShader.Get(), nullptr, 0);

    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
}

bool Shader::CompileShader(const std::wstring& filename, const std::string& entryPoint, const std::string& profile, const D3D_SHADER_MACRO* defines, ID3DBlob** shaderBlob) {
    ComPtr<ID3DBlob> errorBlob;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
#endif

    HRESULT hr = D3DCompileFromFile(
        filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint.c_str(), profile.c_str(),
        flags, 0, shaderBlob, errorBlob.GetAddressOf()
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            std::string err(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
            GAMMA_LOG_ERROR(LogCategory::Render, "Shader Compile Error: " + err);
        }
        else {
            std::string fileStr(filename.begin(), filename.end());
            GAMMA_LOG_ERROR(LogCategory::Render, "Shader file not found: " + fileStr);
        }
        return false;
    }
    return true;
}