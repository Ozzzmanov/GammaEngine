//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// TerrainGpuScene.cpp
// ================================================================================
#include "TerrainGpuScene.h"
#include "../Graphics/ComputeShader.h"
#include "../Core/Logger.h"
#include "../Graphics/LevelTextureManager.h"
#include "../Config/EngineConfig.h"

constexpr int CHUNK_LOOKUP_SIZE = 512;
constexpr int CHUNK_LOOKUP_OFFSET = 256;

TerrainGpuScene::TerrainGpuScene(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
}

TerrainGpuScene::~TerrainGpuScene() = default;

void TerrainGpuScene::Initialize() {
    m_cullingShader = std::make_unique<ComputeShader>(m_device, m_context);
    if (!m_cullingShader->Load(L"Assets/Shaders/TerrainCulling.hlsl")) {
        GAMMA_LOG_ERROR(LogCategory::Render, "Failed to load TerrainCulling.hlsl");
    }

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(CullingConstants);

    // Защита от кривого выравнивания
    if (cbDesc.ByteWidth % 16 != 0) {
        cbDesc.ByteWidth += 16 - (cbDesc.ByteWidth % 16);
    }

    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HR_CHECK_VOID(m_device->CreateBuffer(&cbDesc, nullptr, m_cullingCB.GetAddressOf()), "Failed to create Culling CB");

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MipLODBias = -0.5f;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    HR_CHECK_VOID(m_device->CreateSamplerState(&sampDesc, m_samplerPointClamp.GetAddressOf()), "Failed to create PointClamp Sampler");

    CreateGrid();
}

void TerrainGpuScene::AddChunk(const ChunkGpuData& data, const DirectX::BoundingBox& aabb) {
    m_cpuChunkData.push_back(data);
    m_cpuAABBs.push_back(aabb);
}

