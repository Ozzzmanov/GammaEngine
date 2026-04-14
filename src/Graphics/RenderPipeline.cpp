//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
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
#include "FloraGpuScene.h"
#include "ShadowManager.h"
#include "PostProcessManager.h"
#include <algorithm>
#include "LightManager.h"

RenderPipeline::RenderPipeline(DX11Renderer* renderer)
    : m_renderer(renderer)
{
}

RenderPipeline::~RenderPipeline() {}

bool RenderPipeline::Initialize() {
    Logger::Info(LogCategory::Render, "Initializing Render Pipeline...");
    const auto& cfg = EngineConfig::Get();
    const auto& activeProfile = cfg.GetActiveProfile();

    m_terrainArrayManager = std::make_unique<TerrainArrayManager>();
    m_terrainArrayManager->Initialize(m_renderer->GetDevice(), m_renderer->GetContext());

    const UINT maxInstances = 1000000;
    std::vector<uint32_t> instanceIds(maxInstances);
    for (uint32_t i = 0; i < maxInstances; ++i) {
        instanceIds[i] = i;
    }

    D3D11_BUFFER_DESC idDesc = {};
    idDesc.ByteWidth = sizeof(uint32_t) * maxInstances;
    idDesc.Usage = D3D11_USAGE_IMMUTABLE;
    idDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA idData = { instanceIds.data(), 0, 0 };
    if (FAILED(m_renderer->GetDevice()->CreateBuffer(&idDesc, &idData, m_instanceIdBuffer.GetAddressOf()))) {
        Logger::Error(LogCategory::Render, "Failed to create Instance ID Buffer!");
        return false;
    }

    m_terrainGpuScene = std::make_unique<TerrainGpuScene>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_terrainGpuScene->Initialize();

    m_clipmapManager = std::make_unique<ClipmapRvtManager>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_rvtBaker = std::make_unique<RvtBaker>(m_renderer->GetDevice(), m_renderer->GetContext());

    if (activeProfile.TerrainLod.EnableRVT) {
        m_clipmapManager->Initialize();
        m_rvtBaker->Initialize();
    }

    m_staticScene = std::make_unique<StaticGpuScene>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_floraScene = std::make_unique<FloraGpuScene>(m_renderer->GetDevice(), m_renderer->GetContext());

    m_shadowManager = std::make_unique<ShadowManager>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_shadowManager->Initialize(activeProfile.Shadows.Resolution, activeProfile.Shadows.CascadeCount)) {
        Logger::Error(LogCategory::Render, "Failed to initialize ShadowManager!");
        return false;
    }

    m_hzbManager = std::make_unique<HzbManager>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_hzbManager->Initialize(cfg.System.WindowWidth, cfg.System.WindowHeight);

    m_gpuCuller = std::make_unique<GpuCullingSystem>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_gpuCuller->Initialize();

    m_sceneHDR = std::make_unique<RenderTarget>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_sceneHDR->Initialize(cfg.System.WindowWidth, cfg.System.WindowHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, false);

    m_finalGameRT = std::make_unique<RenderTarget>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_finalGameRT->Initialize(cfg.System.WindowWidth, cfg.System.WindowHeight, DXGI_FORMAT_R8G8B8A8_UNORM, false);

    int volWidth = cfg.System.WindowWidth;
    int volHeight = cfg.System.WindowHeight;

    if (cfg.VolumetricLighting.HalfResolution) {
        volWidth = std::max(1, volWidth / 2);
        volHeight = std::max(1, volHeight / 2);
        Logger::Info(LogCategory::Render, "Volumetric Lighting initialized in Half-Resolution mode.");
    }

    m_volumetricRT = std::make_unique<RenderTarget>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_volumetricRT->Initialize(volWidth, volHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, false);

    m_volumetricShader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_volumetricShader->Load(L"Assets/Shaders/VolumetricLight.hlsl")) return false;

    m_refractionHDR = std::make_unique<RenderTarget>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_refractionHDR->Initialize(cfg.System.WindowWidth, cfg.System.WindowHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, false);

    m_postProcess = std::make_unique<PostProcessManager>(m_renderer);
    if (!m_postProcess->Initialize(cfg.System.WindowWidth, cfg.System.WindowHeight)) return false;

    m_spriteBatch = std::make_unique<DirectX::SpriteBatch>(m_renderer->GetContext());
    m_states = std::make_unique<DirectX::CommonStates>(m_renderer->GetDevice());

    D3D11_SAMPLER_DESC cmpSampDesc = {};
    cmpSampDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
    cmpSampDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    cmpSampDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    cmpSampDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    cmpSampDesc.BorderColor[0] = 1.0f;
    cmpSampDesc.BorderColor[1] = 1.0f;
    cmpSampDesc.BorderColor[2] = 1.0f;
    cmpSampDesc.BorderColor[3] = 1.0f;
    cmpSampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;

    HRESULT hr = m_renderer->GetDevice()->CreateSamplerState(&cmpSampDesc, m_shadowSampler.GetAddressOf());
    if (FAILED(hr)) {
        Logger::Error(LogCategory::Render, "Failed to create Shadow Sampler!");
        return false;
    }

    m_terrainShader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_terrainShader->Load(L"Assets/Shaders/Terrain.hlsl")) return false;

    m_modelShader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_modelShader->Load(L"Assets/Shaders/Model.hlsl")) return false;

    m_floraShader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_floraShader->Load(L"Assets/Shaders/Flora.hlsl", nullptr, true)) return false;

    m_shadowModelShader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_shadowModelShader->Load(L"Assets/Shaders/ShadowModel.hlsl")) return false;

    m_shadowFloraShader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_shadowFloraShader->Load(L"Assets/Shaders/ShadowFlora.hlsl", nullptr, true)) return false;

    m_shadowTerrainShader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_shadowTerrainShader->Load(L"Assets/Shaders/ShadowTerrain.hlsl")) return false;

    m_waterShaderLow = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_waterShaderUltra = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());

    D3D_SHADER_MACRO potatoMacros[] = { { "POTATO_MODE", "1" }, { NULL, NULL } };
    if (!m_waterShaderLow->Load(L"Assets/Shaders/Water.hlsl", potatoMacros, false, true, false)) return false;
    if (!m_waterShaderUltra->Load(L"Assets/Shaders/Water.hlsl", nullptr, false, true, true)) return false;

    m_transformBuffer = std::make_unique<ConstantBuffer<CB_VS_Transform>>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_transformBuffer->Initialize(true);

    m_weatherBuffer = std::make_unique<ConstantBuffer<CB_GlobalWeather>>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_weatherBuffer->Initialize(true);

    m_gbufferAlbedo = std::make_unique<RenderTarget>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_gbufferAlbedo->Initialize(cfg.System.WindowWidth, cfg.System.WindowHeight, DXGI_FORMAT_R8G8B8A8_UNORM, false);

    m_gbufferNormal = std::make_unique<RenderTarget>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_gbufferNormal->Initialize(cfg.System.WindowWidth, cfg.System.WindowHeight, DXGI_FORMAT_R10G10B10A2_UNORM, false);

    m_gbufferMRAO = std::make_unique<RenderTarget>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_gbufferMRAO->Initialize(cfg.System.WindowWidth, cfg.System.WindowHeight, DXGI_FORMAT_R8G8B8A8_UNORM, false);

    m_lightBuffer = std::make_unique<ConstantBuffer<CB_PS_Lighting>>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_lightBuffer->Initialize(true);

    m_deferredLightShader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_deferredLightShader->Load(L"Assets/Shaders/DeferredLight.hlsl")) return false;

    return true;
}

