//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// WorldLoader.h
// Парсер и загрузчик игровых уровней. Поддерживает быстрый формат .glevel
// и legacy формат .chunk.
// ================================================================================
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <d3d11.h>
#include <DirectXMath.h>
#include <unordered_map>

// ==========================================
// СТРУКТУРЫ НОВОГО ФОРМАТА .GLEVEL
// ==========================================
#pragma pack(push, 1)
struct GLevelHeader {
    char Magic[4];           // "GLVL"
    uint32_t Version;
    uint32_t NumChunks;
    uint32_t StringPoolSize;
    uint32_t TocOffset;
    char Padding[12];
};

struct GChunkHeader {
    int32_t GridX;
    int32_t GridZ;
    float MinHeight;
    float MaxHeight;
};

struct GEntity {
    uint16_t ModelID;
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Scale;
    DirectX::XMFLOAT4 RotQuat;
};
#pragma pack(pop)
// ==========================================

class Chunk;
class WaterVLO;
class SpaceSettings;
class LevelTextureManager;
class StaticGpuScene;
class TerrainArrayManager;
class TerrainGpuScene;
class FloraGpuScene;

/**
 * @class WorldLoader
 * @brief Загрузчик геометрии, террейна и сущностей игрового мира.
 */
class WorldLoader {
public:
    WorldLoader(ID3D11Device* device, ID3D11DeviceContext* context);
    ~WorldLoader();

    // FIXME: TIER B. Слишком много параметров. Лучше создать структуру `SceneContext` 
    // или `WorldContext` и передавать её по ссылке.
    void LoadLocation(const std::string& folderPath,
        std::vector<std::unique_ptr<Chunk>>& outChunks,
        std::vector<std::shared_ptr<WaterVLO>>& outWaterObjects,
        std::unique_ptr<SpaceSettings>& outSettings,
        std::unique_ptr<LevelTextureManager>& outTexMgr,
        StaticGpuScene* staticScene,
        FloraGpuScene* floraScene,
        TerrainArrayManager* arrayManager,
        TerrainGpuScene* terrainGpuScene);

    void LoadVLOByUID(const std::string& rawUid, const std::string& type,
        const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& scale,
        std::vector<std::shared_ptr<WaterVLO>>& outWaterObjects);

private:
    bool ParseGridCoordinates(const std::string& filename, int& outX, int& outZ);
    std::string CleanUID(std::string input);

    bool LoadFromGLevel(const std::string& filepath,
        std::vector<std::unique_ptr<Chunk>>& outChunks,
        std::vector<std::shared_ptr<WaterVLO>>& outWaterObjects,
        const std::unordered_map<uint64_t, int>& coordToSlice,
        StaticGpuScene* staticScene,
        FloraGpuScene* floraScene,
        TerrainGpuScene* terrainGpuScene,
        LevelTextureManager* texMgr);

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    // Используем unordered_map для более быстрого поиска O(1) вместо O(log n)
    std::unordered_map<std::string, std::shared_ptr<WaterVLO>> m_globalWaterStorage;
};