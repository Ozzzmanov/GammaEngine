//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Chunk.h
// ================================================================================

#pragma once
#include "../Core/Prerequisites.h"
#include <DirectXCollision.h>
#include <vector>
#include <memory>
#include <string>

class Terrain;
class BwPackedSection;
class LevelTextureManager;
struct SpaceParams;

struct ChunkVloInfo {
    std::string uid, type;
    DirectX::XMFLOAT3 position = { 0,0,0 };
    DirectX::XMFLOAT3 scale = { 1,1,1 };
    bool hasPosition = false, hasScale = false;
};

// Структура для хранения распаршенных данных модели
struct ParsedStaticEntity {
    std::string ModelPath;
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Scale;
    DirectX::XMFLOAT3 RotXYZ;
};

class Chunk {
public:
    Chunk(ID3D11Device* device, ID3D11DeviceContext* context);
    ~Chunk();

    bool Load(const std::string& chunkFilePath, int gridX, int gridZ,
        const SpaceParams& params, LevelTextureManager* texMgr);

    DirectX::XMFLOAT3 GetPosition() const { return m_boundingBox.Center; }
    const DirectX::BoundingBox& GetBoundingBox() const { return m_boundingBox; }

    bool IsInFrustum(const DirectX::BoundingFrustum& frustum) const {
        return frustum.Intersects(m_boundingBox);
    }

    Terrain* GetTerrain() const { return m_terrain.get(); }

    void SetArraySlice(int slice) { m_arraySlice = slice; }
    int GetArraySlice() const { return m_arraySlice; }

    //Отдаем собранные данные наружу
    const std::vector<ParsedStaticEntity>& GetStaticEntities() const { return m_staticEntities; }

private:
    void PreScanForWater(const std::vector<char>& buffer);
    void ScanForVLOs(std::shared_ptr<BwPackedSection> root);
    void ScanForModels(std::shared_ptr<BwPackedSection> root);
    void RecursiveVloScan(std::shared_ptr<BwPackedSection> section, std::vector<ChunkVloInfo>& outInfos);

    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    std::string m_chunkName;
    int m_gridX = 0, m_gridZ = 0;
    DirectX::XMFLOAT3 m_position = { 0,0,0 };
    DirectX::BoundingBox m_boundingBox;

    std::unique_ptr<Terrain> m_terrain;
    int m_arraySlice = -1;

    // Хранилище сущностей
    std::vector<ParsedStaticEntity> m_staticEntities;
};