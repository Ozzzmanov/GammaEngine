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

#ifdef GAMMA_EDITOR
#include <chrono>
#include "Editor/EditorCamera.h"
#endif

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
#include "Graphics/FloraGpuScene.h"
#include "Resources/TextureBaker.h"
#include "Resources/GeometryBaker.h"
#include "Graphics/LightManager.h"

// FIXME: Глобальный указатель (Anti-pattern). 
// Лучше передавать контекст загрузчика в функцию парсинга или использовать шину событий.
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
    delete m_gameState; // Если нельзя unique_ptr, удаляем руками
    g_pEngineInstance = nullptr;
}

bool GammaEngine::Initialize() {
    Logger::Info(LogCategory::General, "Starting Gamma Engine Initialization");

    const auto& cfg = EngineConfig::Get();
    const auto& activeProfile = cfg.GetActiveProfile();

    // System Initialization
    if (!m_window->Initialize(cfg.System.WindowWidth, cfg.System.WindowHeight, cfg.System.WindowTitle)) return false;
    if (!m_renderer->Initialize(m_window->GetHWND(), cfg.System.WindowWidth, cfg.System.WindowHeight)) return false;

    // Core Managers
    TaskScheduler::Get().Initialize();
    ResourceManager::Get().Initialize(m_renderer->GetDevice(), m_renderer->GetContext());
    TextureBaker::Initialize();
    ResourceManager::Get().SetAsyncMode(true);
    LightManager::Get().Initialize(m_renderer->GetDevice(), m_renderer->GetContext());
    ShaderManager::Get().Initialize(m_renderer->GetDevice());
    ModelManager::Get().Initialize(m_renderer->GetDevice(), m_renderer->GetContext());

    // Input System
    m_input = std::make_unique<InputSystem>();
    m_input->Initialize();

    // FIXME: Вынести биндинг кнопок в отдельный метод m_input->ApplyBindings(cfg)
    for (const auto& bind : cfg.KeyBindings) {
        if (bind.keyCode != 0) m_input->BindAction(bind.actionName, bind.keyCode);
    }
    for (const auto& bind : cfg.EditorKeyBindings) {
        if (bind.keyCode != 0) m_input->BindAction(bind.actionName, bind.keyCode);
    }

    // Render Settings & Camera
    m_renderSettings.RenderDistance = activeProfile.Graphics.RenderDistance;
    m_renderSettings.EnableCulling = activeProfile.Optimization.EnableFrustumCulling;
    m_renderSettings.EnableLODs = activeProfile.Lod.Enabled;
    m_renderSettings.EnableZPrepass = activeProfile.Optimization.EnableZPrepass;

#ifdef GAMMA_EDITOR
    m_camera = std::make_unique<EditorCamera>();
#else
    m_camera = std::make_unique<Camera>();
#endif

    float fovRad = activeProfile.Graphics.FOV * (3.14159f / 180.0f);
    m_camera->Initialize(fovRad, (float)cfg.System.WindowWidth / cfg.System.WindowHeight, activeProfile.Graphics.NearZ, activeProfile.Graphics.FarZ);

    // Game State & Logic Modul
    m_gameState = new GammaState();
    ZeroMemory(m_gameState, sizeof(GammaState));

    m_gameState->config.volumetricEnabled = cfg.VolumetricLighting.Enabled;
    m_gameState->config.volumetricDensity = cfg.VolumetricLighting.Density;

    m_gameModule.Load();
    m_gameModule.SafeInit(m_gameState);

    // Pipeline & World Loading
    m_renderPipeline = std::make_unique<RenderPipeline>(m_renderer.get());
    if (!m_renderPipeline->Initialize()) {
        Logger::Error(LogCategory::General, "Failed to initialize RenderPipeline!");
        return false;
    }

    m_worldLoader = std::make_unique<WorldLoader>(m_renderer->GetDevice(), m_renderer->GetContext());

    Logger::Info(LogCategory::General, "--- LOADING WORLD ---");

	// FIXME: Убрать хардкод пути. Сделать загрузку пути стартовой сцены из конфига или через cmd в далеком будущем через UI выбора уровня.
    std::string levelPath = "Assets/GammaLevels/outlands";

    GeometryBaker::Initialize();
    ModelManager::Get().PreloadManifest(levelPath);
    bool isGeometryCached = GeometryBaker::LoadCache(levelPath);

    m_worldLoader->LoadLocation(levelPath, m_chunks, m_waterObjects,
        m_spaceSettings, m_levelTextureManager,
        m_renderPipeline->GetStaticScene(),
        m_renderPipeline->GetFloraScene(),
        m_renderPipeline->GetTerrainArrayManager(),
        m_renderPipeline->GetTerrainScene());

    if (!isGeometryCached) {
        GeometryBaker::SaveCache(levelPath);
    }

    Logger::Info(LogCategory::General, "Building Global Geometry Buffers...");
    ModelManager::Get().BuildGlobalBuffers();

    Logger::Info(LogCategory::General, "Building Texture Buckets...");
    ModelManager::Get().BuildTextureBuckets(levelPath);

    Logger::Info(LogCategory::General, "Building GPU Scenes...");
    m_renderPipeline->GetStaticScene()->BuildGpuBuffers();
    m_renderPipeline->GetFloraScene()->BuildGpuBuffers();

#ifdef GAMMA_EDITOR
    m_editor = std::make_unique<GammaEditor>();
    if (!m_editor->Initialize(m_window.get(), m_renderer.get())) {
        Logger::Error(LogCategory::System, "Failed to initialize Gamma Editor!");
        return false;
    }
#endif

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

#ifdef GAMMA_EDITOR
        m_profilerStats.FPS = m_fps;
        auto t0 = std::chrono::high_resolution_clock::now();
#endif

        Update();

#ifdef GAMMA_EDITOR
        auto t1 = std::chrono::high_resolution_clock::now();
        m_profilerStats.UpdateTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
#endif

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
        m_window->SetTitle(EngineConfig::Get().System.WindowTitle + " | FPS: " + std::to_string(m_fps) + " | " + mode);
    }

    // Input Bindings Update
    if (EngineConfig::Get().bInputBindingsChanged) {
        m_input->ClearBindings();
        // FIXME: Вынести в InputSystem
        for (const auto& bind : EngineConfig::Get().KeyBindings) {
            if (bind.keyCode != 0) m_input->BindAction(bind.actionName, bind.keyCode);
        }
        for (const auto& bind : EngineConfig::Get().EditorKeyBindings) {
            if (bind.keyCode != 0) m_input->BindAction(bind.actionName, bind.keyCode);
        }
        EngineConfig::Get().bInputBindingsChanged = false;
    }

    TaskScheduler::Get().ProcessMainThreadCallbacks(0.003f);

