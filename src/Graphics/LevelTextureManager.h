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

struct TerrainMaterial {
    DirectX::XMFLOAT4 UProj;        // 16 байт
    DirectX::XMFLOAT4 VProj;        // 16 байт
    uint32_t DiffuseIndex;          // 4 байта (Слой в Texture2DArray)
    DirectX::XMFLOAT3 Padding;      // 12 байт для выравнивания
};

class LevelTextureManager {
public:
    LevelTextureManager(ID3D11Device* device, ID3D11DeviceContext* context);
    ~LevelTextureManager() = default;

    int RegisterMaterial(const std::string& name, const DirectX::XMFLOAT4& uProj, const DirectX::XMFLOAT4& vProj);

    const std::vector<TerrainMaterial>& GetMaterials() const { return m_materials; }

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

    std::vector<TerrainMaterial> m_materials;
};