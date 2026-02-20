//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Terrain.h
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "../Graphics/LevelTextureManager.h"
#include "../Resources/TerrainBaker.h"
#include <vector>
#include <string>

class Terrain {
public:
    Terrain();
    ~Terrain() = default;

    bool Initialize(int gridX, int gridZ, LevelTextureManager* texManager);
    void SetPosition(float x, float y, float z) { m_position = DirectX::XMFLOAT3(x, y, z); }

    float GetHeight(float worldX, float worldZ) const;
    float GetMinHeight() const { return m_minHeight; }
    float GetMaxHeight() const { return m_maxHeight; }

    const int* GetMaterialIndices() const { return m_materialIndices; }

private:
    DirectX::XMFLOAT3 m_position = { 0, 0, 0 };
    float m_minHeight = 0.0f;
    float m_maxHeight = 0.0f;

    int m_materialIndices[24] = { 0 };

    std::vector<float> m_heightMap;
    UINT  m_width = 0;
    UINT  m_depth = 0;
    float m_spacing = 1.0f;
};