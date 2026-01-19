#pragma once
#include "Core/Prerequisites.h"

class DX11Renderer {
public:
    DX11Renderer();
    ~DX11Renderer() = default;

    bool Initialize(HWND hwnd, int width, int height);
    void Clear(float r, float g, float b, float a);
    void Present();

    // переключение режима сетки
    void SetWireframe(bool enable);

    ID3D11Device* GetDevice() { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() { return m_context.Get(); }

private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_renderTargetView;
    ComPtr<ID3D11DepthStencilView> m_depthStencilView;
    ComPtr<ID3D11Texture2D> m_depthStencilBuffer;

    ComPtr<ID3D11RasterizerState> m_rasterStateSolid;
    ComPtr<ID3D11RasterizerState> m_rasterStateWireframe;
};