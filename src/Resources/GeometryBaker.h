//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GeometryBaker.h
// Сохраняет и загружает состояние ModelManager (Глобальные Vertex/Index буферы)
// на диск, чтобы мгновенно загружать локации без повторного парсинга GLTF/BWO.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <string>

class GeometryBaker {
public:
    static void Initialize();

    /// @brief Пытается загрузить геометрию из кэша (.gcache). 
    /// Если файл .glevel новее кэша, вернет false (инвалидация, нужен ребейк).
    static bool LoadCache(const std::string& levelPath);

    /// @brief Сохраняет текущее состояние ModelManager на диск.
    static void SaveCache(const std::string& levelPath);

private:
    static std::string GetCachePath(const std::string& levelPath);
};