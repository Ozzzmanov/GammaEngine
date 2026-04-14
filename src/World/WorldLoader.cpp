//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// WorldLoader.cpp
// ================================================================================

#include "WorldLoader.h"
#include "../Core/Logger.h"
#include "Chunk.h"
#include "WaterVLO.h"
#include "../Resources/SpaceSettings.h"
#include "../Resources/TerrainArrayManager.h"
#include "../Resources/TerrainBaker.h"
#include "../Graphics/TerrainGpuScene.h"
#include "../Graphics/StaticGpuScene.h"
#include "../Graphics/ModelManager.h"
#include "../Config/EngineConfig.h"
#include "Terrain.h"
#include "../Graphics/FloraGpuScene.h"
#include "../Resources/TextureBaker.h"
#include "../Graphics/LightManager.h"

#include <filesystem>
#include <algorithm>
#include <sstream>
#include <fstream>

namespace fs = std::filesystem;
using namespace DirectX;

// ================================================================================
// ПРИВАТНЫЕ СТРУКТУРЫ ДЛЯ БЫСТРОГО ПАРСИНГА GLEVEL
// Жестко фиксируем структуры, чтобы избежать C++ выравнивания
// ================================================================================
namespace {
#pragma pack(push, 1)
    struct PackedVLO {
        uint16_t UidID;
        uint16_t TypeID;
        DirectX::XMFLOAT3 Pos;
        DirectX::XMFLOAT3 Scale;
    };
    struct GMaterialData {
        uint16_t MatID;
        uint16_t Pad;
        DirectX::XMFLOAT4 UProj;
        DirectX::XMFLOAT4 VProj;
    };
    struct PackedGEntity {
        uint16_t ModelID;
        DirectX::XMFLOAT3 Pos;
        DirectX::XMFLOAT3 Scale;
        DirectX::XMFLOAT4 RotQuat;
    };
    struct PackedOmniLight {
        DirectX::XMFLOAT3 Color;
        DirectX::XMFLOAT3 Position;
        float InnerRadius;
        float OuterRadius;
        float Multiplier;
    };
    struct PackedSpotLight {
        DirectX::XMFLOAT3 Color;
        DirectX::XMFLOAT3 Position;
        DirectX::XMFLOAT3 Direction;
        float InnerRadius;
        float OuterRadius;
        float CosConeAngle;
        float Multiplier;
    };
#pragma pack(pop)

    // Хелпер для поиска составных объектов (LODов)
    [[maybe_unused]] static bool ParseCompositeName(const std::string& name, std::string& outBase, int& outIndex) {
        size_t lastUnderscore = name.find_last_of('_');
        if (lastUnderscore == std::string::npos || lastUnderscore == name.length() - 1) return false;
        std::string suffix = name.substr(lastUnderscore + 1);
        if (suffix.empty() || !std::all_of(suffix.begin(), suffix.end(), ::isdigit)) return false;
        try {
            outIndex = std::stoi(suffix);
            outBase = name.substr(0, lastUnderscore);
            return true;
        }
        catch (...) { return false; }
    }
} // namespace

WorldLoader::WorldLoader(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context)
{
}

WorldLoader::~WorldLoader() {}

