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
// ================================================================================

#pragma once
#include "../Core/Prerequisites.h"

class TerrainArrayManager {
public:
    static const int MAX_CHUNKS = 2048;

    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    int AllocateSlice();
    bool LoadChunkIntoArray(int gridX, int gridZ, int sliceIndex);

    // Геттеры для биндинга в шейдер
    ID3D11ShaderResourceView* GetHeightArray() const { return m_heightArraySRV.Get(); }
    ID3D11ShaderResourceView* GetHoleArray() const { return m_holeArraySRV.Get(); }
    ID3D11ShaderResourceView* GetIndexArray() const { return m_indexArraySRV.Get(); }
    ID3D11ShaderResourceView* GetWeightArray() const { return m_weightArraySRV.Get(); }

private:
    void CreateTextureArray(int width, int height, DXGI_FORMAT format, ComPtr<ID3D11Texture2D>& outTex, ComPtr<ID3D11ShaderResourceView>& outSRV);
    void CopyToSlice(ID3D11Texture2D* srcTex, ID3D11Texture2D* dstArray, int sliceIndex);

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

    int m_nextFreeSlice = 0;
};