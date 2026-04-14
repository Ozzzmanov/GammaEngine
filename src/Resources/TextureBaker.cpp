//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// TextureBaker.cpp
// ================================================================================
#include "TextureBaker.h"
#include "../Core/Logger.h"
#include "../Core/ResourceManager.h"
#include <filesystem>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;

// In-Memory кэш манифеста. Используем uint64_t хэш вместо конкатенации строк для скорости.
static std::unordered_map<uint64_t, MaterialManifestEntry> g_ManifestCache;

void TextureBaker::Initialize() {
    // FIXME: TIER B. Хардкод папки "Cache/Textures". Должно браться из настроек движка.
    if (!fs::exists("Cache/Textures")) {
        fs::create_directories("Cache/Textures");
        GAMMA_LOG_INFO(LogCategory::System, "Created Cache/Textures directory.");
    }
}

int TextureBaker::DetermineBucketID(int width, int height) {
    int maxDim = std::max(width, height);
    if (maxDim <= 256) return static_cast<int>(TextureBucketType::Res256);
    if (maxDim <= 512) return static_cast<int>(TextureBucketType::Res512);
    if (maxDim <= 1024) return static_cast<int>(TextureBucketType::Res1024);

    return static_cast<int>(TextureBucketType::Res2048); // Все что выше 1024, жмем в 2048
}

int TextureBaker::GetBucketResolution(int bucketID) {
    switch (static_cast<TextureBucketType>(bucketID)) {
    case TextureBucketType::Res256:  return 256;
    case TextureBucketType::Res512:  return 512;
    case TextureBucketType::Res1024: return 1024;
    case TextureBucketType::Res2048: return 2048;
    default:                         return 512; // Fallback
    }
}

uint64_t TextureBaker::GetFileTimestamp(const std::string& path) {
    if (path.empty()) return 0;

    fs::path p;
    // FIXME: TIER A. Каждый вызов GetFileTimestamp лезет в ResolveTexturePath, 
    // который внутри сканирует m_fileRegistry (а там могут быть тысячи файлов). 
    // Это нужно кэшировать! !"!11
    if (ResourceManager::Get().ResolveTexturePath(path, p)) {
        std::error_code ec;
        auto ftime = fs::last_write_time(p, ec);
        if (!ec) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(ftime.time_since_epoch()).count();
        }
    }
    return 0;
}

std::string TextureBaker::GetManifestPath(const std::string& locationName) {
    std::string cleanName = fs::path(locationName).filename().string();
    return "Cache/Textures/" + cleanName + ".manifest";
}

uint64_t TextureBaker::GenerateMaterialHash(const std::string& albedo, const std::string& mrao, const std::string& normal) {
    // Простой и быстрый хэш для комбинации из 3 строк (djb2 или std::hash)
    std::hash<std::string> hasher;
    uint64_t h1 = hasher(albedo);
    uint64_t h2 = hasher(mrao);
    uint64_t h3 = hasher(normal);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
}

bool TextureBaker::ValidateCache(const std::string& locationName, const std::vector<MaterialManifestEntry>& currentMaterials) {
    std::vector<MaterialManifestEntry> cachedMaterials = LoadManifest(locationName);

    // Изменилось количество материалов
    if (cachedMaterials.size() != currentMaterials.size()) {
        GAMMA_LOG_WARN(LogCategory::Texture, "Manifest mismatch: Material count changed. Bake required.");
        return false;
    }

    // Проверка соответствия каждого материала
    for (size_t i = 0; i < currentMaterials.size(); ++i) {
        const auto& cur = currentMaterials[i];
        const auto& cache = cachedMaterials[i];

        if (cur.AlbedoPath != cache.AlbedoPath || cur.MraoPath != cache.MraoPath || cur.NormalPath != cache.NormalPath ||
            cur.BucketID != cache.BucketID || cur.SliceID != cache.SliceID) {
            GAMMA_LOG_WARN(LogCategory::Texture, "Manifest mismatch: Material paths or slots changed. Bake required.");
            return false;
        }

        if (cur.Timestamp > cache.Timestamp) {
            GAMMA_LOG_WARN(LogCategory::Texture, "Manifest mismatch: Texture file modified (" + cur.AlbedoPath + "). Bake required.");
            return false;
        }
    }

    // Проверка физического существования DDS файлов
    std::string cleanName = fs::path(locationName).filename().string();
    for (int i = 0; i < static_cast<int>(TextureBucketType::Count); ++i) {
        std::string albedoTest = "Cache/Textures/" + cleanName + "_Bucket" + std::to_string(i) + "_Albedo.dds";

        bool isBucketUsed = false;
        for (const auto& mat : currentMaterials) {
            if (mat.BucketID == i) { isBucketUsed = true; break; }
        }

        if (isBucketUsed && !fs::exists(albedoTest)) {
            GAMMA_LOG_WARN(LogCategory::Texture, "Manifest mismatch: Missing DDS array " + albedoTest);
            return false;
        }
    }

    GAMMA_LOG_INFO(LogCategory::Texture, "Texture Cache is VALID for location: " + locationName);
    return true;
}

