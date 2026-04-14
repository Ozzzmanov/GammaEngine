//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GpuCullingSystem.cpp
// ================================================================================
#include "GpuCullingSystem.h"
#include "../Graphics/ComputeShader.h"
#include "../Core/Logger.h"
#include <algorithm>

using namespace DirectX;

GpuCullingSystem::~GpuCullingSystem() = default;

GpuCullingSystem::GpuCullingSystem(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context)
{
    m_cullingShader = std::make_unique<ComputeShader>(device, context);
    ZeroMemory(&m_cachedFrameData, sizeof(CullingParams));
}

void GpuCullingSystem::Initialize() {
    if (!m_cullingShader->Load(L"Assets/Shaders/InstanceCulling.hlsl", "CSMain")) {
        GAMMA_LOG_ERROR(LogCategory::Render, "Failed to load InstanceCulling shader");
    }

    D3D11_BUFFER_DESC desc = {};
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.ByteWidth = sizeof(CullingParams);

    if (desc.ByteWidth % 16 != 0) {
        desc.ByteWidth += 16 - (desc.ByteWidth % 16);
    }

    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_device->CreateBuffer(&desc, nullptr, m_cullingParamsCB.GetAddressOf());
    HR_CHECK_VOID(hr, "Failed to create Culling Params Constant Buffer");

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = m_device->CreateSamplerState(&sampDesc, m_pointClampSampler.GetAddressOf());
    HR_CHECK_VOID(hr, "Failed to create HZB Point Clamp Sampler");
}

void GpuCullingSystem::UpdateFrameConstants(
    const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj,
    const DirectX::XMMATRIX& prevView, const DirectX::XMMATRIX& prevProj,
    const DirectX::BoundingFrustum& frustum, const DirectX::XMFLOAT3& cameraPos,
    const LodSettings& lodSettings, bool enableLODs,
    bool enableFrustum, bool enableSize, bool enableOcclusion,
    float screenHeight, DirectX::XMFLOAT2 hzbSize, float renderDistance,
    float minDistance, float maxDistance,
    bool isShadowPass, bool enableShadowSizeCulling,
    float shadowSizeCullingDist, float minShadowSize,
    bool enableShadowFrustumCulling, const DirectX::XMFLOAT4* shadowPlanes)
{
    DirectX::XMStoreFloat4x4(&m_cachedFrameData.View, DirectX::XMMatrixTranspose(view));
    DirectX::XMStoreFloat4x4(&m_cachedFrameData.Projection, DirectX::XMMatrixTranspose(proj));
    DirectX::XMStoreFloat4x4(&m_cachedFrameData.PrevView, DirectX::XMMatrixTranspose(prevView));
    DirectX::XMStoreFloat4x4(&m_cachedFrameData.PrevProjection, DirectX::XMMatrixTranspose(prevProj));

    DirectX::XMVECTOR planes[6];
    frustum.GetPlanes(&planes[0], &planes[1], &planes[2], &planes[3], &planes[4], &planes[5]);
    for (int i = 0; i < 6; ++i) {
        DirectX::XMStoreFloat4(&m_cachedFrameData.FrustumPlanes[i], planes[i]);
    }

    m_cachedFrameData.CameraPos = cameraPos;
    m_cachedFrameData.LODDist1Sq = lodSettings.Dist1 * lodSettings.Dist1;
    m_cachedFrameData.LODDist2Sq = lodSettings.Dist2 * lodSettings.Dist2;
    m_cachedFrameData.LODDist3Sq = std::min(renderDistance, lodSettings.Dist3) * std::min(renderDistance, lodSettings.Dist3);

    m_cachedFrameData.EnableLODs = enableLODs ? 1 : 0;
    m_cachedFrameData.EnableFrustum = enableFrustum ? 1 : 0;
    m_cachedFrameData.EnableOcclusion = enableOcclusion ? 1 : 0;
    m_cachedFrameData.EnableSizeCulling = enableSize ? 1 : 0;

    float minPx = EngineConfig::Get().GetActiveProfile().Optimization.MinPixelSize;
    m_cachedFrameData.MinPixelSizeSq = minPx * minPx;
    m_cachedFrameData.ScreenHeight = screenHeight;
    m_cachedFrameData.HZBSize = hzbSize;

    m_cachedFrameData.MinDistanceSq = minDistance * minDistance;
    m_cachedFrameData.MaxDistanceSq = maxDistance * maxDistance;

    m_cachedFrameData.IsShadowPass = isShadowPass ? 1 : 0;
    m_cachedFrameData.EnableShadowSizeCulling = enableShadowSizeCulling ? 1 : 0;
    m_cachedFrameData.ShadowSizeCullingDistSq = shadowSizeCullingDist * shadowSizeCullingDist;
    m_cachedFrameData.MinShadowSize = minShadowSize;
    m_cachedFrameData.EnableShadowFrustumCulling = enableShadowFrustumCulling ? 1 : 0;

    if (shadowPlanes) {
        for (int i = 0; i < 18; ++i) {
            m_cachedFrameData.ShadowPlanes[i] = shadowPlanes[i];
        }
    }
}

void GpuCullingSystem::PerformCulling(
    ID3D11ShaderResourceView* instancesSRV,
    ID3D11ShaderResourceView* metaDataSRV,
    ID3D11ShaderResourceView* hzbSRV,
    ID3D11ShaderResourceView* batchInfoSRV,
    ID3D11UnorderedAccessView** visibleIndicesUAVs,
    ID3D11UnorderedAccessView** indirectArgsUAVs,
    int numUAVs,
    uint32_t numInstances)
{
    if (numInstances == 0) return;

    m_cachedFrameData.NumInstances = numInstances;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_cullingParamsCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &m_cachedFrameData, sizeof(CullingParams));
        m_context->Unmap(m_cullingParamsCB.Get(), 0);
    }

    m_cullingShader->Bind();
    m_context->CSSetConstantBuffers(0, 1, m_cullingParamsCB.GetAddressOf());

    if (m_pointClampSampler) {
        m_context->CSSetSamplers(0, 1, m_pointClampSampler.GetAddressOf());
    }

    // 4 СЛОТА (t0, t1, t2, t3)
    ID3D11ShaderResourceView* srvs[4] = { instancesSRV, metaDataSRV, hzbSRV, batchInfoSRV };
    m_context->CSSetShaderResources(0, 4, srvs);

    ID3D11UnorderedAccessView* uavs[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    UINT initCounts[6] = { (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1 };

    for (int i = 0; i < numUAVs; ++i) {
        uavs[i] = visibleIndicesUAVs[i];
        uavs[i + 3] = indirectArgsUAVs[i];
    }

    m_context->CSSetUnorderedAccessViews(0, 6, uavs, initCounts);

    UINT groups = (numInstances + 63) / 64;
    m_cullingShader->Dispatch(groups, 1, 1);

    ID3D11UnorderedAccessView* nullUAVs[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    m_context->CSSetUnorderedAccessViews(0, 6, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[4] = { nullptr, nullptr, nullptr, nullptr };
    m_context->CSSetShaderResources(0, 4, nullSRVs);

    m_cullingShader->Unbind();
}