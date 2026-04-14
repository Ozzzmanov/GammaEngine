//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// LevelTextureManager.h
// Собирает список текстур со всех чанков террейна и строит единый Texture Array 
// и StructuredBuffer с параметрами проекции (тайлинга).
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "TextureArray.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

/**
 * @struct TerrainMaterial
 * @brief Описание материала слоя террейна для GPU. Строго выровнено по 16 байт.
 */
__declspec(align(16)) struct TerrainMaterial {
    DirectX::XMFLOAT4 UProj;        // 16 байт
    DirectX::XMFLOAT4 VProj;        // 16 байт
    uint32_t DiffuseIndex;          // 4 байта (Слой в Texture2DArray)
    DirectX::XMFLOAT3 Padding;      // 12 байт для выравнивания (Итого: 48 байт)
};

class LevelTextureManager {
public:
    LevelTextureManager(ID3D11Device* device, ID3D11DeviceContext* context);
    ~LevelTextureManager() = default;

    /// @brief Добавляет текстуру в очередь на сборку.
    void RegisterTexture(const std::string& name);

    /// @brief Добавляет материал (текстура + тайлинг) и возвращает его индекс для GPU.
    int RegisterMaterial(const std::string& name, const DirectX::XMFLOAT4& uProj, const DirectX::XMFLOAT4& vProj);

    /// @brief Возвращает индекс загруженной текстуры в будущем массиве.
    int GetTextureIndex(const std::string& name) const;

    /// @brief Собирает итоговый Texture2DArray и StructuredBuffer материалов.
    bool BuildArray();

    const std::vector<TerrainMaterial>& GetMaterials() const { return m_materials; }

    ID3D11ShaderResourceView* GetMaterialSRV() const { return m_materialSRV.Get(); }
    ID3D11ShaderResourceView* GetSRV() const { return m_textureArray ? m_textureArray->GetSRV() : nullptr; }

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    std::vector<std::string>             m_uniqueTextureNames;
    std::unordered_map<std::string, int> m_nameToIndexMap;

    std::unique_ptr<TextureArray>        m_textureArray;

    ComPtr<ID3D11Buffer>             m_materialBuffer;
    ComPtr<ID3D11ShaderResourceView> m_materialSRV;

    std::vector<TerrainMaterial> m_materials;
};