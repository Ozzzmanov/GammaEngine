//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// TerrainGpuScene.cpp
// ================================================================================
#include "TerrainGpuScene.h"
#include "../Graphics/ComputeShader.h"
#include "../Core/Logger.h"
#include "../Graphics/LevelTextureManager.h"

TerrainGpuScene::TerrainGpuScene(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
}

TerrainGpuScene::~TerrainGpuScene() = default;

void TerrainGpuScene::Initialize() {
    m_cullingShader = std::make_unique<ComputeShader>(m_device, m_context);
    if (!m_cullingShader->Load(L"Assets/Shaders/TerrainCulling.hlsl")) {
        Logger::Error(LogCategory::Render, "Failed to load TerrainCulling.hlsl");
    }

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(CullingConstants);
    if (cbDesc.ByteWidth % 16 != 0) cbDesc.ByteWidth += 16 - (cbDesc.ByteWidth % 16);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    m_device->CreateBuffer(&cbDesc, nullptr, m_cullingCB.GetAddressOf());

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    // Слегка размывает маску, убирая ступеньки точности
    sampDesc.MipLODBias = -0.5f;

    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    m_device->CreateSamplerState(&sampDesc, m_samplerPointClamp.GetAddressOf());

    TerrainGpuScene::CreateGrid();
}

void TerrainGpuScene::AddChunk(const ChunkGpuData& data, const DirectX::BoundingBox& aabb) {
    m_cpuChunkData.push_back(data);
    m_cpuAABBs.push_back(aabb);
}

void TerrainGpuScene::BuildGpuBuffers(LevelTextureManager* texManager) {
    if (m_cpuChunkData.empty()) return;

    UINT numChunks = (UINT)m_cpuChunkData.size();

    // Создаем буфер глобальных материалов (GlobalMaterialBuffer)
    if (texManager) {
        const auto& materials = texManager->GetMaterials();
        if (!materials.empty()) {
            D3D11_BUFFER_DESC matDesc = {};
            matDesc.Usage = D3D11_USAGE_IMMUTABLE;
            matDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            matDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            matDesc.ByteWidth = (UINT)(sizeof(TerrainMaterial) * materials.size());
            matDesc.StructureByteStride = sizeof(TerrainMaterial);

            D3D11_SUBRESOURCE_DATA matInit = { materials.data(), 0, 0 };
            m_device->CreateBuffer(&matDesc, &matInit, m_materialBuffer.ReleaseAndGetAddressOf());

            D3D11_SHADER_RESOURCE_VIEW_DESC matSrvDesc = {};
            matSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            matSrvDesc.Buffer.NumElements = (UINT)materials.size();
            m_device->CreateShaderResourceView(m_materialBuffer.Get(), &matSrvDesc, m_materialSRV.ReleaseAndGetAddressOf());
        }
    }

    // AABB нужен только для куллинга, а ChunkGpuData для рендера.
    // Для оптимизации памяти используем структуру ShaderChunkInfo только для куллинга.
    struct ShaderChunkInfo {
        DirectX::XMFLOAT3 Extents; float Pad1;
        DirectX::XMFLOAT3 Center;  float Pad2;
    };
    std::vector<ShaderChunkInfo> cullData(numChunks);
    for (UINT i = 0; i < numChunks; ++i) {
        cullData[i].Center = m_cpuAABBs[i].Center;
        cullData[i].Extents = m_cpuAABBs[i].Extents;
    }

    // Буфер для куллинга (AABBs)
    ComPtr<ID3D11Buffer> cullingDataBuffer;
    ComPtr<ID3D11ShaderResourceView> cullingDataSRV;
    D3D11_BUFFER_DESC desc = {};
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    desc.ByteWidth = sizeof(ShaderChunkInfo) * numChunks;
    desc.StructureByteStride = sizeof(ShaderChunkInfo);
    D3D11_SUBRESOURCE_DATA initData = { cullData.data(), 0, 0 };
    m_device->CreateBuffer(&desc, &initData, cullingDataBuffer.GetAddressOf());

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = numChunks;
    m_device->CreateShaderResourceView(cullingDataBuffer.Get(), &srvDesc, cullingDataSRV.GetAddressOf());

    // Буфер для рендера (ChunkGpuData)
    desc.ByteWidth = sizeof(ChunkGpuData) * numChunks;
    desc.StructureByteStride = sizeof(ChunkGpuData);
    initData.pSysMem = m_cpuChunkData.data();
    m_device->CreateBuffer(&desc, &initData, m_chunkDataBuffer.ReleaseAndGetAddressOf());
    m_device->CreateShaderResourceView(m_chunkDataBuffer.Get(), &srvDesc, m_chunkDataSRV.ReleaseAndGetAddressOf());

    // Буфер видимых индексов (UAV + SRV)
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.ByteWidth = sizeof(uint32_t) * numChunks;
    desc.StructureByteStride = sizeof(uint32_t);
    m_device->CreateBuffer(&desc, nullptr, m_visibleIndicesBuffer.ReleaseAndGetAddressOf());
    m_device->CreateShaderResourceView(m_visibleIndicesBuffer.Get(), &srvDesc, m_visibleIndicesSRV.ReleaseAndGetAddressOf());

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = numChunks;
    m_device->CreateUnorderedAccessView(m_visibleIndicesBuffer.Get(), &uavDesc, m_visibleIndicesUAV.ReleaseAndGetAddressOf());

    // Indirect Args Buffer
    // 5 uints: IndexCount, InstanceCount, StartIndex, BaseVertex, StartInstance
    uint32_t indirectArgsInit[5] = { 0, 0, 0, 0, 0 };
    // Заметка: количество индексов на один чанк (сетка 37x37) = 36 * 36 * 6 = 7776.
    indirectArgsInit[0] = 7776;

    desc.ByteWidth = sizeof(uint32_t) * 5;
    desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    initData.pSysMem = indirectArgsInit;

    m_device->CreateBuffer(&desc, &initData, m_indirectArgsBuffer.ReleaseAndGetAddressOf());

    indirectArgsInit[1] = 0; // Для сброса InstanceCount = 0
    m_device->CreateBuffer(&desc, &initData, m_indirectArgsResetBuffer.ReleaseAndGetAddressOf());

    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    uavDesc.Buffer.NumElements = 5;
    m_device->CreateUnorderedAccessView(m_indirectArgsBuffer.Get(), &uavDesc, m_indirectArgsUAV.ReleaseAndGetAddressOf());

    // Временно сохраняем Culling SRV как член класса
    // FIXME: !!! Добавь ComPtr<ID3D11ShaderResourceView> m_cullingDataSRV в хидер!
    m_cullingDataSRV = cullingDataSRV;
}

