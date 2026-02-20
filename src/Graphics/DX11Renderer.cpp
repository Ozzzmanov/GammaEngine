#include "DX11Renderer.h"

DX11Renderer::DX11Renderer() {}

bool DX11Renderer::Initialize(HWND hwnd, int width, int height) {
    Logger::Info(LogCategory::General, "Initializing DirectX 11...");

    // 1. Swap Chain
    DXGI_SWAP_CHAIN_DESC scd = { 0 };
    scd.BufferCount = 1;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_UNORDERED_ACCESS; // Добавил флаг на всякий случай
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &scd, m_swapChain.GetAddressOf(),
        m_device.GetAddressOf(), nullptr, m_context.GetAddressOf()
    );
    HR_CHECK(hr, "Failed to create Device and SwapChain");

    // 2. RTV
    ComPtr<ID3D11Texture2D> backBuffer;
    m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
    m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());

    // 3. DEPTH BUFFER (Z-BUFFER) - ИЗМЕНЕНИЯ ЗДЕСЬ
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;

    // ВАЖНО: Используем TYPELESS формат, чтобы можно было создать и DSV (глубина), и SRV (текстура)
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    // ВАЖНО: Разрешаем биндить как глубину и как шейдерный ресурс
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    hr = m_device->CreateTexture2D(&depthDesc, nullptr, m_depthStencilBuffer.GetAddressOf());
    HR_CHECK(hr, "Failed to create Depth Buffer Texture");

    // Создаем вьюпорт для ЗАПИСИ глубины (DSV)
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // Явный формат глубины
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    hr = m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc, m_depthStencilView.GetAddressOf());
    HR_CHECK(hr, "Failed to create DSV");

    // Создаем вьюпорт для ЧТЕНИЯ глубины (SRV) - Это нужно для HZB
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; // Читаем красный канал как глубину
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &srvDesc, m_depthSRV.GetAddressOf());
    HR_CHECK(hr, "Failed to create Depth SRV");


    // 4. STATES (Без изменений логики, только код)
    // Default Depth
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
    m_device->CreateDepthStencilState(&dsDesc, m_depthStencilState.GetAddressOf());

    // Occlusion Depth (Read Only)
    D3D11_DEPTH_STENCIL_DESC occDsDesc = dsDesc;
    occDsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    m_device->CreateDepthStencilState(&occDsDesc, m_depthStateRead.GetAddressOf());

    // =========================================================
    // НОВОЕ: Состояние для Color Pass (Z-Prepass)
    // =========================================================
    D3D11_DEPTH_STENCIL_DESC dssEq = {};
    dssEq.DepthEnable = TRUE;
    dssEq.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; // Запись отключена!
    dssEq.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;      // Равенство проходит
    m_device->CreateDepthStencilState(&dssEq, m_depthStateLessEqual.GetAddressOf());

    // Blend States
    D3D11_BLEND_DESC noColorDesc = {};
    noColorDesc.RenderTarget[0].BlendEnable = FALSE;
    noColorDesc.RenderTarget[0].RenderTargetWriteMask = 0;
    m_device->CreateBlendState(&noColorDesc, m_blendStateNoColor.GetAddressOf());

    D3D11_BLEND_DESC defBlendDesc = {};
    defBlendDesc.RenderTarget[0].BlendEnable = FALSE;
    defBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_device->CreateBlendState(&defBlendDesc, m_blendStateDefault.GetAddressOf());

    // Bind Initial
    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 1);
    float blendFactor[4] = { 0,0,0,0 };
    m_context->OMSetBlendState(m_blendStateDefault.Get(), blendFactor, 0xffffffff);

    // Viewport
    D3D11_VIEWPORT vp;
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    m_context->RSSetViewports(1, &vp);

    // Rasterizers
    D3D11_RASTERIZER_DESC wfdesc = {};
    wfdesc.FillMode = D3D11_FILL_WIREFRAME;
    wfdesc.CullMode = D3D11_CULL_NONE;
    wfdesc.DepthClipEnable = true;
    m_device->CreateRasterizerState(&wfdesc, m_rasterStateWireframe.GetAddressOf());

    D3D11_RASTERIZER_DESC solidDesc = {};
    solidDesc.FillMode = D3D11_FILL_SOLID;
    solidDesc.CullMode = D3D11_CULL_BACK;
    solidDesc.DepthClipEnable = true;
    m_device->CreateRasterizerState(&solidDesc, m_rasterStateSolid.GetAddressOf());

    SetWireframe(false);
    Logger::Info(LogCategory::Render, "DirectX 11 Initialized (HZB Ready).");
    return true;
}

