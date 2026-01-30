//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GammaEngine.cpp
// Реализация основного класса движка.
// ================================================================================

#include "GammaEngine.h"

// Subsystems
#include "Core/Logger.h"
#include "Core/ResourceManager.h"
#include "Graphics/DX11Renderer.h"
#include "Graphics/Shader.h"
#include "Graphics/ConstantBuffer.h"
#include "Graphics/LevelTextureManager.h" 
#include "Resources/SpaceSettings.h"
#include "World/Chunk.h"
#include "World/WaterVLO.h"

static GammaEngine* g_pEngineInstance = nullptr;

void RegisterDetectedVLO(const std::string& uid, const std::string& type,
    const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& scale)
{
    if (g_pEngineInstance) {
        g_pEngineInstance->RegisterVLO(uid, type, position, scale);
    }
}

// ============================================================================
// CONSTRUCTOR & DESTRUCTOR
// ============================================================================

GammaEngine::GammaEngine() {
    m_window = std::make_unique<Window>();
    m_renderer = std::make_unique<DX11Renderer>();
    g_pEngineInstance = this;
}

GammaEngine::~GammaEngine() {
    g_pEngineInstance = nullptr;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool GammaEngine::Initialize() {
    Logger::Info(LogCategory::General, "Starting Gamma Engine Initialization");

    if (!m_window->Initialize(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, "Gamma Engine")) return false;
    if (!m_renderer->Initialize(m_window->GetHWND(), DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT)) return false;

    ResourceManager::Get().Initialize(m_renderer->GetDevice(), m_renderer->GetContext());

    // Инициализация подсистем
    m_input = std::make_unique<InputSystem>();
    m_input->Initialize();

    // НАЗНАЧЕНИЕ КЛАВИШ (BINDINGS) FIX ME ЭТО ДОЛЖНО БЫТЬ ВНЕШНЕ.
    m_input->BindAction("ToggleCulling", VK_F1);
    m_input->BindAction("ToggleDebugCam", VK_F2);
    m_input->BindAction("ToggleWireframe", VK_TAB);
    m_input->BindAction("RenderDistUp", 'X');
    m_input->BindAction("RenderDistDown", 'Z');


    m_camera = std::make_unique<Camera>();

    m_camera->Initialize(DirectX::XM_PIDIV4, (float)DEFAULT_WINDOW_WIDTH / DEFAULT_WINDOW_HEIGHT, 0.1f, 20000.0f);

    m_debugOverlay = std::make_unique<DebugOverlay>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_debugOverlay->LoadContent(L"Assets/Fonts/consolas.spritefont");

    m_worldLoader = std::make_unique<WorldLoader>(m_renderer->GetDevice(), m_renderer->GetContext());

    // Шейдеры
    m_shader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_shader->Load(L"Assets/Shaders/Terrain.hlsl")) return false;

    m_shaderLegacy = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_shaderLegacy->Load(L"Assets/Shaders/TerrainLegacy.hlsl");

    m_transformBuffer = std::make_unique<ConstantBuffer<CB_VS_Transform>>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_transformBuffer->Initialize(true);

    // Загрузка мира
    m_worldLoader->LoadLocation("Assets/tunguska",
        m_chunks, m_waterObjects, m_spaceSettings, m_levelTextureManager, m_useLegacyRender);

    Logger::Info(LogCategory::General, "Initialization Complete");
    return true;
}

void GammaEngine::RegisterVLO(const std::string& uid, const std::string& type,
    const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& scale)
{
    if (m_worldLoader) {
        m_worldLoader->LoadVLOByUID(uid, type, pos, scale, m_waterObjects);
    }
}

// ============================================================================
// RUN / UPDATE / RENDER
// ============================================================================

void GammaEngine::Run() {
    m_timer.Tick();
    while (m_window->ProcessMessages()) {
        m_timer.Tick();
        Update();
        Render();
    }
}