void WorldLoader::LoadLocation(const std::string& folderPath,
    std::vector<std::unique_ptr<Chunk>>& outChunks,
    std::vector<std::shared_ptr<WaterVLO>>& outWaterObjects,
    std::unique_ptr<SpaceSettings>& outSettings,
    std::unique_ptr<LevelTextureManager>& outTexMgr,
    StaticGpuScene* staticScene,
    FloraGpuScene* floraScene,
    TerrainArrayManager* arrayManager,
    TerrainGpuScene* terrainGpuScene)
{
    if (!fs::exists(folderPath)) {
        GAMMA_LOG_ERROR(LogCategory::General, "Location not found: " + folderPath);
        return;
    }

    outTexMgr = std::make_unique<LevelTextureManager>(m_device, m_context);
    outSettings = std::make_unique<SpaceSettings>();

    std::string settingsPath = folderPath + "/space.settings";
    SpaceParams currentParams;
    if (fs::exists(settingsPath)) {
        outSettings->Load(settingsPath);
        currentParams = outSettings->GetParams();
    }

    outChunks.clear();
    outWaterObjects.clear();
    m_globalWaterStorage.clear();
    LightManager::Get().ClearStaticLights();

    TerrainBaker::Initialize();

    std::string glevelPath = "";

    // Проверяем все .glevel файлы внутри папки
    if (fs::is_directory(folderPath)) {
        for (const auto& entry : fs::directory_iterator(folderPath)) {
            if (entry.path().extension() == ".glevel") {
                glevelPath = entry.path().string();
                break;
            }
        }
    }

    // Если нашли файл .glevel
    if (!glevelPath.empty() && fs::exists(glevelPath)) {
        GAMMA_LOG_INFO(LogCategory::General, "Found .glevel file: " + glevelPath + " ! Using Fast Path.");

        std::ifstream file(glevelPath, std::ios::binary);
        GLevelHeader header;
        file.read((char*)&header, sizeof(GLevelHeader));
        file.close();

        if (!TerrainBaker::IsLocationBaked(folderPath, header.NumChunks)) {
            TerrainBaker::BakeFromGLevel(folderPath, glevelPath);
        }

        // ЧИТАЕМ META ФАЙЛ, ЧТОБЫ УЗНАТЬ ПРАВИЛЬНЫЕ СЛОИ
        std::string prefix = TerrainBaker::GetCachePrefix(folderPath);
        std::unordered_map<uint64_t, int> coordToSlice;
        std::ifstream metaFile(prefix + "_Terrain.meta", std::ios::binary);
        if (metaFile.is_open()) {
            uint32_t m, count;
            metaFile.read((char*)&m, sizeof(m));
            metaFile.read((char*)&count, sizeof(count));
            std::vector<UnifiedChunkMeta> chunkMetas(count);
            metaFile.read((char*)chunkMetas.data(), count * sizeof(UnifiedChunkMeta));
            metaFile.close();

            for (uint32_t i = 0; i < count; ++i) {
                uint64_t key = ((uint64_t)(uint32_t)chunkMetas[i].GridX << 32) | (uint32_t)chunkMetas[i].GridZ;
                coordToSlice[key] = i;
            }
        }

        // Передаем coordToSlice в загрузчик
        if (LoadFromGLevel(glevelPath, outChunks, outWaterObjects, coordToSlice, staticScene, floraScene, terrainGpuScene, outTexMgr.get())) {
            if (arrayManager) arrayManager->LoadMegaArrays(prefix);
            if (outTexMgr) outTexMgr->BuildArray();
            if (terrainGpuScene) terrainGpuScene->BuildGpuBuffers(outTexMgr.get());

            GAMMA_LOG_INFO(LogCategory::General, "Location loaded instantly via .glevel!");
            return;
        }
    }

    // ========================================================
    // СТАРЫЙ МЕДЛЕННЫЙ ПУТЬ (Fallback / Совместимость)
    // FIXME: TIER B. Вынести весь этот блок в отдельный метод LoadLegacyChunks(),
    // чтобы не засорять основную функцию LoadLocation.
    // ========================================================
    GAMMA_LOG_INFO(LogCategory::General, "Falling back to legacy .chunk parsing...");

    std::vector<ChunkBakeTask> chunkTasks;
    for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
        if (entry.is_directory()) continue;
        std::string filename = entry.path().filename().string();
        int gx = 0, gz = 0;

        if (filename.find("o.chunk") != std::string::npos && ParseGridCoordinates(filename, gx, gz)) {
            fs::path cdataPath = entry.path();
            cdataPath.replace_extension(".cdata");

            if (fs::exists(cdataPath)) {
                chunkTasks.push_back({ gx, gz, cdataPath.string() });
            }
        }
    }

    if (!chunkTasks.empty()) {
        if (!TerrainBaker::IsLocationBaked(folderPath, chunkTasks.size())) {
            TerrainBaker::BakeLocationInMemory(folderPath, chunkTasks);
        }

        std::string prefix = TerrainBaker::GetCachePrefix(folderPath);

        if (arrayManager) {
            arrayManager->LoadMegaArrays(prefix);
        }

        std::ifstream metaFile(prefix + "_Terrain.meta", std::ios::binary);
        if (!metaFile.is_open()) {
            GAMMA_LOG_ERROR(LogCategory::System, "CRITICAL: Failed to open Terrain.meta! Aborting terrain load.");
            return;
        }

        uint32_t m, count;
        metaFile.read((char*)&m, sizeof(m));
        metaFile.read((char*)&count, sizeof(count));

        std::vector<UnifiedChunkMeta> chunkMetas(count);
        metaFile.read((char*)chunkMetas.data(), count * sizeof(UnifiedChunkMeta));
        metaFile.close();

        std::ifstream physFile(prefix + "_Physics.bin", std::ios::binary | std::ios::ate);
        if (!physFile.is_open()) {
            GAMMA_LOG_ERROR(LogCategory::System, "CRITICAL: Failed to open Physics.bin! Aborting terrain load.");
            return;
        }

        size_t physSize = physFile.tellg();
        physFile.seekg(0, std::ios::beg);
        std::vector<float> allPhysicsHeights(physSize / sizeof(float));
        physFile.read((char*)allPhysicsHeights.data(), physSize);
        physFile.close();

        std::unordered_map<uint64_t, int> coordToSlice;
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t key = ((uint64_t)(uint32_t)chunkMetas[i].GridX << 32) | (uint32_t)chunkMetas[i].GridZ;
            coordToSlice[key] = i;
        }

        for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
            if (entry.is_directory()) continue;
            std::string filename = entry.path().filename().string();
            int gx = 0, gz = 0;

            if (filename.find("o.chunk") != std::string::npos && ParseGridCoordinates(filename, gx, gz)) {
                uint64_t key = ((uint64_t)(uint32_t)gx << 32) | (uint32_t)gz;
                int sliceIndex = -1;
                const UnifiedChunkMeta* metaPtr = nullptr;
                const float* heightPtr = nullptr;

                if (coordToSlice.find(key) != coordToSlice.end()) {
                    sliceIndex = coordToSlice[key];
                    metaPtr = &chunkMetas[sliceIndex];
                    heightPtr = &allPhysicsHeights[sliceIndex * TerrainBaker::HEIGHTMAP_SIZE * TerrainBaker::HEIGHTMAP_SIZE];
                }

                auto chunk = std::make_unique<Chunk>(m_device, m_context);
                chunk->SetArraySlice(sliceIndex);

                if (chunk->Load(entry.path().string(), gx, gz, currentParams, outTexMgr.get(), metaPtr, heightPtr)) {
                    if (staticScene) {
                        // Лямбда для автоматической генерации путей к LOD-ам
                        auto getLodPath = [](const std::string& originalPath, int lod) -> std::string {
                            std::string path = originalPath;
                            size_t extPos = path.find_last_of('.');
                            if (extPos == std::string::npos) return path;

                            std::string lowerPath = path;
                            std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

                            size_t lodPos = lowerPath.find("_lod");
                            if (lodPos != std::string::npos && lodPos < extPos) {
                                size_t numEnd = lodPos + 4;
                                while (numEnd < extPos && std::isdigit(path[numEnd])) numEnd++;
                                return path.substr(0, lodPos + 4) + std::to_string(lod) + path.substr(numEnd);
                            }
                            return path.substr(0, extPos) + "_lod" + std::to_string(lod) + path.substr(extPos);
                            };

                        for (const auto& parsedEntity : chunk->GetStaticEntities()) {
                            auto modelLod0 = ModelManager::Get().GetModel(parsedEntity.ModelPath);
                            if (modelLod0) {
                                EntityLODs lods;
                                lods.Lod0 = modelLod0.get();

                                std::string pathLod1 = getLodPath(parsedEntity.ModelPath, 1);
                                auto modelLod1 = ModelManager::Get().GetModel(pathLod1);
                                lods.Lod1 = modelLod1 ? modelLod1.get() : nullptr;

                                std::string pathLod2 = getLodPath(parsedEntity.ModelPath, 2);
                                auto modelLod2 = ModelManager::Get().GetModel(pathLod2);
                                lods.Lod2 = modelLod2 ? modelLod2.get() : nullptr;

                                lods.HideLod1 = false; lods.HideLod2 = false;

                                InstanceData data;
                                data.Position = parsedEntity.Position;
                                data.Scale = parsedEntity.Scale;
                                data.RotPacked = PackQuaternionXYZ(parsedEntity.RotXYZ);
                                data.EntityID = 0;

                                staticScene->AddEntity(lods, data);
                            }
                        }
                    }

                    if (floraScene) {
                        for (const auto& parsedEntity : chunk->GetFloraEntities()) {
                            auto treeModelLod0 = ModelManager::Get().GetTreeModel(parsedEntity.ModelPath, 0);
                            if (treeModelLod0) {
                                TreeLODs lods;
                                lods.Lod0 = treeModelLod0.get();
                                auto treeModelLod1 = ModelManager::Get().GetTreeModel(parsedEntity.ModelPath, 1);
                                lods.Lod1 = treeModelLod1 ? treeModelLod1.get() : nullptr;
                                auto treeModelLod2 = ModelManager::Get().GetTreeModel(parsedEntity.ModelPath, 2);
                                lods.Lod2 = treeModelLod2 ? treeModelLod2.get() : nullptr;
                                lods.HideLod1 = false; lods.HideLod2 = false;

                                InstanceData data;
                                data.Position = parsedEntity.Position;
                                data.Scale = parsedEntity.Scale;
                                data.RotPacked = PackQuaternionXYZ(parsedEntity.RotXYZ);
                                data.EntityID = 0;

                                floraScene->AddEntity(lods, data);
                            }
                        }
                    }

                    if (terrainGpuScene && sliceIndex >= 0) {
                        ChunkGpuData gpuData = {};
                        gpuData.WorldPos = DirectX::XMFLOAT3(gx * 100.0f + 50.0f, 0.0f, gz * 100.0f + 50.0f);
                        gpuData.ArraySlice = (uint32_t)sliceIndex;

                        if (chunk->GetTerrain()) {
                            const int* matIndices = chunk->GetTerrain()->GetMaterialIndices();
                            for (int i = 0; i < 24; ++i) {
                                gpuData.MaterialIndices[i] = matIndices[i];
                            }
                        }

                        DirectX::BoundingBox aabb;
                        aabb.Center = gpuData.WorldPos;
                        aabb.Center.y = 100.0f;
                        aabb.Extents = DirectX::XMFLOAT3(50.0f, 200.0f, 50.0f);

                        terrainGpuScene->AddChunk(gpuData, aabb);
                    }

                    outChunks.push_back(std::move(chunk));
                }
            }
        }
    }

    if (outTexMgr) outTexMgr->BuildArray();
    if (terrainGpuScene) {
        terrainGpuScene->BuildGpuBuffers(outTexMgr.get());
        GAMMA_LOG_INFO(LogCategory::General, "TerrainGpuScene built successfully.");
    }
}

