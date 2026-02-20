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
#include "ComputeShader.h"
#include "../Core/Logger.h"
#include "../Config/EngineConfig.h"
#include <cmath>
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
    if (!m_cullingShader->Load(L"Assets/Shaders/InstanceCulling.hlsl")) {
        Logger::Error(LogCategory::Render, "Failed to load InstanceCulling shader");
    }

    D3D11_BUFFER_DESC desc = {};
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.ByteWidth = sizeof(CullingParams); // Строго 288
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    m_device->CreateBuffer(&desc, nullptr, m_cullingParamsCB.GetAddressOf());

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    m_device->CreateSamplerState(&sampDesc, m_pointClampSampler.GetAddressOf());
}

void GpuCullingSystem::UpdateFrameConstants(const XMMATRIX& view, const XMMATRIX& proj, const XMMATRIX& prevView, const XMMATRIX& prevProj, const BoundingFrustum& frustum, const XMFLOAT3& cameraPos, bool enableLODs, bool enableFrustum, bool enableSize, bool enableOcclusion, float screenHeight, XMFLOAT2 hzbSize, float renderDistance) {

    XMStoreFloat4x4(&m_cachedFrameData.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&m_cachedFrameData.Projection, XMMatrixTranspose(proj));

    XMStoreFloat4x4(&m_cachedFrameData.PrevView, XMMatrixTranspose(prevView));
    XMStoreFloat4x4(&m_cachedFrameData.PrevProjection, XMMatrixTranspose(prevProj));

    XMVECTOR planes[6];
    frustum.GetPlanes(&planes[0], &planes[1], &planes[2], &planes[3], &planes[4], &planes[5]);
    for (int i = 0; i < 6; ++i) XMStoreFloat4(&m_cachedFrameData.FrustumPlanes[i], planes[i]);

    m_cachedFrameData.CameraPos = cameraPos;

    const auto& cfg = EngineConfig::Get();


    // LOD1 и LOD2 всегда жестко фиксированы из конфига,
    //    чтобы объекты не меняли геометрию при смене общей дальности прорисовки
    m_cachedFrameData.LODDist1Sq = cfg.Lod.Dist1 * cfg.Lod.Dist1;
    m_cachedFrameData.LODDist2Sq = cfg.Lod.Dist2 * cfg.Lod.Dist2;

    // LOD3 (Исчезновение). Берем минимальное значение между дистанцией чанков и лимитом из конфига.
    //    Уменьшаем (Z): объекты режутся по renderDistance вместе с землей.
    //    - Увеличиваем (X): объекты режутся по cfg.Lod.Dist3, чтобы не перегружать GPU.
    float finalObjectDist = std::min(renderDistance, cfg.Lod.Dist3);
    m_cachedFrameData.LODDist3Sq = finalObjectDist * finalObjectDist;

    m_cachedFrameData.EnableLODs = enableLODs ? 1 : 0;
    m_cachedFrameData.EnableFrustum = enableFrustum ? 1 : 0;
    m_cachedFrameData.EnableOcclusion = enableOcclusion ? 1 : 0;
    m_cachedFrameData.EnableSizeCulling = enableSize ? 1 : 0;

    m_cachedFrameData.MinPixelSizeSq = cfg.MinPixelSize * cfg.MinPixelSize;
    m_cachedFrameData.ScreenHeight = screenHeight;
    m_cachedFrameData.HZBSize = hzbSize;
}

void GpuCullingSystem::PerformCulling(
    ID3D11ShaderResourceView* instancesSRV,
    ID3D11ShaderResourceView* metaDataSRV,
    ID3D11ShaderResourceView* hzbSRV,
    ID3D11UnorderedAccessView* visibleIndicesUAV,
    ID3D11UnorderedAccessView* indirectArgsUAV,
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

    ID3D11ShaderResourceView* srvs[] = { instancesSRV, metaDataSRV, hzbSRV };
    m_context->CSSetShaderResources(0, 3, srvs);

    UINT initCounts[2] = { (UINT)-1, (UINT)-1 };
    ID3D11UnorderedAccessView* uavs[] = { visibleIndicesUAV, indirectArgsUAV };
    m_context->CSSetUnorderedAccessViews(0, 2, uavs, initCounts);

    UINT groups = (UINT)std::ceil((float)numInstances / 64.0f);
    m_cullingShader->Dispatch(groups, 1, 1);

    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr };
    m_context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr };
    m_context->CSSetShaderResources(0, 3, nullSRVs);

    m_cullingShader->Unbind();
}