//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// PostProcessManager.cpp
// ================================================================================
#include "PostProcessManager.h"
#include "DX11Renderer.h"
#include "../Core/Logger.h"
#include "../Config/EngineConfig.h"

PostProcessManager::PostProcessManager(DX11Renderer* renderer) : m_renderer(renderer) {}
PostProcessManager::~PostProcessManager() {}

bool PostProcessManager::Initialize(int width, int height) {
    GAMMA_LOG_INFO(LogCategory::Render, "Initializing Optimized PostProcessManager...");
    m_width = width;
    m_height = height;

    // Загрузка графических шейдеров
    m_sunEffectsShader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_sunEffectsShader->Load(L"Assets/Shaders/PostProcess/SunEffects.hlsl")) return false;

    m_uberTonemapShader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_uberTonemapShader->Load(L"Assets/Shaders/PostProcess/Tonemap.hlsl")) return false;

    m_bloomShader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_bloomShader->Load(L"Assets/Shaders/PostProcess/Bloom.hlsl")) return false;

    //СОЗДАНИЕ BLOOM ЦЕПОЧКИ
    int mipWidth = width / 2;
    int mipHeight = height / 2;
    for (int i = 0; i < 5; ++i) {
        auto rt = std::make_unique<RenderTarget>(m_renderer->GetDevice(), m_renderer->GetContext());

        // Первый мип (экстракт) должен быть точным R16G16B16A16. 
        // Размытие пишем в R11G11B10_FLOAT (экономия 50% VRAM и шины!)
        DXGI_FORMAT format = (i == 0) ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R11G11B10_FLOAT;

        if (!rt->Initialize(mipWidth, mipHeight, format, false)) return false;
        m_bloomChain.push_back(std::move(rt));

        mipWidth = std::max(1, mipWidth / 2);
        mipHeight = std::max(1, mipHeight / 2);
    }

    // Инициализация буферов
    m_bloomCB = std::make_unique<ConstantBuffer<CB_Bloom>>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_bloomCB->Initialize(true);

    m_uberTonemapCB = std::make_unique<ConstantBuffer<CB_UberTonemap>>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_uberTonemapCB->Initialize(true);

    // Стейты
    HRESULT hr;
    D3D11_BLEND_DESC addDesc;
    ZeroMemory(&addDesc, sizeof(addDesc));
    addDesc.RenderTarget[0].BlendEnable = TRUE;
    addDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    addDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    addDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    addDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    addDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    addDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    addDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = m_renderer->GetDevice()->CreateBlendState(&addDesc, m_blendStateAdditive.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_DEPTH_STENCIL_DESC dsDesc;
    ZeroMemory(&dsDesc, sizeof(dsDesc));
    dsDesc.DepthEnable = FALSE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;

    hr = m_renderer->GetDevice()->CreateDepthStencilState(&dsDesc, m_depthStateNone.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_SAMPLER_DESC sampDesc;
    ZeroMemory(&sampDesc, sizeof(sampDesc));
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    hr = m_renderer->GetDevice()->CreateSamplerState(&sampDesc, m_samplerLinear.GetAddressOf());
    if (FAILED(hr)) return false;

    // Загрузка автоэкспозиции (Compute Shaders)
    m_csHistogramBuild = std::make_unique<ComputeShader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_csHistogramBuild->Load(L"Assets/Shaders/PostProcess/AutoExposure.hlsl", "CS_BuildHistogram")) return false;

    m_csHistogramAdapt = std::make_unique<ComputeShader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_csHistogramAdapt->Load(L"Assets/Shaders/PostProcess/AutoExposure.hlsl", "CS_AdaptExposure")) return false;

    m_autoExposureCB = std::make_unique<ConstantBuffer<CB_AutoExposure>>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_autoExposureCB->Initialize(true);

    D3D11_BUFFER_DESC histDesc = {};
    histDesc.ByteWidth = 128 * sizeof(uint32_t);
    histDesc.Usage = D3D11_USAGE_DEFAULT;
    histDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    histDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    histDesc.StructureByteStride = sizeof(uint32_t);

    hr = m_renderer->GetDevice()->CreateBuffer(&histDesc, nullptr, m_histogramBuffer.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC histUavDesc = {};
    histUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    histUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    histUavDesc.Buffer.NumElements = 128;

    hr = m_renderer->GetDevice()->CreateUnorderedAccessView(m_histogramBuffer.Get(), &histUavDesc, m_histogramUAV.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC histSrvDesc = {};
    histSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    histSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    histSrvDesc.Buffer.NumElements = 128;

    hr = m_renderer->GetDevice()->CreateShaderResourceView(m_histogramBuffer.Get(), &histSrvDesc, m_histogramSRV.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = 1;
    texDesc.Height = 1;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

    float initExposure = 1.0f;
    D3D11_SUBRESOURCE_DATA initData = { &initExposure, sizeof(float), 0 };
    for (int i = 0; i < 2; ++i) {
        hr = m_renderer->GetDevice()->CreateTexture2D(&texDesc, &initData, m_exposureTex[i].GetAddressOf());
        if (FAILED(hr)) return false;

        hr = m_renderer->GetDevice()->CreateUnorderedAccessView(m_exposureTex[i].Get(), nullptr, m_exposureUAV[i].GetAddressOf());
        if (FAILED(hr)) return false;

        hr = m_renderer->GetDevice()->CreateShaderResourceView(m_exposureTex[i].Get(), nullptr, m_exposureSRV[i].GetAddressOf());
        if (FAILED(hr)) return false;
    }

    return true;
}

void PostProcessManager::Render(ID3D11ShaderResourceView* depthSRV, ID3D11ShaderResourceView* sceneHdrSRV,
    RenderTarget* sceneHdrTarget, RenderTarget* finalOutRT, ConstantBuffer<CB_GlobalWeather>* weatherCB)
{
    auto context = m_renderer->GetContext();
    const auto& cfg = EngineConfig::Get();

    // Сброс геометрии (рисуем полноэкранный треугольник без вершинного буфера)
    context->IASetInputLayout(nullptr);
    ID3D11Buffer* nullVBs[2] = { nullptr, nullptr };
    UINT strides[2] = { 0, 0 };
    UINT offsets[2] = { 0, 0 };
    context->IASetVertexBuffers(0, 2, nullVBs, strides, offsets);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->OMSetDepthStencilState(m_depthStateNone.Get(), 0);
    context->PSSetSamplers(0, 1, m_samplerLinear.GetAddressOf());

    //LENS FLARES
    if (cfg.PostProcess.EnableLensFlares) {
        ID3D11RenderTargetView* hdrRTV = sceneHdrTarget->GetRTV();
        context->OMSetRenderTargets(1, &hdrRTV, nullptr);
        context->OMSetBlendState(m_blendStateAdditive.Get(), nullptr, 0xffffffff);

        m_sunEffectsShader->Bind();
        context->VSSetShaderResources(0, 1, &depthSRV);
        context->PSSetShaderResources(0, 1, &depthSRV);
        weatherCB->BindVS(1);
        weatherCB->BindPS(1);

        context->Draw(3, 0);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->VSSetShaderResources(0, 1, &nullSRV);
        context->PSSetShaderResources(0, 1, &nullSRV);

        ID3D11RenderTargetView* nullRTV = nullptr;
        context->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

    m_renderer->RestoreDefaultBlendState();

    // BLOOM ПАЙПЛАЙН (Dual Kawase)
    if (cfg.Bloom.Enabled && !m_bloomChain.empty()) {
        m_bloomShader->Bind();
        context->PSSetSamplers(0, 1, m_samplerLinear.GetAddressOf());
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        CB_Bloom bData;

        // EXTRACT & KARIS AVERAGE
        auto* rtExtract = m_bloomChain[0].get();
        D3D11_VIEWPORT vpExt = { 0.0f, 0.0f, static_cast<float>(rtExtract->GetWidth()), static_cast<float>(rtExtract->GetHeight()), 0.0f, 1.0f };
        context->RSSetViewports(1, &vpExt);

        ID3D11RenderTargetView* rtvExt = rtExtract->GetRTV();
        context->OMSetRenderTargets(1, &rtvExt, nullptr);
        context->ClearRenderTargetView(rtvExt, clearColor);

        bData.Params = Vector4(cfg.Bloom.Threshold, 0.0f, 1.0f / m_width, 1.0f / m_height);
        m_bloomCB->UpdateDynamic(bData);
        m_bloomCB->BindPS(0);
        context->PSSetShaderResources(0, 1, &sceneHdrSRV);
        context->Draw(3, 0);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->PSSetShaderResources(0, 1, &nullSRV);
        ID3D11RenderTargetView* nullRTV = nullptr;
        context->OMSetRenderTargets(1, &nullRTV, nullptr);

        // DOWNSAMPLE
        for (size_t i = 0; i < m_bloomChain.size() - 1; ++i) {
            auto* rtSrc = m_bloomChain[i].get();
            auto* rtDst = m_bloomChain[i + 1].get();

            D3D11_VIEWPORT vpDown = { 0.0f, 0.0f, static_cast<float>(rtDst->GetWidth()), static_cast<float>(rtDst->GetHeight()), 0.0f, 1.0f };
            context->RSSetViewports(1, &vpDown);

            ID3D11RenderTargetView* rtvDst = rtDst->GetRTV();
            context->OMSetRenderTargets(1, &rtvDst, nullptr);
            context->ClearRenderTargetView(rtvDst, clearColor);

            bData.Params = Vector4(0.0f, 1.0f, 1.0f / rtSrc->GetWidth(), 1.0f / rtSrc->GetHeight());
            m_bloomCB->UpdateDynamic(bData);

            ID3D11ShaderResourceView* srvSrc = rtSrc->GetSRV();
            context->PSSetShaderResources(0, 1, &srvSrc);
            context->Draw(3, 0);

            context->PSSetShaderResources(0, 1, &nullSRV);
            context->OMSetRenderTargets(1, &nullRTV, nullptr);
        }

        // UPSAMPLE
        context->OMSetBlendState(m_blendStateAdditive.Get(), nullptr, 0xffffffff);

        for (int i = static_cast<int>(m_bloomChain.size()) - 1; i > 0; --i) {
            auto* rtSrc = m_bloomChain[i].get();
            auto* rtDst = m_bloomChain[i - 1].get();

            D3D11_VIEWPORT vpUp = { 0.0f, 0.0f, static_cast<float>(rtDst->GetWidth()), static_cast<float>(rtDst->GetHeight()), 0.0f, 1.0f };
            context->RSSetViewports(1, &vpUp);

            ID3D11RenderTargetView* rtvDst = rtDst->GetRTV();
            context->OMSetRenderTargets(1, &rtvDst, nullptr);

            bData.Params = Vector4(0.0f, 2.0f, 1.0f / rtSrc->GetWidth(), 1.0f / rtSrc->GetHeight());
            m_bloomCB->UpdateDynamic(bData);

            ID3D11ShaderResourceView* srvSrc = rtSrc->GetSRV();
            context->PSSetShaderResources(0, 1, &srvSrc);
            context->Draw(3, 0);

            context->PSSetShaderResources(0, 1, &nullSRV);
            context->OMSetRenderTargets(1, &nullRTV, nullptr);
        }

        m_renderer->RestoreDefaultBlendState();
    }

    // AUTO EXPOSURE (ГИСТОГРАММА)
    int nextExposureIndex = (m_exposureIndex + 1) % 2;

    if (cfg.AutoExposure.Enabled) {
        static auto s_lastTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - s_lastTime).count();
        s_lastTime = currentTime;

        UINT clearVals[4] = { 0, 0, 0, 0 };
        context->ClearUnorderedAccessViewUint(m_histogramUAV.Get(), clearVals);

        CB_AutoExposure aeData;
        aeData.LogLuminanceParams = Vector4(-8.0f, 4.0f, cfg.AutoExposure.MinExposure, cfg.AutoExposure.MaxExposure);
        aeData.TimeParams = Vector4(deltaTime, cfg.AutoExposure.SpeedUp, cfg.AutoExposure.SpeedDown, static_cast<float>(m_width * m_height));
        m_autoExposureCB->UpdateDynamic(aeData);

        ID3D11Buffer* cb = m_autoExposureCB->Get();
        context->CSSetConstantBuffers(0, 1, &cb);

        m_csHistogramBuild->Bind();
        context->CSSetShaderResources(0, 1, &sceneHdrSRV);
        context->CSSetUnorderedAccessViews(0, 1, m_histogramUAV.GetAddressOf(), nullptr);

        UINT dispatchX = static_cast<UINT>(std::ceil(m_width / 16.0f));
        UINT dispatchY = static_cast<UINT>(std::ceil(m_height / 16.0f));
        m_csHistogramBuild->Dispatch(dispatchX, dispatchY, 1);

        ID3D11UnorderedAccessView* nullUAV = nullptr;
        context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->CSSetShaderResources(0, 1, &nullSRV);

        m_csHistogramAdapt->Bind();
        ID3D11ShaderResourceView* adaptSRVs[2] = { m_histogramSRV.Get(), m_exposureSRV[m_exposureIndex].Get() };
        context->CSSetShaderResources(0, 2, adaptSRVs);
        context->CSSetUnorderedAccessViews(0, 1, m_exposureUAV[nextExposureIndex].GetAddressOf(), nullptr);
        m_csHistogramAdapt->Dispatch(1, 1, 1);

        ID3D11ShaderResourceView* nullSRVsCS[2] = { nullptr, nullptr };
        context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        context->CSSetShaderResources(0, 2, nullSRVsCS);
        m_csHistogramAdapt->Unbind();
    }
    m_exposureIndex = nextExposureIndex;

    // UBER-TONEMAP (Цвет, Экспозиция, Блум, Виньетка)
    D3D11_VIEWPORT vpFull = { 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f };
    context->RSSetViewports(1, &vpFull);

    ID3D11RenderTargetView* rtvOut = finalOutRT ? finalOutRT->GetRTV() : m_renderer->GetBackBufferRTV();
    context->OMSetRenderTargets(1, &rtvOut, nullptr);

    m_uberTonemapShader->Bind();
    context->PSSetSamplers(0, 1, m_samplerLinear.GetAddressOf());

    CB_UberTonemap uberData;
    uberData.BloomParams = Vector4(cfg.Bloom.Enabled ? cfg.Bloom.Intensity : 0.0f, 0.0f, 0.0f, 0.0f);

    float vigInt = 0.3f;
    float vigPow = 2.0f;
    float expComp = 0.0f;

    uberData.VignetteParams = Vector4(vigInt, vigPow, 0.0f, 0.0f);
    uberData.GrainParams = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    uberData.TonemapParams = Vector4(expComp, 0.0f, 0.0f, 0.0f);

    m_uberTonemapCB->UpdateDynamic(uberData);
    m_uberTonemapCB->BindPS(0);

    ID3D11ShaderResourceView* finalBloomSRV = cfg.Bloom.Enabled ? m_bloomChain[0]->GetSRV() : nullptr;
    ID3D11ShaderResourceView* srvs[3] = { sceneHdrSRV, finalBloomSRV, m_exposureSRV[m_exposureIndex].Get() };

    context->PSSetShaderResources(0, 3, srvs);
    context->Draw(3, 0);

    ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
    context->PSSetShaderResources(0, 3, nullSRVs);
}