#ifdef GAMMA_EDITOR
    if (m_editor) {
        bool shouldBlock = !m_editor->IsMainViewportHovered();
        if (m_input->IsKeyDown(VK_RBUTTON) || m_input->IsKeyDown(VK_MBUTTON)) {
            shouldBlock = false;
        }
        m_input->SetBlockInput(shouldBlock);
    }
#endif

    m_input->Update(m_window->GetHWND());

    // Synchronize Input with Game State
    // FIXME: Весь этот блок трансляции инпутов лучше инкапсулировать внутри InputSystem
    auto& bindings = EngineConfig::Get().KeyBindings;
    int count = (std::min)((int)bindings.size(), 64);
    m_gameState->input.actionCount = count;

    for (int i = 0; i < count; ++i) {
        const auto& bind = bindings[i];
        m_gameState->input.actions[i].nameHash = bind.hash;

        if (bind.keyCode != 0) {
            m_gameState->input.actions[i].isDown = m_input->IsKeyDown(bind.keyCode);
            m_gameState->input.actions[i].isTriggered = m_input->IsKeyPressed(bind.keyCode);
        }
        else {
            m_gameState->input.actions[i].isDown = false;
            m_gameState->input.actions[i].isTriggered = false;
        }
    }

    m_gameState->input.mouseDeltaX = m_input->GetMouseDelta().x;
    m_gameState->input.mouseDeltaY = m_input->GetMouseDelta().y;
    m_gameState->input.mouseScroll = m_input->GetMouseScrollDelta();

    // Synchronize Graphics Settings ---
    m_gameState->config.volumetricEnabled = EngineConfig::Get().VolumetricLighting.Enabled;
    m_gameState->config.volumetricDensity = EngineConfig::Get().VolumetricLighting.Density;
    m_gameState->config.windSpeed = EngineConfig::Get().Wind.Speed;

    auto& activeProfile = EngineConfig::Get().GetActiveProfile();
    m_renderSettings.RenderDistance = activeProfile.Graphics.RenderDistance;
    m_renderSettings.EnableCulling = activeProfile.Optimization.EnableFrustumCulling;
    m_renderSettings.EnableLODs = activeProfile.Lod.Enabled;
    m_renderSettings.EnableZPrepass = activeProfile.Optimization.EnableZPrepass;

    // Logic Execution
    m_gameModule.ReloadIfNeeded(m_gameState, m_timer.GetDeltaTime());
    m_gameModule.SafeUpdate(m_gameState, m_timer.GetDeltaTime());

    // Debug Hotkeys
    // FIXME: Сделать отдельную систему DebugCommands, чтобы не засорять основной цикл движка
    if (m_input->IsActionTriggered("ToggleCulling")) {
        activeProfile.Optimization.EnableFrustumCulling = !activeProfile.Optimization.EnableFrustumCulling;
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
        activeProfile.Lod.Enabled = !activeProfile.Lod.Enabled;
    }

    if (m_input->IsActionActive("RenderDistDown")) activeProfile.Graphics.RenderDistance -= 500.0f * m_timer.GetDeltaTime();
    if (m_input->IsActionActive("RenderDistUp"))   activeProfile.Graphics.RenderDistance += 500.0f * m_timer.GetDeltaTime();

    activeProfile.Graphics.RenderDistance = std::clamp(activeProfile.Graphics.RenderDistance, 100.0f, 30000.0f);

    m_camera->Update(m_timer.GetDeltaTime(), *m_input);
}

