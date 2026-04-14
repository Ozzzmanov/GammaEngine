#include "DX11Renderer.h"
#include <comdef.h>

DX11Renderer::DX11Renderer() {}

bool DX11Renderer::Initialize(HWND hwnd, int width, int height) {
    Logger::Info(LogCategory::General, "Initializing DirectX 11...");

    DXGI_SWAP_CHAIN_DESC scd = { 0 };
    scd.BufferCount = 1;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_UNORDERED_ACCESS;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;

    UINT createDeviceFlags = 0;
    //Включаем дебаг слой не только в чистом Debug, но и в Editor!
//#if defined(_DEBUG) || defined(GAMMA_EDITOR)
  //  createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
//#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, nullptr, 0,
        D3D11_SDK_VERSION, &scd, m_swapChain.GetAddressOf(),
        m_device.GetAddressOf(), nullptr, m_context.GetAddressOf()
    );
    HR_CHECK(hr, "Failed to create Device and SwapChain");

    // ==========================================
    // DEEP DEBUG LAYER (НОВОЕ)
    // ==========================================
/*#if defined(_DEBUG) || defined(GAMMA_EDITOR)
    if (SUCCEEDED(m_device.As(&m_d3dDebug))) {
        if (SUCCEEDED(m_d3dDebug.As(&m_infoQueue))) {
            m_infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
            m_infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);

            D3D11_INFO_QUEUE_FILTER filter = {};
            D3D11_MESSAGE_ID hide[] = {
                D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS,
            };
            filter.DenyList.NumIDs = _countof(hide);
            filter.DenyList.pIDList = hide;
            m_infoQueue->AddStorageFilterEntries(&filter);

            Logger::Info(LogCategory::Render, "D3D11 InfoQueue forced ON for Editor. Strict GPU debugging enabled.");
        }
    }
    else {
        Logger::Warn(LogCategory::Render, "Failed to load D3D11 Debug Layer. Install 'Graphics Tools' in Windows Optional Features.");
    }
#endif */

    m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)m_backBufferTexture.GetAddressOf());
    m_device->CreateRenderTargetView(m_backBufferTexture.Get(), nullptr, m_renderTargetView.GetAddressOf());

    // =====================================================
    // DEPTH BUFFER (основной, для записи + DSV)
    // =====================================================
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    hr = m_device->CreateTexture2D(&depthDesc, nullptr, m_depthStencilBuffer.GetAddressOf());
    HR_CHECK(hr, "Failed to create Depth Buffer Texture");

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    // dsvDesc.Flags = 0  <-- обычный DSV, запись разрешена

    hr = m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc, m_depthStencilView.GetAddressOf());
    HR_CHECK(hr, "Failed to create DSV");

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &srvDesc, m_depthSRV.GetAddressOf());
    HR_CHECK(hr, "Failed to create Depth SRV");

    // =====================================================
    // WATER DEPTH COPY
    // Отдельная текстура куда мы копируем глубину перед
    // water pass. Вода читает из неё — конфликт DSV/SRV
    // полностью исключён, никаких Read-Only флагов не нужно.
    // =====================================================
    D3D11_TEXTURE2D_DESC waterDepthDesc = depthDesc;
    // Убираем D3D11_BIND_DEPTH_STENCIL — эта текстура только для чтения
    waterDepthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    hr = m_device->CreateTexture2D(&waterDepthDesc, nullptr, m_waterDepthTexture.GetAddressOf());
    HR_CHECK(hr, "Failed to create Water Depth Copy Texture");

    // SRV на копию — тот же формат
    hr = m_device->CreateShaderResourceView(m_waterDepthTexture.Get(), &srvDesc, m_waterDepthSRV.GetAddressOf());
    HR_CHECK(hr, "Failed to create Water Depth Copy SRV");

    // =====================================================
    // DEPTH STENCIL STATES
    // =====================================================
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
    m_device->CreateDepthStencilState(&dsDesc, m_depthStencilState.GetAddressOf());

    D3D11_DEPTH_STENCIL_DESC occDsDesc = dsDesc;
    occDsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    m_device->CreateDepthStencilState(&occDsDesc, m_depthStateRead.GetAddressOf());

    D3D11_DEPTH_STENCIL_DESC dssEq = {};
    dssEq.DepthEnable = TRUE;
    dssEq.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dssEq.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    m_device->CreateDepthStencilState(&dssEq, m_depthStateLessEqual.GetAddressOf());

    // =====================================================
    // BLEND STATES
    // =====================================================
    D3D11_BLEND_DESC noColorDesc = {};
    noColorDesc.RenderTarget[0].BlendEnable = FALSE;
    noColorDesc.RenderTarget[0].RenderTargetWriteMask = 0;
    m_device->CreateBlendState(&noColorDesc, m_blendStateNoColor.GetAddressOf());

    D3D11_BLEND_DESC defBlendDesc = {};
    defBlendDesc.RenderTarget[0].BlendEnable = FALSE;
    defBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_device->CreateBlendState(&defBlendDesc, m_blendStateDefault.GetAddressOf());

    D3D11_BLEND_DESC alphaDesc = {};
    alphaDesc.RenderTarget[0].BlendEnable = TRUE;
    alphaDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    alphaDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    alphaDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    alphaDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    alphaDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    alphaDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    alphaDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_device->CreateBlendState(&alphaDesc, m_blendStateAlpha.GetAddressOf());

    // Bind Initial
    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 1);
    float blendFactor[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_blendStateDefault.Get(), blendFactor, 0xffffffff);

    D3D11_VIEWPORT vp;
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    m_context->RSSetViewports(1, &vp);

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

    D3D11_RASTERIZER_DESC noCullDesc = {};
    noCullDesc.FillMode = D3D11_FILL_SOLID;
    noCullDesc.CullMode = D3D11_CULL_NONE;
    noCullDesc.DepthClipEnable = true;
    m_device->CreateRasterizerState(&noCullDesc, m_rasterStateNoCull.GetAddressOf());

    // Wrap сэмплер (уже создавался в Shader.cpp отдельно,
   // но здесь нам нужен Clamp для чтения depth texture в воде)
    D3D11_SAMPLER_DESC clampDesc = {};
    clampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    clampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    clampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    clampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    clampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    clampDesc.MinLOD = 0;
    clampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    m_device->CreateSamplerState(&clampDesc, m_samplerClamp.GetAddressOf());

    SetWireframe(false);

    Logger::Info(LogCategory::Render, "DirectX 11 Initialized (HZB + Water Depth Copy Ready).");
    return true;
}

