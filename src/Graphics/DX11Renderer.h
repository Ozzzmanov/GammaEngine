// ================================================================================
// DX11Renderer.h
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "../Core/Logger.h"

class DX11Renderer {
public:
    DX11Renderer();
    ~DX11Renderer() = default;

    bool Initialize(HWND hwnd, int width, int height);
    void Clear(float r, float g, float b, float a);
    void Present(bool vsync);

    void SetWireframe(bool enable);
    void BindDefaultDepthState();
    void BindOcclusionState();
    void RestoreDefaultBlendState();

    void BindZPrepassState();
    void BindColorPassState();
    void BindWaterPassState();

    ID3D11Device* GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }

    // --- НОВОЕ: Геттеры для возврата на экран ---
    ID3D11RenderTargetView* GetBackBufferRTV() const { return m_renderTargetView.Get(); }
    ID3D11DepthStencilView* GetDepthDSV() const { return m_depthStencilView.Get(); }

    // --- Для HZB ---
    ID3D11ShaderResourceView* GetDepthSRV() const { return m_depthSRV.Get(); }
    void BindUAVs(int startSlot, int count, ID3D11UnorderedAccessView** uavs);
    void UnbindUAVs(int startSlot, int count);
    void UnbindRenderTargets();
    void BindRenderTarget();

private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_renderTargetView;

    ComPtr<ID3D11Texture2D> m_depthStencilBuffer;
    ComPtr<ID3D11DepthStencilView> m_depthStencilView;
    ComPtr<ID3D11DepthStencilState> m_depthStencilState;
    ComPtr<ID3D11ShaderResourceView> m_depthSRV;

    ComPtr<ID3D11RasterizerState> m_rasterStateSolid;
    ComPtr<ID3D11RasterizerState> m_rasterStateWireframe;
    ComPtr<ID3D11SamplerState> m_samplerWrap;

    ComPtr<ID3D11BlendState> m_blendStateNoColor;
    ComPtr<ID3D11BlendState> m_blendStateDefault;
    ComPtr<ID3D11DepthStencilState> m_depthStateRead;
    ComPtr<ID3D11DepthStencilState> m_depthStateLessEqual;
};