RenderStats RenderPipeline::RenderFrame(
    Camera* camera,
    const EnvironmentData& env,
    const std::vector<std::shared_ptr<WaterVLO>>& waterObjects,
    LevelTextureManager* levelTexMgr,
    float gameTime,
    const RenderSettings& settings)
{
    RenderStats stats;
    const auto& cfg = EngineConfig::Get();
    const auto& activeProfile = cfg.GetActiveProfile();

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

    const auto& vlCfg = cfg.VolumetricLighting;

    Matrix viewMat = camera->GetViewMatrix();
    Matrix projMat = camera->GetProjectionMatrix();
    Vector4 sunDirVec = Vector4(env.sun.direction.x, env.sun.direction.y, env.sun.direction.z, 0.0f);
    Vector4 sunViewDir = Vector4::Transform(sunDirVec, viewMat);
    float safeDist = activeProfile.Graphics.FarZ * 0.5f;
    Vector4 sunViewPos = Vector4(sunViewDir.x * safeDist, sunViewDir.y * safeDist, sunViewDir.z * safeDist, 1.0f);
    Vector4 sunClipPos = Vector4::Transform(sunViewPos, projMat);
    Vector2 sunScreenPos = { -1.0f, -1.0f };
    if (sunClipPos.w > 0.001f && !std::isnan(sunClipPos.x) && !std::isnan(sunClipPos.y)) {
        sunScreenPos.x = (sunClipPos.x / sunClipPos.w) * 0.5f + 0.5f;
        sunScreenPos.y = (-sunClipPos.y / sunClipPos.w) * 0.5f + 0.5f;
    }

    if (std::isnan(sunScreenPos.x) || std::isnan(sunScreenPos.y)) {
        sunScreenPos = { -1.0f, -1.0f };
    }

    CB_GlobalWeather weatherData;
    weatherData.WindParams1 = Vector4(cfg.Wind.Speed, cfg.Wind.TrunkBendAmplitude, cfg.Wind.RootStiffness, 0.0f);
    weatherData.WindParams2 = Vector4(cfg.Wind.LeafFlutterSpeed, cfg.Wind.LeafFlutterAmplitude, cfg.Wind.LeafMicroTurbulence, 0.0f);
    weatherData.SunDirection = Vector4(env.sun.direction.x, env.sun.direction.y, env.sun.direction.z, 0.0f);
    weatherData.SunColor = Vector4(env.sun.color.x, env.sun.color.y, env.sun.color.z, env.sun.intensity);
    weatherData.SkyZenithColor = Vector4(env.zenithColor.x, env.zenithColor.y, env.zenithColor.z, 1.0f);
    weatherData.SkyHorizonColor = Vector4(env.horizonColor.x, env.horizonColor.y, env.horizonColor.z, 1.0f);

    weatherData.SunParams = Vector4(
        env.sun.godRaysDensity,
        vlCfg.GAnisotropy,
        vlCfg.Enabled ? (float)vlCfg.QualitySteps : 0.0f,
        vlCfg.RayLength
    );

    weatherData.SunPositionNDC = Vector4(
        sunScreenPos.x,
        sunScreenPos.y,
        sunClipPos.w,
        cfg.PostProcess.EnableLensFlares ? 1.0f : 0.0f
    );

    static uint32_t s_rvtFrameCount = 0;
    s_rvtFrameCount++;

    if (activeProfile.TerrainLod.EnableRVT) {
        m_clipmapManager->Update(camPos);
        auto tasks = m_clipmapManager->PopUpdateTasks();

        for (const auto& task : tasks) {
            BakeJobParams bp = {};
            bp.WorldPosMinX = task.StartCellX * task.TexelSize;
            bp.WorldPosMinZ = task.StartCellZ * task.TexelSize;
            bp.UpdateWidth = task.UpdateWidth;
            bp.UpdateHeight = task.UpdateHeight;
            bp.TexelSize = task.TexelSize;
            bp.Resolution = (uint32_t)m_clipmapManager->GetResolution();
            bp.CascadeIndex = (uint32_t)task.CascadeIndex;

            m_rvtBaker->BakeClipmap(
                bp,
                m_terrainArrayManager.get(),
                levelTexMgr,
                m_terrainGpuScene->GetChunkSliceLookupSRV(),
                m_terrainGpuScene->GetChunkDataSRV(),
                m_clipmapManager->GetAlbedoArrayUAV(),
                m_clipmapManager->GetNormalArrayUAV()
            );
        }
    }

    PassGPUCulling(settings, (float)cfg.System.WindowHeight);

    if (activeProfile.Shadows.Enabled) {
        if (activeProfile.Shadows.UpdateEveryFrame || !m_shadowsBaked) {
            float maxShadowDist = activeProfile.Shadows.MaxShadowDistance;
            DirectX::XMFLOAT3 lightDirForShadows = { env.sun.direction.x, env.sun.direction.y, env.sun.direction.z };

            m_shadowManager->Update(camera, lightDirForShadows, maxShadowDist);

            m_renderer->BindSolidState();
            PassShadows(gameTime, camera, settings);
            m_shadowsBaked = true;
        }
    }

    m_weatherBuffer->UpdateDynamic(weatherData);
    m_weatherBuffer->BindPS(1);

    m_renderer->Clear(0.1f, 0.15f, 0.25f, 1.0f);
    m_sceneHDR->Clear(0.1f, 0.15f, 0.25f, 1.0f);
    m_renderer->SetWireframe(settings.IsWireframe);

    CB_VS_Transform camData;
    camData.World = DirectX::XMMatrixIdentity();
    camData.View = DirectX::XMMatrixTranspose(view);
    camData.Projection = DirectX::XMMatrixTranspose(proj);
    float rvtFlag = activeProfile.TerrainLod.EnableRVT ? 1.0f : 0.0f;
    camData.TimeAndParams = Vector4(gameTime, activeProfile.TerrainLod.Dist1, activeProfile.TerrainLod.Dist2, rvtFlag);
    m_transformBuffer->UpdateDynamic(camData);
    m_transformBuffer->BindVS(0);
    m_transformBuffer->BindPS(0);

    ID3D11ShaderResourceView* diffuseSRV = levelTexMgr ? levelTexMgr->GetSRV() : nullptr;

    if (settings.EnableZPrepass && !settings.FreezeCulling) {
        PassZPrepass(settings, levelTexMgr);
    }
    else {
        m_renderer->BindDefaultDepthState();
        m_renderer->RestoreDefaultBlendState();
    }

    PassOpaque(levelTexMgr);
    PassLighting(camera->GetViewMatrix(), camera->GetProjectionMatrix(), camPos);
    PassWater(waterObjects, m_cullView, m_cullProj, camPos, gameTime, cullFrustum, renderDistSq, settings.EnableCulling);

#ifdef GAMMA_EDITOR
    RenderTarget* finalRT = m_finalGameRT.get();
#else
    RenderTarget* finalRT = nullptr;
#endif

    m_postProcess->Render(
        m_renderer->GetDepthSRV(),
        m_sceneHDR->GetSRV(),
        m_sceneHDR.get(),
        finalRT,
        m_weatherBuffer.get()
    );

    if (activeProfile.Optimization.EnableOcclusionCulling) {
        PassHZB((float)cfg.System.WindowWidth, (float)cfg.System.WindowHeight);
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

    const auto& activeProfile = EngineConfig::Get().GetActiveProfile();

    if (m_staticScene && m_staticScene->GetArgsBuffer() != nullptr) {
        m_renderer->GetContext()->CopyResource(m_staticScene->GetArgsBuffer(), m_staticScene->GetArgsResetBuffer());

        m_gpuCuller->UpdateFrameConstants(
            cullView, cullProj, prevViewMat, prevProjMat, cullFrustum, cullOrigin,
            activeProfile.Lod, settings.EnableLODs,
            settings.EnableCulling, activeProfile.Optimization.EnableSizeCulling, activeProfile.Optimization.EnableOcclusionCulling,
            windowHeight, m_hzbManager->GetHzbSize(), settings.RenderDistance,
            0.0f, settings.RenderDistance,
            false, false, 0.0f, 0.0f, false, nullptr
        );

        ID3D11UnorderedAccessView* visUAVs[1] = { m_staticScene->GetVisibleIndexUAV() };
        ID3D11UnorderedAccessView* argUAVs[1] = { m_staticScene->GetIndirectArgsUAV() };

        m_gpuCuller->PerformCulling(
            m_staticScene->GetInstanceSRV(),   // ИСПРАВЛЕНО
            m_staticScene->GetMetaDataSRV(),   // ИСПРАВЛЕНО
            m_hzbManager->GetHzbSRV(),
            m_staticScene->GetBatchInfoSRV(),  // ИСПРАВЛЕНО
            visUAVs, argUAVs, 1, m_staticScene->GetTotalInstanceCount() // ИСПРАВЛЕНО
        );
    }

    if (m_terrainGpuScene) {
        m_terrainGpuScene->PerformCulling(
            cullView, cullProj, prevViewMat, prevProjMat, cullFrustum, cullOrigin,
            m_hzbManager->GetHzbSRV(), m_hzbManager->GetHzbSize(),
            settings.EnableCulling, activeProfile.Optimization.EnableOcclusionCulling, settings.EnableLODs, settings.RenderDistance
        );
    }

    if (m_floraScene && m_floraScene->GetArgsBuffer() != nullptr) {
        m_renderer->GetContext()->CopyResource(m_floraScene->GetArgsBuffer(), m_floraScene->GetArgsResetBuffer());

        m_gpuCuller->UpdateFrameConstants(
            cullView, cullProj, prevViewMat, prevProjMat, cullFrustum, cullOrigin,
            activeProfile.FloraLod, settings.EnableLODs,
            settings.EnableCulling, activeProfile.Optimization.EnableSizeCulling, activeProfile.Optimization.EnableOcclusionCulling,
            windowHeight, m_hzbManager->GetHzbSize(), settings.RenderDistance,
            0.0f, settings.RenderDistance,
            false, false, 0.0f, 0.0f, false, nullptr
        );

        ID3D11UnorderedAccessView* visUAVs[1] = { m_floraScene->GetVisibleIndexUAV() };
        ID3D11UnorderedAccessView* argUAVs[1] = { m_floraScene->GetIndirectArgsUAV() };

        m_gpuCuller->PerformCulling(
            m_floraScene->GetInstanceSRV(),
            m_floraScene->GetMetaDataSRV(),
            m_hzbManager->GetHzbSRV(),
            m_floraScene->GetBatchInfoSRV(),
            visUAVs, argUAVs, 1, m_floraScene->GetTotalInstanceCount()
        );
    }
}

void RenderPipeline::PassZPrepass(const RenderSettings& settings, LevelTextureManager* levelTexMgr) {
    m_renderer->BindZPrepassState();

    if (m_terrainGpuScene && m_terrainShader) {
        m_terrainShader->Bind();
        ID3D11SamplerState* samplers[1] = { m_states->LinearClamp() };
        m_renderer->GetContext()->VSSetSamplers(1, 1, samplers);
        m_renderer->GetContext()->PSSetSamplers(1, 1, samplers);

        // FIXME: Оптимизация Z-Prepass для Террейна.
        // Террейн - это глухая геометрия. В Z-Prepass ему нужен ТОЛЬКО HeightArray (для смещения вершин).
        // Биндить Albedo, Normal и MRAO бессмысленно и тратит пропускную способность видеопамяти.
        // Нужно создать отдельный m_terrainGpuScene->BindZPrepassResources(...)
        m_terrainGpuScene->BindResources(
            m_terrainArrayManager.get(),
            levelTexMgr,
            levelTexMgr ? levelTexMgr->GetSRV() : nullptr,
            m_clipmapManager->GetAlbedoArraySRV()
        );

        m_terrainGpuScene->BindGeometry(m_instanceIdBuffer.Get());
        for (int lod = 0; lod < 3; ++lod) {
            m_renderer->GetContext()->DrawIndexedInstancedIndirect(m_terrainGpuScene->GetIndirectArgsBuffer(), lod * 20);
        }
    }

    if (m_staticScene && m_staticScene->GetArgsBuffer() != nullptr) {
        // Для глухой статики (камни) можно вообще отключать Pixel Shader в Z-Prepass
        if (m_modelShader) m_modelShader->Bind();
        m_staticScene->Render(m_instanceIdBuffer.Get());
    }

    if (m_floraScene && m_floraScene->GetArgsBuffer() != nullptr) {
        // FIXME: Оптимизация Z-Prepass для Флоры.
        // биндим m_floraShader, который считает PBR, нормали и кучу математики.
        // Для Z-Prepass нужен специальный легкий шейдер (ZPrepassFlora.hlsl), 
        // который только двигает ветки по ветру и читает альфа-канал
        if (m_floraShader) m_floraShader->Bind();

        m_weatherBuffer->BindVS(2);

        m_renderer->BindSolidState();
        m_floraScene->RenderOpaque(m_instanceIdBuffer.Get());

        m_renderer->BindNoCullState();
        m_floraScene->RenderAlpha(m_instanceIdBuffer.Get());

        m_renderer->BindSolidState();
    }

    m_renderer->BindColorPassState();
}

void RenderPipeline::PassOpaque(LevelTextureManager* levelTexMgr) {
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_renderer->GetContext()->ClearRenderTargetView(m_gbufferAlbedo->GetRTV(), clearColor);
    m_renderer->GetContext()->ClearRenderTargetView(m_gbufferNormal->GetRTV(), clearColor);

    if (m_gbufferMRAO) {
        m_renderer->GetContext()->ClearRenderTargetView(m_gbufferMRAO->GetRTV(), clearColor);
    }

    ID3D11RenderTargetView* rtvs[3] = {
        m_gbufferAlbedo->GetRTV(),
        m_gbufferNormal->GetRTV(),
        m_gbufferMRAO->GetRTV()
    };
    m_renderer->GetContext()->OMSetRenderTargets(3, rtvs, m_renderer->GetDepthDSV());
    ID3D11SamplerState* samplers[] = { m_states->AnisotropicWrap(), m_states->LinearClamp() };

    const auto& activeProfile = EngineConfig::Get().GetActiveProfile();

    if (m_terrainGpuScene && m_terrainShader) {
        if (activeProfile.TerrainLod.EnableRVT) {
            ID3D11Buffer* clipmapCB = m_clipmapManager->GetConstantsBuffer();
            if (clipmapCB) {
                m_renderer->GetContext()->PSSetConstantBuffers(3, 1, &clipmapCB); // b3
            }
        }
        m_terrainShader->Bind();
        m_terrainGpuScene->BindResources(
            m_terrainArrayManager.get(),
            levelTexMgr,
            levelTexMgr ? levelTexMgr->GetSRV() : nullptr,
            m_clipmapManager->GetAlbedoArraySRV()
        );
        m_renderer->GetContext()->VSSetSamplers(0, 2, samplers);
        m_renderer->GetContext()->PSSetSamplers(0, 2, samplers);
        m_terrainGpuScene->BindGeometry(m_instanceIdBuffer.Get());
        for (int lod = 0; lod < 3; ++lod) {
            m_renderer->GetContext()->DrawIndexedInstancedIndirect(m_terrainGpuScene->GetIndirectArgsBuffer(), lod * 20);
        }
    }

    ID3D11ShaderResourceView* nullSRVs[12] = { nullptr };
    m_renderer->GetContext()->VSSetShaderResources(0, 12, nullSRVs);
    m_renderer->GetContext()->PSSetShaderResources(0, 12, nullSRVs);

    if (m_staticScene && m_staticScene->GetArgsBuffer() != nullptr) {
        if (m_modelShader) m_modelShader->Bind();
        m_staticScene->Render(m_instanceIdBuffer.Get());
    }

    if (m_floraScene && m_floraScene->GetArgsBuffer() != nullptr) {
        if (m_floraShader) m_floraShader->Bind();

        m_weatherBuffer->BindVS(2);
        m_renderer->BindSolidState();
        m_floraScene->RenderOpaque(m_instanceIdBuffer.Get());
        m_renderer->BindNoCullState();
        m_floraScene->RenderAlpha(m_instanceIdBuffer.Get());
        m_renderer->BindSolidState();
    }

    m_renderer->GetContext()->VSSetShaderResources(0, 12, nullSRVs);
    m_renderer->GetContext()->PSSetShaderResources(0, 12, nullSRVs);
}

void RenderPipeline::PassLighting(const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& proj, const DirectX::XMFLOAT3& camPos) {
    const auto& cfg = EngineConfig::Get();
    const auto& activeProfile = cfg.GetActiveProfile();
    const auto& lightCfg = activeProfile.Lighting;

    auto context = m_renderer->GetContext();

    DirectX::XMMATRIX v = DirectX::XMLoadFloat4x4(&view);
    DirectX::XMMATRIX p = DirectX::XMLoadFloat4x4(&proj);
    DirectX::XMMATRIX vp = DirectX::XMMatrixMultiply(v, p);
    DirectX::XMVECTOR det;
    DirectX::XMMATRIX invVP = DirectX::XMMatrixInverse(&det, vp);

    CB_PS_Lighting lightData;
    lightData.InvViewProj = DirectX::XMMatrixTranspose(invVP);
    lightData.ViewMatrix = DirectX::XMMatrixTranspose(v);
    lightData.CameraPos = camPos;
    lightData.CascadeCount = (float)m_shadowManager->GetCascadeCount();
    for (int i = 0; i < 4; ++i) {
        lightData.LightViewProj[i] = i < lightData.CascadeCount ? m_shadowManager->GetCascades()[i].ViewProj : DirectX::XMMatrixIdentity();
    }
    lightData.CascadeSplits = DirectX::XMFLOAT4(
        lightData.CascadeCount > 0 ? m_shadowManager->GetCascades()[0].SplitDepth : 0.0f,
        lightData.CascadeCount > 1 ? m_shadowManager->GetCascades()[1].SplitDepth : 0.0f,
        lightData.CascadeCount > 2 ? m_shadowManager->GetCascades()[2].SplitDepth : 0.0f,
        lightData.CascadeCount > 3 ? m_shadowManager->GetCascades()[3].SplitDepth : 0.0f
    );


    lightData.LightSettings = DirectX::XMFLOAT4(
        (float)LightManager::Get().GetPointLightCount(), // Сколько поинт-лайтов
        (float)LightManager::Get().GetSpotLightCount(),  // Сколько спот-лайтов
        lightCfg.MaxLightDistance,                       // Дальность куллинга
        0.0f                                             // Резерв
    );
    lightData.ExtraSettings = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f); // Резерв

    m_lightBuffer->UpdateDynamic(lightData);
    m_lightBuffer->BindPS(0);

    m_renderer->GetContext()->IASetInputLayout(nullptr);
    ID3D11Buffer* nullVBs[2] = { nullptr, nullptr };
    UINT strides[2] = { 0, 0 };
    UINT offsets[2] = { 0, 0 };
    m_renderer->GetContext()->IASetVertexBuffers(0, 2, nullVBs, strides, offsets);

    // РЕНДЕР ОБЪЕМНОГО ТУМАНА
    if (cfg.VolumetricLighting.Enabled && m_volumetricRT) {
        ID3D11RenderTargetView* volRTV = m_volumetricRT->GetRTV();
        context->OMSetRenderTargets(1, &volRTV, nullptr);

        D3D11_VIEWPORT vpVol = { 0.0f, 0.0f, (float)m_volumetricRT->GetWidth(), (float)m_volumetricRT->GetHeight(), 0.0f, 1.0f };
        context->RSSetViewports(1, &vpVol);

        float clearVol[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        context->ClearRenderTargetView(volRTV, clearVol);

        m_volumetricShader->Bind();
        ID3D11ShaderResourceView* volSRVs[2] = { m_renderer->GetDepthSRV(), m_shadowManager->GetShadowMapSRV() };
        context->PSSetShaderResources(0, 2, volSRVs);
        ID3D11SamplerState* volSamps[2] = { m_states->PointClamp(), m_shadowSampler.Get() };
        context->PSSetSamplers(0, 2, volSamps);

        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->Draw(3, 0);

        ID3D11ShaderResourceView* nullVolSRVs[2] = { nullptr, nullptr };
        context->PSSetShaderResources(0, 2, nullVolSRVs);
    }

    //ГЛАВНЫЙ ПАСС ОСВЕЩЕНИЯ (Deferred Light)
    ID3D11RenderTargetView* hdrRTV = m_sceneHDR->GetRTV();
    context->OMSetRenderTargets(1, &hdrRTV, nullptr);

    D3D11_VIEWPORT vpFull = { 0.0f, 0.0f, (float)cfg.System.WindowWidth, (float)cfg.System.WindowHeight, 0.0f, 1.0f };
    context->RSSetViewports(1, &vpFull);

    m_deferredLightShader->Bind();

    // БИНДИМ МАССИВЫ СВЕТА В СЛОТЫ t6 И t7
    ID3D11ShaderResourceView* srvs[8] = {
        m_gbufferAlbedo->GetSRV(),
        m_gbufferNormal->GetSRV(),
        m_gbufferMRAO->GetSRV(),
        m_renderer->GetDepthSRV(),
        m_shadowManager->GetShadowMapSRV(),
        cfg.VolumetricLighting.Enabled ? m_volumetricRT->GetSRV() : nullptr,
        LightManager::Get().GetPointLightsSRV(), // t6: Буфер Point Lights
        LightManager::Get().GetSpotLightsSRV()   // t7: Буфер Spot Lights
    };
    context->PSSetShaderResources(0, 8, srvs);

    ID3D11SamplerState* samplers[3] = { m_states->PointClamp(), m_shadowSampler.Get(), m_states->LinearClamp() };
    context->PSSetSamplers(0, 3, samplers);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->Draw(3, 0);

    ID3D11ShaderResourceView* nullSRVs[8] = { nullptr };
    context->PSSetShaderResources(0, 8, nullSRVs);
}

void RenderPipeline::PassWater(
    const std::vector<std::shared_ptr<WaterVLO>>& waterObjects,
    const DirectX::XMFLOAT4X4& view,
    const DirectX::XMFLOAT4X4& proj,
    const DirectX::XMFLOAT3& camPos,
    float gameTime,
    const DirectX::BoundingFrustum& cullFrustum,
    float renderDistSq,
    bool enableCulling)
{
    if (waterObjects.empty()) return;

    const auto& wConfig = EngineConfig::Get().Water;
    bool needsDepth = wConfig.EnableDepthAbsorption || wConfig.EnableFoam;

    if (wConfig.EnableRefraction) {
        m_renderer->GetContext()->CopyResource(
            m_refractionHDR->GetTexture(),
            m_sceneHDR->GetTexture()
        );
    }

    if (needsDepth) {
        m_renderer->CopyDepthForWater();
    }

    ID3D11RenderTargetView* hdrRTV = m_sceneHDR->GetRTV();
    m_renderer->GetContext()->OMSetRenderTargets(1, &hdrRTV, m_renderer->GetDepthDSV());

    m_renderer->BindWaterPassState();

    bool useTess = wConfig.EnableTessellation;
    if (useTess) {
        m_waterShaderUltra->Bind();
    }
    else {
        m_waterShaderLow->Bind();
    }

    ID3D11SamplerState* waterSamplers[2] = {
        m_states->AnisotropicWrap(),
        m_renderer->GetSamplerClamp()
    };
    m_renderer->GetContext()->PSSetSamplers(0, 2, waterSamplers);

    m_transformBuffer->BindHS(0);
    m_transformBuffer->BindDS(0);

    ID3D11ShaderResourceView* depthSRV = needsDepth ? m_renderer->GetWaterDepthSRV() : nullptr;
    ID3D11ShaderResourceView* refSRV = wConfig.EnableRefraction ? m_refractionHDR->GetSRV() : nullptr;

    m_renderer->GetContext()->PSSetShaderResources(3, 1, &depthSRV);
    m_renderer->GetContext()->PSSetShaderResources(4, 1, &refSRV);

    for (const auto& water : waterObjects) {
        water->Render(view, proj, camPos, gameTime, cullFrustum, renderDistSq, enableCulling);
    }

    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    m_renderer->GetContext()->PSSetShaderResources(3, 2, nullSRVs);

    m_renderer->BindDefaultDepthState();
    m_renderer->RestoreDefaultBlendState();

    m_renderer->GetContext()->HSSetShader(nullptr, nullptr, 0);
    m_renderer->GetContext()->DSSetShader(nullptr, nullptr, 0);
    m_renderer->GetContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void RenderPipeline::PassHZB(float windowWidth, float windowHeight) {
    m_renderer->UnbindRenderTargets();
    m_hzbManager->Generate(m_renderer);

#ifdef GAMMA_EDITOR
    ID3D11RenderTargetView* rtvOut = m_finalGameRT->GetRTV();
#else
    ID3D11RenderTargetView* rtvOut = m_renderer->GetBackBufferRTV();
#endif

    m_renderer->GetContext()->OMSetRenderTargets(1, &rtvOut, m_renderer->GetDepthDSV());

    D3D11_VIEWPORT vp = { 0 };
    vp.Width = windowWidth;
    vp.Height = windowHeight;
    vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
    m_renderer->GetContext()->RSSetViewports(1, &vp);
}

void RenderPipeline::PassShadows(float gameTime, Camera* camera, const RenderSettings& settings) {
    const auto& cfg = EngineConfig::Get();
    const auto& activeProfile = cfg.GetActiveProfile();

    if (!activeProfile.Shadows.Enabled) return;

    m_renderer->BindZPrepassState();

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

    int cascadeCount = std::clamp(activeProfile.Shadows.CascadeCount, 1, 3);

    DirectX::XMFLOAT4 shadowPlanes[18] = {};
    for (int i = 0; i < cascadeCount; ++i) {
        for (int p = 0; p < 6; ++p) {
            shadowPlanes[i * 6 + p] = m_shadowManager->GetCascades()[i].FrustumPlanes[p];
        }
    }

    if (m_staticScene && m_staticScene->GetTotalInstanceCount() > 0) {
        for (int i = 0; i < cascadeCount; ++i) {
            if (m_staticScene->GetShadowArgsBuffer(i)) {
                m_renderer->GetContext()->CopyResource(m_staticScene->GetShadowArgsBuffer(i), m_staticScene->GetArgsResetBuffer());
            }
        }

        m_gpuCuller->UpdateFrameConstants(
            cullView, cullProj, prevViewMat, prevProjMat, cullFrustum, cullOrigin,
            activeProfile.Lod, settings.EnableLODs,
            false, false, false,
            (float)cfg.System.WindowHeight, m_hzbManager->GetHzbSize(), settings.RenderDistance,
            0.0f, activeProfile.Shadows.MaxShadowDistance,
            true, activeProfile.Shadows.EnableShadowSizeCulling,
            activeProfile.Shadows.ShadowSizeCullingDistance, activeProfile.Shadows.MinShadowSize,
            activeProfile.Shadows.EnableShadowFrustumCulling, shadowPlanes
        );

        ID3D11UnorderedAccessView* visUAVs[3] = { m_staticScene->GetShadowVisibleIndexUAV(0), m_staticScene->GetShadowVisibleIndexUAV(1), m_staticScene->GetShadowVisibleIndexUAV(2) };
        ID3D11UnorderedAccessView* argUAVs[3] = { m_staticScene->GetShadowIndirectArgsUAV(0), m_staticScene->GetShadowIndirectArgsUAV(1), m_staticScene->GetShadowIndirectArgsUAV(2) };


        m_gpuCuller->PerformCulling(
            m_staticScene->GetInstanceSRV(),  
            m_staticScene->GetMetaDataSRV(),   
            m_hzbManager->GetHzbSRV(),
            m_staticScene->GetBatchInfoSRV(),
            visUAVs, argUAVs, cascadeCount, m_staticScene->GetTotalInstanceCount()
        );
    }

    if (m_floraScene && m_floraScene->GetTotalInstanceCount() > 0) {
        for (int i = 0; i < cascadeCount; ++i) {
            if (m_floraScene->GetShadowArgsBuffer(i)) {
                m_renderer->GetContext()->CopyResource(m_floraScene->GetShadowArgsBuffer(i), m_floraScene->GetArgsResetBuffer());
            }
        }

        m_gpuCuller->UpdateFrameConstants(
            cullView, cullProj, prevViewMat, prevProjMat, cullFrustum, cullOrigin,
            activeProfile.FloraLod, settings.EnableLODs,
            false, false, false,
            (float)cfg.System.WindowHeight, m_hzbManager->GetHzbSize(), settings.RenderDistance,
            0.0f, activeProfile.Shadows.MaxShadowDistance,
            true, activeProfile.Shadows.EnableShadowSizeCulling,
            activeProfile.Shadows.ShadowSizeCullingDistance, activeProfile.Shadows.MinShadowSize,
            activeProfile.Shadows.EnableShadowFrustumCulling, shadowPlanes
        );

        ID3D11UnorderedAccessView* visUAVs[3] = { m_floraScene->GetShadowVisibleIndexUAV(0), m_floraScene->GetShadowVisibleIndexUAV(1), m_floraScene->GetShadowVisibleIndexUAV(2) };
        ID3D11UnorderedAccessView* argUAVs[3] = { m_floraScene->GetShadowIndirectArgsUAV(0), m_floraScene->GetShadowIndirectArgsUAV(1), m_floraScene->GetShadowIndirectArgsUAV(2) };

        m_gpuCuller->PerformCulling(
            m_floraScene->GetInstanceSRV(),
            m_floraScene->GetMetaDataSRV(),
            m_hzbManager->GetHzbSRV(),
            m_floraScene->GetBatchInfoSRV(),
            visUAVs, argUAVs, 1, m_floraScene->GetTotalInstanceCount()
        );
    }

    for (int i = 0; i < cascadeCount; ++i) {
        ID3D11DepthStencilView* dsv = m_shadowManager->GetCascadeDSV(i);
        m_renderer->GetContext()->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

        ID3D11RenderTargetView* nullRTV = nullptr;
        m_renderer->GetContext()->OMSetRenderTargets(1, &nullRTV, dsv);

        D3D11_VIEWPORT vp = {};
        vp.Width = (float)activeProfile.Shadows.Resolution; vp.Height = (float)activeProfile.Shadows.Resolution;
        vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
        m_renderer->GetContext()->RSSetViewports(1, &vp);

        const auto& cascade = m_shadowManager->GetCascades()[i];

        CB_VS_Transform camData;
        camData.World = DirectX::XMMatrixIdentity();
        camData.View = DirectX::XMMatrixIdentity();
        camData.Projection = cascade.ViewProj;
        camData.TimeAndParams = Vector4(gameTime, 0, 0, 0);
        m_transformBuffer->UpdateDynamic(camData);
        m_transformBuffer->BindVS(0);

        if (m_terrainGpuScene && m_shadowTerrainShader) {
            m_shadowTerrainShader->Bind();

            m_renderer->GetContext()->PSSetShader(nullptr, nullptr, 0);

            ID3D11SamplerState* samplers[1] = { m_states->LinearClamp() };
            m_renderer->GetContext()->VSSetSamplers(1, 1, samplers);
            m_renderer->GetContext()->PSSetSamplers(1, 1, samplers);

            m_terrainGpuScene->BindShadowResources(i, m_terrainArrayManager.get());
            m_terrainGpuScene->BindGeometry(m_instanceIdBuffer.Get());
            for (int lod = 0; lod < 3; ++lod) {
                m_renderer->GetContext()->DrawIndexedInstancedIndirect(m_terrainGpuScene->GetIndirectArgsBuffer(), lod * 20);
            }
        }

        // СТАТИКА
        if (m_staticScene && m_staticScene->GetShadowArgsBuffer(i) != nullptr) {
            m_shadowModelShader->Bind();

            m_renderer->GetContext()->PSSetShader(nullptr, nullptr, 0);

            m_staticScene->RenderShadows(i, m_instanceIdBuffer.Get());
        }

        // ФЛОРА
        if (m_floraScene && m_floraScene->GetShadowArgsBuffer(i) != nullptr) {
            m_shadowFloraShader->Bind();
            m_weatherBuffer->BindVS(2);

            // РЕНДЕР СТВОЛОВ 
            m_renderer->BindSolidState();
            m_renderer->GetContext()->PSSetShader(nullptr, nullptr, 0);
            m_floraScene->RenderShadowsOpaque(i, m_instanceIdBuffer.Get());

            // Проверка альфа-куллинга
            bool renderAlpha = true;
            if (activeProfile.Shadows.EnableShadowAlphaCulling) {
                float cascadeStartDist = (i == 0) ? 0.0f : activeProfile.Shadows.Splits[i - 1];
                if (cascadeStartDist >= activeProfile.Shadows.ShadowAlphaCullingDistance) {
                    renderAlpha = false;
                }
            }

            if (renderAlpha) {
                m_renderer->BindNoCullState();

                m_shadowFloraShader->Bind();

                m_floraScene->RenderShadowsAlpha(i, m_instanceIdBuffer.Get());
            }
        }
    }

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)cfg.System.WindowWidth; vp.Height = (float)cfg.System.WindowHeight;
    vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
    m_renderer->GetContext()->RSSetViewports(1, &vp);
}


void RenderPipeline::RenderHzbDebug() {
    ID3D11ShaderResourceView* hzbSRV = m_hzbManager->GetHzbSRV();
    if (!hzbSRV) return;

    m_spriteBatch->Begin(DirectX::SpriteSortMode_Deferred, m_states->Opaque());
    RECT destRect = { 10, 10, 512, 288 };
    m_spriteBatch->Draw(hzbSRV, destRect, DirectX::Colors::White);
    m_spriteBatch->End();

    m_renderer->BindDefaultDepthState();
    m_renderer->RestoreDefaultBlendState();
}