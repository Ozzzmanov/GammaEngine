//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GammaEngine.cpp
// ================================================================================

#include "GammaEngine.h"

#include "Core/Logger.h"
#include "Core/ResourceManager.h"
#include "Core/TaskScheduler.h"
#include "Graphics/DX11Renderer.h"
#include "Graphics/LevelTextureManager.h" 
#include "Graphics/ModelManager.h"
#include "Graphics/ShaderManager.h"
#include "Graphics/StaticGpuScene.h"    
#include "Graphics/TerrainGpuScene.h"     
#include "Resources/SpaceSettings.h"
#include "Resources/TerrainArrayManager.h"
#include "World/Chunk.h"
#include "World/WaterVLO.h"
#include "Config/EngineConfig.h"

static GammaEngine* g_pEngineInstance = nullptr;

void RegisterDetectedVLO(const std::string& uid, const std::string& type,
    const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& scale)
{
    if (g_pEngineInstance) {
        g_pEngineInstance->RegisterVLO(uid, type, position, scale);
    }
}

GammaEngine::GammaEngine() {
    m_window = std::make_unique<Window>();
    m_renderer = std::make_unique<DX11Renderer>();
    g_pEngineInstance = this;
}

GammaEngine::~GammaEngine() {
    TaskScheduler::Get().Shutdown();
    g_pEngineInstance = nullptr;
}

bool GammaEngine::Initialize() {
    Logger::Info(LogCategory::General, "Starting Gamma Engine Initialization");

    const auto& cfg = EngineConfig::Get();

    if (!m_window->Initialize(cfg.WindowWidth, cfg.WindowHeight, cfg.WindowTitle)) return false;
    if (!m_renderer->Initialize(m_window->GetHWND(), cfg.WindowWidth, cfg.WindowHeight)) return false;

    // Инициализация подсистем
    TaskScheduler::Get().Initialize();
    ResourceManager::Get().Initialize(m_renderer->GetDevice(), m_renderer->GetContext());
    ResourceManager::Get().SetAsyncMode(true);
    ShaderManager::Get().Initialize(m_renderer->GetDevice());
    ModelManager::Get().Initialize(m_renderer->GetDevice(), m_renderer->GetContext());

    m_input = std::make_unique<InputSystem>();
    m_input->Initialize();

    for (const auto& bind : cfg.KeyBindings) {
        if (bind.keyCode != 0) m_input->BindAction(bind.actionName, bind.keyCode);
    }

    // Применяем начальные настройки
    m_renderSettings.RenderDistance = cfg.RenderDistance;
    m_renderSettings.EnableCulling = cfg.EnableFrustumCulling;
    m_renderSettings.EnableLODs = cfg.Lod.Enabled;
    m_renderSettings.EnableZPrepass = cfg.EnableZPrepass;

    m_camera = std::make_unique<Camera>();
    float fovRad = cfg.FOV * (3.14159f / 180.0f);
    m_camera->Initialize(fovRad, (float)cfg.WindowWidth / cfg.WindowHeight, cfg.NearZ, cfg.FarZ);

    m_debugOverlay = std::make_unique<DebugOverlay>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_debugOverlay->LoadContent(L"Assets/Fonts/consolas.spritefont");

    // Инициализируем пайплайн
    m_renderPipeline = std::make_unique<RenderPipeline>(m_renderer.get());
    if (!m_renderPipeline->Initialize()) {
        Logger::Error(LogCategory::General, "Failed to initialize RenderPipeline!");
        return false;
    }

    m_worldLoader = std::make_unique<WorldLoader>(m_renderer->GetDevice(), m_renderer->GetContext());

    Logger::Info(LogCategory::General, "--- LOADING WORLD ---");
    std::string levelPath = "Assets/outlands";

    // Передаем сцены из пайплайна в WorldLoader
    m_worldLoader->LoadLocation(levelPath, m_chunks, m_waterObjects,
        m_spaceSettings, m_levelTextureManager,
        m_renderPipeline->GetStaticScene(),
        m_renderPipeline->GetTerrainArrayManager(),
        m_renderPipeline->GetTerrainScene());

    Logger::Info(LogCategory::General, "Building Static GPU Scene...");
    m_renderPipeline->GetStaticScene()->BuildGpuBuffers();

    return true;
}