void TerrainGpuScene::BuildGpuBuffers(LevelTextureManager* texManager) {
    if (m_cpuChunkData.empty()) return;

    UINT numChunks = static_cast<UINT>(m_cpuChunkData.size());

    // Culling data (AABBs)
    struct ShaderChunkInfo {
        DirectX::XMFLOAT3 Extents; float Pad1;
        DirectX::XMFLOAT3 Center;  float Pad2;
    };
    std::vector<ShaderChunkInfo> cullData(numChunks);
    for (UINT i = 0; i < numChunks; ++i) {
        cullData[i].Center = m_cpuAABBs[i].Center;
        cullData[i].Extents = m_cpuAABBs[i].Extents;
    }

    D3D11_BUFFER_DESC desc = {};
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.ByteWidth = sizeof(ShaderChunkInfo) * numChunks;
    desc.StructureByteStride = sizeof(ShaderChunkInfo);

    D3D11_SUBRESOURCE_DATA initData = { cullData.data(), 0, 0 };
    m_device->CreateBuffer(&desc, &initData, m_cullingDataBuffer.ReleaseAndGetAddressOf());

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = numChunks;
    m_device->CreateShaderResourceView(m_cullingDataBuffer.Get(), &srvDesc, m_cullingDataSRV.ReleaseAndGetAddressOf());

    // Chunk data (рендер)
    desc.ByteWidth = sizeof(ChunkGpuData) * numChunks;
    desc.StructureByteStride = sizeof(ChunkGpuData);
    initData.pSysMem = m_cpuChunkData.data();
    m_device->CreateBuffer(&desc, &initData, m_chunkDataBuffer.ReleaseAndGetAddressOf());
    m_device->CreateShaderResourceView(m_chunkDataBuffer.Get(), &srvDesc, m_chunkDataSRV.ReleaseAndGetAddressOf());

    // Visible indices buffer
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.ByteWidth = sizeof(uint32_t) * numChunks * 3; // Для 3 LODов
    desc.StructureByteStride = sizeof(uint32_t);
    m_device->CreateBuffer(&desc, nullptr, m_visibleIndicesBuffer.ReleaseAndGetAddressOf());

    srvDesc.Buffer.NumElements = numChunks * 3;
    m_device->CreateShaderResourceView(m_visibleIndicesBuffer.Get(), &srvDesc, m_visibleIndicesSRV.ReleaseAndGetAddressOf());

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = numChunks * 3;
    m_device->CreateUnorderedAccessView(m_visibleIndicesBuffer.Get(), &uavDesc, m_visibleIndicesUAV.ReleaseAndGetAddressOf());

    // Indirect args (3 LOD команды)
    uint32_t indirectArgsInit[15] = {
        m_lodIndexCounts[0], 0, m_lodIndexOffsets[0], 0, 0,
        m_lodIndexCounts[1], 0, m_lodIndexOffsets[1], 0, numChunks,
        m_lodIndexCounts[2], 0, m_lodIndexOffsets[2], 0, numChunks * 2
    };

    D3D11_BUFFER_DESC argsDesc = {};
    argsDesc.ByteWidth = sizeof(uint32_t) * 15;
    argsDesc.Usage = D3D11_USAGE_DEFAULT;
    argsDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    argsDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    D3D11_SUBRESOURCE_DATA argsData = { indirectArgsInit, 0, 0 };

    m_device->CreateBuffer(&argsDesc, &argsData, m_indirectArgsBuffer.ReleaseAndGetAddressOf());
    m_device->CreateBuffer(&argsDesc, &argsData, m_indirectArgsResetBuffer.ReleaseAndGetAddressOf());

    D3D11_UNORDERED_ACCESS_VIEW_DESC rawUavDesc = {};
    rawUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    rawUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    rawUavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    rawUavDesc.Buffer.NumElements = 15;
    m_device->CreateUnorderedAccessView(m_indirectArgsBuffer.Get(), &rawUavDesc, m_indirectArgsUAV.ReleaseAndGetAddressOf());

    // Shadow буферы
    for (int i = 0; i < 3; ++i) {
        desc.ByteWidth = sizeof(uint32_t) * numChunks * 3;
        desc.StructureByteStride = sizeof(uint32_t);
        m_device->CreateBuffer(&desc, nullptr, m_shadowVisibleIndicesBuffer[i].ReleaseAndGetAddressOf());

        srvDesc.Buffer.NumElements = numChunks * 3;
        m_device->CreateShaderResourceView(m_shadowVisibleIndicesBuffer[i].Get(), &srvDesc, m_shadowVisibleIndicesSRV[i].ReleaseAndGetAddressOf());
        m_device->CreateUnorderedAccessView(m_shadowVisibleIndicesBuffer[i].Get(), &uavDesc, m_shadowVisibleIndicesUAV[i].ReleaseAndGetAddressOf());

        D3D11_BUFFER_DESC shArgsDesc = argsDesc;
        m_device->CreateBuffer(&shArgsDesc, &argsData, m_shadowIndirectArgsBuffer[i].ReleaseAndGetAddressOf());
        m_device->CreateUnorderedAccessView(m_shadowIndirectArgsBuffer[i].Get(), &rawUavDesc, m_shadowIndirectArgsUAV[i].ReleaseAndGetAddressOf());
    }

    // CHUNK SLICE LOOKUP TEXTURE — O(1) для RvtBaker
    {
        // FIXME: Текстура 512x512 ограничивает мир до 51.2 км x 51.2 км. 
        // Для бесконечных миров потребуется Sparse Texture или хеш-таблица на GPU.
        std::vector<uint32_t> lookupData(CHUNK_LOOKUP_SIZE * CHUNK_LOOKUP_SIZE, 0xFFFFFFFF);

        for (uint32_t i = 0; i < numChunks; ++i) {
            int gx = static_cast<int>(std::floor((m_cpuChunkData[i].WorldPos.x - 50.0f) / 100.0f));
            int gz = static_cast<int>(std::floor((m_cpuChunkData[i].WorldPos.z - 50.0f) / 100.0f));

            int lx = gx + CHUNK_LOOKUP_OFFSET;
            int lz = gz + CHUNK_LOOKUP_OFFSET;

            if (lx >= 0 && lx < CHUNK_LOOKUP_SIZE && lz >= 0 && lz < CHUNK_LOOKUP_SIZE) {
                lookupData[lz * CHUNK_LOOKUP_SIZE + lx] = i;
            }
            else {
                GAMMA_LOG_WARN(LogCategory::Render, "Chunk out of bounds for Lookup Texture!");
            }
        }

        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = CHUNK_LOOKUP_SIZE;
        texDesc.Height = CHUNK_LOOKUP_SIZE;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R32_UINT;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_IMMUTABLE;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA texData = {};
        texData.pSysMem = lookupData.data();
        texData.SysMemPitch = CHUNK_LOOKUP_SIZE * sizeof(uint32_t);

        m_device->CreateTexture2D(&texDesc, &texData, m_chunkSliceLookup.ReleaseAndGetAddressOf());

        D3D11_SHADER_RESOURCE_VIEW_DESC lookupSRVDesc = {};
        lookupSRVDesc.Format = DXGI_FORMAT_R32_UINT;
        lookupSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        lookupSRVDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(m_chunkSliceLookup.Get(), &lookupSRVDesc, m_chunkSliceLookupSRV.ReleaseAndGetAddressOf());

        GAMMA_LOG_INFO(LogCategory::Render, "Built Chunk Slice Lookup Texture (512x512).");
    }
}