bool WorldLoader::LoadFromGLevel(const std::string& filepath,
    std::vector<std::unique_ptr<Chunk>>& outChunks,
    std::vector<std::shared_ptr<WaterVLO>>& outWaterObjects,
    const std::unordered_map<uint64_t, int>& coordToSlice,
    StaticGpuScene* staticScene,
    FloraGpuScene* floraScene,
    TerrainGpuScene* terrainGpuScene,
    LevelTextureManager* texMgr)
{
    // Загружаем ВЕСЬ файл в ОЗУ одним куском
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    size_t fileSize = (size_t)file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(fileSize);
    file.read(buffer.data(), fileSize);
    file.close();

    const char* ptr = buffer.data();
    const char* endPtr = buffer.data() + fileSize;

    if (fileSize < sizeof(GLevelHeader)) return false;

    GLevelHeader* header = (GLevelHeader*)ptr;
    ptr += sizeof(GLevelHeader);

    if (strncmp(header->Magic, "GLVL", 4) != 0) {
        GAMMA_LOG_ERROR(LogCategory::System, "Invalid .glevel magic header!");
        return false;
    }

    GAMMA_LOG_INFO(LogCategory::System, "Loading fast .glevel format from memory. Chunks: " + std::to_string(header->NumChunks));

    // Читаем String Pool
    uint32_t strCount = *(uint32_t*)ptr;
    ptr += 4;

    std::vector<std::string> stringPool;
    stringPool.reserve(strCount);

    for (uint32_t i = 0; i < strCount; ++i) {
        std::string s(ptr);
        stringPool.push_back(s);
        ptr += s.length() + 1; // +1 для пропуска null-терминатора
    }

    // --- ЧИТАЕМ ГЛОБАЛЬНЫЕ VLO И МГНОВЕННО СОЗДАЕМ ВОДУ ---
    uint32_t vloCount = *(uint32_t*)ptr;
    ptr += 4;

    for (uint32_t i = 0; i < vloCount; ++i) {
        if (ptr >= endPtr) break;

        PackedVLO* vlo = (PackedVLO*)ptr;
        ptr += sizeof(PackedVLO);

        std::string uid = stringPool[vlo->UidID];
        std::string type = stringPool[vlo->TypeID];

        // Создаем воду
        if (m_globalWaterStorage.count(uid) == 0) {
            auto water = std::make_shared<WaterVLO>(m_device, m_context);
            water->LoadDefaults(uid);
            water->SetWorldPosition(DirectX::XMFLOAT3(vlo->Pos.x, vlo->Pos.y, vlo->Pos.z));
            water->OverrideSize(DirectX::XMFLOAT2(vlo->Scale.x, vlo->Scale.z));

            m_globalWaterStorage[uid] = water;
            outWaterObjects.push_back(water);

            GAMMA_LOG_INFO(LogCategory::General, "[VLO] Fast Loaded Global Water: " + uid);
        }
    }

    // Быстрый разбор чанков по указателям
    for (uint32_t i = 0; i < header->NumChunks; ++i) {
        if (ptr >= endPtr) break;

        GChunkHeader* chHdr = (GChunkHeader*)ptr;
        ptr += sizeof(GChunkHeader);

        auto chunk = std::make_unique<Chunk>(m_device, m_context);

        uint64_t key = ((uint64_t)(uint32_t)chHdr->GridX << 32) | (uint32_t)chHdr->GridZ;
        int sliceIndex = i; // Fallback
        if (coordToSlice.find(key) != coordToSlice.end()) {
            sliceIndex = coordToSlice.at(key);
        }

        chunk->SetArraySlice(sliceIndex);

        // --- ДАННЫЕ ТЕРРЕЙНА ---
        const float* chunkHeights = (const float*)ptr;

        // FIXME: Крайне опасный хардкод смещений байт. 
        // Необходимо использовать sizeof(Struct) или вычислять смещение динамически.
        // Если размер карты высот (TerrainBaker::HEIGHTMAP_SIZE) изменится с 64x64, загрузчик сломается.
        ptr += 16384;

        ptr += 64;

        GMaterialData* matData = (GMaterialData*)ptr;
        ptr += sizeof(GMaterialData) * 24;

        ptr += 131072; // FIXME: Аналогичный опасный хардкод размера текстур.

        UnifiedChunkMeta mockMeta = {};
        mockMeta.MinHeight = chHdr->MinHeight;
        mockMeta.MaxHeight = chHdr->MaxHeight;
        mockMeta.NumLayers = 24;

        for (int m = 0; m < 24; ++m) {
            std::string texName = "default";
            if (matData[m].MatID < stringPool.size()) {
                texName = stringPool[matData[m].MatID];
            }
            strncpy_s(mockMeta.Layers[m].textureName, texName.c_str(), sizeof(mockMeta.Layers[m].textureName) - 1);
            mockMeta.Layers[m].uProj = matData[m].UProj;
            mockMeta.Layers[m].vProj = matData[m].VProj;
        }

        chunk->AllocateTerrain();
        chunk->GetTerrain()->SetPosition(chHdr->GridX * 100.0f + 50.0f, 0.0f, chHdr->GridZ * 100.0f + 50.0f);
        chunk->GetTerrain()->Initialize(&mockMeta, chunkHeights, texMgr);

        // --- СТАТИКА ---
        uint32_t statCount = *(uint32_t*)ptr;
        ptr += 4;

        if (staticScene) {
            // Лямбда для автоматической генерации путей к LOD-ам
            auto getLodPath = [](const std::string& originalPath, int lod) -> std::string {
                std::string path = originalPath;
                size_t extPos = path.find_last_of('.');
                if (extPos == std::string::npos) return path;

                std::string lowerPath = path;
                std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

                size_t lodPos = lowerPath.find("_lod");
                if (lodPos != std::string::npos && lodPos < extPos) {
                    size_t numEnd = lodPos + 4;
                    while (numEnd < extPos && std::isdigit(path[numEnd])) numEnd++;
                    return path.substr(0, lodPos + 4) + std::to_string(lod) + path.substr(numEnd);
                }

                return path.substr(0, extPos) + "_lod" + std::to_string(lod) + path.substr(extPos);
                };

            for (uint32_t s = 0; s < statCount; ++s) {
                PackedGEntity* ent = (PackedGEntity*)ptr;
                ptr += sizeof(PackedGEntity);

                std::string modelPath = stringPool[ent->ModelID];
                auto modelLod0 = ModelManager::Get().GetModel(modelPath);

                if (modelLod0) {
                    EntityLODs lods;
                    lods.Lod0 = modelLod0.get();

                    // Пытаемся загрузить LOD1
                    std::string pathLod1 = getLodPath(modelPath, 1);
                    auto modelLod1 = ModelManager::Get().GetModel(pathLod1);
                    lods.Lod1 = modelLod1 ? modelLod1.get() : nullptr;

                    // Пытаемся загрузить LOD2
                    std::string pathLod2 = getLodPath(modelPath, 2);
                    auto modelLod2 = ModelManager::Get().GetModel(pathLod2);
                    lods.Lod2 = modelLod2 ? modelLod2.get() : nullptr;

                    lods.HideLod1 = false; lods.HideLod2 = false;

                    InstanceData data;
                    data.Position = ent->Pos;
                    data.Scale = ent->Scale;

                    DirectX::XMFLOAT4 q = ent->RotQuat;
                    if (q.w < 0.0f) { q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w; }
                    data.RotPacked = PackQuaternionXYZ(DirectX::XMFLOAT3(q.x, q.y, q.z));
                    data.EntityID = 0;

                    staticScene->AddEntity(lods, data);
                }
            }
        }
        else {
            ptr += statCount * sizeof(PackedGEntity);
        }

        // --- ФЛОРА ---
        uint32_t floraCount = *(uint32_t*)ptr;
        ptr += 4;

        if (floraScene) {
            for (uint32_t f = 0; f < floraCount; ++f) {
                PackedGEntity* ent = (PackedGEntity*)ptr;
                ptr += sizeof(PackedGEntity);

                std::string modelPath = stringPool[ent->ModelID];
                auto treeModelLod0 = ModelManager::Get().GetTreeModel(modelPath, 0);
                if (treeModelLod0) {
                    TreeLODs lods;
                    lods.Lod0 = treeModelLod0.get();
                    auto tLod1 = ModelManager::Get().GetTreeModel(modelPath, 1);
                    lods.Lod1 = tLod1 ? tLod1.get() : nullptr;
                    auto tLod2 = ModelManager::Get().GetTreeModel(modelPath, 2);
                    lods.Lod2 = tLod2 ? tLod2.get() : nullptr;
                    lods.HideLod1 = false; lods.HideLod2 = false;

                    InstanceData data;
                    data.Position = ent->Pos;
                    data.Scale = ent->Scale;

                    DirectX::XMFLOAT4 q = ent->RotQuat;
                    if (q.w < 0.0f) { q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w; }
                    data.RotPacked = PackQuaternionXYZ(DirectX::XMFLOAT3(q.x, q.y, q.z));
                    data.EntityID = 0;

                    floraScene->AddEntity(lods, data);
                }
            }
        }
        else {
            ptr += floraCount * sizeof(PackedGEntity);
        }

        // --- СВЕТ ---
        uint32_t omniCount = *(uint32_t*)ptr;
        ptr += 4;
        for (uint32_t l = 0; l < omniCount; ++l) {
            PackedOmniLight* raw = (PackedOmniLight*)ptr;
            ptr += sizeof(PackedOmniLight);

            GPUPointLight light;
            light.Position = raw->Position;
            light.Color = raw->Color;
            light.Radius = raw->OuterRadius;
            light.Intensity = raw->Multiplier * 2.0f; // Конвертируем в физическую величину (Люмены)

            LightManager::Get().AddStaticPointLight(light);
        }

        uint32_t spotCount = *(uint32_t*)ptr;
        ptr += 4;
        for (uint32_t l = 0; l < spotCount; ++l) {
            PackedSpotLight* raw = (PackedSpotLight*)ptr;
            ptr += sizeof(PackedSpotLight);

            GPUSpotLight light;
            light.Position = raw->Position;
            light.Color = raw->Color;
            light.Radius = raw->OuterRadius;
            light.Direction = raw->Direction;
            light.CosConeAngle = raw->CosConeAngle;
            light.Intensity = raw->Multiplier * 2.0f;

            LightManager::Get().AddStaticSpotLight(light);
        }

        // --- GPU ДАННЫЕ ДЛЯ КУЛЛИНГА ---
        if (terrainGpuScene) {
            ChunkGpuData gpuData = {};
            gpuData.WorldPos = DirectX::XMFLOAT3(chHdr->GridX * 100.0f + 50.0f, 0.0f, chHdr->GridZ * 100.0f + 50.0f);
            gpuData.ArraySlice = sliceIndex;

            if (chunk->GetTerrain()) {
                const int* matIndices = chunk->GetTerrain()->GetMaterialIndices();
                for (int m = 0; m < 24; ++m) {
                    gpuData.MaterialIndices[m] = matIndices[m];
                }
            }

            DirectX::BoundingBox aabb;
            aabb.Center = gpuData.WorldPos;
            aabb.Center.y = (chHdr->MinHeight + chHdr->MaxHeight) * 0.5f;
            aabb.Extents = DirectX::XMFLOAT3(50.0f, (std::max)((chHdr->MaxHeight - chHdr->MinHeight) * 0.5f, 1.0f) + 50.0f, 50.0f);

            terrainGpuScene->AddChunk(gpuData, aabb);
        }

        outChunks.push_back(std::move(chunk));
    }

    return true;
}

