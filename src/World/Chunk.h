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
#include "../Graphics/ConstantBuffer.h"
#include <DirectXCollision.h>

class Terrain;
class BwPackedSection;
class LevelTextureManager;
class VegetationManager;
struct SpaceParams;

// Структура для передачи данных о воде
struct ChunkVloInfo {
    std::string uid;
    std::string type;

    DirectX::XMFLOAT3 position = { 0,0,0 };
    DirectX::XMFLOAT3 scale = { 1,1,1 };

    // Флаги, указывающие, были ли эти данные найдены в файле
    bool hasPosition = false;
    bool hasScale = false;
};

class Chunk {
public:
    Chunk(ID3D11Device* device, ID3D11DeviceContext* context);
    ~Chunk();

    bool Load(const std::string& chunkFilePath, int gridX, int gridZ,
        const SpaceParams& params,
        LevelTextureManager* texMgr,
        VegetationManager* vegMgr,
        bool onlyScan);

    // Оптимизированный рендер: проверяет видимость перед отрисовкой
    bool Render(ConstantBuffer<CB_VS_Transform>* cb,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& proj,
        const DirectX::BoundingFrustum& cameraFrustum,
        const DirectX::XMFLOAT3& camPos, // Позиция камеры
        float renderDistanceSq,          // Дистанция в квадрате
        bool checkVisibility);

private:
    void PreScanForWater(const std::vector<char>& buffer);
    void ScanForVLOs(std::shared_ptr<BwPackedSection> root);
    void RecursiveVloScan(std::shared_ptr<BwPackedSection> section,
        std::vector<ChunkVloInfo>& outInfos);

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    std::string          m_chunkName;
    int                  m_gridX = 0;
    int                  m_gridZ = 0;
    DirectX::XMFLOAT3    m_position = { 0, 0, 0 };

    // Границы чанка для Frustum Culling
    DirectX::BoundingBox m_boundingBox;

    std::unique_ptr<Terrain> m_terrain;
};