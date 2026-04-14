//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GeometryBaker.cpp
// ================================================================================
#include "GeometryBaker.h"
#include "../Graphics/ModelManager.h"
#include "../Graphics/StaticModel.h"
#include "../Graphics/TreeModel.h"
#include "../Core/Logger.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

#pragma pack(push, 1)
struct GCacheHeader {
    char Magic[4];
    uint32_t Version;
    uint32_t StatVerts;
    uint32_t StatInds;
    uint32_t FloraVerts;
    uint32_t FloraInds;
    uint32_t StatModelsCount;
    uint32_t FloraModelsCount;
};
#pragma pack(pop)

constexpr uint32_t GCACHE_VERSION = 1;

// Хелперы сериализации строк
static void WriteStr(std::ofstream& f, const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.length());
    f.write(reinterpret_cast<const char*>(&len), sizeof(len));
    if (len > 0) f.write(s.data(), len);
}

static std::string ReadStr(std::ifstream& f) {
    uint32_t len;
    f.read(reinterpret_cast<char*>(&len), sizeof(len));
    if (len == 0) return "";

    std::string s(len, '\0');
    f.read(&s[0], len);
    return s;
}

void GeometryBaker::Initialize() {
    // FIXME: Вынести хардкод папки кэша в глобальные константы/конфиг.
    if (!fs::exists("Cache/Geometry")) {
        fs::create_directories("Cache/Geometry");
        GAMMA_LOG_INFO(LogCategory::System, "Created Cache/Geometry directory.");
    }
}

std::string GeometryBaker::GetCachePath(const std::string& levelPath) {
    std::string cleanName = fs::path(levelPath).filename().string();
    return "Cache/Geometry/" + cleanName + ".gcache";
}

bool GeometryBaker::LoadCache(const std::string& levelPath) {
    std::string cachePath = GetCachePath(levelPath);
    if (!fs::exists(cachePath)) return false;

    // Умная инвалидация кэша: если исходный уровень обновился
    if (fs::exists(levelPath)) {
        auto levelTime = fs::last_write_time(levelPath);
        auto cacheTime = fs::last_write_time(cachePath);
        if (levelTime > cacheTime) {
            GAMMA_LOG_INFO(LogCategory::System, "Level file is newer than Geometry Cache. Rebaking required.");
            return false;
        }
    }

    std::ifstream f(cachePath, std::ios::binary);
    if (!f.is_open()) return false;

    GCacheHeader header;
    f.read(reinterpret_cast<char*>(&header), sizeof(GCacheHeader));

    if (strncmp(header.Magic, "GCAH", 4) != 0 || header.Version != GCACHE_VERSION) {
        GAMMA_LOG_WARN(LogCategory::System, "Geometry Cache version mismatch or corrupted. Rebaking required.");
        return false;
    }

    GAMMA_LOG_INFO(LogCategory::System, "Loading Geometry Cache: " + cachePath);

    // FIXME: Нарушение инкапсуляции
    // GeometryBaker не должен напрямую лезть в публичные переменные ModelManager (m_allStaticVertices и т.д.).
    // !!!!! Это нужно обернуть в методы ModelManager
    auto& mm = ModelManager::Get();

    // МГНОВЕННОЕ КОПИРОВАНИЕ МЕГА-МАССИВОВ (Zero-parsing)
    mm.m_allStaticVertices.resize(header.StatVerts);
    if (header.StatVerts > 0) {
        f.read(reinterpret_cast<char*>(mm.m_allStaticVertices.data()), header.StatVerts * sizeof(SimpleVertex));
    }

    mm.m_allStaticIndices.resize(header.StatInds);
    if (header.StatInds > 0) {
        f.read(reinterpret_cast<char*>(mm.m_allStaticIndices.data()), header.StatInds * sizeof(uint32_t));
    }

    mm.m_allFloraVertices.resize(header.FloraVerts);
    if (header.FloraVerts > 0) {
        f.read(reinterpret_cast<char*>(mm.m_allFloraVertices.data()), header.FloraVerts * sizeof(TreeVertex));
    }

    mm.m_allFloraIndices.resize(header.FloraInds);
    if (header.FloraInds > 0) {
        f.read(reinterpret_cast<char*>(mm.m_allFloraIndices.data()), header.FloraInds * sizeof(uint32_t));
    }

    // ВОССТАНОВЛЕНИЕ СТАТИКИ
    for (uint32_t i = 0; i < header.StatModelsCount; ++i) {
        std::string pathId = ReadStr(f);

        DirectX::BoundingSphere sphere;
        f.read(reinterpret_cast<char*>(&sphere), sizeof(sphere));

        uint32_t partsCount;
        f.read(reinterpret_cast<char*>(&partsCount), sizeof(partsCount));

        std::vector<ModelPart> parts(partsCount);
        if (partsCount > 0) {
            f.read(reinterpret_cast<char*>(parts.data()), partsCount * sizeof(ModelPart));
        }

        // Создаем пустышку и кормим ей готовые данные
        auto model = std::make_shared<StaticModel>(mm.m_device, mm.m_context);
        model->LoadFromCache(sphere, parts);
        mm.m_cache[pathId] = model;
    }

    // ВОССТАНОВЛЕНИЕ ДЕРЕВЬЕВ
    for (uint32_t i = 0; i < header.FloraModelsCount; ++i) {
        std::string pathId = ReadStr(f);

        DirectX::BoundingSphere sphere;
        f.read(reinterpret_cast<char*>(&sphere), sizeof(sphere));

        uint32_t opCount, alCount;
        f.read(reinterpret_cast<char*>(&opCount), sizeof(opCount));

        std::vector<TreePart> opaque(opCount);
        if (opCount > 0) {
            f.read(reinterpret_cast<char*>(opaque.data()), opCount * sizeof(TreePart));
        }

        f.read(reinterpret_cast<char*>(&alCount), sizeof(alCount));
        std::vector<TreePart> alpha(alCount);
        if (alCount > 0) {
            f.read(reinterpret_cast<char*>(alpha.data()), alCount * sizeof(TreePart));
        }

        auto tree = std::make_shared<TreeModel>(mm.m_device, mm.m_context);
        tree->LoadFromCache(sphere, opaque, alpha);
        mm.m_treeCache[pathId] = tree;
    }

    GAMMA_LOG_INFO(LogCategory::System, "Geometry Cache loaded instantly!");
    return true;
}

