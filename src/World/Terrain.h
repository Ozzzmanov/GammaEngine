//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "../Graphics/TextureArray.h"
#include "../Resources/SpaceSettings.h"
#include <vector>
#include <string>
#include <memory>

// Структура заголовка слоя внутри .cdata (BigWorld format)
#pragma pack(push, 1)
struct BlendHeader {
    uint32_t magic_;      // "bld\0"
    uint32_t width_;
    uint32_t height_;
    uint32_t bpp_;
    DirectX::XMFLOAT4 uProjection_;
    DirectX::XMFLOAT4 vProjection_;
    uint32_t version_;
    uint32_t pad_[3];
};
#pragma pack(pop)

// Внутреннее представление слоя террейна
struct TerrainLayer {
    std::string textureName;
    std::vector<uint8_t> blendData; // Альфа-канал смешивания (8 бит)
    int width = 0;
    int height = 0;
    DirectX::XMFLOAT4 uProj;
    DirectX::XMFLOAT4 vProj;
};

class Terrain {
public:
    Terrain(ID3D11Device* device, ID3D11DeviceContext* context);
    ~Terrain() = default;

    // Загрузка и инициализация ландшафта из .cdata
    bool Initialize(const std::string& cdataFile, const SpaceParams& spaceParams);

    // Отрисовка
    void Render();

    // Установка позиции (для правильной привязки координат)
    void SetPosition(float x, float y, float z) { m_position = DirectX::XMFLOAT3(x, y, z); }

    // Геттеры для отладки
    float GetMinHeight() const { return m_minHeight; }
    float GetMaxHeight() const { return m_maxHeight; }
    const std::vector<std::string>& GetTextureNames() const { return m_layerTextureNames; }

private:
    // --- Парсинг .cdata ---
    void ParseLayers(const std::vector<char>& fileData);
    bool TryLoadLayerData(const char* dataPtr, size_t maxLen, TerrainLayer& outLayer);
    size_t FindPattern(const std::vector<char>& data, const std::string& pattern, size_t startOffset);

    // --- Создание текстур смешивания ---
    void CreateCombinedBlendMap();

    SpaceParams m_spaceParams;
    DirectX::XMFLOAT3 m_position = { 0, 0, 0 };
    float m_minHeight = 0.0f;
    float m_maxHeight = 0.0f;
    std::string m_currentCDataPath;

    // DX11 Ресурсы
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_indexCount = 0;

    std::vector<TerrainLayer> m_layers;
    std::vector<std::string> m_layerTextureNames;
    std::unique_ptr<TextureArray> m_textureArray;

    // Две карты смешивания (ARGB + ARGB = 8 каналов)
    ComPtr<ID3D11Texture2D> m_blendMap1;
    ComPtr<ID3D11ShaderResourceView> m_blendMapView1;
    ComPtr<ID3D11Texture2D> m_blendMap2;
    ComPtr<ID3D11ShaderResourceView> m_blendMapView2;

    // Константный буфер для информации о слоях
    struct LayerConstants {
        DirectX::XMINT4 TextureIndices[2]; // Индексы текстур в массиве
        DirectX::XMFLOAT4 UProj[8];        // Матрицы проекции UV (U)
        DirectX::XMFLOAT4 VProj[8];        // Матрицы проекции UV (V)
    };
    ComPtr<ID3D11Buffer> m_layerBuffer;
};