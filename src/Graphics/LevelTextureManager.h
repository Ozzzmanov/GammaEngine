//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// LevelTextureManager.h
// Собирает список текстур со всех чанков и строит единый Texture Array.
// ================================================================================

#pragma once
#include "../Core/Prerequisites.h"
#include "TextureArray.h"

class LevelTextureManager {
public:
    LevelTextureManager(ID3D11Device* device, ID3D11DeviceContext* context);
    ~LevelTextureManager() = default;

    void RegisterTexture(const std::string& name);
    bool BuildArray();
    int GetTextureIndex(const std::string& name) const;

    ID3D11ShaderResourceView* GetSRV() const {
        return m_textureArray ? m_textureArray->GetSRV() : nullptr;
    }

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    std::vector<std::string> m_uniqueTextureNames;
    std::unordered_map<std::string, int> m_nameToIndexMap;
    std::unique_ptr<TextureArray> m_textureArray;
};