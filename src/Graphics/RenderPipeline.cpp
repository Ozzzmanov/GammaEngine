//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// RenderPipeline.cpp
// ================================================================================

#include "RenderPipeline.h"
#include "DX11Renderer.h"
#include "Shader.h"
#include "ConstantBuffer.h"
#include "HzbManager.h"
#include "GpuCullingSystem.h"
#include "StaticGpuScene.h"
#include "TerrainGpuScene.h"
#include "LevelTextureManager.h"
#include "../Resources/TerrainArrayManager.h"
#include "../World/WaterVLO.h"
#include "Camera.h"
#include "../Config/EngineConfig.h"
#include "../Core/Logger.h"

#include <algorithm>

RenderPipeline::RenderPipeline(DX11Renderer* renderer)
    : m_renderer(renderer)
{
}

RenderPipeline::~RenderPipeline() {}

bool RenderPipeline::Initialize() {
    Logger::Info(LogCategory::Render, "Initializing Render Pipeline...");
    const auto& cfg = EngineConfig::Get();

    m_terrainArrayManager = std::make_unique<TerrainArrayManager>();
    m_terrainArrayManager->Initialize(m_renderer->GetDevice(), m_renderer->GetContext());
    m_terrainGpuScene = std::make_unique<TerrainGpuScene>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_terrainGpuScene->Initialize();
    m_staticScene = std::make_unique<StaticGpuScene>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_hzbManager = std::make_unique<HzbManager>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_hzbManager->Initialize(cfg.WindowWidth, cfg.WindowHeight);
    m_gpuCuller = std::make_unique<GpuCullingSystem>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_gpuCuller->Initialize();

    m_spriteBatch = std::make_unique<DirectX::SpriteBatch>(m_renderer->GetContext());
    m_states = std::make_unique<DirectX::CommonStates>(m_renderer->GetDevice());

    m_terrainShader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_terrainShader->Load(L"Assets/Shaders/Terrain.hlsl")) return false;
    m_modelShader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_modelShader->Load(L"Assets/Shaders/Model.hlsl")) return false;

    m_transformBuffer = std::make_unique<ConstantBuffer<CB_VS_Transform>>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_transformBuffer->Initialize(true);

    m_gbufferAlbedo = std::make_unique<RenderTarget>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_gbufferAlbedo->Initialize(cfg.WindowWidth, cfg.WindowHeight, DXGI_FORMAT_R8G8B8A8_UNORM, false);

    m_gbufferNormal = std::make_unique<RenderTarget>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_gbufferNormal->Initialize(cfg.WindowWidth, cfg.WindowHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, false);

    m_lightBuffer = std::make_unique<ConstantBuffer<CB_PS_Lighting>>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_lightBuffer->Initialize(true);

    m_deferredLightShader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_deferredLightShader->Load(L"Assets/Shaders/DeferredLight.hlsl")) return false;

    return true;
}

RenderStats RenderPipeline::RenderFrame(
    Camera* camera,
    const std::vector<std::shared_ptr<WaterVLO>>& waterObjects,
    LevelTextureManager* levelTexMgr,
    float gameTime,
    const RenderSettings& settings)
{
    RenderStats stats;
    const auto& cfg = EngineConfig::Get();

    auto view = DirectX::XMLoadFloat4x4(&camera->GetViewMatrix());
    auto proj = DirectX::XMLoadFloat4x4(&camera->GetProjectionMatrix());
    auto camPos = camera->GetPosition();
    float renderDistSq = settings.RenderDistance * settings.RenderDistance;

    if (!settings.FreezeCulling) {
        m_cullView = camera->GetViewMatrix();
        m_cullProj = camera->GetProjectionMatrix();
    }

    auto cullView = DirectX::XMLoadFloat4x4(&m_cullView);
    auto cullProj = DirectX::XMLoadFloat4x4(&m_cullProj);

    if (m_isFirstFrame) {
        m_prevView = m_cullView;
        m_prevProj = m_cullProj;
        m_isFirstFrame = false;
    }

    DirectX::BoundingFrustum cullFrustum;
    DirectX::BoundingFrustum::CreateFromMatrix(cullFrustum, cullProj);
    cullFrustum.Transform(cullFrustum, DirectX::XMMatrixInverse(nullptr, cullView));

    m_renderer->Clear(0.1f, 0.15f, 0.25f, 1.0f);
    m_renderer->SetWireframe(settings.IsWireframe);

    CB_VS_Transform camData;
    camData.World = DirectX::XMMatrixIdentity();
    camData.View = DirectX::XMMatrixTranspose(view);
    camData.Projection = DirectX::XMMatrixTranspose(proj);
    m_transformBuffer->UpdateDynamic(camData);
    m_transformBuffer->BindVS(0);

    ID3D11ShaderResourceView* diffuseSRV = levelTexMgr ? levelTexMgr->GetSRV() : nullptr;

    // ПРОХОДЫ
    PassGPUCulling(settings, (float)cfg.WindowHeight);

    if (settings.EnableZPrepass && !settings.FreezeCulling) {
        PassZPrepass(settings, diffuseSRV);
    }
    else {
        m_renderer->BindDefaultDepthState();
        m_renderer->RestoreDefaultBlendState();
    }

    PassOpaque(diffuseSRV);

    PassLighting(camera->GetViewMatrix(), camera->GetProjectionMatrix(), camPos);

    PassWater(waterObjects, m_cullView, m_cullProj, camPos, gameTime, cullFrustum, renderDistSq, settings.EnableCulling);

    if (cfg.EnableOcclusionCulling) {
        PassHZB((float)cfg.WindowWidth, (float)cfg.WindowHeight);
    }

    if (settings.ShowHzbDebug) {
        RenderHzbDebug();
    }

    if (!settings.FreezeCulling) {
        m_prevView = m_cullView;
        m_prevProj = m_cullProj;
    }

    return stats;
}

