//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// TerrainAtlasManager.h
// Управляет глобальными Атласами (Мега-текстурами) для GPU-Driven террейна.
// ================================================================================

// Не используется, возможно в дальнейшем релазиация для очень больших миров.
#pragma once
#include "../Core/Prerequisites.h"

// Информация о выделенном слоте в Атласе
struct AtlasAllocation {
    int tileX = -1;
    int tileY = -1;
    DirectX::XMFLOAT2 uvOffset = { 0.0f, 0.0f }; // Смещение для шейдера
    float uvScale = 1.0f;                        // Масштаб для шейдера

    bool IsValid() const { return tileX >= 0; }
};

class TerrainAtlasManager {
public:
    // 64x64 сетка = 4096 чанков одновременно в памяти. (Около 6.4 км x 6.4 км)
    static const int ATLAS_GRID_SIZE = 64;

    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    // Выделяет свободный тайл в атласе
    AtlasAllocation AllocateTile();

    // Загружает .dds из кэша и копирует их в видеопамять в указанный слот
    bool LoadChunkIntoAtlas(int gridX, int gridZ, const AtlasAllocation& alloc);

    // Геттеры для биндинга в шейдер
    ID3D11ShaderResourceView* GetHeightAtlas() const { return m_heightAtlasSRV.Get(); }
    ID3D11ShaderResourceView* GetHoleAtlas() const { return m_holeAtlasSRV.Get(); }
    ID3D11ShaderResourceView* GetBlendAtlas(int i) const { return m_blendAtlasSRV[i].Get(); }

private:
    void CreateAtlasTexture(int tileSize, DXGI_FORMAT format, ComPtr<ID3D11Texture2D>& outTex, ComPtr<ID3D11ShaderResourceView>& outSRV);
    void CopyTextureToAtlas(ID3D11Texture2D* srcTex, ID3D11Texture2D* dstAtlas, int tileX, int tileY, int tileSize, bool isBC);

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    ComPtr<ID3D11Texture2D> m_heightAtlas;
    ComPtr<ID3D11ShaderResourceView> m_heightAtlasSRV;

    ComPtr<ID3D11Texture2D> m_holeAtlas;
    ComPtr<ID3D11ShaderResourceView> m_holeAtlasSRV;

    ComPtr<ID3D11Texture2D> m_blendAtlas[6];
    ComPtr<ID3D11ShaderResourceView> m_blendAtlasSRV[6];

    int m_nextFreeTileX = 0;
    int m_nextFreeTileY = 0;
};