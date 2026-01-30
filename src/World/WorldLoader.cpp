//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// WorldLoader.cpp
// Реализация загрузчика мира.
// ================================================================================

#include "WorldLoader.h"
#include "../Core/Logger.h"
#include "Chunk.h"
#include "WaterVLO.h"
#include "../Resources/SpaceSettings.h"
#include "../Graphics/LevelTextureManager.h"

#include <filesystem>
#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;
using namespace DirectX;

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
    bool useLegacyRender)
{
    if (!fs::exists(folderPath)) {
        Logger::Error(LogCategory::General, "Location not found: " + folderPath);
        return;
    }

    if (!useLegacyRender) {
        outTexMgr = std::make_unique<LevelTextureManager>(m_device, m_context);
    }

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

    Logger::Info(LogCategory::General, "Scanning chunks for VLO references...");

    for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
        if (entry.is_directory()) continue;
        std::string filename = entry.path().filename().string();

        int gx = 0, gz = 0;
        bool isChunk = (filename.find("o.chunk") != std::string::npos);

        if (isChunk && ParseGridCoordinates(filename, gx, gz)) {
            auto chunk = std::make_unique<Chunk>(m_device, m_context);
            LevelTextureManager* mgr = useLegacyRender ? nullptr : outTexMgr.get();

            // Загрузка чанка.
            // Важно: чанк внутри себя может триггернуть LoadVLOByUID через глобальный колбэк.
            // Мы передаем this или обрабатываем это на уровне Engine (ниже).
            if (chunk->Load(entry.path().string(), gx, gz, currentParams, mgr, nullptr, false)) {
                outChunks.push_back(std::move(chunk));
            }
        }
    }

    // Построение массивов текстур
    if (!useLegacyRender && outTexMgr) {
        outTexMgr->BuildArray();
    }
}

void WorldLoader::LoadVLOByUID(const std::string& rawUid, const std::string& type,
    const DirectX::XMFLOAT3& positionFromChunk, const DirectX::XMFLOAT3& scaleFromChunk,
    std::vector<std::shared_ptr<WaterVLO>>& outWaterObjects)
{
    std::string uid = CleanUID(rawUid);
    if (uid.empty()) return;

    // Проверка кеша
    if (m_globalWaterStorage.count(uid) > 0) return;

    auto water = std::make_shared<WaterVLO>(m_device, m_context);

    // Поиск файла .vlo
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
        if (!water->Load(foundPath.string())) {
            fileFound = false;
        }
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
        // Логика обрезки мусора
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