void GammaEngine::Update() {
    // Статистика
    m_frameCount++;
    m_timeElapsed += m_timer.GetDeltaTime();
    if (m_timeElapsed >= 1.0f) {
        m_fps = m_frameCount;
        m_frameCount = 0;
        m_timeElapsed = 0.0f;
        m_window->SetTitle("Gamma Engine | FPS: " + std::to_string(m_fps));
    }

    // Ввод
    m_input->Update(m_window->GetHWND());

    // Логика движка через ACTIONS

    if (m_input->IsActionTriggered("ToggleCulling")) {
        m_enableCulling = !m_enableCulling;
        Logger::Info(LogCategory::Render, m_enableCulling ? "Culling ON" : "Culling OFF");
    }

    if (m_input->IsActionTriggered("ToggleDebugCam")) {
        m_camera->ToggleDebugMode();
        Logger::Info(LogCategory::Render, m_camera->IsDebugMode() ? "Debug Camera: ON" : "Debug Camera: OFF");
    }

    if (m_input->IsActionTriggered("ToggleWireframe")) {
        m_isWireframe = !m_isWireframe;
        m_renderer->SetWireframe(m_isWireframe);
    }

    // Дальность прорисовки (Hold Actions - используем Active, а не Triggered)
    if (m_input->IsActionActive("RenderDistDown")) m_renderDistance -= 25.0f;
    if (m_input->IsActionActive("RenderDistUp"))   m_renderDistance += 25.0f;

    // Лимиты
    if (m_renderDistance < 100.0f) m_renderDistance = 100.0f;
    if (m_renderDistance > 30000.0f) m_renderDistance = 30000.0f;

    // Обновление камеры
    m_camera->Update(m_timer.GetDeltaTime(), *m_input);
}

void GammaEngine::Render() {
    m_renderer->Clear(0.1f, 0.15f, 0.25f, 1.0f);
    m_renderer->BindDefaultDepthState();

    // Setup Render State
    if (m_useLegacyRender) {
        if (m_shaderLegacy) m_shaderLegacy->Bind();
    }
    else {
        if (m_shader) m_shader->Bind();
        if (m_levelTextureManager) {
            ID3D11ShaderResourceView* srv = m_levelTextureManager->GetSRV();
            if (srv) m_renderer->GetContext()->PSSetShaderResources(0, 1, &srv);
        }
    }

    float renderDistSq = m_renderDistance * m_renderDistance;
    int chunksDrawn = 0;

    // --- Чанки ---
    // Используем геттеры камеры:
    // View/Proj - всегда актуальные
    // Frustum/CullOrigin - могут быть "замороженными" для дебага
    for (const auto& chunk : m_chunks) {
        if (chunk->Render(m_transformBuffer.get(),
            XMLoadFloat4x4(&m_camera->GetViewMatrix()),
            XMLoadFloat4x4(&m_camera->GetProjectionMatrix()),
            m_camera->GetFrustum(),
            m_camera->GetCullOrigin(),
            renderDistSq, m_enableCulling))
        {
            chunksDrawn++;
        }
    }

    // --- Вода ---
    float gameTime = m_timer.GetTotalTime();
    for (const auto& water : m_waterObjects) {
        // Для рендеринга воды нужна реальная позиция камеры (GetPosition), 
        // но для отсечения (Frustum) берем данные из камеры (которые могут быть заморожены)
        water->Render(XMLoadFloat4x4(&m_camera->GetViewMatrix()),
            XMLoadFloat4x4(&m_camera->GetProjectionMatrix()),
            m_camera->GetPosition(),
            gameTime, m_transformBuffer.get(),
            m_camera->GetFrustum(), renderDistSq, m_enableCulling);
    }

    // --- Pass 3: Debug GUI ---
    DebugOverlay::Stats stats;
    stats.camPos = m_camera->GetPosition();
    stats.renderDist = m_renderDistance;
    stats.isCulling = m_enableCulling;
    stats.isDebugCam = m_camera->IsDebugMode();
    stats.chunksDrawn = chunksDrawn;
    stats.totalChunks = (int)m_chunks.size();
    stats.waterCount = (int)m_waterObjects.size();

    m_debugOverlay->Render(stats);

    m_renderer->Present(false);
}