void WorldLoader::LoadVLOByUID(const std::string& rawUid, const std::string& type,
    const DirectX::XMFLOAT3& positionFromChunk, const DirectX::XMFLOAT3& scaleFromChunk,
    std::vector<std::shared_ptr<WaterVLO>>& outWaterObjects)
{
    std::string uid = CleanUID(rawUid);
    if (uid.empty()) return;

    if (m_globalWaterStorage.count(uid) > 0) return;

    auto water = std::make_shared<WaterVLO>(m_device, m_context);

    fs::path foundPath;
    bool fileFound = false;
    std::string t1 = uid + ".vlo";
    std::string t2 = "_" + uid + ".vlo";

    // FIXME: Этот рекурсивный поиск файлов крайне медленный (O(N) операций с диском).
    // Необходимо использовать кэшированный `m_fileRegistry` из ResourceManager.
    for (const auto& entry : fs::recursive_directory_iterator("Assets")) {
        if (!entry.is_regular_file()) continue;
        std::string fn = entry.path().filename().string();
        std::transform(fn.begin(), fn.end(), fn.begin(), ::tolower);

        if (fn == t1 || fn == t2) {
            foundPath = entry.path();
            fileFound = true;
            break;
        }
    }

    if (fileFound) {
        if (!water->Load(foundPath.string())) fileFound = false;
    }

    if (!fileFound) {
        water->LoadDefaults(uid);
        water->SetWorldPosition(Vector3(positionFromChunk.x, positionFromChunk.y, positionFromChunk.z));
        water->OverrideSize(Vector2(scaleFromChunk.x, scaleFromChunk.z));
    }

    m_globalWaterStorage[uid] = water;
    outWaterObjects.push_back(water);

    GAMMA_LOG_INFO(LogCategory::General, "[VLO] Registering Water: " + uid + (fileFound ? " from .vlo" : " from .chunk"));
}

