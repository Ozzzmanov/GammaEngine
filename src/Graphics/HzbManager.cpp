//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// HzbManager.cpp
// ================================================================================
#include "HzbManager.h"
#include "ComputeShader.h"
#include "../Graphics/DX11Renderer.h"
#include "../Core/Logger.h"
#include <cmath>
#include <algorithm>

HzbManager::~HzbManager() = default;

HzbManager::HzbManager(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context)
{
    m_csDownscale = std::make_unique<ComputeShader>(device, context);
}

void HzbManager::Initialize(int width, int height) {
    if (!m_csDownscale->Load(L"Assets/Shaders/HzbDownscale.hlsl", "CSMain")) {
        GAMMA_LOG_ERROR(LogCategory::Render, "Failed to load HzbDownscale shader!");
    }
    Resize(width, height);
}

void HzbManager::Cleanup() {
    m_hzbTexture.Reset();
    m_hzbSRV.Reset();
    m_hzbMipSRVs.clear();
    m_hzbMipUAVs.clear();
}

void HzbManager::Resize(int width, int height) {
    if (m_width == width && m_height == height) return;
    Cleanup();
    CreateResources(width, height);
}

void HzbManager::CreateResources(int width, int height) {
    m_width = width;
    m_height = height;

    // HZB всегда в 2 раза меньше экрана (Mip 0)
    int w = std::max(1, width / 2);
    int h = std::max(1, height / 2);

    m_hzbSize = DirectX::XMFLOAT2(static_cast<float>(w), static_cast<float>(h));
    m_mipCount = static_cast<int>(std::floor(std::log2(std::max(w, h)))) + 1;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = m_mipCount;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_FLOAT; // Для глубины достаточно одного канала
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, m_hzbTexture.GetAddressOf());
    HR_CHECK_VOID(hr, "Failed to create HZB Texture");

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = m_mipCount;
    srvDesc.Texture2D.MostDetailedMip = 0;
    HR_CHECK_VOID(m_device->CreateShaderResourceView(m_hzbTexture.Get(), &srvDesc, m_hzbSRV.GetAddressOf()), "Failed to create HZB Global SRV");

    for (int i = 0; i < m_mipCount; ++i) {
        // SRV конкретного мипа
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = i;
        ComPtr<ID3D11ShaderResourceView> mipSRV;
        m_device->CreateShaderResourceView(m_hzbTexture.Get(), &srvDesc, mipSRV.GetAddressOf());
        m_hzbMipSRVs.push_back(mipSRV);

        // UAV конкретного мипа
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = i;
        ComPtr<ID3D11UnorderedAccessView> mipUAV;
        m_device->CreateUnorderedAccessView(m_hzbTexture.Get(), &uavDesc, mipUAV.GetAddressOf());
        m_hzbMipUAVs.push_back(mipUAV);
    }

    GAMMA_LOG_INFO(LogCategory::Render, "HZB Rebuilt: " + std::to_string(w) + "x" + std::to_string(h) + ", Mips: " + std::to_string(m_mipCount));
}

void HzbManager::Generate(DX11Renderer* renderer) {
    if (!m_csDownscale) return;

    // Жестко отвязываем HZB SRV, который мог остаться привязанным 
    // после этапа куллинга в GpuCullingSystem.
    ID3D11ShaderResourceView* nullSRVs[8] = { nullptr };
    m_context->CSSetShaderResources(0, 8, nullSRVs);

    m_csDownscale->Bind();

    // ПРОХОД 1: Scene Depth -> HZB Mip 0
    ID3D11ShaderResourceView* depthSRV = renderer->GetDepthSRV();
    if (!depthSRV) return;

    ID3D11UnorderedAccessView* mip0UAV = m_hzbMipUAVs[0].Get();

    m_context->CSSetShaderResources(0, 1, &depthSRV);
    renderer->BindUAVs(0, 1, &mip0UAV);

    int currentW = m_width;
    int currentH = m_height;

    UINT groupsX = static_cast<UINT>(std::ceil((currentW / 2.0f) / 8.0f));
    UINT groupsY = static_cast<UINT>(std::ceil((currentH / 2.0f) / 8.0f));
    groupsX = std::max(1u, groupsX);
    groupsY = std::max(1u, groupsY);

    m_csDownscale->Dispatch(groupsX, groupsY, 1);

    // Строгая отвязка (Hazard Prevention)
    renderer->UnbindUAVs(0, 1);
    ID3D11ShaderResourceView* nullSRV = nullptr;
    m_context->CSSetShaderResources(0, 1, &nullSRV);

    // ПРОХОД 2..N (Построение пирамиды)
    currentW /= 2;
    currentH /= 2;

    for (int i = 1; i < m_mipCount; ++i) {
        ID3D11ShaderResourceView* inputSRV = m_hzbMipSRVs[i - 1].Get();
        ID3D11UnorderedAccessView* outputUAV = m_hzbMipUAVs[i].Get();

        m_context->CSSetShaderResources(0, 1, &inputSRV);
        renderer->BindUAVs(0, 1, &outputUAV);

        int nextW = std::max(1, currentW / 2);
        int nextH = std::max(1, currentH / 2);

        groupsX = static_cast<UINT>(std::ceil(static_cast<float>(nextW) / 8.0f));
        groupsY = static_cast<UINT>(std::ceil(static_cast<float>(nextH) / 8.0f));
        groupsX = std::max(1u, groupsX);
        groupsY = std::max(1u, groupsY);

        m_csDownscale->Dispatch(groupsX, groupsY, 1);

        // Строгая отвязка перед следующей итерацией
        renderer->UnbindUAVs(0, 1);
        m_context->CSSetShaderResources(0, 1, &nullSRV);

        currentW = nextW;
        currentH = nextH;
    }

    m_csDownscale->Unbind();
}