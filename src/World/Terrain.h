//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Terrain.h
// Класс ландшафта.
// ================================================================================

#pragma once
#include "../Core/Prerequisites.h"
#include "../Resources/SpaceSettings.h"
#include "../Graphics/LevelTextureManager.h" 
#include <vector>
#include <string>
#include <memory>

#pragma pack(push, 1)
struct BlendHeader {
    uint32_t magic_;
    uint32_t width_;
    uint32_t height_;
    uint32_t bpp_;
    DirectX::XMFLOAT4 uProjection_;
    DirectX::XMFLOAT4 vProjection_;
    uint32_t version_;
    uint32_t pad_[3];
};
#pragma pack(pop)

// Структура для CPU (временная, только для загрузки)
struct TerrainLayer {
    std::string textureName;
    std::vector<uint8_t> blendData;
    int width = 0;
    int height = 0;
    DirectX::XMFLOAT4 uProj;
    DirectX::XMFLOAT4 vProj;
};

// Структура для GPU (Constant Buffer)
struct LayerConstants {
    DirectX::XMINT4   TextureIndices[6]; // Индексы в TextureArray
    DirectX::XMFLOAT4 UProj[24];         // Проекция U
    DirectX::XMFLOAT4 VProj[24];         // Проекция V
};

class Terrain {
public:
    Terrain(ID3D11Device* device, ID3D11DeviceContext* context);
    ~Terrain() = default;

    bool Initialize(const std::string& cdataFile, const SpaceParams& spaceParams,
        LevelTextureManager* texManager, bool onlyScan, bool useLegacy);

    void Render();

    void SetPosition(float x, float y, float z) { m_position = DirectX::XMFLOAT3(x, y, z); }

    // Physics / Gameplay
    float GetHeight(float worldX, float worldZ) const;
    float GetMinHeight() const { return m_minHeight; }
    float GetMaxHeight() const { return m_maxHeight; }

private:
    // Parsing logic
    void ParseLayers(const std::vector<char>& fileData, std::vector<TerrainLayer>& outLayers);
    bool TryLoadLayerData(const char* dataPtr, size_t maxLen, TerrainLayer& outLayer);
    size_t FindPattern(const std::vector<char>& data, const std::string& pattern, size_t startOffset);

    // GPU Resources creation
    void CreateCombinedBlendMap(const std::vector<TerrainLayer>& layers);
    void UpdateLayerConstants(const std::vector<TerrainLayer>& layers, LevelTextureManager* texManager);

    SpaceParams m_spaceParams;
    DirectX::XMFLOAT3 m_position = { 0, 0, 0 };
    bool m_useLegacy = false;

    float m_minHeight = 0.0f;
    float m_maxHeight = 0.0f;

    ComPtr<ID3D11Device>        m_device;
    ComPtr<ID3D11DeviceContext> m_context;

    // Geometry
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_indexCount = 0;

    // Resources
    std::vector<ID3D11ShaderResourceView*> m_legacyTextures; // Только для Legacy режима

    // Blend Maps (до 6 штук по 4 канала = 24 слоя)
    ComPtr<ID3D11Texture2D>          m_blendMaps[6];
    ComPtr<ID3D11ShaderResourceView> m_blendMapViews[6];

    // Constant Buffer (Static)
    ComPtr<ID3D11Buffer> m_layerBuffer;

    std::vector<float> m_heightMap;
    UINT  m_width = 0;
    UINT  m_depth = 0;
    float m_spacing = 1.0f;
};