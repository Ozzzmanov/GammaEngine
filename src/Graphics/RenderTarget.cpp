//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// RenderTarget.cpp
// ================================================================================

#include "RenderTarget.h"
#include "../Core/Logger.h"

RenderTarget::RenderTarget(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
}

bool RenderTarget::Initialize(int width, int height, DXGI_FORMAT format, bool withDepth) {
    m_width = width;
    m_height = height;

    // Создаем текстуру цвета
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, m_colorTexture.GetAddressOf());
    if (FAILED(hr)) {
        Logger::Error(LogCategory::Render, "RenderTarget: Failed to create Color Texture.");
        return false;
    }

    // Создаем RTV (Render Target View) чтобы в нее писать
    hr = m_device->CreateRenderTargetView(m_colorTexture.Get(), nullptr, m_rtv.GetAddressOf());
    if (FAILED(hr)) {
        Logger::Error(LogCategory::Render, "RenderTarget: Failed to create RTV.");
        return false;
    }

    // Создаем SRV (Shader Resource View) чтобы ее читать в других шейдерах
    hr = m_device->CreateShaderResourceView(m_colorTexture.Get(), nullptr, m_srv.GetAddressOf());
    if (FAILED(hr)) {
        Logger::Error(LogCategory::Render, "RenderTarget: Failed to create SRV.");
        return false;
    }

    // Опционально создаем собственный Depth Buffer
    if (withDepth) {
        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        hr = m_device->CreateTexture2D(&depthDesc, nullptr, m_depthTexture.GetAddressOf());
        if (FAILED(hr)) {
            Logger::Error(LogCategory::Render, "RenderTarget: Failed to create Depth Texture.");
            return false;
        }

        hr = m_device->CreateDepthStencilView(m_depthTexture.Get(), nullptr, m_dsv.GetAddressOf());
        if (FAILED(hr)) {
            Logger::Error(LogCategory::Render, "RenderTarget: Failed to create DSV.");
            return false;
        }
    }

    return true;
}

void RenderTarget::Clear(float r, float g, float b, float a) {
    if (m_rtv) {
        float color[4] = { r, g, b, a };
        m_context->ClearRenderTargetView(m_rtv.Get(), color);
    }
}

void RenderTarget::ClearDepth(float depth, uint8_t stencil) {
    if (m_dsv) {
        m_context->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, stencil);
    }
}

void RenderTarget::Bind() {
    ID3D11RenderTargetView* rtv = m_rtv.Get();
    ID3D11DepthStencilView* dsv = m_dsv ? m_dsv.Get() : nullptr;
    m_context->OMSetRenderTargets(1, &rtv, dsv);

    // Устанавливаем вьюпорт по размеру таргета
    D3D11_VIEWPORT vp;
    vp.Width = (float)m_width;
    vp.Height = (float)m_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    m_context->RSSetViewports(1, &vp);
}

void RenderTarget::BindWithCustomDepth(ID3D11DepthStencilView* customDepthDSV) {
    ID3D11RenderTargetView* rtv = m_rtv.Get();
    m_context->OMSetRenderTargets(1, &rtv, customDepthDSV);

    D3D11_VIEWPORT vp;
    vp.Width = (float)m_width;
    vp.Height = (float)m_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    m_context->RSSetViewports(1, &vp);
}