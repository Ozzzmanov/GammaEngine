//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// ModelManager.cpp
// ================================================================================
#include "ModelManager.h"
#include "../Graphics/StaticModel.h"
#include "../Graphics/TreeModel.h"
#include "../Core/Logger.h"
#include "../Core/ResourceManager.h"
#include "../Resources/TextureBaker.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

void ModelManager::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    m_device = device;
    m_context = context;
    GAMMA_LOG_INFO(LogCategory::General, "ModelManager Initialized.");
}

bool ModelManager::ResolvePath(const std::string& input, std::string& outPath) {
    if (input.empty()) return false;

    std::string clean = input;
    // Очистка пробелов
    size_t first = clean.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return false;
    size_t last = clean.find_last_not_of(" \t\n\r");
    clean = clean.substr(first, (last - first + 1));

    // Нормализация слешей
    std::replace(clean.begin(), clean.end(), '\\', '/');

    // Проверки существования
    if (fs::exists(clean)) { outPath = clean; return true; }
    if (fs::exists("Assets/" + clean)) { outPath = "Assets/" + clean; return true; }
    if (fs::exists("res/" + clean)) { outPath = "res/" + clean; return true; }

    // Фикс для путей вида "/models/..."
    if (!clean.empty() && clean[0] == '/') {
        std::string sub = clean.substr(1);
        if (fs::exists("Assets/" + sub)) { outPath = "Assets/" + sub; return true; }
    }

    return false;
}

std::shared_ptr<StaticModel> ModelManager::GetModel(const std::string& path) {
    if (!m_device) return nullptr;

    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        return it->second;
    }

    auto model = std::make_shared<StaticModel>(m_device, m_context);

    if (model->Load(path)) {
        m_cache[path] = model;
        return model;
    }

    return nullptr;
}

std::shared_ptr<TreeModel> ModelManager::GetTreeModel(const std::string& path, int targetLod) {
    if (!m_device) return nullptr;

    // Уникальный ключ кэша: путь + номер ЛОДа
    std::string cacheKey = path + "_LOD" + std::to_string(targetLod);

    auto it = m_treeCache.find(cacheKey);
    if (it != m_treeCache.end()) {
        return it->second;
    }

    auto model = std::make_shared<TreeModel>(m_device, m_context);

    if (model->Load(path, targetLod)) {
        m_treeCache[cacheKey] = model;
        return model;
    }

    return nullptr;
}

void ModelManager::AllocateStaticGeometry(const std::vector<SimpleVertex>& verts, const std::vector<uint32_t>& inds, std::vector<ModelPart>& parts) {
    uint32_t baseVertex = static_cast<uint32_t>(m_allStaticVertices.size());
    uint32_t startIndex = static_cast<uint32_t>(m_allStaticIndices.size());

    m_allStaticVertices.insert(m_allStaticVertices.end(), verts.begin(), verts.end());
    m_allStaticIndices.insert(m_allStaticIndices.end(), inds.begin(), inds.end());

    // Сдвигаем индексы частей относительно глобального буфера
    for (auto& part : parts) {
        part.baseVertex += baseVertex;
        part.startIndex += startIndex;
    }
}

void ModelManager::AllocateFloraGeometry(const std::vector<TreeVertex>& verts, const std::vector<uint32_t>& inds, std::vector<TreePart>& opaqueParts, std::vector<TreePart>& alphaParts) {
    uint32_t baseVertex = static_cast<uint32_t>(m_allFloraVertices.size());
    uint32_t startIndex = static_cast<uint32_t>(m_allFloraIndices.size());

    m_allFloraVertices.insert(m_allFloraVertices.end(), verts.begin(), verts.end());
    m_allFloraIndices.insert(m_allFloraIndices.end(), inds.begin(), inds.end());

    for (auto& part : opaqueParts) {
        part.baseVertex += baseVertex;
        part.startIndex += startIndex;
    }
    for (auto& part : alphaParts) {
        part.baseVertex += baseVertex;
        part.startIndex += startIndex;
    }
}

