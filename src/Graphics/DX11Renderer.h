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

    void CopyDepthForWater();
    ID3D11ShaderResourceView* GetWaterDepthSRV() const { return m_waterDepthSRV.Get(); }
    ID3D11SamplerState* GetSamplerClamp()    const { return m_samplerClamp.Get(); }

    void BindUAVs(int startSlot, int count, ID3D11UnorderedAccessView** uavs);
    void UnbindUAVs(int startSlot, int count);
    void UnbindRenderTargets();
    void BindRenderTarget();
    void BindNoCullState();
    void BindSolidState();
    void PrintGpuMessages();

    ID3D11Device* GetDevice()       const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext()      const { return m_context.Get(); }
    ID3D11RenderTargetView* GetBackBufferRTV()const { return m_renderTargetView.Get(); }
    ID3D11DepthStencilView* GetDepthDSV()    const { return m_depthStencilView.Get(); }
    ID3D11ShaderResourceView* GetDepthSRV()   const { return m_depthSRV.Get(); }

private:
    ComPtr<ID3D11Device>             m_device;
    ComPtr<ID3D11DeviceContext>      m_context;
    ComPtr<IDXGISwapChain>           m_swapChain;
    ComPtr<ID3D11RenderTargetView>   m_renderTargetView;

    // Основной depth buffer
    ComPtr<ID3D11Texture2D>          m_depthStencilBuffer;
    ComPtr<ID3D11DepthStencilView>   m_depthStencilView;
    ComPtr<ID3D11ShaderResourceView> m_depthSRV;

    // Копия depth buffer для water pass (читается шейдером без конфликта с DSV)
    ComPtr<ID3D11Texture2D>          m_waterDepthTexture;
    ComPtr<ID3D11ShaderResourceView> m_waterDepthSRV;

    // Depth stencil states
    ComPtr<ID3D11DepthStencilState>  m_depthStencilState;   // обычный (запись включена)
    ComPtr<ID3D11DepthStencilState>  m_depthStateRead;      // read-only (запись выключена)
    ComPtr<ID3D11DepthStencilState>  m_depthStateLessEqual; // LESS_EQUAL для color pass

    // Rasterizer states
    ComPtr<ID3D11RasterizerState>    m_rasterStateSolid;
    ComPtr<ID3D11RasterizerState>    m_rasterStateNoCull;
    ComPtr<ID3D11RasterizerState>    m_rasterStateWireframe;

    // Sampler
    ComPtr<ID3D11SamplerState>       m_samplerWrap;
    ComPtr<ID3D11SamplerState>       m_samplerClamp; 

    // Текстура для захвата сцены под водой
    ComPtr<ID3D11Texture2D>          m_backBufferTexture; // Ссылка на оригинальный буфер

    ComPtr<ID3D11Debug>     m_d3dDebug;
    ComPtr<ID3D11InfoQueue> m_infoQueue;

    // Blend states
    ComPtr<ID3D11BlendState>         m_blendStateNoColor;  // Z-prepass (без цвета)
    ComPtr<ID3D11BlendState>         m_blendStateDefault;  // обычный непрозрачный
    ComPtr<ID3D11BlendState>         m_blendStateAlpha;    // прозрачность для воды  <-- это и было потеряно
};