// ================================================================================
// VirtualTextureManager.cpp
// ================================================================================
#include "VirtualTextureManager.h"
#include "../Core/Logger.h"
#include "../Config/EngineConfig.h"   
#include "../Resources/RvtBaker.h"    
#include "TerrainGpuScene.h"

VirtualTextureManager::VirtualTextureManager(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context), m_physicalSize(0), m_pageSize(0), m_pagesPerRow(0) {
}

VirtualTextureManager::~VirtualTextureManager() {}

bool VirtualTextureManager::Initialize(int physicalSize, int pageSize) {
    m_physicalSize = physicalSize;
    m_pageSize = pageSize;
    m_pagesPerRow = physicalSize / pageSize;

    Logger::Info(LogCategory::Render, "Initializing RVT... Cache: " + std::to_string(physicalSize) +
        "x" + std::to_string(physicalSize) + ", Page: " + std::to_string(pageSize));

    // 1. СОЗДАЕМ ФИЗИЧЕСКИЙ КЭШ
    D3D11_TEXTURE2D_DESC cacheDesc = {};
    cacheDesc.Width = physicalSize;
    cacheDesc.Height = physicalSize;
    cacheDesc.MipLevels = 1;
    cacheDesc.ArraySize = 1;
    cacheDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // RGBA 32-bit (Отлично подходит для Albedo + Alpha и Normals)
    cacheDesc.SampleDesc.Count = 1;
    cacheDesc.Usage = D3D11_USAGE_DEFAULT;
    // ВАЖНО: UAV для запекания, SRV для чтения в Terrain.hlsl
    cacheDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    HR_CHECK(m_device->CreateTexture2D(&cacheDesc, nullptr, m_physicalAlbedo.GetAddressOf()), "RVT Albedo Create");
    HR_CHECK(m_device->CreateTexture2D(&cacheDesc, nullptr, m_physicalNormal.GetAddressOf()), "RVT Normal Create");

    m_device->CreateShaderResourceView(m_physicalAlbedo.Get(), nullptr, m_physicalAlbedoSRV.GetAddressOf());
    m_device->CreateShaderResourceView(m_physicalNormal.Get(), nullptr, m_physicalNormalSRV.GetAddressOf());
    m_device->CreateUnorderedAccessView(m_physicalAlbedo.Get(), nullptr, m_physicalAlbedoUAV.GetAddressOf());
    m_device->CreateUnorderedAccessView(m_physicalNormal.Get(), nullptr, m_physicalNormalUAV.GetAddressOf());

    // 2. СОЗДАЕМ ТАБЛИЦУ СТРАНИЦ (Page Table)
    // Размер таблицы зависит от размера карты. Возьмем с запасом 512x512 тайлов мира.
    // Если чанк 100 метров, то 512x512 это карта 51х51 КИЛОМЕТР! Хватит с головой.
    D3D11_TEXTURE2D_DESC ptDesc = {};
    ptDesc.Width = 512;
    ptDesc.Height = 512;
    ptDesc.MipLevels = 1;
    ptDesc.ArraySize = 1;
    ptDesc.Format = DXGI_FORMAT_R16G16B16A16_UINT; // X = PhysX, Y = PhysY, Z = Mip, W = Status
    ptDesc.SampleDesc.Count = 1;
    ptDesc.Usage = D3D11_USAGE_DEFAULT;
    ptDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    HR_CHECK(m_device->CreateTexture2D(&ptDesc, nullptr, m_pageTable.GetAddressOf()), "RVT PageTable Create");
    m_device->CreateShaderResourceView(m_pageTable.Get(), nullptr, m_pageTableSRV.GetAddressOf());
    m_device->CreateUnorderedAccessView(m_pageTable.Get(), nullptr, m_pageTableUAV.GetAddressOf());

    // 3. ИНИЦИАЛИЗИРУЕМ ПУЛ СТРАНИЦ НА CPU
    int totalPages = m_pagesPerRow * m_pagesPerRow;
    m_pagePool.resize(totalPages);

    for (int i = 0; i < totalPages; ++i) {
        m_pagePool[i].PhysicalX = (i % m_pagesPerRow) * pageSize;
        m_pagePool[i].PhysicalY = (i / m_pagesPerRow) * pageSize;
        m_pagePool[i].IsOccupied = false;
        m_pagePool[i].LastAccessedFrame = 0;
    }

    // Заливаем таблицу страниц значениями 0xFFFF (65535), что значит "Не запечено"
    uint32_t clearVals[4] = { 65535, 65535, 65535, 65535 };
    m_context->ClearUnorderedAccessViewUint(m_pageTableUAV.Get(), clearVals);

    Logger::Info(LogCategory::Render, "RVT Initialized. Total Physical Pages: " + std::to_string(totalPages));
    return true;
}

void VirtualTextureManager::Update(uint32_t currentFrame) {
    // Здесь мы будем обрабатывать запросы от GPU (GPU Feedback)
    // и обновлять алгоритм вытеснения страниц (LRU).
}

