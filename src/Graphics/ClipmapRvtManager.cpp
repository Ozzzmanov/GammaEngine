//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// ClipmapRvtManager.cpp
// ================================================================================
#include "ClipmapRvtManager.h"
#include "../Core/Logger.h"
#include <cmath>
#include <algorithm>
#include "../Config/EngineConfig.h"

ClipmapRvtManager::ClipmapRvtManager(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context), m_resolution(0), m_numCascades(0) {
}

ClipmapRvtManager::~ClipmapRvtManager() {}

bool ClipmapRvtManager::Initialize() {
    const auto& tlod = EngineConfig::Get().GetActiveProfile().TerrainLod;

    m_resolution = tlod.RVTResolution;
    m_numCascades = std::clamp(tlod.RVTCascadeCount, 1, 6);

    float coverages[6] = {
        tlod.RVTCascade0Coverage,
        tlod.RVTCascade1Coverage,
        tlod.RVTCascade2Coverage,
        tlod.RVTCascade3Coverage,
        tlod.RVTCascade3Coverage * 2.0f,
        tlod.RVTCascade3Coverage * 4.0f
    };

    float renderDist = EngineConfig::Get().GetActiveProfile().Graphics.RenderDistance;
    float requiredCoverage = renderDist * 2.1f;
    if (coverages[m_numCascades - 1] < requiredCoverage) {
        GAMMA_LOG_WARN(LogCategory::Render,
            "RVT: Last cascade coverage " +
            std::to_string((int)coverages[m_numCascades - 1]) +
            "m < required " + std::to_string((int)requiredCoverage) +
            "m for RenderDistance " + std::to_string((int)renderDist) +
            "m. Checkerboard artifacts possible!");
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = desc.Height = m_resolution;
    desc.MipLevels = 1;
    desc.ArraySize = m_numCascades;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    m_device->CreateTexture2D(&desc, nullptr, m_albedoArray.GetAddressOf());
    m_device->CreateTexture2D(&desc, nullptr, m_normalArray.GetAddressOf());

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.ArraySize = m_numCascades;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    m_device->CreateShaderResourceView(m_albedoArray.Get(), &srvDesc, m_albedoArraySRV.GetAddressOf());
    m_device->CreateShaderResourceView(m_normalArray.Get(), &srvDesc, m_normalArraySRV.GetAddressOf());

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = desc.Format;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
    uavDesc.Texture2DArray.MipSlice = 0;
    uavDesc.Texture2DArray.FirstArraySlice = 0;
    uavDesc.Texture2DArray.ArraySize = m_numCascades;
    m_device->CreateUnorderedAccessView(m_albedoArray.Get(), &uavDesc, m_albedoArrayUAV.GetAddressOf());
    m_device->CreateUnorderedAccessView(m_normalArray.Get(), &uavDesc, m_normalArrayUAV.GetAddressOf());

    float gray[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
    float normal[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
    m_context->ClearUnorderedAccessViewFloat(m_albedoArrayUAV.Get(), gray);
    m_context->ClearUnorderedAccessViewFloat(m_normalArrayUAV.Get(), normal);

    m_cascades.resize(m_numCascades);
    for (int i = 0; i < m_numCascades; ++i) {
        m_cascades[i].WorldCoverage = coverages[i];
        m_cascades[i].TexelSize = coverages[i] / static_cast<float>(m_resolution);
        m_cascades[i].SnappedGridX = 0;
        m_cascades[i].SnappedGridZ = 0;
        m_cascades[i].IsFirstFrame = true;

        m_cbData.CascadeScales[i] = {
            1.0f / coverages[i],
            1.0f / coverages[i],
            coverages[i],
            m_cascades[i].TexelSize
        };

        float transitionEnd = coverages[i] * 0.5f;
        float transitionStart = transitionEnd * 0.90f;
        m_cbData.CascadeDistances[i] = { transitionStart, transitionEnd, 0.0f, 0.0f };
    }

    m_cbData.NumCascades = m_numCascades;
    m_cbData.Resolution = m_resolution;

    float nearDist = tlod.RVTNearBlendDistance;
    float fadeRange = std::max(5.0f, nearDist * 0.5f);

    m_cbData.NearBlendStart = nearDist;
    m_cbData.NearBlendInvFade = 1.0f / fadeRange;

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(CB_ClipmapData);
    cbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    D3D11_SUBRESOURCE_DATA cbInit = { &m_cbData, 0, 0 };
    HRESULT hr = m_device->CreateBuffer(&cbDesc, &cbInit, m_clipmapCB.GetAddressOf());
    HR_CHECK(hr, "Failed to create RVT Constant Buffer");

    return true;
}

void ClipmapRvtManager::Update(const DirectX::XMFLOAT3& cameraPos) {
    for (int i = 0; i < m_numCascades; ++i) {
        auto& casc = m_cascades[i];

        int cx = static_cast<int>(std::floor(cameraPos.x / casc.TexelSize));
        int cz = static_cast<int>(std::floor(cameraPos.z / casc.TexelSize));

        if (casc.IsFirstFrame) {
            ClipmapUpdateTask task = { i, cx - m_resolution / 2, cz - m_resolution / 2, m_resolution, m_resolution, casc.TexelSize };
            m_pendingTasks.push_back(task);
            casc.SnappedGridX = cx;
            casc.SnappedGridZ = cz;
            casc.IsFirstFrame = false;
            continue;
        }

        int dx = cx - casc.SnappedGridX;
        int dz = cz - casc.SnappedGridZ;

        if (dx == 0 && dz == 0) continue;

        int res = m_resolution;
        int halfRes = res / 2;

        if (abs(dx) >= res || abs(dz) >= res) {
            ClipmapUpdateTask task = { i, cx - halfRes, cz - halfRes, res, res, casc.TexelSize };
            m_pendingTasks.push_back(task);
        }
        else {
            if (dx != 0) {
                ClipmapUpdateTask task;
                task.CascadeIndex = i;
                task.StartCellX = (dx > 0) ? (cx + halfRes - dx) : (cx - halfRes);
                task.StartCellZ = cz - halfRes;
                task.UpdateWidth = abs(dx);
                task.UpdateHeight = res;
                task.TexelSize = casc.TexelSize;
                m_pendingTasks.push_back(task);
            }
            if (dz != 0) {
                ClipmapUpdateTask task;
                task.CascadeIndex = i;
                task.StartCellX = (dx > 0) ? (cx - halfRes) : (cx - halfRes - dx);
                task.StartCellZ = (dz > 0) ? (cz + halfRes - dz) : (cz - halfRes);
                task.UpdateWidth = res - abs(dx);
                task.UpdateHeight = abs(dz);
                task.TexelSize = casc.TexelSize;
                m_pendingTasks.push_back(task);
            }
        }

        casc.SnappedGridX = cx;
        casc.SnappedGridZ = cz;
    }

}

std::vector<ClipmapUpdateTask> ClipmapRvtManager::PopUpdateTasks() {
    std::vector<ClipmapUpdateTask> tasks = std::move(m_pendingTasks);
    m_pendingTasks.clear();
    return tasks;
}