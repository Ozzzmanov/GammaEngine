//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// StaticGpuScene.cpp
// ================================================================================
#include "StaticGpuScene.h"
#include "../Core/Logger.h"

StaticGpuScene::StaticGpuScene(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
}

void StaticGpuScene::AddEntity(const EntityLODs& lods, const InstanceData& data) {
    if (!lods.Lod0) return;
    EntityGroupKey key = { lods.Lod0, lods.Lod1, lods.Lod2, lods.HideLod1, lods.HideLod2 };
    m_buildData[key].push_back(data);
}

void StaticGpuScene::BuildGpuBuffers() {
    if (m_buildData.empty()) return;

    std::vector<InstanceData> flatInstances;
    std::vector<EntityMetaData> metaDataList;
    std::vector<IndirectCommand> indirectCommands;

    // сразу инициализируем массив нулями. Места хватит под все детали всех LOD-ов
    std::vector<uint32_t> flatVisibleIndices;

    UINT currentVisibleIndexOffset = 0;

    for (auto& pair : m_buildData) {
        const EntityGroupKey& key = pair.first;
        std::vector<InstanceData>& instances = pair.second;
        UINT instanceCount = (UINT)instances.size();

        if (instanceCount == 0) continue;

        UINT entityID = (UINT)metaDataList.size();
        EntityMetaData meta = {};
        ZeroMemory(&meta, sizeof(EntityMetaData));

        if (key.Lod0) {
            const auto& sphere = key.Lod0->GetBoundingSphere();
            meta.LocalCenter = sphere.Center;
            meta.Radius = sphere.Radius;
        }

        // Лямбда для настройки одного уровня LOD
        auto processLOD = [&](StaticModel* model, int lodLevel) {
            if (!model) {
                meta.Lod[lodLevel].FirstBatch = 0;
                meta.Lod[lodLevel].PartCount = 0;
                meta.Lod[lodLevel].FirstVisibleOffset = 0;
                meta.Lod[lodLevel].MaxInstances = 0;
                return;
            }

            meta.Lod[lodLevel].FirstBatch = (UINT)m_batches.size();
            meta.Lod[lodLevel].PartCount = (UINT)model->GetPartCount();
            meta.Lod[lodLevel].FirstVisibleOffset = currentVisibleIndexOffset;
            meta.Lod[lodLevel].MaxInstances = instanceCount;

            for (size_t p = 0; p < model->GetPartCount(); ++p) {
                const auto& part = model->GetPart(p);

                IndirectCommand cmd = {};
                cmd.IndexCountPerInstance = part.indexCount;
                cmd.StartIndexLocation = part.startIndex;
                cmd.BaseVertexLocation = part.baseVertex;
                cmd.StartInstanceLocation = 0;

                // FIX ME !!!! ВРЕМЕННЫЙ КОД (до написания Compute Shader):
                // Заполняем счетчики только для LOD0, чтобы объекты отображались
                if (lodLevel == 0) {
                    cmd.InstanceCount = instanceCount;
                    for (UINT i = 0; i < instanceCount; ++i) {
                        flatVisibleIndices.push_back((UINT)flatInstances.size() + i);
                    }
                }
                else {
                    cmd.InstanceCount = 0;
                    for (UINT i = 0; i < instanceCount; ++i) {
                        flatVisibleIndices.push_back(0); // Резервируем нули
                    }
                }

                indirectCommands.push_back(cmd);

                RenderBatch batch;
                batch.Model = model;
                batch.PartIndex = (int)p;
                batch.StartInstanceOffset = currentVisibleIndexOffset;
                m_batches.push_back(batch);

                currentVisibleIndexOffset += instanceCount; // Резерв памяти!
            }
            };

        // Обрабатываем каждый уровень
        // Обрабатываем LOD 0 (База всегда есть)
        processLOD(key.Lod0, 0);

        // Обрабатываем LOD 1
        if (key.HideLod1) {
            // Оставляем нули, чтобы объект ИСЧЕЗ вдали
            meta.Lod[1].FirstBatch = 0; meta.Lod[1].PartCount = 0;
        }
        else if (key.Lod1 != nullptr) {
            processLOD(key.Lod1, 1); // Есть свой LOD
        }
        else {
            meta.Lod[1] = meta.Lod[0]; // Фоллбэк: рисуем LOD0 вместо LOD1
        }

        // Обрабатываем LOD 2
        if (key.HideLod2) {
            meta.Lod[2].FirstBatch = 0; meta.Lod[2].PartCount = 0;
        }
        else if (key.Lod2 != nullptr) {
            processLOD(key.Lod2, 2);
        }
        else {
            meta.Lod[2] = meta.Lod[1]; // Фоллбэк
        }

        metaDataList.push_back(meta);

        // Обновляем EntityID у инстансов и кладем в общий котел
        for (auto& inst : instances) {
            inst.EntityID = entityID;
            flatInstances.push_back(inst);
        }
    }


    // СОЗДАНИЕ GPU БУФЕРОВ

    // Instance Buffer
    D3D11_BUFFER_DESC instDesc = {};
    instDesc.ByteWidth = sizeof(InstanceData) * (UINT)flatInstances.size();
    instDesc.Usage = D3D11_USAGE_IMMUTABLE;
    instDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    instDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    instDesc.StructureByteStride = sizeof(InstanceData);
    D3D11_SUBRESOURCE_DATA instInit = { flatInstances.data(), 0, 0 };
    m_device->CreateBuffer(&instDesc, &instInit, m_instanceBuffer.ReleaseAndGetAddressOf());

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = (UINT)flatInstances.size();
    m_device->CreateShaderResourceView(m_instanceBuffer.Get(), &srvDesc, m_instanceSRV.ReleaseAndGetAddressOf());

    // MetaData Buffer
    D3D11_BUFFER_DESC metaDesc = instDesc;
    metaDesc.ByteWidth = sizeof(EntityMetaData) * (UINT)metaDataList.size();
    metaDesc.StructureByteStride = sizeof(EntityMetaData);
    D3D11_SUBRESOURCE_DATA metaInit = { metaDataList.data(), 0, 0 };
    m_device->CreateBuffer(&metaDesc, &metaInit, m_metaDataBuffer.ReleaseAndGetAddressOf());

    srvDesc.Buffer.NumElements = (UINT)metaDataList.size();
    m_device->CreateShaderResourceView(m_metaDataBuffer.Get(), &srvDesc, m_metaDataSRV.ReleaseAndGetAddressOf());

    // Visible Indices Buffer (SRV + UAV)
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

    // Indirect Args Buffer (UAV: RAW View для InterlockedAdd)
    D3D11_BUFFER_DESC argsDesc = {};
    argsDesc.ByteWidth = sizeof(IndirectCommand) * (UINT)indirectCommands.size();
    argsDesc.Usage = D3D11_USAGE_DEFAULT;
    argsDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    argsDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

    D3D11_SUBRESOURCE_DATA argsInit = { indirectCommands.data(), 0, 0 };
    m_device->CreateBuffer(&argsDesc, &argsInit, m_indirectArgsBuffer.ReleaseAndGetAddressOf());

    // Создаем буфер-сбросчик (все InstanceCount = 0)
    for (auto& cmd : indirectCommands) cmd.InstanceCount = 0;
    argsInit.pSysMem = indirectCommands.data();
    m_device->CreateBuffer(&argsDesc, &argsInit, m_indirectArgsResetBuffer.ReleaseAndGetAddressOf());

    // Создаем UAV для рабочего буфера
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavArgsDesc = {};
    uavArgsDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavArgsDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavArgsDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    uavArgsDesc.Buffer.NumElements = argsDesc.ByteWidth / 4;
    m_device->CreateUnorderedAccessView(m_indirectArgsBuffer.Get(), &uavArgsDesc, m_indirectArgsUAV.ReleaseAndGetAddressOf());

    // Batch Constant Buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(CB_Batch);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    m_device->CreateBuffer(&cbDesc, nullptr, m_batchCB.ReleaseAndGetAddressOf());

    m_renderOrder.resize(m_batches.size());
    for (size_t i = 0; i < m_batches.size(); ++i) {
        m_renderOrder[i] = (int)i;
    }

    // Сортируем индексы батчей по адресу указателя на текстуру!
    // Это сгруппирует объекты с одинаковой текстурой вместе.
    std::sort(m_renderOrder.begin(), m_renderOrder.end(), [&](int a, int b) {
        auto texA = m_batches[a].Model->GetPart(m_batches[a].PartIndex).texture.get();
        auto texB = m_batches[b].Model->GetPart(m_batches[b].PartIndex).texture.get();
        return texA < texB; // Сортировка по адресу в памяти
        });

    m_totalInstanceCount = (UINT)flatInstances.size();
    m_buildData.clear();
}