void DX11Renderer::CopyDepthForWater() {
    // Перед копированием ОБЯЗАТЕЛЬНО отвязываем depth SRV из шейдеров,
    // иначе DX11 может проигнорировать копирование (ресурс занят как input).
    ID3D11ShaderResourceView* nullSRV = nullptr;
    m_context->PSSetShaderResources(3, 1, &nullSRV);

    // Копируем весь depth buffer в отдельную текстуру.
    // Форматы совместимы (оба R24G8_TYPELESS) — CopyResource работает напрямую.
    m_context->CopyResource(m_waterDepthTexture.Get(), m_depthStencilBuffer.Get());
}

void DX11Renderer::PrintGpuMessages() {
    if (!m_infoQueue) return;

    UINT64 messageCount = m_infoQueue->GetNumStoredMessages();
    for (UINT64 i = 0; i < messageCount; ++i) {
        SIZE_T messageLength = 0;
        m_infoQueue->GetMessage(i, nullptr, &messageLength);

        if (messageLength > 0) {
            std::vector<char> messageData(messageLength);
            D3D11_MESSAGE* message = reinterpret_cast<D3D11_MESSAGE*>(messageData.data());

            if (SUCCEEDED(m_infoQueue->GetMessage(i, message, &messageLength))) {
                std::string msg(message->pDescription);

                if (message->Severity == D3D11_MESSAGE_SEVERITY_ERROR || message->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION) {
                    Logger::Error(LogCategory::Render, "[DX11 ERROR] " + msg);
                }
                else if (message->Severity == D3D11_MESSAGE_SEVERITY_WARNING) {
                    Logger::Warn(LogCategory::Render, "[DX11 WARN] " + msg);
                }
            }
        }
    }
    m_infoQueue->ClearStoredMessages();
}

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

void DX11Renderer::BindNoCullState() {
    m_context->RSSetState(m_rasterStateNoCull.Get());
}

void DX11Renderer::BindSolidState() {
    m_context->RSSetState(m_rasterStateSolid.Get());
}

void DX11Renderer::Present(bool vsync) {
    HRESULT hr = m_swapChain->Present(vsync ? 1 : 0, 0);

    // Если видеокарта отвалилась (TDR)
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        HRESULT reason = m_device->GetDeviceRemovedReason();

        _com_error err(reason);
        std::string errMsg = "CRITICAL ERROR: GPU Device Removed!\nReason: ";
        errMsg += err.ErrorMessage();

        Logger::Error(LogCategory::Render, errMsg);

        // Ставим жесткий брейкпоинт, чтобы отладчик остановился прямо здесь
        __debugbreak();
    }
}

void DX11Renderer::BindUAVs(int startSlot, int count, ID3D11UnorderedAccessView** uavs) {
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
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 1);
    float blendFactor[4] = { 0,0,0,0 };
    m_context->OMSetBlendState(m_blendStateNoColor.Get(), blendFactor, 0xffffffff);
}

void DX11Renderer::BindColorPassState() {
    m_context->OMSetDepthStencilState(m_depthStateLessEqual.Get(), 1);
    float blendFactor[4] = { 0,0,0,0 };
    m_context->OMSetBlendState(m_blendStateDefault.Get(), blendFactor, 0xffffffff);
}

void DX11Renderer::BindWaterPassState() {
    m_context->OMSetDepthStencilState(m_depthStateRead.Get(), 1);
    float blendFactor[4] = { 0,0,0,0 };
    // ИСПРАВЛЕНИЕ: Включаем честную прозрачность
    m_context->OMSetBlendState(m_blendStateAlpha.Get(), blendFactor, 0xffffffff); 
}