void GeometryBaker::SaveCache(const std::string& levelPath) {
    std::string cachePath = GetCachePath(levelPath);
    std::ofstream f(cachePath, std::ios::binary);
    if (!f.is_open()) return;

    GAMMA_LOG_INFO(LogCategory::System, "Baking Geometry Cache to SSD...");

    auto& mm = ModelManager::Get();

    GCacheHeader header = {};
    memcpy(header.Magic, "GCAH", 4);
    header.Version = GCACHE_VERSION;
    header.StatVerts = static_cast<uint32_t>(mm.m_allStaticVertices.size());
    header.StatInds = static_cast<uint32_t>(mm.m_allStaticIndices.size());
    header.FloraVerts = static_cast<uint32_t>(mm.m_allFloraVertices.size());
    header.FloraInds = static_cast<uint32_t>(mm.m_allFloraIndices.size());
    header.StatModelsCount = static_cast<uint32_t>(mm.m_cache.size());
    header.FloraModelsCount = static_cast<uint32_t>(mm.m_treeCache.size());

    f.write(reinterpret_cast<const char*>(&header), sizeof(GCacheHeader));

    // Пишем Дампы геометрии
    if (header.StatVerts > 0)  f.write(reinterpret_cast<const char*>(mm.m_allStaticVertices.data()), header.StatVerts * sizeof(SimpleVertex));
    if (header.StatInds > 0)   f.write(reinterpret_cast<const char*>(mm.m_allStaticIndices.data()), header.StatInds * sizeof(uint32_t));
    if (header.FloraVerts > 0) f.write(reinterpret_cast<const char*>(mm.m_allFloraVertices.data()), header.FloraVerts * sizeof(TreeVertex));
    if (header.FloraInds > 0)  f.write(reinterpret_cast<const char*>(mm.m_allFloraIndices.data()), header.FloraInds * sizeof(uint32_t));

    // Пишем Статику
    for (auto& pair : mm.m_cache) {
        WriteStr(f, pair.first);
        auto sphere = pair.second->GetBoundingSphere();
        f.write(reinterpret_cast<const char*>(&sphere), sizeof(sphere));

        uint32_t pCount = static_cast<uint32_t>(pair.second->GetPartCount());
        f.write(reinterpret_cast<const char*>(&pCount), sizeof(pCount));

        for (uint32_t i = 0; i < pCount; ++i) {
            auto part = pair.second->GetPart(i);
            f.write(reinterpret_cast<const char*>(&part), sizeof(ModelPart));
        }
    }

    // Пишем Деревья
    for (auto& pair : mm.m_treeCache) {
        WriteStr(f, pair.first);
        auto sphere = pair.second->GetBoundingSphere();
        f.write(reinterpret_cast<const char*>(&sphere), sizeof(sphere));

        const auto& opParts = pair.second->GetOpaqueParts();
        uint32_t opCount = static_cast<uint32_t>(opParts.size());
        f.write(reinterpret_cast<const char*>(&opCount), sizeof(opCount));

        if (opCount > 0) {
            f.write(reinterpret_cast<const char*>(opParts.data()), opCount * sizeof(TreePart));
        }

        const auto& alParts = pair.second->GetAlphaParts();
        uint32_t alCount = static_cast<uint32_t>(alParts.size());
        f.write(reinterpret_cast<const char*>(&alCount), sizeof(alCount));

        if (alCount > 0) {
            f.write(reinterpret_cast<const char*>(alParts.data()), alCount * sizeof(TreePart));
        }
    }

    f.close();
    GAMMA_LOG_INFO(LogCategory::System, "Geometry Cache Baked Successfully.");
}