void TerrainGpuScene::PerformCulling(
    const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj,
    const DirectX::XMMATRIX& prevView, const DirectX::XMMATRIX& prevProj,
    const DirectX::BoundingFrustum& frustum,
    const DirectX::XMFLOAT3& cameraPos,
    ID3D11ShaderResourceView* hzbSRV,
    DirectX::XMFLOAT2 hzbSize,
    bool enableFrustum, bool enableOcclusion,
    bool enableLODs, float renderDistance)
{
    if (m_cpuChunkData.empty() || !m_cullingShader) return;

    // Сбрасываем аргументы отрисовки (InstanceCount = 0)
    m_context->CopyResource(m_indirectArgsBuffer.Get(), m_indirectArgsResetBuffer.Get());

    // Обновляем параметры
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_cullingCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        CullingConstants* data = static_cast<CullingConstants*>(mapped.pData);

        DirectX::XMStoreFloat4x4(&data->ViewProj, DirectX::XMMatrixTranspose(view * proj));
        DirectX::XMStoreFloat4x4(&data->PrevViewProj, DirectX::XMMatrixTranspose(prevView * prevProj));

        DirectX::XMVECTOR planes[6];
        frustum.GetPlanes(&planes[0], &planes[1], &planes[2], &planes[3], &planes[4], &planes[5]);
        for (int i = 0; i < 6; ++i) {
            DirectX::XMStoreFloat4(&data->FrustumPlanes[i], planes[i]);
        }

        data->CameraPos = cameraPos;
        data->NumChunks = static_cast<uint32_t>(m_cpuChunkData.size());
        data->HZBSize = hzbSize;
        data->MaxDistanceSq = renderDistance * renderDistance;
        data->EnableFrustum = enableFrustum ? 1 : 0;
        data->EnableOcclusion = enableOcclusion ? 1 : 0;

        const auto& tLod = EngineConfig::Get().GetActiveProfile().TerrainLod;
        if (tLod.Enabled && enableLODs) {
            data->LOD1_DistSq = tLod.Dist1 * tLod.Dist1;
            data->LOD2_DistSq = tLod.Dist2 * tLod.Dist2;
        }
        else {
            data->LOD1_DistSq = 30000.0f * 30000.0f;
            data->LOD2_DistSq = 30000.0f * 30000.0f;
        }
        data->Pad = 0;

        m_context->Unmap(m_cullingCB.Get(), 0);
    }

    // Выполняем Compute Shader
    m_cullingShader->Bind();
    m_context->CSSetConstantBuffers(0, 1, m_cullingCB.GetAddressOf());
    m_context->CSSetSamplers(0, 1, m_samplerPointClamp.GetAddressOf());

    ID3D11ShaderResourceView* srvs[2] = { m_cullingDataSRV.Get(), hzbSRV };
    m_context->CSSetShaderResources(0, 2, srvs);

    UINT initCounts[2] = { (UINT)-1, (UINT)-1 };
    ID3D11UnorderedAccessView* uavs[2] = { m_visibleIndicesUAV.Get(), m_indirectArgsUAV.Get() };
    m_context->CSSetUnorderedAccessViews(0, 2, uavs, initCounts);

    UINT groups = static_cast<UINT>(std::ceil(m_cpuChunkData.size() / 64.0f));
    m_cullingShader->Dispatch(groups, 1, 1);

    // Очищаем слоты
    ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
    m_context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    m_context->CSSetShaderResources(0, 2, nullSRVs);

    m_cullingShader->Unbind();
}

