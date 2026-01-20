//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Реализация SpaceSettings.
// Использует простой строковый поиск для парсинга XML, так как структура файла проста.
// ================================================================================

#include "SpaceSettings.h"
#include "../Core/Logger.h"
#include <fstream>
#include <sstream>
#include <iostream>

bool SpaceSettings::Load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        Logger::Error("SpaceSettings: Failed to open file -> " + filename);
        return false;
    }

    // Читаем весь файл в строку
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    // --- Парсинг границ карты ---
    std::string sMinX = GetTagValue(content, "minX");
    std::string sMaxX = GetTagValue(content, "maxX");
    std::string sMinY = GetTagValue(content, "minY");
    std::string sMaxY = GetTagValue(content, "maxY");

    if (!sMinX.empty()) m_params.minX = std::stoi(sMinX);
    if (!sMaxX.empty()) m_params.maxX = std::stoi(sMaxX);
    if (!sMinY.empty()) m_params.minY = std::stoi(sMinY);
    if (!sMaxY.empty()) m_params.maxY = std::stoi(sMaxY);

    // --- Парсинг размеров карт ---
    // Ищем теги глобально, так как имена уникальны в контексте файла
    std::string sHeightSz = GetTagValue(content, "heightMapSize");
    std::string sBlendSz = GetTagValue(content, "blendMapSize");
    std::string sHoleSz = GetTagValue(content, "holeMapSize");
    std::string sShadowSz = GetTagValue(content, "shadowMapSize");
    std::string sNormSz = GetTagValue(content, "normalMapSize");

    if (!sHeightSz.empty()) m_params.heightMapSize = std::stoi(sHeightSz);
    if (!sBlendSz.empty())  m_params.blendMapSize = std::stoi(sBlendSz);
    if (!sHoleSz.empty())   m_params.holeMapSize = std::stoi(sHoleSz);
    if (!sShadowSz.empty()) m_params.shadowMapSize = std::stoi(sShadowSz);
    if (!sNormSz.empty())   m_params.normalMapSize = std::stoi(sNormSz);

    // --- Парсинг LOD настроек ---
    std::string sLodTexStart = GetTagValue(content, "lodTextureStart");
    std::string sLodTexDist = GetTagValue(content, "lodTextureDistance");
    std::string sLodNormStart = GetTagValue(content, "lodNormalStart");
    std::string sLodNormDist = GetTagValue(content, "lodNormalDistance");
    std::string sDetailDist = GetTagValue(content, "detailHeightMapDistance");

    if (!sLodTexStart.empty())  m_params.lodTextureStart = std::stof(sLodTexStart);
    if (!sLodTexDist.empty())   m_params.lodTextureDistance = std::stof(sLodTexDist);
    if (!sLodNormStart.empty()) m_params.lodNormalStart = std::stof(sLodNormStart);
    if (!sLodNormDist.empty())  m_params.lodNormalDistance = std::stof(sLodNormDist);
    if (!sDetailDist.empty())   m_params.detailHeightMapDistance = std::stof(sDetailDist);

    // --- Глобальные параметры ---
    std::string sStartPos = GetTagValue(content, "startPosition");
    std::string sStartDir = GetTagValue(content, "startDirection");
    std::string sFarPlane = GetTagValue(content, "farPlane");

    if (!sStartPos.empty()) m_params.startPosition = ParseVector3(sStartPos);
    if (!sStartDir.empty()) m_params.startDirection = ParseVector3(sStartDir);
    if (!sFarPlane.empty()) m_params.farPlane = std::stof(sFarPlane);

    Logger::Info("Space Settings Loaded. BlendMapSize: " + std::to_string(m_params.blendMapSize));
    return true;
}

std::string SpaceSettings::GetTagValue(const std::string& content, const std::string& tagName) {
    std::string openTag = "<" + tagName + ">";
    std::string closeTag = "</" + tagName + ">";

    size_t start = content.find(openTag);
    if (start == std::string::npos) return "";

    start += openTag.length();
    size_t end = content.find(closeTag, start);
    if (end == std::string::npos) return "";

    std::string val = content.substr(start, end - start);

    // Удаляем пробелы и переносы строк по краям
    size_t first = val.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) return val;
    size_t last = val.find_last_not_of(" \t\r\n");
    return val.substr(first, (last - first + 1));
}

DirectX::XMFLOAT3 SpaceSettings::ParseVector3(const std::string& str) {
    std::stringstream ss(str);
    float x = 0, y = 0, z = 0;
    ss >> x >> y >> z;
    return DirectX::XMFLOAT3(x, y, z);
}