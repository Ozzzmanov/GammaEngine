//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// TextureBaker.h
// Отвечает за объединение разрозненных текстур сцены в гигантские 
// Texture Arrays (Texture2DArray) для оптимизации Draw Calls (Bindless подход).
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <string>
#include <vector>
#include <unordered_map>
#include "../Graphics/ModelManager.h"

// Константы бакетов текстур
enum class TextureBucketType : int {
    Res256 = 0,
    Res512 = 1,
    Res1024 = 2,
    Res2048 = 3,
    Count = 4
};

/**
 * @struct MaterialManifestEntry
 * @brief Метаданные одного материала в кэше.
 */
struct MaterialManifestEntry {
    std::string AlbedoPath;
    std::string MraoPath;
    std::string NormalPath;
    int BucketID;
    int SliceID;
    uint64_t Timestamp; // Максимальная дата изменения среди 3-х текстур
};

/**
 * @class TextureBaker
 * @brief Утилита для запекания и кэширования массивов текстур.
 */
class TextureBaker {
public:
    static void Initialize();

    // --- Параметры бакетов ---
    static int DetermineBucketID(int width, int height);
    static int GetBucketResolution(int bucketID);

    // --- Управление Кэшем (Манифестом) ---
    static bool ValidateCache(const std::string& locationName, const std::vector<MaterialManifestEntry>& currentMaterials);
    static void SaveManifest(const std::string& locationName, const std::vector<MaterialManifestEntry>& materials);
    static std::vector<MaterialManifestEntry> LoadManifest(const std::string& locationName);

    // --- Процесс запекания ---
    static void BakeArrays(const std::string& locationName, const std::vector<TextureBucket>& buckets, const std::vector<MaterialManifestEntry>& manifest);

    // --- In-Memory Кэш манифеста для быстрой загрузки ---
    static void LoadManifestIntoCache(const std::string& locationName);
    static bool TryGetFromManifestCache(const std::string& albedo, const std::string& mrao, const std::string& normal, int& outBucket, int& outSlice);
    static void ClearManifestCache();

    // Получает дату модификации файла (Timestamp) для проверки инвалидации кэша
    static uint64_t GetFileTimestamp(const std::string& path);

private:
    static std::string GetManifestPath(const std::string& locationName);
    static uint64_t GenerateMaterialHash(const std::string& albedo, const std::string& mrao, const std::string& normal);
};