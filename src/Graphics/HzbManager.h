//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// HzbManager.h
// Генерирует Hierarchical Z-Buffer (HZB) для Occlusion Culling на GPU.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <vector>
#include <memory>
#include <DirectXMath.h>

class ComputeShader;
class DX11Renderer;

class HzbManager {
public:
    HzbManager(ID3D11Device* device, ID3D11DeviceContext* context);
    ~HzbManager();

    void Initialize(int width, int height);
    void Resize(int width, int height);

    /// @brief Читает глобальный Depth-буфер и строит пирамиду мипов HZB
    void Generate(DX11Renderer* renderer);

    ID3D11ShaderResourceView* GetHzbSRV() const { return m_hzbSRV.Get(); }
    DirectX::XMFLOAT2 GetHzbSize() const { return m_hzbSize; }

private:
    void Cleanup();
    void CreateResources(int width, int height);

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    std::unique_ptr<ComputeShader> m_csDownscale;

    ComPtr<ID3D11Texture2D> m_hzbTexture;
    ComPtr<ID3D11ShaderResourceView> m_hzbSRV;

    std::vector<ComPtr<ID3D11ShaderResourceView>> m_hzbMipSRVs;
    std::vector<ComPtr<ID3D11UnorderedAccessView>> m_hzbMipUAVs;

    int m_width = 0;
    int m_height = 0;
    int m_mipCount = 0;
    DirectX::XMFLOAT2 m_hzbSize = { 0.0f, 0.0f };
};