void RenderPipeline::PassGPUCulling(const RenderSettings& settings, float windowHeight) {
    if (settings.FreezeCulling) return;

    auto cullView = DirectX::XMLoadFloat4x4(&m_cullView);
    auto cullProj = DirectX::XMLoadFloat4x4(&m_cullProj);
    auto prevViewMat = DirectX::XMLoadFloat4x4(&m_prevView);
    auto prevProjMat = DirectX::XMLoadFloat4x4(&m_prevProj);

    DirectX::BoundingFrustum cullFrustum;
    DirectX::BoundingFrustum::CreateFromMatrix(cullFrustum, cullProj);
    cullFrustum.Transform(cullFrustum, DirectX::XMMatrixInverse(nullptr, cullView));

    DirectX::XMFLOAT3 cullOrigin;
    DirectX::XMVECTOR det;
    DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(&det, cullView);
    DirectX::XMStoreFloat3(&cullOrigin, invView.r[3]);

    const auto& cfg = EngineConfig::Get();

    if (m_staticScene && m_staticScene->GetArgsBuffer() != nullptr) {
        m_renderer->GetContext()->CopyResource(m_staticScene->GetArgsBuffer(), m_staticScene->GetArgsResetBuffer());

        m_gpuCuller->UpdateFrameConstants(
            cullView, cullProj, prevViewMat, prevProjMat, cullFrustum, cullOrigin,
            settings.EnableLODs, settings.EnableCulling, cfg.EnableSizeCulling, cfg.EnableOcclusionCulling,
            windowHeight, m_hzbManager->GetHzbSize(), settings.RenderDistance
        );

        m_gpuCuller->PerformCulling(
            m_staticScene->GetInstanceSRV(), m_staticScene->GetMetaDataSRV(),
            m_hzbManager->GetHzbSRV(), m_staticScene->GetVisibleIndexUAV(),
            m_staticScene->GetIndirectArgsUAV(), m_staticScene->GetTotalInstanceCount()
        );
    }

    if (m_terrainGpuScene) {
        m_terrainGpuScene->PerformCulling(
            cullView, cullProj, prevViewMat, prevProjMat, cullFrustum, cullOrigin,
            m_hzbManager->GetHzbSRV(), m_hzbManager->GetHzbSize(),
            settings.EnableCulling, cfg.EnableOcclusionCulling, settings.RenderDistance
        );
    }
}

void RenderPipeline::PassZPrepass(const RenderSettings& settings, ID3D11ShaderResourceView* diffuseSRV) {
    m_renderer->BindZPrepassState();

    if (m_terrainGpuScene && m_terrainShader) {
        m_terrainShader->Bind();
        m_terrainGpuScene->BindResources(m_terrainArrayManager.get(), diffuseSRV);
        m_terrainGpuScene->BindGeometry();
        m_renderer->GetContext()->DrawIndexedInstancedIndirect(m_terrainGpuScene->GetIndirectArgsBuffer(), 0);
    }

    if (m_staticScene && m_staticScene->GetArgsBuffer() != nullptr) {
        if (m_modelShader) m_modelShader->Bind();
        m_staticScene->Render();
    }

    m_renderer->BindColorPassState();
}