void TerrainGpuScene::PerformCulling(
    const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj,
    const DirectX::XMMATRIX& prevView, const DirectX::XMMATRIX& prevProj,
    const DirectX::BoundingFrustum& frustum, const DirectX::XMFLOAT3& cameraPos,
    ID3D11ShaderResourceView* hzbSRV, DirectX::XMFLOAT2 hzbSize,
    bool enableFrustum, bool enableOcclusion, float renderDistance)
{
    if (m_cpuChunkData.empty() || !m_cullingShader) return;

    m_context->CopyResource(m_indirectArgsBuffer.Get(), m_indirectArgsResetBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_cullingCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        CullingConstants* data = (CullingConstants*)mapped.pData;

        DirectX::XMStoreFloat4x4(&data->ViewProj, DirectX::XMMatrixTranspose(view * proj));
        // Записываем матрицу прошлого кадра для точного HZB
        DirectX::XMStoreFloat4x4(&data->PrevViewProj, DirectX::XMMatrixTranspose(prevView * prevProj));

        DirectX::XMVECTOR planes[6];
        frustum.GetPlanes(&planes[0], &planes[1], &planes[2], &planes[3], &planes[4], &planes[5]);
        for (int i = 0; i < 6; ++i) DirectX::XMStoreFloat4(&data->FrustumPlanes[i], planes[i]);

        data->CameraPos = cameraPos;
        data->NumChunks = (uint32_t)m_cpuChunkData.size();
        data->HZBSize = hzbSize;
        data->MaxDistanceSq = renderDistance * renderDistance; // Квадрат дистанции
        data->EnableFrustum = enableFrustum ? 1 : 0;
        data->EnableOcclusion = enableOcclusion ? 1 : 0;

        m_context->Unmap(m_cullingCB.Get(), 0);
    }

    // Биндинг и Dispatch
    m_cullingShader->Bind();
    m_context->CSSetConstantBuffers(0, 1, m_cullingCB.GetAddressOf());
    m_context->CSSetSamplers(0, 1, m_samplerPointClamp.GetAddressOf());

    ID3D11ShaderResourceView* srvs[] = { m_cullingDataSRV.Get(), hzbSRV };
    m_context->CSSetShaderResources(0, 2, srvs);

    UINT initCounts[2] = { (UINT)-1, (UINT)-1 };
    ID3D11UnorderedAccessView* uavs[] = { m_visibleIndicesUAV.Get(), m_indirectArgsUAV.Get() };
    m_context->CSSetUnorderedAccessViews(0, 2, uavs, initCounts);

    UINT groups = (UINT)std::ceil(m_cpuChunkData.size() / 64.0f);
    m_cullingShader->Dispatch(groups, 1, 1);

    // Очистка
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr };
    m_context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };
    m_context->CSSetShaderResources(0, 2, nullSRVs);
    m_cullingShader->Unbind();
}