void StaticGpuScene::Render() {
    if (m_batches.empty() || !m_instanceSRV || !m_visibleIndexSRV || !m_indirectArgsBuffer || !m_batchCB) return;

    ID3D11ShaderResourceView* srvs[2] = { m_instanceSRV.Get(), m_visibleIndexSRV.Get() };
    m_context->VSSetShaderResources(1, 2, srvs);

    // запоминаем последнюю установленную текстуру, чтобы не биндить ее повторно!
    ID3D11ShaderResourceView* currentTexSRV = nullptr;

    // Идем по отсортированному списку
    for (int batchIdx : m_renderOrder) {
        const auto& batch = m_batches[batchIdx];

        // Смещение привязано к реальному индексу батча, а не к порядку цикла
        UINT argsOffset = batchIdx * sizeof(IndirectCommand);

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_batchCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            CB_Batch* data = (CB_Batch*)mapped.pData;
            data->BatchOffset = batch.StartInstanceOffset;
            m_context->Unmap(m_batchCB.Get(), 0);
        }
        m_context->VSSetConstantBuffers(1, 1, m_batchCB.GetAddressOf());

        batch.Model->BindGeometry();

        const auto& part = batch.Model->GetPart(batch.PartIndex);
        if (part.texture) {
            ID3D11ShaderResourceView* texSRV = part.texture->Get();

            // Устанавливаем текстуру ТОЛЬКО если она изменилась!
            if (texSRV != currentTexSRV) {
                m_context->PSSetShaderResources(0, 1, &texSRV);
                currentTexSRV = texSRV;
            }
        }

        m_context->DrawIndexedInstancedIndirect(m_indirectArgsBuffer.Get(), argsOffset);
    }

    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    m_context->VSSetShaderResources(1, 2, nullSRVs);
}