void TerrainGpuScene::CreateGrid() {
    const auto& tLod = EngineConfig::Get().GetActiveProfile().TerrainLod;

    const int width = 37;
    const int depth = 37;
    const float spacing = 100.0f / 36.0f; // 100 метров на 36 сегментов
    float widthOffset = 50.0f;
    float depthOffset = 50.0f;

    std::vector<SimpleVertex> vertices(width * depth);
    std::vector<uint32_t> indices;

    for (int z = 0; z < depth; ++z) {
        for (int x = 0; x < width; ++x) {
            SimpleVertex& v = vertices[z * width + x];
            v.Pos = DirectX::XMFLOAT3(x * spacing - widthOffset, 0.0f, z * spacing - depthOffset);
            v.Tex = DirectX::XMFLOAT2((float)x / (width - 1), (float)z / (depth - 1));
            v.Normal = DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);
            v.Color = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
            v.Tangent = DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);
        }
    }

    auto buildLOD = [&](int step, int lodLevel) {
        UINT startIndex = static_cast<UINT>(indices.size());

        for (int z = 0; z < depth - 1; z += step) {
            for (int x = 0; x < width - 1; x += step) {
                int nextX = std::min(x + step, width - 1);
                int nextZ = std::min(z + step, depth - 1);

                uint32_t bl = z * width + x;
                uint32_t br = z * width + nextX;
                uint32_t tl = nextZ * width + x;
                uint32_t tr = nextZ * width + nextX;

                indices.push_back(bl); indices.push_back(tl); indices.push_back(tr);
                indices.push_back(bl); indices.push_back(tr); indices.push_back(br);
            }
        }

        m_lodIndexOffsets[lodLevel] = startIndex;
        m_lodIndexCounts[lodLevel] = static_cast<UINT>(indices.size()) - startIndex;
        };

    buildLOD(tLod.Step0 > 0 ? tLod.Step0 : 1, 0);
    buildLOD(tLod.Step1 > 0 ? tLod.Step1 : 2, 1);
    buildLOD(tLod.Step2 > 0 ? tLod.Step2 : 4, 2);

    m_indexCount = static_cast<UINT>(indices.size());

    D3D11_BUFFER_DESC vbd = { static_cast<UINT>(sizeof(SimpleVertex) * vertices.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA vData = { vertices.data(), 0, 0 };
    m_device->CreateBuffer(&vbd, &vData, m_vertexBuffer.ReleaseAndGetAddressOf());

    D3D11_BUFFER_DESC ibd = { static_cast<UINT>(sizeof(uint32_t) * indices.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA iData = { indices.data(), 0, 0 };
    m_device->CreateBuffer(&ibd, &iData, m_indexBuffer.ReleaseAndGetAddressOf());
}

void TerrainGpuScene::BindGeometry(ID3D11Buffer* instanceIdBuffer) {
    ID3D11Buffer* vbs[2] = { m_vertexBuffer.Get(), instanceIdBuffer };
    UINT strides[2] = { sizeof(SimpleVertex), sizeof(uint32_t) };
    UINT offsets[2] = { 0, 0 };

    m_context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
    m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void TerrainGpuScene::BindResources(
    TerrainArrayManager* arrayMgr,
    LevelTextureManager* texManager,
    ID3D11ShaderResourceView* diffuseArraySRV,
    ID3D11ShaderResourceView* rvtAlbedoSRV)
{
    if (!arrayMgr) return;

    ID3D11ShaderResourceView* buffers[2] = { m_chunkDataSRV.Get(), m_visibleIndicesSRV.Get() };
    m_context->VSSetShaderResources(0, 2, buffers);
    m_context->PSSetShaderResources(0, 2, buffers);

    ID3D11ShaderResourceView* arrays[5] = {
        arrayMgr->GetHeightArray(),      // t2
        arrayMgr->GetHoleArray(),        // t3
        arrayMgr->GetIndexArray(),       // t4
        arrayMgr->GetWeightArray(),      // t5
        arrayMgr->GetNormalArray()       // t6
    };

    m_context->VSSetShaderResources(2, 5, arrays);
    m_context->PSSetShaderResources(2, 5, arrays);
    m_context->PSSetShaderResources(10, 1, &diffuseArraySRV);

    if (texManager && texManager->GetMaterialSRV()) {
        ID3D11ShaderResourceView* matSRV = texManager->GetMaterialSRV();
        m_context->PSSetShaderResources(11, 1, &matSRV);
    }

    if (rvtAlbedoSRV) {
        m_context->PSSetShaderResources(7, 1, &rvtAlbedoSRV);
    }
}

void TerrainGpuScene::BindShadowResources(int cascadeIndex, TerrainArrayManager* arrayMgr) {
    if (!arrayMgr || cascadeIndex < 0 || cascadeIndex >= 3) return;

    ID3D11ShaderResourceView* buffers[2] = { m_chunkDataSRV.Get(), m_visibleIndicesSRV.Get() };
    m_context->VSSetShaderResources(0, 2, buffers);

    ID3D11ShaderResourceView* heightArray = arrayMgr->GetHeightArray();
    m_context->VSSetShaderResources(2, 1, &heightArray);

    ID3D11ShaderResourceView* holeArray = arrayMgr->GetHoleArray();
    m_context->PSSetShaderResources(3, 1, &holeArray);
}