void TextureBaker::SaveManifest(const std::string& locationName, const std::vector<MaterialManifestEntry>& materials) {
    std::string path = GetManifestPath(locationName);
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return;

    // Магическое число "TMAN" и Версия (1)
    uint32_t magic = 0x4E414D54;
    uint32_t version = 1;
    uint32_t count = static_cast<uint32_t>(materials.size());

    file.write(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<char*>(&version), sizeof(version));
    file.write(reinterpret_cast<char*>(&count), sizeof(count));

    auto writeString = [&file](const std::string& str) {
        uint32_t len = static_cast<uint32_t>(str.size());
        file.write(reinterpret_cast<char*>(&len), sizeof(len));
        if (len > 0) file.write(str.c_str(), len);
        };

    for (const auto& mat : materials) {
        writeString(mat.AlbedoPath);
        writeString(mat.MraoPath);
        writeString(mat.NormalPath);
        file.write(reinterpret_cast<const char*>(&mat.BucketID), sizeof(mat.BucketID));
        file.write(reinterpret_cast<const char*>(&mat.SliceID), sizeof(mat.SliceID));
        file.write(reinterpret_cast<const char*>(&mat.Timestamp), sizeof(mat.Timestamp));
    }
}

std::vector<MaterialManifestEntry> TextureBaker::LoadManifest(const std::string& locationName) {
    std::vector<MaterialManifestEntry> result;
    std::string path = GetManifestPath(locationName);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return result;

    uint32_t magic, version, count;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    if (magic != 0x4E414D54 || version != 1) return result;

    auto readString = [&file]() -> std::string {
        uint32_t len;
        file.read(reinterpret_cast<char*>(&len), sizeof(len));
        if (len == 0) return "";
        std::string str(len, '\0');
        file.read(&str[0], len);
        return str;
        };

    for (uint32_t i = 0; i < count; ++i) {
        MaterialManifestEntry mat;
        mat.AlbedoPath = readString();
        mat.MraoPath = readString();
        mat.NormalPath = readString();
        file.read(reinterpret_cast<char*>(&mat.BucketID), sizeof(mat.BucketID));
        file.read(reinterpret_cast<char*>(&mat.SliceID), sizeof(mat.SliceID));
        file.read(reinterpret_cast<char*>(&mat.Timestamp), sizeof(mat.Timestamp));
        result.push_back(mat);
    }
    return result;
}