void ModelManager::BuildGlobalBuffers() {
    GAMMA_LOG_INFO(LogCategory::General, "Building Mega-Buffers...");

    // СТАТИКА
    if (!m_allStaticVertices.empty()) {
        D3D11_BUFFER_DESC vbd = { static_cast<UINT>(sizeof(SimpleVertex) * m_allStaticVertices.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA vd = { m_allStaticVertices.data(), 0, 0 };
        HR_CHECK_VOID(m_device->CreateBuffer(&vbd, &vd, m_globalStaticVB.GetAddressOf()), "Create Global Static VB");

        D3D11_BUFFER_DESC ibd = { static_cast<UINT>(sizeof(uint32_t) * m_allStaticIndices.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA id = { m_allStaticIndices.data(), 0, 0 };
        HR_CHECK_VOID(m_device->CreateBuffer(&ibd, &id, m_globalStaticIB.GetAddressOf()), "Create Global Static IB");

        m_allStaticVertices.clear(); m_allStaticVertices.shrink_to_fit();
        m_allStaticIndices.clear(); m_allStaticIndices.shrink_to_fit();
    }

    //  ФЛОРА 
    if (!m_allFloraVertices.empty()) {
        D3D11_BUFFER_DESC vbd = { static_cast<UINT>(sizeof(TreeVertex) * m_allFloraVertices.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA vd = { m_allFloraVertices.data(), 0, 0 };
        HR_CHECK_VOID(m_device->CreateBuffer(&vbd, &vd, m_globalFloraVB.GetAddressOf()), "Create Global Flora VB");

        D3D11_BUFFER_DESC ibd = { static_cast<UINT>(sizeof(uint32_t) * m_allFloraIndices.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA id = { m_allFloraIndices.data(), 0, 0 };
        HR_CHECK_VOID(m_device->CreateBuffer(&ibd, &id, m_globalFloraIB.GetAddressOf()), "Create Global Flora IB");

        m_allFloraVertices.clear(); m_allFloraVertices.shrink_to_fit();
        m_allFloraIndices.clear(); m_allFloraIndices.shrink_to_fit();
    }
}

// ТЕКСТУРНЫЕ БАКЕТЫ
void ModelManager::RegisterMaterial(const std::string& albedo, const std::string& mrao, const std::string& normal,
    int& outBucketIndex, int& outSliceIndex)
{
    std::string safeAlbedo = albedo.empty() ? "Assets/GammaTexture/default_albedo.dds" : albedo;
    std::string safeMrao = mrao.empty() ? "Assets/GammaTexture/default_mrao.dds" : mrao;
    std::string safeNormal = normal.empty() ? "Assets/GammaTexture/default_normal.dds" : normal;

    // FAST PATH: МГНОВЕННЫЙ ВОЗВРАТ ИЗ МАНИФЕСТА
    if (TextureBaker::TryGetFromManifestCache(safeAlbedo, safeMrao, safeNormal, outBucketIndex, outSliceIndex)) {
        return; // Мы вообще не трогаем диск!
    }

    // sLOW PATH: Выполнится только при самом первом запуске уровня (когда нет кэша) ===
    fs::path sourcePath;
    if (!ResourceManager::Get().ResolveTexturePath(safeAlbedo, sourcePath)) {
        ResourceManager::Get().ResolveTexturePath("Assets/Texture/default_albedo.dds", sourcePath);
    }

    // FIXME: (ПРОИЗВОДИТЕЛЬНОСТЬ). 
    // Читать ВЕСЬ файл картинки с диска, декодировать его (в ОЗУ) только ради того, чтобы узнать Width/Height — это ОЧЕНЬ медленно.
    // Нужно написать или использовать функцию, которая читает только первые 32 байта файла (заголовок DDS/PNG) и парсит Width/Height.
    DirectX::ScratchImage image;
    DirectX::TexMetadata meta = {};
    if (ResourceManager::Get().LoadRawImage(sourcePath, image)) {
        meta = image.GetMetadata();
    }
    else {
        meta.width = 1024; meta.height = 1024;
    }

    int targetBucketID = TextureBaker::DetermineBucketID(static_cast<int>(meta.width), static_cast<int>(meta.height));

    if (m_bucketsList.empty()) {
        m_bucketsList.resize(4);
    }

    TextureBucket& bucket = m_bucketsList[targetBucketID];

    // Ищем, не добавляли ли мы эту комбинацию ранее
    for (size_t i = 0; i < bucket.albedoNames.size(); ++i) {
        if (bucket.albedoNames[i] == safeAlbedo &&
            bucket.mraoNames[i] == safeMrao &&
            bucket.normalNames[i] == safeNormal)
        {
            outBucketIndex = targetBucketID;
            outSliceIndex = static_cast<int>(i);
            return;
        }
    }

    outSliceIndex = static_cast<int>(bucket.albedoNames.size());
    outBucketIndex = targetBucketID;

    bucket.albedoNames.push_back(safeAlbedo);
    bucket.mraoNames.push_back(safeMrao);
    bucket.normalNames.push_back(safeNormal);
}

void ModelManager::PreloadManifest(const std::string& locationName) {
    TextureBaker::LoadManifestIntoCache(locationName);

    m_bucketsList.clear();
    m_bucketsList.resize(4);

    std::vector<MaterialManifestEntry> manifest = TextureBaker::LoadManifest(locationName);
    for (const auto& mat : manifest) {
        // Убеждаемся, что массивы достаточно велики для SliceID
        if (m_bucketsList[mat.BucketID].albedoNames.size() <= static_cast<size_t>(mat.SliceID)) {
            m_bucketsList[mat.BucketID].albedoNames.resize(mat.SliceID + 1);
            m_bucketsList[mat.BucketID].mraoNames.resize(mat.SliceID + 1);
            m_bucketsList[mat.BucketID].normalNames.resize(mat.SliceID + 1);
        }

        m_bucketsList[mat.BucketID].albedoNames[mat.SliceID] = mat.AlbedoPath;
        m_bucketsList[mat.BucketID].mraoNames[mat.SliceID] = mat.MraoPath;
        m_bucketsList[mat.BucketID].normalNames[mat.SliceID] = mat.NormalPath;
    }

    GAMMA_LOG_INFO(LogCategory::Texture, "Preloaded manifest: restored " + std::to_string(manifest.size()) + " textures.");
}

void ModelManager::BuildTextureBuckets(const std::string& locationName) {
    GAMMA_LOG_INFO(LogCategory::Texture, "Initializing Texture Buckets for: " + locationName);

    // Формируем актуальный манифест
    std::vector<MaterialManifestEntry> currentMaterials;
    for (int b = 0; b < 4; ++b) {
        if (b >= m_bucketsList.size()) break;

        for (size_t i = 0; i < m_bucketsList[b].albedoNames.size(); ++i) {
            MaterialManifestEntry entry;
            entry.AlbedoPath = m_bucketsList[b].albedoNames[i];
            entry.MraoPath = m_bucketsList[b].mraoNames[i];
            entry.NormalPath = m_bucketsList[b].normalNames[i];
            entry.BucketID = b;
            entry.SliceID = static_cast<int>(i);

            // Находим самый свежий файл из трех
            uint64_t tA = TextureBaker::GetFileTimestamp(entry.AlbedoPath);
            uint64_t tM = TextureBaker::GetFileTimestamp(entry.MraoPath);
            uint64_t tN = TextureBaker::GetFileTimestamp(entry.NormalPath);
            entry.Timestamp = std::max({ tA, tM, tN });

            currentMaterials.push_back(entry);
        }
    }

    // Валидация кэша. Если провалена - печем!
    if (!TextureBaker::ValidateCache(locationName, currentMaterials)) {
        TextureBaker::BakeArrays(locationName, m_bucketsList, currentMaterials);
    }

    // Мгновенная загрузка бакетов (Zero-copy)
    // FIXME: Загрузка 4 бакетов (по 3 Texture2DArray в каждом) происходит синхронно. 
    // Это огромный объем данных, который заморозит игру на несколько секунд. Перевести на TaskScheduler
    std::string cleanName = fs::path(locationName).filename().string();

    for (int b = 0; b < 4; ++b) {
        if (b >= m_bucketsList.size() || m_bucketsList[b].albedoNames.empty()) continue;

        GAMMA_LOG_INFO(LogCategory::Texture, "Loading Bucket " + std::to_string(b) + " (" + std::to_string(TextureBaker::GetBucketResolution(b)) + ")");

        m_bucketsList[b].AlbedoArray = std::make_unique<TextureArray>(m_device, m_context);
        m_bucketsList[b].MRAOArray = std::make_unique<TextureArray>(m_device, m_context);
        m_bucketsList[b].NormalArray = std::make_unique<TextureArray>(m_device, m_context);

        std::string baseName = "Cache/Textures/" + cleanName + "_Bucket" + std::to_string(b);

        m_bucketsList[b].AlbedoArray->LoadFromBakedFile(baseName + "_Albedo.dds");
        m_bucketsList[b].MRAOArray->LoadFromBakedFile(baseName + "_MRAO.dds");
        m_bucketsList[b].NormalArray->LoadFromBakedFile(baseName + "_Normal.dds");
    }
}