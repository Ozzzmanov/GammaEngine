//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// LevelTextureManager.cpp
// ================================================================================

#include "LevelTextureManager.h"
#include "../Core/Logger.h"

LevelTextureManager::LevelTextureManager(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context)
{
    m_textureArray = std::make_unique<TextureArray>(device, context);
}

void LevelTextureManager::RegisterTexture(const std::string& name) {
    // Пропускаем пустые строки или строки состоящие только из пробелов/нулей FIX ME сделать строгую структуру.
    if (name.empty() || name.find_first_not_of(" \t\r\n\0") == std::string::npos) {
        return;
    }

    if (m_nameToIndexMap.find(name) == m_nameToIndexMap.end()) {
        int index = (int)m_uniqueTextureNames.size();
        m_uniqueTextureNames.push_back(name);
        m_nameToIndexMap[name] = index;
    }
}

bool LevelTextureManager::BuildArray() {
    if (m_uniqueTextureNames.empty()) {
        Logger::Warn(LogCategory::Texture, "No textures to build array.");
        return false;
    }

    /*НАЧАЛО ОТЛАДКИ-- -
    Logger::Info(LogCategory::System, "=== TEXTURE ARRAY DICTIONARY ===");
    for (size_t i = 0; i < m_uniqueTextureNames.size(); ++i) {
        Logger::Info(LogCategory::System, "Slot [" + std::to_string(i) + "] -> " + m_uniqueTextureNames[i]);
    }
    Logger::Info(LogCategory::System, "================================");
    // КОНЕЦ ОТЛАДКИ ---*/

    return m_textureArray->Initialize(m_uniqueTextureNames);
}

int LevelTextureManager::RegisterMaterial(const std::string& name, const DirectX::XMFLOAT4& uProj, const DirectX::XMFLOAT4& vProj) {
    if (name.empty() || name.find_first_not_of(" \t\r\n\0") == std::string::npos) return 0;

    // Сначала регистрируем саму текстуру, чтобы получить её индекс в Texture2DArray
    int texIndex = 0;
    if (m_nameToIndexMap.find(name) == m_nameToIndexMap.end()) {
        texIndex = (int)m_uniqueTextureNames.size();
        m_uniqueTextureNames.push_back(name);
        m_nameToIndexMap[name] = texIndex;
    }
    else {
        texIndex = m_nameToIndexMap[name];
    }

    // Есть ли уже такой материал (Текстура + ее уникальные UProj/VProj)
    // В большинстве случаев UProj/VProj одинаковые для одной текстуры, но если они отличаются на разных чанках - создастся новый материал.
    for (size_t i = 0; i < m_materials.size(); ++i) {
        if (m_materials[i].DiffuseIndex == texIndex &&
            m_materials[i].UProj.x == uProj.x && m_materials[i].UProj.y == uProj.y && // Базовая проверка на совпадение проекции
            m_materials[i].VProj.x == vProj.x && m_materials[i].VProj.y == vProj.y)
        {
            return (int)i; // Такой материал уже есть return
        }
    }

    // Создаем новый глобальный материал
    TerrainMaterial mat = {};
    mat.UProj = uProj;
    mat.VProj = vProj;
    mat.DiffuseIndex = texIndex;
    mat.Padding = { 0, 0, 0 };

    int matIndex = (int)m_materials.size();
    m_materials.push_back(mat);
    return matIndex;
}

int LevelTextureManager::GetTextureIndex(const std::string& name) const {
    auto it = m_nameToIndexMap.find(name);
    if (it != m_nameToIndexMap.end()) {
        return it->second;
    }
    return 0;
}