//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// RenderTarget.h
// Удобная обертка для off-screen рендеринга.
// ================================================================================

#pragma once
#include "../Core/Prerequisites.h"

class RenderTarget {
public:
    RenderTarget(ID3D11Device* device, ID3D11DeviceContext* context);
    ~RenderTarget() = default;

    // Инициализация. Если withDepth = true, создаст собственный Z-буфер
    bool Initialize(int width, int height, DXGI_FORMAT format, bool withDepth = false);

    // Очистка
    void Clear(float r, float g, float b, float a);
    void ClearDepth(float depth = 1.0f, uint8_t stencil = 0);

    // Биндинг
    void Bind(); // Биндит RTV + свой внутренний Depth (если есть)
    void BindWithCustomDepth(ID3D11DepthStencilView* customDepthDSV); // Биндит RTV + чужой Depth

    ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }
    ID3D11RenderTargetView* GetRTV() const { return m_rtv.Get(); }
    ID3D11DepthStencilView* GetDSV() const { return m_dsv.Get(); }

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    int m_width = 0;
    int m_height = 0;

    ComPtr<ID3D11Texture2D>          m_colorTexture;
    ComPtr<ID3D11RenderTargetView>   m_rtv;
    ComPtr<ID3D11ShaderResourceView> m_srv;

    ComPtr<ID3D11Texture2D>          m_depthTexture;
    ComPtr<ID3D11DepthStencilView>   m_dsv;
};