void TerrainGpuScene::CreateGrid() {
    const int width = 37;
    const int depth = 37;
    const float spacing = 100.0f / 36.0f; // Ровно 100 метров на чанк
    float widthOffset = 50.0f;
    float depthOffset = 50.0f;

    std::vector<SimpleVertex> vertices(width * depth);
    std::vector<uint32_t> indices;

    // Генерируем ПЛОСКУЮ сетку (Y всегда 0)
    for (int z = 0; z < depth; ++z) {
        for (int x = 0; x < width; ++x) {
            SimpleVertex& v = vertices[z * width + x];
            v.Pos = DirectX::XMFLOAT3(x * spacing - widthOffset, 0.0f, z * spacing - depthOffset);
            v.Tex = DirectX::XMFLOAT2((float)x / (width - 1), (float)z / (depth - 1));
            v.Normal = DirectX::XMFLOAT3(0, 1, 0);
            v.Color = DirectX::XMFLOAT3(1, 1, 1);
        }
    }

    for (int z = 0; z < depth - 1; ++z) {
        for (int x = 0; x < width - 1; ++x) {
            uint32_t bl = z * width + x;
            uint32_t br = z * width + (x + 1);
            uint32_t tl = (z + 1) * width + x;
            uint32_t tr = (z + 1) * width + (x + 1);
            indices.push_back(bl); indices.push_back(tl); indices.push_back(tr);
            indices.push_back(bl); indices.push_back(tr); indices.push_back(br);
        }
    }
    m_indexCount = (UINT)indices.size();

    D3D11_BUFFER_DESC vbd = { (UINT)(sizeof(SimpleVertex) * vertices.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA vData = { vertices.data(), 0, 0 };
    m_device->CreateBuffer(&vbd, &vData, m_vertexBuffer.ReleaseAndGetAddressOf());

    D3D11_BUFFER_DESC ibd = { (UINT)(sizeof(uint32_t) * indices.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA iData = { indices.data(), 0, 0 };
    m_device->CreateBuffer(&ibd, &iData, m_indexBuffer.ReleaseAndGetAddressOf());
}



void TerrainGpuScene::BindGeometry() {
    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void TerrainGpuScene::BindResources(TerrainArrayManager* arrayMgr, ID3D11ShaderResourceView* diffuseArraySRV) {
    if (!arrayMgr) return;

    ID3D11ShaderResourceView* buffers[2] = { m_chunkDataSRV.Get(), m_visibleIndicesSRV.Get() };
    m_context->VSSetShaderResources(0, 2, buffers);
    m_context->PSSetShaderResources(0, 2, buffers);

    // 4 массива
    ID3D11ShaderResourceView* arrays[4] = {
        arrayMgr->GetHeightArray(),      // t2
        arrayMgr->GetHoleArray(),        // t3
        arrayMgr->GetIndexArray(),       // t4
        arrayMgr->GetWeightArray()       // t5
    };

    m_context->VSSetShaderResources(2, 1, &arrays[0]);
    m_context->PSSetShaderResources(2, 4, arrays);

    m_context->PSSetShaderResources(10, 1, &diffuseArraySRV);

    if (m_materialSRV) {
        m_context->PSSetShaderResources(11, 1, m_materialSRV.GetAddressOf());
    }
}