void GammaEngine::RegisterVLO(const std::string& uid, const std::string& type,
    const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& scale)
{
    if (m_worldLoader) {
        m_worldLoader->LoadVLOByUID(uid, type, pos, scale, m_waterObjects);
    }
}

void GammaEngine::Run() {
    m_timer.Tick();
    while (m_window->ProcessMessages()) {
        m_timer.Tick();
        Update();
        Render();
    }
}

void GammaEngine::Update() {
    m_frameCount++;
    m_timeElapsed += m_timer.GetDeltaTime();
    if (m_timeElapsed >= 1.0f) {
        m_fps = m_frameCount;
        m_frameCount = 0;
        m_timeElapsed = 0.0f;
        std::string mode = ResourceManager::Get().IsAsyncMode() ? "Async" : "Sync";
        m_window->SetTitle(EngineConfig::Get().WindowTitle + " | FPS: " + std::to_string(m_fps) + " | " + mode);
    }

    TaskScheduler::Get().ProcessMainThreadCallbacks(0.003f);
    m_input->Update(m_window->GetHWND());

    // Управление настройками рендера
    if (m_input->IsActionTriggered("ToggleCulling")) {
        m_renderSettings.EnableCulling = !m_renderSettings.EnableCulling;
    }
    if (m_input->IsActionTriggered("ToggleDebugCam")) {
        m_camera->ToggleDebugMode();
        m_renderSettings.FreezeCulling = m_camera->IsDebugMode();
    }
    if (m_input->IsActionTriggered("ToggleWireframe")) {
        m_renderSettings.IsWireframe = !m_renderSettings.IsWireframe;
    }
    if (m_input->IsActionTriggered("ToggleHZB")) {
        m_renderSettings.ShowHzbDebug = !m_renderSettings.ShowHzbDebug;
    }
    if (m_input->IsActionTriggered("ToggleLODs")) {
        m_renderSettings.EnableLODs = !m_renderSettings.EnableLODs;
    }

    if (m_input->IsActionActive("RenderDistDown")) m_renderSettings.RenderDistance -= 500.0f * m_timer.GetDeltaTime();
    if (m_input->IsActionActive("RenderDistUp"))   m_renderSettings.RenderDistance += 500.0f * m_timer.GetDeltaTime();

    if (m_renderSettings.RenderDistance < 100.0f) m_renderSettings.RenderDistance = 100.0f;
    if (m_renderSettings.RenderDistance > 30000.0f) m_renderSettings.RenderDistance = 30000.0f;

    m_camera->Update(m_timer.GetDeltaTime(), *m_input);
}

void GammaEngine::Render() {
    RenderStats stats = m_renderPipeline->RenderFrame(
        m_camera.get(),
        m_waterObjects,
        m_levelTextureManager.get(),
        m_timer.GetTotalTime(),
        m_renderSettings
    );

    // Отрисовка UI
    DebugOverlay::Stats uiStats;
    uiStats.camPos = m_camera->GetPosition();
    uiStats.renderDist = m_renderSettings.RenderDistance;
    uiStats.isCulling = m_renderSettings.EnableCulling;
    uiStats.isDebugCam = m_renderSettings.FreezeCulling;
    uiStats.chunksDrawn = -1;
    uiStats.totalChunks = (int)m_chunks.size();
    uiStats.waterCount = (int)m_waterObjects.size();
    uiStats.fps = m_fps;

    m_debugOverlay->Render(uiStats);

    // Переворачиваем буферы
    m_renderer->Present(EngineConfig::Get().VSync);
}