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
    return m_textureArray->Initialize(m_uniqueTextureNames);
}

int LevelTextureManager::GetTextureIndex(const std::string& name) const {
    auto it = m_nameToIndexMap.find(name);
    if (it != m_nameToIndexMap.end()) {
        return it->second;
    }
    return 0;
}