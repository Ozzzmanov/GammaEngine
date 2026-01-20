//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
#include "DX11Renderer.h"

DX11Renderer::DX11Renderer() {}

bool DX11Renderer::Initialize(HWND hwnd, int width, int height) {
    Logger::Info(LogCategory::General, "Initializing DirectX 11...");

    // 1. Описание цепочки обмена (Swap Chain)
    DXGI_SWAP_CHAIN_DESC scd = { 0 };
    scd.BufferCount = 1;                                    // Один задний буфер
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;     // Формат цвета 32 бита
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;      // Будем рисовать в этот буфер
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;                               // Мультисэмплинг выключен (для простоты)
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;                                    // Оконный режим

    // 2. Создание устройства и цепочки
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,                    // Адаптер по умолчанию
        D3D_DRIVER_TYPE_HARDWARE,   // Аппаратное ускорение
        nullptr,                    // Программный растеризатор (не нужен)
        0,                          // Флаги (можно добавить D3D11_CREATE_DEVICE_DEBUG для отладки)
        nullptr, 0,                 // Уровни функций
        D3D11_SDK_VERSION,
        &scd,
        m_swapChain.GetAddressOf(),
        m_device.GetAddressOf(),
        nullptr,
        m_context.GetAddressOf()
    );
    HR_CHECK(hr, "Failed to create Device and SwapChain");

    // 3. Создание Render Target View (доступ к заднему буферу)
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
    HR_CHECK(hr, "Failed to get BackBuffer");

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
    HR_CHECK(hr, "Failed to create RTV");

    // 4. Создание буфера глубины (Z-Buffer)
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // 24 бита под глубину, 8 под трафарет
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = m_device->CreateTexture2D(&depthDesc, nullptr, m_depthStencilBuffer.GetAddressOf());
    HR_CHECK(hr, "Failed to create Depth Buffer Texture");

    hr = m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), nullptr, m_depthStencilView.GetAddressOf());
    HR_CHECK(hr, "Failed to create DSV");

    // 5. Создание состояния глубины (Depth Stencil State)
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS; // Проходит, если пиксель ближе к камере
    dsDesc.StencilEnable = FALSE;

    hr = m_device->CreateDepthStencilState(&dsDesc, m_depthStencilState.GetAddressOf());
    HR_CHECK(hr, "Failed to create DepthStencilState");

    // Привязка RTV и DSV к конвейеру
    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 1);

    // 6. Настройка Вьюпорта (Viewport)
    D3D11_VIEWPORT vp;
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    m_context->RSSetViewports(1, &vp);

    // 7. Создание состояний растеризатора (Wireframe/Solid)
    D3D11_RASTERIZER_DESC wfdesc = {};
    wfdesc.FillMode = D3D11_FILL_WIREFRAME;
    wfdesc.CullMode = D3D11_CULL_NONE; // Рисуем обе стороны треугольника
    wfdesc.DepthClipEnable = true;
    hr = m_device->CreateRasterizerState(&wfdesc, m_rasterStateWireframe.GetAddressOf());
    HR_CHECK(hr, "Failed to create Wireframe RS");

    D3D11_RASTERIZER_DESC solidDesc = {};
    solidDesc.FillMode = D3D11_FILL_SOLID;
    solidDesc.CullMode = D3D11_CULL_NONE; // В будущем лучше использовать CULL_BACK для оптимизации
    solidDesc.DepthClipEnable = true;
    hr = m_device->CreateRasterizerState(&solidDesc, m_rasterStateSolid.GetAddressOf());
    HR_CHECK(hr, "Failed to create Solid RS");

    // Установка состояния по умолчанию
    SetWireframe(false);

    Logger::Info(LogCategory::Render, "DirectX 11 Initialized Successfully.");
    return true;
}

void DX11Renderer::SetWireframe(bool enable) {
    if (enable) m_context->RSSetState(m_rasterStateWireframe.Get());
    else        m_context->RSSetState(m_rasterStateSolid.Get());
}

void DX11Renderer::BindDefaultDepthState() {
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 1);
}

void DX11Renderer::Clear(float r, float g, float b, float a) {
    float color[] = { r, g, b, a };
    // Очистка цвета
    m_context->ClearRenderTargetView(m_renderTargetView.Get(), color);
    // Очистка глубины
    m_context->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void DX11Renderer::Present(bool vsync) {
    // 1 - включить VSync (ограничение FPS частотой монитора), 0 - выключить
    m_swapChain->Present(vsync ? 1 : 0, 0);
}