//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// TerrainBaker.h
// Офлайн-конвертер (Baker) данных террейна.
// Читает сырые .cdata, стандартизирует размеры и сохраняет в кэш как .dds.
// ================================================================================

#pragma once
#include "../Core/Prerequisites.h"
#include <string>

struct LayerMeta {
    char textureName[128];
    DirectX::XMFLOAT4 uProj;
    DirectX::XMFLOAT4 vProj;
};

struct ChunkMetaHeader {
    uint32_t magic;
    uint32_t numLayers;
};

class TerrainBaker {
public:
    static const int HEIGHTMAP_SIZE = 64;   // Оптимально для сетки 37x37 с небольшим запасом
    static const int BLENDMAP_SIZE = 128;   // Стандарт для весов текстур
    static const int HOLEMAP_SIZE = 32;     // Для маски дыр

    // Инициализация директорий кэша
    static void Initialize();

    // Проверяет, существуют ли уже запеченные файлы для этого чанка
    static bool IsChunkBaked(int gridX, int gridZ);

    // Читает .cdata, ресайзит/сжимает текстуры и сохраняет их в .dds
    static bool BakeChunk(const std::string& cdataFilePath, int gridX, int gridZ);

    // Возвращает путь к запеченному файлу
    static std::wstring GetCachePath(const std::string& prefix, int gridX, int gridZ, int index = -1);
};