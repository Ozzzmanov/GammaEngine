//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Chunk.h
// Контейнер данных одного участка мира (Чанка). Хранит террейн, статику и флору.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <DirectXCollision.h>
#include <vector>
#include <memory>
#include <string>

// Константы размеров чанка для мира
constexpr float CHUNK_SIZE = 100.0f;
constexpr float CHUNK_HALF_SIZE = 50.0f;

class Terrain;
class BwPackedSection;
class LevelTextureManager;
struct SpaceParams;
struct UnifiedChunkMeta;

/**
 * @struct ChunkVloInfo
 * @brief Информация о Very Large Object (например, воде), найденном в чанке.
 */
struct ChunkVloInfo {
    std::string uid;
    std::string type;
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };
    bool hasPosition = false;
    bool hasScale = false;
};

/**
 * @struct ParsedStaticEntity
 * @brief Сырые данные распакованной модели (статика/дерево) для передачи в GpuScene.
 */
struct ParsedStaticEntity {
    std::string ModelPath;
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Scale;
    DirectX::XMFLOAT3 RotXYZ;
};

/**
 * @class Chunk
 * @brief Логическая единица мира. Отвечает за парсинг legacy-файлов (.chunk) и хранение CPU-данных.
 */
class Chunk {
public:
    Chunk(ID3D11Device* device, ID3D11DeviceContext* context);
    ~Chunk();

    /// @brief Загружает данные из старого формата (.chunk).
    bool Load(const std::string& chunkFilePath, int gridX, int gridZ,
        const SpaceParams& params, LevelTextureManager* texMgr,
        const UnifiedChunkMeta* meta = nullptr, const float* heightData = nullptr);

    DirectX::XMFLOAT3 GetPosition() const { return m_boundingBox.Center; }
    const DirectX::BoundingBox& GetBoundingBox() const { return m_boundingBox; }

    bool IsInFrustum(const DirectX::BoundingFrustum& frustum) const {
        return frustum.Intersects(m_boundingBox);
    }

    Terrain* GetTerrain() const { return m_terrain.get(); }
    void AllocateTerrain();

    void SetArraySlice(int slice) { m_arraySlice = slice; }
    int GetArraySlice() const { return m_arraySlice; }

    const std::vector<ParsedStaticEntity>& GetStaticEntities() const { return m_staticEntities; }
    const std::vector<ParsedStaticEntity>& GetFloraEntities() const { return m_floraEntities; }

private:
    void PreScanForWater(const std::vector<char>& buffer);
    void ScanForVLOs(std::shared_ptr<BwPackedSection> root);
    void ScanForModels(std::shared_ptr<BwPackedSection> root);
    void RecursiveVloScan(std::shared_ptr<BwPackedSection> section, std::vector<ChunkVloInfo>& outInfos);

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    std::string m_chunkName;
    int m_gridX = 0;
    int m_gridZ = 0;
    DirectX::XMFLOAT3 m_position = { 0.0f, 0.0f, 0.0f };
    DirectX::BoundingBox m_boundingBox;

    std::unique_ptr<Terrain> m_terrain;
    int m_arraySlice = -1;

    std::vector<ParsedStaticEntity> m_staticEntities;
    std::vector<ParsedStaticEntity> m_floraEntities;
};