//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// RvtBaker.cpp
// ================================================================================
#include "RvtBaker.h"
#include "../Graphics/ComputeShader.h"
#include "../Resources/TerrainArrayManager.h"
#include "../Graphics/LevelTextureManager.h"
#include "../Core/Logger.h"

RvtBaker::RvtBaker(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
}

RvtBaker::~RvtBaker() {}

bool RvtBaker::Initialize() {
    m_bakerShader = std::make_unique<ComputeShader>(m_device, m_context);

    // FIXME: Хардкод пути к шейдеру. Желательно грузить через ShaderManager.
    if (!m_bakerShader->Load(L"Assets/Shaders/RvtBaker.hlsl", "CSMain")) {
        GAMMA_LOG_ERROR(LogCategory::Render, "Failed to load RvtBaker.hlsl");
        return false;
    }

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(BakeJobParams); // Гарантированно кратно 16 благодаря __declspec(align(16))
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HR_CHECK(m_device->CreateBuffer(&cbDesc, nullptr, m_bakeParamsCB.GetAddressOf()), "Failed to create RVT Bake Job CB");

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    HR_CHECK(m_device->CreateSamplerState(&sd, m_samplerWrap.GetAddressOf()), "Failed to create RVT Wrap Sampler");

    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    HR_CHECK(m_device->CreateSamplerState(&sd, m_samplerClamp.GetAddressOf()), "Failed to create RVT Clamp Sampler");

    return true;
}

void RvtBaker::BakeClipmap(
    const BakeJobParams& params,
    TerrainArrayManager* arrayMgr,
    LevelTextureManager* texManager,
    ID3D11ShaderResourceView* chunkSliceLookupSRV,
    ID3D11ShaderResourceView* chunkDataSRV,
    ID3D11UnorderedAccessView* outAlbedoUAV,
    ID3D11UnorderedAccessView* outNormalUAV)
{
    if (!m_bakerShader || !arrayMgr || !texManager) return;

    // Обновляем параметры (Const Buffer)
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_bakeParamsCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &params, sizeof(BakeJobParams));
        m_context->Unmap(m_bakeParamsCB.Get(), 0);
    }

    m_bakerShader->Bind();
    m_context->CSSetConstantBuffers(0, 1, m_bakeParamsCB.GetAddressOf());

    ID3D11SamplerState* samplers[2] = { m_samplerWrap.Get(), m_samplerClamp.Get() };
    m_context->CSSetSamplers(0, 2, samplers);

    // Биндим данные
    // t0=Materials, t1=IndexArray, t2=WeightArray, t3=DiffuseArray, 
    // t4=ChunkSliceLookup, t5=ChunkData
    ID3D11ShaderResourceView* srvs[6] = {
        texManager->GetMaterialSRV(),
        arrayMgr->GetIndexArray(),
        arrayMgr->GetWeightArray(),
        texManager->GetSRV(),
        chunkSliceLookupSRV,
        chunkDataSRV
    };
    m_context->CSSetShaderResources(0, 6, srvs);

    ID3D11UnorderedAccessView* uavs[2] = { outAlbedoUAV, outNormalUAV };
    UINT initCounts[2] = { (UINT)-1, (UINT)-1 };
    m_context->CSSetUnorderedAccessViews(0, 2, uavs, initCounts);

    // Выполняем Dispatch
    UINT groupsX = (params.UpdateWidth + 7) / 8;
    UINT groupsY = (params.UpdateHeight + 7) / 8;
    if (groupsX > 0 && groupsY > 0) {
        m_bakerShader->Dispatch(groupsX, groupsY, 1);
    }

    // Очистка слотов видеокарты (Resource Hazards prevention)
    ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
    m_context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[6] = { nullptr };
    m_context->CSSetShaderResources(0, 6, nullSRVs);

    m_bakerShader->Unbind();
}