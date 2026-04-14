//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// TerrainArrayManager.h
// Управляет Texture2DArray для GPU-Driven террейна. 
// Загружает мега-массивы высот, нормалей и материалов, подготовленные бейкером.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"

/**
 * @class TerrainArrayManager
 * @brief Хранилище глобальных массивов террейна для Bindless/GPU-Driven рендеринга.
 */
class TerrainArrayManager {
public:
    static constexpr int MAX_CHUNKS = 2048;

    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    /// @brief Загружает все 5 запеченных массивов террейна из кэша.
    bool LoadMegaArrays(const std::string& locationName);

    // Геттеры для биндинга в шейдер
    ID3D11ShaderResourceView* GetHeightArray() const { return m_heightArraySRV.Get(); }
    ID3D11ShaderResourceView* GetHoleArray() const { return m_holeArraySRV.Get(); }
    ID3D11ShaderResourceView* GetIndexArray() const { return m_indexArraySRV.Get(); }
    ID3D11ShaderResourceView* GetWeightArray() const { return m_weightArraySRV.Get(); }
    ID3D11ShaderResourceView* GetNormalArray() const { return m_normalArraySRV.Get(); }

private:
    // FIXME: Эти методы сейчас не используются, так как мы грузим DDS массивы целиком.
    // Они понадобятся позже для динамического стриминга террейна (когда мы будем подгружать чанки по одному в рантайме)
    void CreateTextureArray(int width, int height, DXGI_FORMAT format, ComPtr<ID3D11Texture2D>& outTex, ComPtr<ID3D11ShaderResourceView>& outSRV);
    void CopyToSlice(ID3D11Texture2D* srcTex, ID3D11Texture2D* dstArray, int sliceIndex);

private:
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    ComPtr<ID3D11Texture2D> m_heightArray;
    ComPtr<ID3D11ShaderResourceView> m_heightArraySRV;

    ComPtr<ID3D11Texture2D> m_holeArray;
    ComPtr<ID3D11ShaderResourceView> m_holeArraySRV;

    ComPtr<ID3D11Texture2D> m_indexArray;
    ComPtr<ID3D11ShaderResourceView> m_indexArraySRV;

    ComPtr<ID3D11Texture2D> m_weightArray;
    ComPtr<ID3D11ShaderResourceView> m_weightArraySRV;

    ComPtr<ID3D11Texture2D> m_normalArray;
    ComPtr<ID3D11ShaderResourceView> m_normalArraySRV;

    int m_nextFreeSlice = 0;
};