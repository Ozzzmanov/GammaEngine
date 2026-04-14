//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// RenderTarget.h
// Удобная обертка для off-screen рендеринга (FBO).
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"

class RenderTarget {
public:
    RenderTarget(ID3D11Device* device, ID3D11DeviceContext* context);
    ~RenderTarget() = default;

    /// @brief Инициализация. Безопасно вызывать повторно при ресайзе окна.
    /// @param withDepth Если true, создаст собственный Z-буфер.
    bool Initialize(int width, int height, DXGI_FORMAT format, bool withDepth = false);

    /// @brief Принудительно освобождает видеопамять
    void Release();

    // Очистка
    void Clear(float r, float g, float b, float a);
    void ClearDepth(float depth = 1.0f, uint8_t stencil = 0);

    // Биндинг
    void Bind();
    void BindWithCustomDepth(ID3D11DepthStencilView* customDepthDSV);

    // Геттеры
    ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }
    ID3D11RenderTargetView* GetRTV() const { return m_rtv.Get(); }
    ID3D11DepthStencilView* GetDSV() const { return m_dsv.Get(); }
    ID3D11Texture2D* GetTexture() const { return m_colorTexture.Get(); }

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

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