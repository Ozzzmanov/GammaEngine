//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Класс SpaceSettings.
// Читает файл space.settings (XML-подобный формат) и извлекает параметры мира:
// границы карты, размеры текстур.
// ================================================================================

#pragma once
#include <string>
#include <vector>
#include <DirectXMath.h>

// Структура параметров мира
struct SpaceParams {
    // Границы сетки чанков
    int minX = 0, maxX = 0;
    int minY = 0, maxY = 0;

    // Размеры карт (в пикселях)
    int heightMapSize = 0;
    int normalMapSize = 0;
    int blendMapSize = 0; 
    int holeMapSize = 0;
    int shadowMapSize = 0;

    // Настройки LOD (Level of Detail)
    float lodTextureStart = 0.0f;
    float lodTextureDistance = 0.0f;
    float lodNormalStart = 0.0f;
    float lodNormalDistance = 0.0f;
    float detailHeightMapDistance = 0.0f;

    // Глобальные настройки
    DirectX::XMFLOAT3 startPosition = { 0, 0, 0 }; // Точка спавна
    DirectX::XMFLOAT3 startDirection = { 0, 0, 1 };
    float farPlane = 0.0f; // Дальность прорисовки
};

class SpaceSettings {
public:
    SpaceSettings() = default;
    ~SpaceSettings() = default;

    // Загружает и парсит файл настроек.
    bool Load(const std::string& filename);

    // Получить загруженные параметры
    const SpaceParams& GetParams() const { return m_params; }

private:
    SpaceParams m_params;

    // Вспомогательная функция для поиска значения тега <tagName>...</tagName>
    std::string GetTagValue(const std::string& content, const std::string& tagName);

    // Парсинг строки вида "X Y Z" в вектор
    DirectX::XMFLOAT3 ParseVector3(const std::string& str);
};