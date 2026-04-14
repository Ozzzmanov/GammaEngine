//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// LightManager.cpp
// ================================================================================
#include "LightManager.h"
#include "../Core/Logger.h"
#include <algorithm>

void LightManager::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    m_device = device;
    m_context = context;

    // Создаем буфер для Point Lights
    D3D11_BUFFER_DESC desc = {};
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(GPUPointLight);
    desc.ByteWidth = sizeof(GPUPointLight) * m_maxPointLights;

    HRESULT hr = m_device->CreateBuffer(&desc, nullptr, m_pointLightsBuffer.GetAddressOf());
    HR_CHECK_VOID(hr, "Failed to create PointLight buffer.");

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = m_maxPointLights;

    hr = m_device->CreateShaderResourceView(m_pointLightsBuffer.Get(), &srvDesc, m_pointLightsSRV.GetAddressOf());
    HR_CHECK_VOID(hr, "Failed to create PointLight SRV.");

    // Создаем буфер для Spot Lights
    desc.StructureByteStride = sizeof(GPUSpotLight);
    desc.ByteWidth = sizeof(GPUSpotLight) * m_maxSpotLights;

    hr = m_device->CreateBuffer(&desc, nullptr, m_spotLightsBuffer.GetAddressOf());
    HR_CHECK_VOID(hr, "Failed to create SpotLight buffer.");

    srvDesc.Buffer.NumElements = m_maxSpotLights;
    hr = m_device->CreateShaderResourceView(m_spotLightsBuffer.Get(), &srvDesc, m_spotLightsSRV.GetAddressOf());
    HR_CHECK_VOID(hr, "Failed to create SpotLight SRV.");

    GAMMA_LOG_INFO(LogCategory::Render, "LightManager initialized.");
}

void LightManager::ClearStaticLights() {
    m_staticPointLights.clear();
    m_staticSpotLights.clear();
}

void LightManager::AddStaticPointLight(const GPUPointLight& light) {
    m_staticPointLights.push_back(light);
}

void LightManager::AddStaticSpotLight(const GPUSpotLight& light) {
    m_staticSpotLights.push_back(light);
}

void LightManager::UpdateGpuBuffers() {
    if (!m_context) return;

    // В будущем здесь мы будем объединять статичный свет (m_staticPointLights) 
    // с динамическим из GammaState (от скриптов и сущностей). 
    // Пока пушим только статику.

    // POINT LIGHTS
    m_pointLightCount = static_cast<uint32_t>(m_staticPointLights.size());
    if (m_pointLightCount > m_maxPointLights) {
        GAMMA_LOG_WARN(LogCategory::Render, "Exceeded Max Point Lights (" + std::to_string(m_maxPointLights) + "). Truncating.");
        m_pointLightCount = m_maxPointLights;
    }

    // Мы ДОЛЖНЫ делать Discard, 
    // даже если источников стало 0, чтобы очистить старые "призрачные" данные из видеопамяти.
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_pointLightsBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        if (m_pointLightCount > 0) {
            memcpy(mapped.pData, m_staticPointLights.data(), sizeof(GPUPointLight) * m_pointLightCount);
        }
        m_context->Unmap(m_pointLightsBuffer.Get(), 0);
    }

    //  SPOT LIGHTS 
    m_spotLightCount = static_cast<uint32_t>(m_staticSpotLights.size());
    if (m_spotLightCount > m_maxSpotLights) {
        GAMMA_LOG_WARN(LogCategory::Render, "Exceeded Max Spot Lights (" + std::to_string(m_maxSpotLights) + "). Truncating.");
        m_spotLightCount = m_maxSpotLights;
    }

    if (SUCCEEDED(m_context->Map(m_spotLightsBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        if (m_spotLightCount > 0) {
            memcpy(mapped.pData, m_staticSpotLights.data(), sizeof(GPUSpotLight) * m_spotLightCount);
        }
        m_context->Unmap(m_spotLightsBuffer.Get(), 0);
    }
}