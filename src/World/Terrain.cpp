//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Terrain.cpp
// ================================================================================
#include "Terrain.h"
#include "Chunk.h"
#include "../Core/Logger.h"
#include <fstream>
#include <algorithm>

Terrain::Terrain() {
    std::fill(std::begin(m_materialIndices), std::end(m_materialIndices), 0);
}

bool Terrain::Initialize(const UnifiedChunkMeta* meta, const float* heightData, LevelTextureManager* texManager) {
    if (!meta || !heightData) return false;

    std::fill(std::begin(m_materialIndices), std::end(m_materialIndices), 0);

    // Регистрация материалов в глобальном менеджере напрямую из метаданных RAM
    int numLayers = std::min<int>(meta->NumLayers, MAX_TERRAIN_LAYERS);
    for (int i = 0; i < numLayers; ++i) {
        if (texManager) {
            m_materialIndices[i] = texManager->RegisterMaterial(
                meta->Layers[i].textureName,
                meta->Layers[i].uProj,
                meta->Layers[i].vProj
            );
        }
    }

    // Копируем высоты для CPU (GetHeights)
    m_heightMap.assign(heightData, heightData + (TerrainBaker::HEIGHTMAP_SIZE * TerrainBaker::HEIGHTMAP_SIZE));

    m_minHeight = meta->MinHeight;
    m_maxHeight = meta->MaxHeight;

    m_width = TerrainBaker::HEIGHTMAP_SIZE;
    m_depth = TerrainBaker::HEIGHTMAP_SIZE;

    // Рассчитываем шаг сетки: Чанк 100 метров делим на количество ячеек (width - 1)
    m_spacing = CHUNK_SIZE / static_cast<float>(m_width - 1);

    return true;
}

float Terrain::GetHeight(float worldX, float worldZ) const {
    if (m_heightMap.empty() || m_width == 0) return 0.0f;

    // Перевод из мировых координат в локальные (относительно левого нижнего угла чанка)
    float localX = (worldX - m_position.x) + CHUNK_HALF_SIZE;
    float localZ = (worldZ - m_position.z) + CHUNK_HALF_SIZE;

    float gridX = localX / m_spacing;
    float gridZ = localZ / m_spacing;

    // Проверка границ
    if (gridX < 0 || gridX >= (m_width - 1) || gridZ < 0 || gridZ >= (m_depth - 1)) {
        return 0.0f;
    }

    int x0 = static_cast<int>(std::floor(gridX));
    int z0 = static_cast<int>(std::floor(gridZ));
    float tx = gridX - x0;
    float tz = gridZ - z0;

    // Выборка 4 соседних вершин для интерполяции
    float h00 = m_heightMap[z0 * m_width + x0];
    float h10 = m_heightMap[z0 * m_width + (x0 + 1)];
    float h01 = m_heightMap[(z0 + 1) * m_width + x0];
    float h11 = m_heightMap[(z0 + 1) * m_width + (x0 + 1)];

    // Билинейная интерполяция высоты
    return (h00 * (1.0f - tx) * (1.0f - tz) +
        h10 * tx * (1.0f - tz) +
        h01 * (1.0f - tx) * tz +
        h11 * tx * tz);
}