void VirtualTextureManager::UpdatePageTableTexture(uint32_t virtX, uint32_t virtY, uint32_t physX, uint32_t physY) {
    // Формат R16G16_UINT
    uint16_t data[2] = { (uint16_t)physX, (uint16_t)physY };

    D3D11_BOX box;
    box.left = virtX;
    box.right = virtX + 1;
    box.top = virtY;
    box.bottom = virtY + 1;
    box.front = 0;
    box.back = 1;

    // Мгновенно обновляем ровно 1 пиксель в текстуре таблицы страниц!
    m_context->UpdateSubresource(m_pageTable.Get(), 0, &box, data, sizeof(uint32_t), 0);
}

void VirtualTextureManager::ProcessFeedback(
    uint32_t currentFrame,
    ID3D11Buffer* feedbackStagingBuffer,
    RvtBaker* baker,
    TerrainArrayManager* arrayMgr,
    LevelTextureManager* texManager,
    const std::vector<ChunkGpuData>& chunkData)
{
    if (!feedbackStagingBuffer || !baker) return;

    const auto& tLod = EngineConfig::Get().TerrainLod;
    if (!tLod.EnableRVT) return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    // Map с флагом READ (чтение данных от GPU)
    if (SUCCEEDED(m_context->Map(feedbackStagingBuffer, 0, D3D11_MAP_READ, 0, &mapped))) {
        uint32_t* data = reinterpret_cast<uint32_t*>(mapped.pData);
        uint32_t numRequests = data[0]; // Индекс 0 хранит количество запросов

        uint32_t maxRequests = (uint32_t)tLod.RVTMaxRequestsPerFrame;
        uint32_t processed = 0;

        for (uint32_t i = 0; i < numRequests && processed < maxRequests; ++i) {
            uint32_t chunkID = data[i + 1];
            if (chunkID >= chunkData.size()) continue;

            // БЕРЕМ РЕАЛЬНЫЕ ДАННЫЕ ЧАНКА!
            const ChunkGpuData& chunk = chunkData[chunkID];

            // Вычисляем виртуальные координаты
            float worldGridX = chunk.WorldPos.x / 100.0f;
            float worldGridY = chunk.WorldPos.z / 100.0f;
            uint32_t virtX = (uint32_t)(floor(worldGridX + 256.0f));
            uint32_t virtY = (uint32_t)(floor(worldGridY + 256.0f));

            uint32_t pageKey = (virtX << 16) | (virtY & 0xFFFF);

            if (m_pageTableMap.find(pageKey) != m_pageTableMap.end()) {
                m_pagePool[m_pageTableMap[pageKey]].LastAccessedFrame = currentFrame;
                continue;
            }

            int bestPageIndex = -1;
            uint32_t oldestFrame = 0xFFFFFFFF;

            for (size_t p = 0; p < m_pagePool.size(); ++p) {
                if (!m_pagePool[p].IsOccupied) {
                    bestPageIndex = (int)p; break;
                }
                if (m_pagePool[p].LastAccessedFrame < oldestFrame) {
                    oldestFrame = m_pagePool[p].LastAccessedFrame;
                    bestPageIndex = (int)p;
                }
            }

            if (bestPageIndex != -1) {
                RVTPageNode& page = m_pagePool[bestPageIndex];

                if (page.IsOccupied) {
                    uint32_t oldKey = (page.VirtualX << 16) | (page.VirtualY & 0xFFFF);
                    m_pageTableMap.erase(oldKey);
                    UpdatePageTableTexture(page.VirtualX, page.VirtualY, 0xFFFF, 0xFFFF);
                }

                page.IsOccupied = true;
                page.VirtualX = virtX;
                page.VirtualY = virtY;
                page.LastAccessedFrame = currentFrame;
                m_pageTableMap[pageKey] = bestPageIndex;

                // --- ПЕРЕДАЕМ РЕАЛЬНЫЕ ПАРАМЕТРЫ ЗАПЕКАНИЯ ---
                BakeJobParams params = {};
                params.DestPixelX = page.PhysicalX;
                params.DestPixelY = page.PhysicalY;

                // В WorldLoader chunk.WorldPos - это ЦЕНТР чанка. Отнимаем 50, чтобы получить верхний левый угол!
                params.WorldPosMinX = chunk.WorldPos.x - 50.0f;
                params.WorldPosMinZ = chunk.WorldPos.z - 50.0f;
                params.WorldSize = 100.0f;
                params.PageSize = m_pageSize;

                params.ArraySlice = chunk.ArraySlice;
                for (int m = 0; m < 24; ++m) {
                    params.MaterialIndices[m] = chunk.MaterialIndices[m];
                }

                baker->BakeTile(params, arrayMgr, texManager, m_physicalAlbedoUAV.Get(), m_physicalNormalUAV.Get());
                UpdatePageTableTexture(virtX, virtY, page.PhysicalX, page.PhysicalY);

                processed++;
            }
        }
        // Обязательно отпускаем ресурс
        m_context->Unmap(feedbackStagingBuffer, 0);
    }
}