// Вспомогательная локальная функция (скрыта от остальных файлов)
static bool BakeTextureList(const std::vector<std::string>& files, int resolution, const std::string& outPath) {
    if (files.empty()) return false;

    GAMMA_LOG_INFO(LogCategory::System, "Baking Array: " + outPath + " (" + std::to_string(files.size()) + " slices, " + std::to_string(resolution) + "x" + std::to_string(resolution) + ")");

    // Инициализируем некомпрессированный массив
    DirectX::ScratchImage uncompressedArray;
    HRESULT hr = uncompressedArray.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, resolution, resolution, files.size(), 1);
    if (FAILED(hr)) return false;

    // Обрабатываем каждый слой
    for (size_t i = 0; i < files.size(); ++i) {
        fs::path sourcePath;
        if (!ResourceManager::Get().ResolveTexturePath(files[i], sourcePath)) {
            ResourceManager::Get().ResolveTexturePath("Assets/Texture/default.dds", sourcePath);
        }

        DirectX::ScratchImage image;
        if (ResourceManager::Get().LoadRawImage(sourcePath, image)) {
            DirectX::ScratchImage rgbaImage;

            if (DirectX::IsCompressed(image.GetMetadata().format)) {
                DirectX::Decompress(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM, rgbaImage);
            }
            else if (image.GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM) {
                DirectX::Convert(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgbaImage);
            }
            else {
                rgbaImage = std::move(image);
            }

            DirectX::ScratchImage resizedImage;
            if (rgbaImage.GetMetadata().width != resolution || rgbaImage.GetMetadata().height != resolution) {
                DirectX::Resize(rgbaImage.GetImages(), rgbaImage.GetImageCount(), rgbaImage.GetMetadata(), resolution, resolution, DirectX::TEX_FILTER_LINEAR, resizedImage);
            }
            else {
                resizedImage = std::move(rgbaImage);
            }

            // БЕЗОПАСНОЕ КОПИРОВАНИЕ ПИКСЕЛЕЙ
            const DirectX::Image* dstImg = uncompressedArray.GetImage(0, i, 0);
            const DirectX::Image* srcImg = resizedImage.GetImage(0, 0, 0);

            if (dstImg && srcImg && dstImg->rowPitch == srcImg->rowPitch && dstImg->height == srcImg->height) {
                memcpy(dstImg->pixels, srcImg->pixels, dstImg->rowPitch * dstImg->height);
            }
            else {
                GAMMA_LOG_ERROR(LogCategory::Texture, "CRITICAL: Texture copy failed due to dimension mismatch in BakeTextureList.");
            }
        }
    }

    // Мип-мапы
    DirectX::ScratchImage mippedArray;
    hr = DirectX::GenerateMipMaps(uncompressedArray.GetImages(), uncompressedArray.GetImageCount(), uncompressedArray.GetMetadata(), DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_SEPARATE_ALPHA, 0, mippedArray);
    if (FAILED(hr)) mippedArray = std::move(uncompressedArray);

    // Компрессия
    DirectX::ScratchImage compressedArray;
    hr = DirectX::Compress(mippedArray.GetImages(), mippedArray.GetImageCount(), mippedArray.GetMetadata(), DXGI_FORMAT_BC3_UNORM, DirectX::TEX_COMPRESS_DEFAULT, 1.0f, compressedArray);

    // Сохранение
    std::wstring wOut(outPath.begin(), outPath.end());
    if (SUCCEEDED(hr)) {
        DirectX::SaveToDDSFile(compressedArray.GetImages(), compressedArray.GetImageCount(), compressedArray.GetMetadata(), DirectX::DDS_FLAGS_NONE, wOut.c_str());
    }
    else {
        DirectX::SaveToDDSFile(mippedArray.GetImages(), mippedArray.GetImageCount(), mippedArray.GetMetadata(), DirectX::DDS_FLAGS_NONE, wOut.c_str());
    }

    return true;
}

void TextureBaker::BakeArrays(const std::string& locationName, const std::vector<TextureBucket>& buckets, const std::vector<MaterialManifestEntry>& manifest) {
    GAMMA_LOG_INFO(LogCategory::System, "--- STARTING TEXTURE BAKE FOR: " + locationName + " ---");
    std::string cleanName = fs::path(locationName).filename().string();

    for (int b = 0; b < static_cast<int>(TextureBucketType::Count); ++b) {
        if (b >= static_cast<int>(buckets.size()) || buckets[b].albedoNames.empty()) continue;

        int res = GetBucketResolution(b);
        std::string baseName = "Cache/Textures/" + cleanName + "_Bucket" + std::to_string(b);

        BakeTextureList(buckets[b].albedoNames, res, baseName + "_Albedo.dds");
        BakeTextureList(buckets[b].mraoNames, res, baseName + "_MRAO.dds");
        BakeTextureList(buckets[b].normalNames, res, baseName + "_Normal.dds");
    }

    SaveManifest(locationName, manifest);
    GAMMA_LOG_INFO(LogCategory::System, "--- TEXTURE BAKE COMPLETED ---");
}

void TextureBaker::LoadManifestIntoCache(const std::string& locationName) {
    g_ManifestCache.clear();
    std::vector<MaterialManifestEntry> mats = LoadManifest(locationName);

    for (const auto& mat : mats) {
        uint64_t hashKey = GenerateMaterialHash(mat.AlbedoPath, mat.MraoPath, mat.NormalPath);
        g_ManifestCache[hashKey] = mat;
    }
}

bool TextureBaker::TryGetFromManifestCache(const std::string& albedo, const std::string& mrao, const std::string& normal, int& outBucket, int& outSlice) {
    if (g_ManifestCache.empty()) return false;

    uint64_t hashKey = GenerateMaterialHash(albedo, mrao, normal);
    auto it = g_ManifestCache.find(hashKey);

    if (it != g_ManifestCache.end()) {
        outBucket = it->second.BucketID;
        outSlice = it->second.SliceID;
        return true;
    }
    return false;
}

void TextureBaker::ClearManifestCache() {
    g_ManifestCache.clear();
}