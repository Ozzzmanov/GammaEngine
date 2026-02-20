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

#include <filesystem>
#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;
using namespace DirectX;

// Хелпер для поиска составных объектов (LODов)
static bool ParseCompositeName(const std::string& name, std::string& outBase, int& outIndex) {
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
    TerrainArrayManager* arrayManager,
    TerrainGpuScene* terrainGpuScene)
{
    if (!fs::exists(folderPath)) {
        Logger::Error(LogCategory::General, "Location not found: " + folderPath);
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

    Logger::Info(LogCategory::General, "Scanning chunks and loading into Texture Arrays...");

    TerrainBaker::Initialize();

    for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
        if (entry.is_directory()) continue;
        std::string filename = entry.path().filename().string();

        int gx = 0, gz = 0;
        bool isChunk = (filename.find("o.chunk") != std::string::npos);

        if (isChunk && ParseGridCoordinates(filename, gx, gz)) {
            fs::path cdataPathObj = entry.path();
            cdataPathObj.replace_extension(".cdata");
            std::string cdataPath = cdataPathObj.string();

            // Проверяем и запекаем кэш, если его нет
            if (fs::exists(cdataPath)) {
                if (!TerrainBaker::IsChunkBaked(gx, gz)) {
                    TerrainBaker::BakeChunk(cdataPath, gx, gz);
                }
            }

            // Выделяем слой в Texture2DArray и грузим туда данные
            int sliceIndex = -1;
            if (arrayManager) {
                sliceIndex = arrayManager->AllocateSlice();
                arrayManager->LoadChunkIntoArray(gx, gz, sliceIndex);
            }

            // Создаем чанк
            auto chunk = std::make_unique<Chunk>(m_device, m_context);
            chunk->SetArraySlice(sliceIndex);

            if (chunk->Load(entry.path().string(), gx, gz, currentParams, outTexMgr.get())) {

                // Отправляем статику в GPU ---
                if (staticScene) {
                    for (const auto& parsedEntity : chunk->GetStaticEntities()) {
                        auto model = ModelManager::Get().GetModel(parsedEntity.ModelPath);
                        if (model) {
                            EntityLODs lods;
                            lods.Lod0 = model.get();
                            lods.Lod1 = nullptr;
                            lods.Lod2 = nullptr;
                            lods.HideLod1 = false;
                            lods.HideLod2 = false;

                            // Поиск LODов
                            if (EngineConfig::Get().Lod.Enabled) {
                                fs::path p(parsedEntity.ModelPath);
                                std::string stem = p.stem().string();
                                std::string parent = p.parent_path().string();
                                std::string basePath = parent.empty() ? stem : (parent + "/" + stem);

                                std::string compBase;
                                int partIdx = -1;
                                bool isComp = ParseCompositeName(stem, compBase, partIdx);
                                std::string sharedBasePath = parent.empty() ? compBase : (parent + "/" + compBase);

                                auto loadLOD = [&](const std::string& b, const std::string& sfx) -> StaticModel* {
                                    std::string g1 = b + sfx + ".gltf";
                                    if (fs::exists(g1)) return ModelManager::Get().GetModel(g1).get();
                                    std::string g2 = b + sfx + ".glb";
                                    if (fs::exists(g2)) return ModelManager::Get().GetModel(g2).get();
                                    return nullptr;
                                    };

                                lods.Lod1 = loadLOD(basePath, "_lod1");
                                if (!lods.Lod1 && isComp) {
                                    StaticModel* sharedLod = loadLOD(sharedBasePath, "_lod1");
                                    if (sharedLod) {
                                        if (partIdx == 1) lods.Lod1 = sharedLod;
                                        else lods.HideLod1 = true;
                                    }
                                }

                                lods.Lod2 = loadLOD(basePath, "_lod2");
                                if (!lods.Lod2 && isComp) {
                                    StaticModel* sharedLod = loadLOD(sharedBasePath, "_lod2");
                                    if (sharedLod) {
                                        if (partIdx == 1) lods.Lod2 = sharedLod;
                                        else lods.HideLod2 = true;
                                    }
                                }
                            }

                            InstanceData data;
                            data.Position = parsedEntity.Position;
                            data.Scale = parsedEntity.Scale;
                            data.RotXYZ = parsedEntity.RotXYZ;
                            data.EntityID = 0;
                            data.Padding[0] = data.Padding[1] = 0.0f;

                            staticScene->AddEntity(lods, data);
                        }
                    }
                }

                // Пакуем данные для GPU Террейна
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

    if (outTexMgr) outTexMgr->BuildArray();

    if (terrainGpuScene) {
        terrainGpuScene->BuildGpuBuffers(outTexMgr.get());
        Logger::Info(LogCategory::General, "TerrainGpuScene built successfully.");
    }
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

    Logger::Info(LogCategory::General, "[VLO] Registering Water: " + uid + (fileFound ? " from .vlo" : " from .chunk"));
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