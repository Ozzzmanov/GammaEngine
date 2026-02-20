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
#include "../Core/Logger.h"
#include <fstream>
#include <algorithm>

Terrain::Terrain() {
    // Очищаем только новый массив глобальных индексов
    memset(m_materialIndices, 0, sizeof(m_materialIndices));
}

bool Terrain::Initialize(int gridX, int gridZ, LevelTextureManager* texManager) {
    // Читаем легкие метаданные слоев
    std::wstring metaPath = TerrainBaker::GetCachePath("M", gridX, gridZ);
    std::ifstream metaFile(metaPath, std::ios::binary);

    if (!metaFile.is_open()) {
        Logger::Error(LogCategory::Terrain, "Failed to open meta file. Is chunk baked?");
        return false;
    }

    ChunkMetaHeader header;
    metaFile.read((char*)&header, sizeof(ChunkMetaHeader));

    // Проверка магического числа 'TMET'
    if (header.magic != 0x54454D54) {
        metaFile.close();
        return false;
    }

    int numLayers = std::min<int>((int)header.numLayers, 24);
    for (int i = 0; i < numLayers; ++i) {
        LayerMeta layer;
        metaFile.read((char*)&layer, sizeof(LayerMeta));

        if (texManager) {
            // Сразу получаем глобальный ID материала
            m_materialIndices[i] = texManager->RegisterMaterial(layer.textureName, layer.uProj, layer.vProj);
        }
    }
    metaFile.close();

    // Читаем запеченные высотыы
    // DDS файл начинается со 128 байт заголовка. Пропускаем его и читаем сырые float.
    std::wstring heightPath = TerrainBaker::GetCachePath("H", gridX, gridZ);
    std::ifstream hFile(heightPath, std::ios::binary | std::ios::ate);
    if (hFile.is_open()) {
        size_t fileSize = hFile.tellg();
        size_t dataSize = fileSize - 128; // Вычитаем размер заголовка DDS

        hFile.seekg(128, std::ios::beg);
        m_heightMap.resize(dataSize / sizeof(float));
        hFile.read((char*)m_heightMap.data(), dataSize);
        hFile.close();

        if (!m_heightMap.empty()) {
            auto minmax = std::minmax_element(m_heightMap.begin(), m_heightMap.end());
            m_minHeight = *minmax.first;
            m_maxHeight = *minmax.second;
        }
    }
    else {
        // Fallback, если файла нет
        m_heightMap.resize(TerrainBaker::HEIGHTMAP_SIZE * TerrainBaker::HEIGHTMAP_SIZE, 0.0f);
    }

    // Сетка высот строго 64x64, так как мы запекли её в Baker'е
    m_width = TerrainBaker::HEIGHTMAP_SIZE;
    m_depth = TerrainBaker::HEIGHTMAP_SIZE;
    // 100 метров делим на 63 сегмента (64 вершины)
    m_spacing = 100.0f / (float)(m_width - 1);

    return true;
}

float Terrain::GetHeight(float worldX, float worldZ) const {
    if (m_heightMap.empty() || m_width == 0) return 0.0f;

    // Смещение центра чанка к его углам (от -50 до +50)
    float widthOffset = 50.0f;
    float depthOffset = 50.0f;

    float localX = (worldX - m_position.x) + widthOffset;
    float localZ = (worldZ - m_position.z) + depthOffset;

    float gridX = localX / m_spacing;
    float gridZ = localZ / m_spacing;

    if (gridX < 0 || gridX >= (m_width - 1) || gridZ < 0 || gridZ >= (m_depth - 1)) return 0.0f;

    int x0 = (int)floor(gridX);
    int z0 = (int)floor(gridZ);
    float tx = gridX - x0;
    float tz = gridZ - z0;

    float h00 = m_heightMap[z0 * m_width + x0];
    float h10 = m_heightMap[z0 * m_width + (x0 + 1)];
    float h01 = m_heightMap[(z0 + 1) * m_width + x0];
    float h11 = m_heightMap[(z0 + 1) * m_width + (x0 + 1)];

    // Билинейная интерполяция высоты
    return (h00 * (1 - tx) * (1 - tz) + h10 * tx * (1 - tz) + h01 * (1 - tx) * tz + h11 * tx * tz);
}