//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// WorldLoader.h
// ================================================================================

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <d3d11.h>
#include <DirectXMath.h>

class Chunk;
class WaterVLO;
class SpaceSettings;
class LevelTextureManager;
class StaticGpuScene;
class TerrainArrayManager;
class TerrainGpuScene;

class WorldLoader {
public:
    WorldLoader(ID3D11Device* device, ID3D11DeviceContext* context);
    ~WorldLoader();

    void LoadLocation(const std::string& folderPath,
        std::vector<std::unique_ptr<Chunk>>& outChunks,
        std::vector<std::shared_ptr<WaterVLO>>& outWaterObjects,
        std::unique_ptr<SpaceSettings>& outSettings,
        std::unique_ptr<LevelTextureManager>& outTexMgr,
        StaticGpuScene* staticScene,
        TerrainArrayManager* arrayManager,
        TerrainGpuScene* terrainGpuScene);

    void LoadVLOByUID(const std::string& rawUid, const std::string& type,
        const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& scale,
        std::vector<std::shared_ptr<WaterVLO>>& outWaterObjects);

private:
    bool ParseGridCoordinates(const std::string& filename, int& outX, int& outZ);
    std::string CleanUID(std::string input);

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    std::map<std::string, std::shared_ptr<WaterVLO>> m_globalWaterStorage;
};