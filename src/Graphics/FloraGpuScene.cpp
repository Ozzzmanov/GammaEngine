//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// FloraGpuScene.cpp
// ================================================================================
#include "FloraGpuScene.h"
#include "../Core/Logger.h"
#include <algorithm>
#include "ModelManager.h"

struct BatchGpuInfo {
    uint32_t PackedData;
};

FloraGpuScene::FloraGpuScene(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
}

void FloraGpuScene::AddEntity(const TreeLODs& lods, const InstanceData& data) {
    if (!lods.Lod0) return;
    FloraGroupKey key = { lods.Lod0, lods.Lod1, lods.Lod2, lods.HideLod1, lods.HideLod2 };
    m_buildData[key].push_back(data);
}

void FloraGpuScene::BuildGpuBuffers() {
    if (m_buildData.empty()) return;

    std::vector<InstanceData> flatInstances;
    std::vector<EntityMetaData> metaDataList;
    std::vector<IndirectCommand> indirectCommands;
    std::vector<uint32_t> flatVisibleIndices;

    UINT currentVisibleIndexOffset = 0;

    for (auto& pair : m_buildData) {
        const FloraGroupKey& key = pair.first;
        std::vector<InstanceData>& instances = pair.second;
        UINT instanceCount = static_cast<UINT>(instances.size());

        if (instanceCount == 0) continue;

        UINT entityID = static_cast<UINT>(metaDataList.size());
        EntityMetaData meta = {};
        ZeroMemory(&meta, sizeof(EntityMetaData));

        if (key.Lod0) {
            const auto& sphere = key.Lod0->GetBoundingSphere();
            meta.LocalCenter = sphere.Center;
            meta.Radius = sphere.Radius;
        }

        auto processLOD = [&](TreeModel* model, int lodLevel) {
            if (!model) {
                meta.Lod[lodLevel].FirstBatch = 0;
                meta.Lod[lodLevel].PartCount = 0;
                meta.Lod[lodLevel].FirstVisibleOffset = 0;
                meta.Lod[lodLevel].MaxInstances = 0;
                return;
            }

            meta.Lod[lodLevel].FirstBatch = static_cast<UINT>(m_batches.size());
            UINT totalParts = static_cast<UINT>(model->GetOpaqueParts().size() + model->GetAlphaParts().size());
            meta.Lod[lodLevel].PartCount = totalParts;
            meta.Lod[lodLevel].FirstVisibleOffset = currentVisibleIndexOffset;
            meta.Lod[lodLevel].MaxInstances = instanceCount;

            auto addParts = [&](const std::vector<TreePart>& parts, bool isOpaque) {
                for (size_t p = 0; p < parts.size(); ++p) {
                    const auto& part = parts[p];

                    IndirectCommand cmd = {};
                    cmd.IndexCountPerInstance = part.indexCount;
                    cmd.StartIndexLocation = part.startIndex;
                    cmd.BaseVertexLocation = part.baseVertex;
                    cmd.StartInstanceLocation = currentVisibleIndexOffset;

                    if (lodLevel == 0) {
                        cmd.InstanceCount = instanceCount;
                        uint32_t isAlphaBit = isOpaque ? 0 : 1;
                        uint32_t batchPacked = (part.sliceIndex & 0x3FF) | (isAlphaBit << 10);

                        for (UINT i = 0; i < instanceCount; ++i) {
                            uint32_t actualIndex = static_cast<UINT>(flatInstances.size()) + i;
                            uint32_t finalPacked = (actualIndex & 0x1FFFFF) | (batchPacked << 21);
                            flatVisibleIndices.push_back(finalPacked);
                        }
                    }
                    else {
                        cmd.InstanceCount = 0;
                        for (UINT i = 0; i < instanceCount; ++i) {
                            flatVisibleIndices.push_back(0);
                        }
                    }

                    indirectCommands.push_back(cmd);

                    FloraRenderBatch batch;
                    batch.Model = model;
                    batch.LocalPartIndex = static_cast<int>(p);
                    batch.IsOpaque = isOpaque;
                    batch.StartInstanceOffset = currentVisibleIndexOffset;

                    int currentBatchIndex = static_cast<int>(m_batches.size());
                    m_batches.push_back(batch);

                    if (isOpaque) m_opaqueRenderOrder.push_back(currentBatchIndex);
                    else m_alphaRenderOrder.push_back(currentBatchIndex);

                    currentVisibleIndexOffset += instanceCount;
                }
                };

            addParts(model->GetOpaqueParts(), true);
            addParts(model->GetAlphaParts(), false);
            };

        processLOD(key.Lod0, 0);

        if (key.HideLod1) { meta.Lod[1].FirstBatch = 0; meta.Lod[1].PartCount = 0; }
        else if (key.Lod1) { processLOD(key.Lod1, 1); }
        else { meta.Lod[1] = meta.Lod[0]; }

        if (key.HideLod2) { meta.Lod[2].FirstBatch = 0; meta.Lod[2].PartCount = 0; }
        else if (key.Lod2) { processLOD(key.Lod2, 2); }
        else { meta.Lod[2] = meta.Lod[1]; }

        metaDataList.push_back(meta);

        for (auto& inst : instances) {
            inst.EntityID = entityID;
            flatInstances.push_back(inst);
        }
    }

    D3D11_BUFFER_DESC instDesc = {};
    instDesc.ByteWidth = static_cast<UINT>(sizeof(InstanceData) * flatInstances.size());
    instDesc.Usage = D3D11_USAGE_IMMUTABLE;
    instDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    instDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    instDesc.StructureByteStride = sizeof(InstanceData);
    D3D11_SUBRESOURCE_DATA instInit = { flatInstances.data(), 0, 0 };
    m_device->CreateBuffer(&instDesc, &instInit, m_instanceBuffer.ReleaseAndGetAddressOf());

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = static_cast<UINT>(flatInstances.size());
    m_device->CreateShaderResourceView(m_instanceBuffer.Get(), &srvDesc, m_instanceSRV.ReleaseAndGetAddressOf());

    D3D11_BUFFER_DESC metaDesc = instDesc;
    metaDesc.ByteWidth = static_cast<UINT>(sizeof(EntityMetaData) * metaDataList.size());
    metaDesc.StructureByteStride = sizeof(EntityMetaData);
    D3D11_SUBRESOURCE_DATA metaInit = { metaDataList.data(), 0, 0 };
    m_device->CreateBuffer(&metaDesc, &metaInit, m_metaDataBuffer.ReleaseAndGetAddressOf());

    srvDesc.Buffer.NumElements = static_cast<UINT>(metaDataList.size());
    m_device->CreateShaderResourceView(m_metaDataBuffer.Get(), &srvDesc, m_metaDataSRV.ReleaseAndGetAddressOf());

    D3D11_BUFFER_DESC idxDesc = {};
    idxDesc.ByteWidth = sizeof(uint32_t) * currentVisibleIndexOffset;
    idxDesc.Usage = D3D11_USAGE_DEFAULT;
    idxDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    idxDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    idxDesc.StructureByteStride = sizeof(uint32_t);
    D3D11_SUBRESOURCE_DATA idxInit = { flatVisibleIndices.data(), 0, 0 };
    m_device->CreateBuffer(&idxDesc, &idxInit, m_visibleIndexBuffer.ReleaseAndGetAddressOf());

    srvDesc.Buffer.NumElements = currentVisibleIndexOffset;
    m_device->CreateShaderResourceView(m_visibleIndexBuffer.Get(), &srvDesc, m_visibleIndexSRV.ReleaseAndGetAddressOf());

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = currentVisibleIndexOffset;
    m_device->CreateUnorderedAccessView(m_visibleIndexBuffer.Get(), &uavDesc, m_visibleIndexUAV.ReleaseAndGetAddressOf());

    D3D11_BUFFER_DESC argsDesc = {};
    argsDesc.ByteWidth = static_cast<UINT>(sizeof(IndirectCommand) * indirectCommands.size());
    argsDesc.Usage = D3D11_USAGE_DEFAULT;
    argsDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    argsDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

    D3D11_SUBRESOURCE_DATA argsInit = { indirectCommands.data(), 0, 0 };
    m_device->CreateBuffer(&argsDesc, &argsInit, m_indirectArgsBuffer.ReleaseAndGetAddressOf());

    for (auto& cmd : indirectCommands) cmd.InstanceCount = 0;
    argsInit.pSysMem = indirectCommands.data();
    m_device->CreateBuffer(&argsDesc, &argsInit, m_indirectArgsResetBuffer.ReleaseAndGetAddressOf());

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavArgsDesc = {};
    uavArgsDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavArgsDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavArgsDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    uavArgsDesc.Buffer.NumElements = argsDesc.ByteWidth / 4;
    m_device->CreateUnorderedAccessView(m_indirectArgsBuffer.Get(), &uavArgsDesc, m_indirectArgsUAV.ReleaseAndGetAddressOf());

    std::vector<BatchGpuInfo> batchInfoData;
    batchInfoData.reserve(m_batches.size());
    for (const auto& batch : m_batches) {
        const auto& part = batch.IsOpaque ? batch.Model->GetOpaqueParts()[batch.LocalPartIndex]
            : batch.Model->GetAlphaParts()[batch.LocalPartIndex];
        BatchGpuInfo info;
        uint32_t isAlphaBit = batch.IsOpaque ? 0 : 1;
        info.PackedData = (part.sliceIndex & 0x3FF) | (isAlphaBit << 10);
        batchInfoData.push_back(info);
    }

    D3D11_BUFFER_DESC biDesc = {};
    biDesc.ByteWidth = static_cast<UINT>(sizeof(BatchGpuInfo) * batchInfoData.size());
    biDesc.Usage = D3D11_USAGE_IMMUTABLE;
    biDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    biDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    biDesc.StructureByteStride = sizeof(BatchGpuInfo);

    D3D11_SUBRESOURCE_DATA biInit = { batchInfoData.data(), 0, 0 };
    m_device->CreateBuffer(&biDesc, &biInit, m_batchInfoBuffer.ReleaseAndGetAddressOf());

    D3D11_SHADER_RESOURCE_VIEW_DESC biSrvDesc = {};
    biSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    biSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    biSrvDesc.Buffer.NumElements = static_cast<UINT>(batchInfoData.size());
    m_device->CreateShaderResourceView(m_batchInfoBuffer.Get(), &biSrvDesc, m_batchInfoSRV.ReleaseAndGetAddressOf());

    for (int i = 0; i < 3; ++i) {
        D3D11_BUFFER_DESC shVisDesc = {};
        shVisDesc.Usage = D3D11_USAGE_DEFAULT;
        shVisDesc.ByteWidth = sizeof(uint32_t) * currentVisibleIndexOffset;
        shVisDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        shVisDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        shVisDesc.StructureByteStride = sizeof(uint32_t);
        m_device->CreateBuffer(&shVisDesc, nullptr, m_shadowVisibleIndexBuffer[i].ReleaseAndGetAddressOf());

        D3D11_UNORDERED_ACCESS_VIEW_DESC shUavDesc = {};
        shUavDesc.Format = DXGI_FORMAT_UNKNOWN;
        shUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        shUavDesc.Buffer.NumElements = currentVisibleIndexOffset;
        m_device->CreateUnorderedAccessView(m_shadowVisibleIndexBuffer[i].Get(), &shUavDesc, m_shadowVisibleIndexUAV[i].ReleaseAndGetAddressOf());

        D3D11_SHADER_RESOURCE_VIEW_DESC shSrvDesc = {};
        shSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        shSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        shSrvDesc.Buffer.NumElements = currentVisibleIndexOffset;
        m_device->CreateShaderResourceView(m_shadowVisibleIndexBuffer[i].Get(), &shSrvDesc, m_shadowVisibleIndexSRV[i].ReleaseAndGetAddressOf());

        D3D11_BUFFER_DESC shArgsDesc = {};
        shArgsDesc.Usage = D3D11_USAGE_DEFAULT;
        shArgsDesc.ByteWidth = static_cast<UINT>(sizeof(IndirectCommand) * indirectCommands.size());
        shArgsDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        shArgsDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

        D3D11_SUBRESOURCE_DATA shArgsInitData = { indirectCommands.data(), 0, 0 };
        m_device->CreateBuffer(&shArgsDesc, &shArgsInitData, m_shadowIndirectArgsBuffer[i].ReleaseAndGetAddressOf());

        D3D11_UNORDERED_ACCESS_VIEW_DESC shArgsUavDesc = {};
        shArgsUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        shArgsUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        shArgsUavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        shArgsUavDesc.Buffer.NumElements = shArgsDesc.ByteWidth / 4;
        m_device->CreateUnorderedAccessView(m_shadowIndirectArgsBuffer[i].Get(), &shArgsUavDesc, m_shadowIndirectArgsUAV[i].ReleaseAndGetAddressOf());
    }

    auto sortBatches = [&](std::vector<int>& orderArray, bool isOpaque) {
        std::sort(orderArray.begin(), orderArray.end(), [&](int a, int b) {
            const auto& partA = isOpaque ? m_batches[a].Model->GetOpaqueParts()[m_batches[a].LocalPartIndex]
                : m_batches[a].Model->GetAlphaParts()[m_batches[a].LocalPartIndex];
            const auto& partB = isOpaque ? m_batches[b].Model->GetOpaqueParts()[m_batches[b].LocalPartIndex]
                : m_batches[b].Model->GetAlphaParts()[m_batches[b].LocalPartIndex];

            if (partA.bucketIndex != partB.bucketIndex) return partA.bucketIndex < partB.bucketIndex;
            return partA.sliceIndex < partB.sliceIndex;
            });
        };

    sortBatches(m_opaqueRenderOrder, true);
    sortBatches(m_alphaRenderOrder, false);

    m_totalInstanceCount = static_cast<UINT>(flatInstances.size());
    m_buildData.clear();
}