void GammaEngine::Render() {
#ifdef GAMMA_EDITOR
    auto t0 = std::chrono::high_resolution_clock::now();
#endif

    // Подготавливаем данные света перед кадром
    LightManager::Get().UpdateGpuBuffers();

    RenderStats stats = m_renderPipeline->RenderFrame(
        m_camera.get(), m_gameState->env, m_waterObjects,
        m_levelTextureManager.get(), m_timer.GetTotalTime(), m_renderSettings
    );

#ifdef GAMMA_EDITOR
    auto t1 = std::chrono::high_resolution_clock::now();
    m_profilerStats.RenderPipelineTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

    // Отрисовка UI Редактора поверх готового кадра
    m_renderer->BindRenderTarget();
    m_renderer->Clear(0.1f, 0.15f, 0.25f, 1.0f);

    if (m_editor) {
        m_editor->Render(m_gameState, m_renderPipeline->GetFinalGameSRV(), m_profilerStats);
    }

    auto t2 = std::chrono::high_resolution_clock::now();
    m_profilerStats.EditorTimeMs = std::chrono::duration<float, std::milli>(t2 - t1).count();
#endif

    m_renderer->Present(EngineConfig::Get().System.VSync);

#ifdef GAMMA_EDITOR
    auto t3 = std::chrono::high_resolution_clock::now();
    m_profilerStats.PresentTimeMs = std::chrono::duration<float, std::milli>(t3 - t2).count();
#endif
}