bool WorldLoader::ParseGridCoordinates(const std::string& filename, int& outX, int& outZ) {
    if (filename.length() < 8) return false;
    try {
        std::string hexX = filename.substr(0, 4);
        std::string hexZ = filename.substr(4, 4);
        unsigned int uX, uZ;

        std::stringstream ssX; ssX << std::hex << hexX; ssX >> uX;
        std::stringstream ssZ; ssZ << std::hex << hexZ; ssZ >> uZ;

        outX = (short)uX;
        outZ = (short)uZ;
        return true;
    }
    catch (...) { return false; }
}

std::string WorldLoader::CleanUID(std::string input) {
    size_t first = input.find_first_not_of(" \t\n\r");
    if (std::string::npos == first) return "";
    size_t last = input.find_last_not_of(" \t\n\r");

    std::string clean = input.substr(first, (last - first + 1));
    std::transform(clean.begin(), clean.end(), clean.begin(), ::tolower);

    if (!clean.empty() && clean[0] == '_') clean = clean.substr(1);

    if (clean.length() > 35) {
        int dots = 0;
        size_t lastChar = 0;
        for (size_t i = 0; i < clean.length(); ++i) {
            if (clean[i] == '.') dots++;
            if ((clean[i] >= '0' && clean[i] <= '9') ||
                (clean[i] >= 'a' && clean[i] <= 'f') || clean[i] == '.')
            {
                lastChar = i;
            }
            else if (dots >= 3) break;
        }
        clean = clean.substr(0, lastChar + 1);
    }
    return clean;
}