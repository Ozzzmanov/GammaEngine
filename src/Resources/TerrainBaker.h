//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// TerrainBaker.h
// Отвечает за оффлайн/рантайм запекание террейна. 
// Склеивает чанки в гигантские Texture2DArray (Heights, Normals, Albedo Indices)
// и рассчитывает глобальное HBAO (Ambient Occlusion) на CPU.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <string>
#include <vector>
#include <map>

// --- Константы террейна ---
constexpr int MAX_TERRAIN_LAYERS = 24;

struct LayerMeta {
    char textureName[128];
    DirectX::XMFLOAT4 uProj;
    DirectX::XMFLOAT4 vProj;
};

struct ChunkBakeTask {
    int gx;
    int gz;
    std::string cdataPath;
};

// Единая мета-информация для целого чанка (вместо мелких .meta файлов)
struct UnifiedChunkMeta {
    int GridX;
    int GridZ;
    int SliceIndex;
    float MinHeight;
    float MaxHeight;
    uint32_t NumLayers;
    LayerMeta Layers[MAX_TERRAIN_LAYERS];
};

/**
 * @class TerrainBaker
 * @brief Утилита для процессинга и запекания глобальных данных ландшафта.
 */
class TerrainBaker {
public:
    static const int HEIGHTMAP_SIZE = 64;
    static const int BLENDMAP_SIZE = 128;
    static const int HOLEMAP_SIZE = 32;

    // --- НАСТРОЙКИ КАЧЕСТВА AO ---
    static const int AO_SAMPLES = 32;       // Количество лучей на пиксель (больше = красивее, но медленнее)
    static const float AO_RADIUS_METERS;    // Радиус запекания в метрах (захватывает каньоны)
    static const float AO_INTENSITY;        // Сила затенения [0..1]

    static void Initialize();

    // Проверяет, актуален ли глобальный кэш
    static bool IsLocationBaked(const std::string& locationName, size_t expectedChunkCount);

    // Печет ВСЮ локацию целиком в памяти и сохраняет мега-файлы (Legacy .cdata)
    static bool BakeLocationInMemory(const std::string& folderPath, const std::vector<ChunkBakeTask>& chunks);

    // Печет локацию из нового быстрого формата .glevel
    static bool BakeFromGLevel(const std::string& folderPath, const std::string& glevelPath);

    static std::string GetCachePrefix(const std::string& locationName);

private:
    // --- ФУНКЦИЯ РАСЧЕТА МАКРО-AO ---
    static float CalculateAOSample(
        const std::vector<float>& globalHeights,
        const std::vector<UnifiedChunkMeta>& metaList,
        const std::map<std::pair<int, int>, int>& gridToSlice,
        int currentSlice, int x, int y, float spacing);
};