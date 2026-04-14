//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Terrain.h
// Управляет картой высот (Heightmap) и масками материалов для CPU.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "../Graphics/LevelTextureManager.h"
#include "../Resources/TerrainBaker.h"
#include <vector>
#include <string>

struct UnifiedChunkMeta;

/**
 * @class Terrain
 * @brief Класс для работы с CPU-стороной террейна (вычисление высоты для ходьбы и т.д.)
 */
class Terrain {
public:
    static constexpr int MAX_TERRAIN_LAYERS = 24;

public:
    Terrain();
    ~Terrain() = default;

    bool Initialize(const UnifiedChunkMeta* meta, const float* heightData, LevelTextureManager* texManager);
    void SetPosition(float x, float y, float z) { m_position = DirectX::XMFLOAT3(x, y, z); }

    /// @brief Возвращает точную высоту террейна в заданных мировых координатах (билинейная интерполяция).
    float GetHeight(float worldX, float worldZ) const;

    float GetMinHeight() const { return m_minHeight; }
    float GetMaxHeight() const { return m_maxHeight; }

    const int* GetMaterialIndices() const { return m_materialIndices; }

private:
    DirectX::XMFLOAT3 m_position = { 0.0f, 0.0f, 0.0f };
    float m_minHeight = 0.0f;
    float m_maxHeight = 0.0f;

    int m_materialIndices[MAX_TERRAIN_LAYERS] = { 0 };

    std::vector<float> m_heightMap;
    UINT  m_width = 0;
    UINT  m_depth = 0;
    float m_spacing = 1.0f;
};