void FloraGpuScene::RenderOpaque(ID3D11Buffer* instanceIdBuffer) {
    if (m_opaqueRenderOrder.empty() || !m_indirectArgsBuffer) return;

    ID3D11ShaderResourceView* srvs[2] = { m_instanceSRV.Get(), m_visibleIndexSRV.Get() };
    m_context->VSSetShaderResources(1, 2, srvs);

    ID3D11Buffer* vbs[2] = { ModelManager::Get().GetGlobalFloraVB(), instanceIdBuffer };
    UINT strides[2] = { sizeof(TreeVertex), sizeof(uint32_t) };
    UINT offsets[2] = { 0, 0 };
    m_context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
    m_context->IASetIndexBuffer(ModelManager::Get().GetGlobalFloraIB(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    int currentBucket = -1;

    for (int batchIdx : m_opaqueRenderOrder) {
        const auto& batch = m_batches[batchIdx];
        const auto& part = batch.Model->GetOpaqueParts()[batch.LocalPartIndex];

        if (part.bucketIndex != currentBucket) {
            currentBucket = part.bucketIndex;
            TextureBucket* bucket = ModelManager::Get().GetBucket(currentBucket);
            if (bucket && !bucket->albedoNames.empty()) {
                ID3D11ShaderResourceView* texArrays[3] = {
                    bucket->AlbedoArray->GetSRV(),
                    bucket->MRAOArray->GetSRV(),
                    bucket->NormalArray->GetSRV()
                };
                m_context->PSSetShaderResources(0, 3, texArrays);
            }
        }

        UINT argsOffset = static_cast<UINT>(batchIdx * sizeof(IndirectCommand));
        m_context->DrawIndexedInstancedIndirect(m_indirectArgsBuffer.Get(), argsOffset);
    }
}

void FloraGpuScene::RenderAlpha(ID3D11Buffer* instanceIdBuffer) {
    if (m_alphaRenderOrder.empty() || !m_indirectArgsBuffer) return;

    ID3D11ShaderResourceView* srvs[2] = { m_instanceSRV.Get(), m_visibleIndexSRV.Get() };
    m_context->VSSetShaderResources(1, 2, srvs);

    ID3D11Buffer* vbs[2] = { ModelManager::Get().GetGlobalFloraVB(), instanceIdBuffer };
    UINT strides[2] = { sizeof(TreeVertex), sizeof(uint32_t) };
    UINT offsets[2] = { 0, 0 };
    m_context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
    m_context->IASetIndexBuffer(ModelManager::Get().GetGlobalFloraIB(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    int currentBucket = -1;

    for (int batchIdx : m_alphaRenderOrder) {
        const auto& batch = m_batches[batchIdx];
        const auto& part = batch.Model->GetAlphaParts()[batch.LocalPartIndex];

        if (part.bucketIndex != currentBucket) {
            currentBucket = part.bucketIndex;
            TextureBucket* bucket = ModelManager::Get().GetBucket(currentBucket);
            if (bucket && !bucket->albedoNames.empty()) {
                ID3D11ShaderResourceView* texArrays[3] = {
                    bucket->AlbedoArray->GetSRV(),
                    bucket->MRAOArray->GetSRV(),
                    bucket->NormalArray->GetSRV()
                };
                m_context->PSSetShaderResources(0, 3, texArrays);
            }
        }

        UINT argsOffset = static_cast<UINT>(batchIdx * sizeof(IndirectCommand));
        m_context->DrawIndexedInstancedIndirect(m_indirectArgsBuffer.Get(), argsOffset);
    }
}

void FloraGpuScene::RenderShadowsOpaque(int cascadeIndex, ID3D11Buffer* instanceIdBuffer) {
    if (m_opaqueRenderOrder.empty() || cascadeIndex < 0 || cascadeIndex >= 3) return;
    if (!m_shadowVisibleIndexSRV[cascadeIndex] || !m_shadowIndirectArgsBuffer[cascadeIndex]) return;

    ID3D11ShaderResourceView* srvs[2] = { m_instanceSRV.Get(), m_shadowVisibleIndexSRV[cascadeIndex].Get() };
    m_context->VSSetShaderResources(1, 2, srvs);

    ID3D11Buffer* vbs[2] = { ModelManager::Get().GetGlobalFloraVB(), instanceIdBuffer };
    UINT strides[2] = { sizeof(TreeVertex), sizeof(uint32_t) };
    UINT offsets[2] = { 0, 0 };
    m_context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
    m_context->IASetIndexBuffer(ModelManager::Get().GetGlobalFloraIB(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (int batchIdx : m_opaqueRenderOrder) {
        UINT argsOffset = static_cast<UINT>(batchIdx * sizeof(IndirectCommand));
        m_context->DrawIndexedInstancedIndirect(m_shadowIndirectArgsBuffer[cascadeIndex].Get(), argsOffset);
    }
}

void FloraGpuScene::RenderShadowsAlpha(int cascadeIndex, ID3D11Buffer* instanceIdBuffer) {
    if (m_alphaRenderOrder.empty() || cascadeIndex < 0 || cascadeIndex >= 3) return;
    if (!m_shadowVisibleIndexSRV[cascadeIndex] || !m_shadowIndirectArgsBuffer[cascadeIndex]) return;

    ID3D11ShaderResourceView* srvs[2] = { m_instanceSRV.Get(), m_shadowVisibleIndexSRV[cascadeIndex].Get() };
    m_context->VSSetShaderResources(1, 2, srvs);

    ID3D11Buffer* vbs[2] = { ModelManager::Get().GetGlobalFloraVB(), instanceIdBuffer };
    UINT strides[2] = { sizeof(TreeVertex), sizeof(uint32_t) };
    UINT offsets[2] = { 0, 0 };
    m_context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
    m_context->IASetIndexBuffer(ModelManager::Get().GetGlobalFloraIB(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    int currentBucket = -1;

    for (int batchIdx : m_alphaRenderOrder) {
        const auto& batch = m_batches[batchIdx];
        const auto& part = batch.Model->GetAlphaParts()[batch.LocalPartIndex];

        if (part.bucketIndex != currentBucket) {
            currentBucket = part.bucketIndex;
            TextureBucket* bucket = ModelManager::Get().GetBucket(currentBucket);
            if (bucket && !bucket->albedoNames.empty()) {
                ID3D11ShaderResourceView* texArrays[1] = { bucket->AlbedoArray->GetSRV() };
                m_context->PSSetShaderResources(0, 1, texArrays);
            }
        }

        UINT argsOffset = static_cast<UINT>(batchIdx * sizeof(IndirectCommand));
        m_context->DrawIndexedInstancedIndirect(m_shadowIndirectArgsBuffer[cascadeIndex].Get(), argsOffset);
    }
}