void RenderPipeline::PassOpaque(ID3D11ShaderResourceView* diffuseSRV) {
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_renderer->GetContext()->ClearRenderTargetView(m_gbufferAlbedo->GetRTV(), clearColor);
    m_renderer->GetContext()->ClearRenderTargetView(m_gbufferNormal->GetRTV(), clearColor);

    ID3D11RenderTargetView* rtvs[2] = { m_gbufferAlbedo->GetRTV(), m_gbufferNormal->GetRTV() };
    m_renderer->GetContext()->OMSetRenderTargets(2, rtvs, m_renderer->GetDepthDSV());

    ID3D11SamplerState* samplers[] = { m_states->AnisotropicWrap(), m_states->LinearClamp() };

    if (m_terrainGpuScene && m_terrainShader) {
        m_terrainShader->Bind();
        m_terrainGpuScene->BindResources(m_terrainArrayManager.get(), diffuseSRV);
        m_renderer->GetContext()->VSSetSamplers(0, 2, samplers);
        m_renderer->GetContext()->PSSetSamplers(0, 2, samplers);
        m_terrainGpuScene->BindGeometry();
        m_renderer->GetContext()->DrawIndexedInstancedIndirect(m_terrainGpuScene->GetIndirectArgsBuffer(), 0);
    }

    ID3D11ShaderResourceView* nullSRVs[12] = { nullptr };
    m_renderer->GetContext()->VSSetShaderResources(0, 12, nullSRVs);
    m_renderer->GetContext()->PSSetShaderResources(0, 12, nullSRVs);

    if (m_staticScene && m_staticScene->GetArgsBuffer() != nullptr) {
        if (m_modelShader) m_modelShader->Bind();
        m_staticScene->Render();
    }

    m_renderer->GetContext()->VSSetShaderResources(0, 12, nullSRVs);
    m_renderer->GetContext()->PSSetShaderResources(0, 12, nullSRVs);
}

void RenderPipeline::PassLighting(const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& proj, const DirectX::XMFLOAT3& camPos) {
    ID3D11RenderTargetView* backBuffer = m_renderer->GetBackBufferRTV();
    m_renderer->GetContext()->OMSetRenderTargets(1, &backBuffer, nullptr);

    DirectX::XMMATRIX v = DirectX::XMLoadFloat4x4(&view);
    DirectX::XMMATRIX p = DirectX::XMLoadFloat4x4(&proj);
    DirectX::XMMATRIX vp = DirectX::XMMatrixMultiply(v, p);

    DirectX::XMVECTOR det;
    DirectX::XMMATRIX invVP = DirectX::XMMatrixInverse(&det, vp);

    CB_PS_Lighting lightData;
    lightData.InvViewProj = DirectX::XMMatrixTranspose(invVP);
    lightData.CameraPos = camPos;
    m_lightBuffer->UpdateDynamic(lightData);
    m_lightBuffer->BindPS(0);

    ID3D11ShaderResourceView* srvs[3] = {
        m_gbufferAlbedo->GetSRV(),
        m_gbufferNormal->GetSRV(),
        m_renderer->GetDepthSRV()
    };
    m_renderer->GetContext()->PSSetShaderResources(0, 3, srvs);

    ID3D11SamplerState* samplers[1] = { m_states->PointClamp() };
    m_renderer->GetContext()->PSSetSamplers(0, 1, samplers);

    m_deferredLightShader->Bind();
    m_renderer->GetContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_renderer->GetContext()->Draw(3, 0);

    ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
    m_renderer->GetContext()->PSSetShaderResources(0, 3, nullSRVs);
}

void RenderPipeline::PassWater(const std::vector<std::shared_ptr<WaterVLO>>& waterObjects, const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& proj, const DirectX::XMFLOAT3& camPos, float gameTime, const DirectX::BoundingFrustum& cullFrustum, float renderDistSq, bool enableCulling) {
    ID3D11RenderTargetView* backBuffer = m_renderer->GetBackBufferRTV();
    m_renderer->GetContext()->OMSetRenderTargets(1, &backBuffer, m_renderer->GetDepthDSV());
    m_renderer->BindWaterPassState();

    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr };
    m_renderer->GetContext()->PSSetShaderResources(0, 2, nullSRVs);

    auto v = DirectX::XMLoadFloat4x4(&view);
    auto p = DirectX::XMLoadFloat4x4(&proj);

    for (const auto& water : waterObjects) {
        water->Render(v, p, camPos, gameTime, m_transformBuffer.get(), cullFrustum, renderDistSq, enableCulling);
    }
}

void RenderPipeline::PassHZB(float windowWidth, float windowHeight) {
    m_renderer->UnbindRenderTargets();
    m_hzbManager->Generate(m_renderer);

    ID3D11RenderTargetView* backBuffer = m_renderer->GetBackBufferRTV();
    m_renderer->GetContext()->OMSetRenderTargets(1, &backBuffer, m_renderer->GetDepthDSV());

    D3D11_VIEWPORT vp = { 0 };
    vp.Width = windowWidth;
    vp.Height = windowHeight;
    vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
    m_renderer->GetContext()->RSSetViewports(1, &vp);
}

// FIX ME ОТЛАДКА, ВЫРЕЗАТЬ ПОСЛЕ НАСТРОЙКИ HZB.
void RenderPipeline::RenderHzbDebug() {
    ID3D11ShaderResourceView* hzbSRV = m_hzbManager->GetHzbSRV();
    if (!hzbSRV) return;

    m_spriteBatch->Begin(DirectX::SpriteSortMode_Deferred, m_states->Opaque());
    RECT destRect = { 10, 10, 512, 288 }; // 16:9
    m_spriteBatch->Draw(hzbSRV, destRect, DirectX::Colors::White);
    m_spriteBatch->End();

    m_renderer->BindDefaultDepthState();
    m_renderer->RestoreDefaultBlendState();
}