// ... Остальные методы (SetWireframe, Clear, Present) без изменений ...

void DX11Renderer::SetWireframe(bool enable) {
    if (enable) m_context->RSSetState(m_rasterStateWireframe.Get());
    else        m_context->RSSetState(m_rasterStateSolid.Get());
}

void DX11Renderer::BindDefaultDepthState() {
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 1);
}

void DX11Renderer::BindOcclusionState() {
    float blendFactor[4] = { 0,0,0,0 };
    m_context->OMSetBlendState(m_blendStateNoColor.Get(), blendFactor, 0xffffffff);
    m_context->OMSetDepthStencilState(m_depthStateRead.Get(), 1);
}

void DX11Renderer::RestoreDefaultBlendState() {
    float blendFactor[4] = { 0,0,0,0 };
    m_context->OMSetBlendState(m_blendStateDefault.Get(), blendFactor, 0xffffffff);
}

void DX11Renderer::Clear(float r, float g, float b, float a) {
    float color[] = { r, g, b, a };
    m_context->ClearRenderTargetView(m_renderTargetView.Get(), color);
    m_context->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void DX11Renderer::Present(bool vsync) {
    m_swapChain->Present(vsync ? 1 : 0, 0);
}

// --- НОВЫЕ МЕТОДЫ ДЛЯ COMPUTE SHADERS ---

void DX11Renderer::BindUAVs(int startSlot, int count, ID3D11UnorderedAccessView** uavs) {
    // -1 означает, что мы не меняем начальный счетчик UAV
    UINT initialCounts[8] = { (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1 };
    m_context->CSSetUnorderedAccessViews(startSlot, count, uavs, initialCounts);
}

void DX11Renderer::UnbindUAVs(int startSlot, int count) {
    ID3D11UnorderedAccessView* nullUAVs[8] = { nullptr };
    UINT initialCounts[8] = { (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1 };
    m_context->CSSetUnorderedAccessViews(startSlot, count, nullUAVs, initialCounts);
}

void DX11Renderer::UnbindRenderTargets() {
    ID3D11RenderTargetView* nullRTV = nullptr;
    ID3D11DepthStencilView* nullDSV = nullptr;
    m_context->OMSetRenderTargets(1, &nullRTV, nullDSV);
}

void DX11Renderer::BindRenderTarget() {
    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());
}

// --- МЕТОДЫ Z-PREPASS ---
void DX11Renderer::BindZPrepassState() {
    ID3D11RenderTargetView* nullRTV = nullptr;
    m_context->OMSetRenderTargets(1, &nullRTV, m_depthStencilView.Get());
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 1);
    float blendFactor[4] = { 0,0,0,0 };
    m_context->OMSetBlendState(m_blendStateNoColor.Get(), blendFactor, 0xffffffff);
}

void DX11Renderer::BindColorPassState() {
    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());
    m_context->OMSetDepthStencilState(m_depthStateLessEqual.Get(), 1);
    float blendFactor[4] = { 0,0,0,0 };
    m_context->OMSetBlendState(m_blendStateDefault.Get(), blendFactor, 0xffffffff);
}

void DX11Renderer::BindWaterPassState() {
    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());
    m_context->OMSetDepthStencilState(m_depthStateRead.Get(), 1);
    float blendFactor[4] = { 0,0,0,0 };
    m_context->OMSetBlendState(m_blendStateDefault.Get(), blendFactor, 0xffffffff);
}