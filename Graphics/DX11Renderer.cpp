#include "Graphics/DX11Renderer.h"

DX11Renderer::DX11Renderer() {}

bool DX11Renderer::Initialize(HWND hwnd, int width, int height) {
    DXGI_SWAP_CHAIN_DESC scd = { 0 };
    scd.BufferCount = 1;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &scd, m_swapChain.GetAddressOf(),
        m_device.GetAddressOf(), nullptr, m_context.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    // Render Target
    ComPtr<ID3D11Texture2D> backBuffer;
    m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
    m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());

    // --- Z-BUFFER (ГЛУБИНА) --- 
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    m_device->CreateTexture2D(&depthDesc, nullptr, m_depthStencilBuffer.GetAddressOf());
    m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), nullptr, m_depthStencilView.GetAddressOf());

    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());

    // Viewport
    D3D11_VIEWPORT vp;
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    m_context->RSSetViewports(1, &vp);

    // --- RASTERIZER STATES ---
    D3D11_RASTERIZER_DESC wfdesc = {};
    wfdesc.FillMode = D3D11_FILL_WIREFRAME; // Режим сетки
    wfdesc.CullMode = D3D11_CULL_NONE;      // Рисовать обе стороны
    wfdesc.DepthClipEnable = true;
    m_device->CreateRasterizerState(&wfdesc, m_rasterStateWireframe.GetAddressOf());

    D3D11_RASTERIZER_DESC solidDesc = {};
    solidDesc.FillMode = D3D11_FILL_SOLID;  // Сплошной режим
    solidDesc.CullMode = D3D11_CULL_NONE;   // Рисовать обе стороны
    solidDesc.DepthClipEnable = true;
    m_device->CreateRasterizerState(&solidDesc, m_rasterStateSolid.GetAddressOf());

    // По умолчанию включаем сплошной режим
    SetWireframe(false);

    return true;
}

void DX11Renderer::SetWireframe(bool enable) {
    if (enable) m_context->RSSetState(m_rasterStateWireframe.Get());
    else        m_context->RSSetState(m_rasterStateSolid.Get());
}

void DX11Renderer::Clear(float r, float g, float b, float a) {
    float color[] = { r, g, b, a };
    m_context->ClearRenderTargetView(m_renderTargetView.Get(), color);
    // Очищаем буфер глубины
    m_context->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void DX11Renderer::Present() {
    m_swapChain->Present(1, 0);
}