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

// World & Resources
#include "Resources/SpaceSettings.h"
#include "Resources/DebugTools.h"
#include "Resources/BwPackedSection.h" 
#include "World/Chunk.h"
#include "World/WaterVLO.h"

// DirectXTK Headers 
#include <CommonStates.h>
#include <SpriteBatch.h>
#include <SpriteFont.h>

// Std
#include <sstream>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;
using namespace DirectX; 

// ============================================================================
// GLOBAL HELPERS
// ============================================================================

static GammaEngine* g_pEngineInstance = nullptr;

void RegisterDetectedVLO(const std::string& uid, const std::string& type,
    const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& scale)
{
    if (g_pEngineInstance) {
        g_pEngineInstance->LoadVLOByUID(uid, type, position, scale);
    }
}

static std::string CleanUID(std::string input) {
    size_t first = input.find_first_not_of(" \t\n\r");
    if (std::string::npos == first) return "";
    size_t last = input.find_last_not_of(" \t\n\r");

    std::string clean = input.substr(first, (last - first + 1));
    std::transform(clean.begin(), clean.end(), clean.begin(), ::tolower);

    if (!clean.empty() && clean[0] == '_') clean = clean.substr(1);

    if (clean.length() > 35) {
        int dots = 0;
        size_t lastChar = 0;
        for (size_t i = 0; i < clean.length(); ++i) {
            if (clean[i] == '.') dots++;
            if ((clean[i] >= '0' && clean[i] <= '9') ||
                (clean[i] >= 'a' && clean[i] <= 'f') || clean[i] == '.')
            {
                lastChar = i;
            }
            else if (dots >= 3) break;
        }
        clean = clean.substr(0, lastChar + 1);
    }
    return clean;
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

    // Создаем объекты DirectXTK
    m_states = std::make_unique<CommonStates>(m_renderer->GetDevice());
    m_spriteBatch = std::make_unique<SpriteBatch>(m_renderer->GetContext());

    try {
        m_font = std::make_unique<SpriteFont>(m_renderer->GetDevice(), L"Assets/Fonts/consolas.spritefont");
    }
    catch (...) {
        Logger::Error(LogCategory::General, "Font missing! Check Assets/Fonts/consolas.spritefont");
    }

    m_shader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_shader->Load(L"Assets/Shaders/Terrain.hlsl")) return false;

    m_shaderLegacy = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_shaderLegacy->Load(L"Assets/Shaders/TerrainLegacy.hlsl");

    m_transformBuffer = std::make_unique<ConstantBuffer<CB_VS_Transform>>(m_renderer->GetDevice(), m_renderer->GetContext());
    m_transformBuffer->Initialize(true); // ТЕСТ D3D11_USAGE_DYNAMIC ТЕСТ

    XMMATRIX P = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)DEFAULT_WINDOW_WIDTH / DEFAULT_WINDOW_HEIGHT, 0.1f, 20000.0f);
    XMStoreFloat4x4(&m_projectionMatrix, P);

    LoadLocation("Assets/karavan");

    Logger::Info(LogCategory::General, "Initialization Complete");
    return true;
}

// ============================================================================
// RESOURCE LOADING
// ============================================================================

void GammaEngine::LoadVLOByUID(const std::string& rawUid, const std::string& type,
    const DirectX::XMFLOAT3& positionFromChunk, const DirectX::XMFLOAT3& scaleFromChunk)
{
    std::string uid = CleanUID(rawUid);
    if (uid.empty()) return;

    if (m_globalWaterStorage.count(uid) > 0) return;

    auto water = std::make_shared<WaterVLO>(m_renderer->GetDevice(), m_renderer->GetContext());

    fs::path foundPath;
    bool fileFound = false;
    std::string t1 = uid + ".vlo";
    std::string t2 = "_" + uid + ".vlo";

    for (const auto& entry : fs::recursive_directory_iterator("Assets")) {
        if (!entry.is_regular_file()) continue;
        std::string fn = entry.path().filename().string();
        std::transform(fn.begin(), fn.end(), fn.begin(), ::tolower);

        if (fn == t1 || fn == t2) {
            foundPath = entry.path();
            fileFound = true;
            break;
        }
    }

    if (fileFound) {
        if (!water->Load(foundPath.string())) {
            fileFound = false;
        }
    }

    if (!fileFound) {
        water->LoadDefaults(uid);
        water->SetWorldPosition(Vector3(positionFromChunk.x, positionFromChunk.y, positionFromChunk.z));
        water->OverrideSize(Vector2(scaleFromChunk.x, scaleFromChunk.z));
    }

    m_globalWaterStorage[uid] = water;
    m_waterObjects.push_back(water);

    Logger::Info(LogCategory::General, "[VLO] Registering Water: " + uid + (fileFound ? " from .vlo" : " from .chunk"));
}

void GammaEngine::LoadLocation(const std::string& folderPath) {
    if (!fs::exists(folderPath)) return;

    if (!m_useLegacyRender) {
        m_levelTextureManager = std::make_unique<LevelTextureManager>(m_renderer->GetDevice(), m_renderer->GetContext());
    }

    m_spaceSettings = std::make_unique<SpaceSettings>();
    std::string settingsPath = folderPath + "/space.settings";
    SpaceParams currentParams;

    if (fs::exists(settingsPath)) {
        m_spaceSettings->Load(settingsPath);
        currentParams = m_spaceSettings->GetParams();
    }

    m_waterObjects.clear();
    m_globalWaterStorage.clear();
    m_chunks.clear();

    Logger::Info(LogCategory::General, "Scanning chunks for VLO references...");

    for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
        if (entry.is_directory()) continue;
        std::string filename = entry.path().filename().string();

        int gx = 0, gz = 0;
        bool isChunk = (filename.find("o.chunk") != std::string::npos);

        if (isChunk && ParseGridCoordinates(filename, gx, gz)) {
            auto chunk = std::make_unique<Chunk>(m_renderer->GetDevice(), m_renderer->GetContext());
            LevelTextureManager* mgr = m_useLegacyRender ? nullptr : m_levelTextureManager.get();

            if (chunk->Load(entry.path().string(), gx, gz, currentParams, mgr, nullptr, false)) {
                m_chunks.push_back(std::move(chunk));
            }
        }
    }

    if (!m_useLegacyRender && m_levelTextureManager) {
        m_levelTextureManager->BuildArray();
    }
}

bool GammaEngine::ParseGridCoordinates(const std::string& filename, int& outX, int& outZ) {
    if (filename.length() < 8) return false;
    try {
        std::string hexX = filename.substr(0, 4);
        std::string hexZ = filename.substr(4, 4);
        unsigned int uX, uZ;

        std::stringstream ssX; ssX << std::hex << hexX; ssX >> uX;
        std::stringstream ssZ; ssZ << std::hex << hexZ; ssZ >> uZ;

        outX = (short)uX;
        outZ = (short)uZ;
        return true;
    }
    catch (...) { return false; }
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
    // Обновление статистики (FPS)
    m_frameCount++;
    m_timeElapsed += m_timer.GetDeltaTime();

    if (m_timeElapsed >= 1.0f) {
        UpdateTitleStats();
        m_frameCount = 0;
        m_timeElapsed = 0.0f;
    }

    // Обработка переключения Frustum Culling (F1)
    static bool f1Pressed = false;
    if (GetAsyncKeyState(VK_F1) & 0x8000) {
        if (!f1Pressed) {
            m_enableCulling = !m_enableCulling;
            f1Pressed = true;
            Logger::Info(LogCategory::Render, m_enableCulling ? "Optimization: Frustum Culling ON" : "Optimization: Frustum Culling OFF");
        }
    }
    else {
        f1Pressed = false;
    }

    // Управление камерой (WASD + Shift)
    float dt = m_timer.GetDeltaTime();
    float speed = 100.0f * dt;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) speed *= 5.0f;

    XMVECTOR camPos = XMLoadFloat3(&m_camPos);
    XMVECTOR forward = XMVectorSet(sinf(m_camYaw), 0, cosf(m_camYaw), 0);
    XMVECTOR right = XMVectorSet(cosf(m_camYaw), 0, -sinf(m_camYaw), 0);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    if (GetAsyncKeyState('W') & 0x8000) camPos += forward * speed;
    if (GetAsyncKeyState('S') & 0x8000) camPos -= forward * speed;
    if (GetAsyncKeyState('A') & 0x8000) camPos -= right * speed;
    if (GetAsyncKeyState('D') & 0x8000) camPos += right * speed;
    if (GetAsyncKeyState('Q') & 0x8000) camPos -= up * speed;
    if (GetAsyncKeyState('E') & 0x8000) camPos += up * speed;

    // Управление дальностью прорисовки (Z / X)
    if (GetAsyncKeyState('Z') & 0x8000) m_renderDistance -= 25.0f;
    if (GetAsyncKeyState('X') & 0x8000) m_renderDistance += 25.0f;

    // Ограничим разумными пределами
    if (m_renderDistance < 10.0f) m_renderDistance = 100.0f;
    if (m_renderDistance > 30000.0f) m_renderDistance = 3000.0f;

    XMStoreFloat3(&m_camPos, camPos);

    // Управление мышью (ПКМ для обзора)
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
        POINT curMouse;
        GetCursorPos(&curMouse);
        ScreenToClient(m_window->GetHWND(), &curMouse);

        if (m_firstMouse) {
            m_prevMousePos = curMouse;
            m_firstMouse = false;
        }

        float deltaX = (float)(curMouse.x - m_prevMousePos.x) * 0.003f;
        float deltaY = (float)(curMouse.y - m_prevMousePos.y) * 0.003f;

        m_camYaw += deltaX;
        m_camPitch += deltaY;

        // Ограничение камеры по вертикали
        if (m_camPitch > 1.5f) m_camPitch = 1.5f;
        if (m_camPitch < -1.5f) m_camPitch = -1.5f;

        m_prevMousePos = curMouse;
    }
    else {
        m_firstMouse = true;
    }

    // Обновление матриц
    XMMATRIX rotation = XMMatrixRotationRollPitchYaw(m_camPitch, m_camYaw, 0);
    XMVECTOR lookDir = XMVector3TransformCoord(XMVectorSet(0, 0, 1, 0), rotation);
    XMVECTOR target = camPos + lookDir;
    XMMATRIX V = XMMatrixLookAtLH(camPos, target, up);
    XMStoreFloat4x4(&m_viewMatrix, V);

    // Обновление Frustum (Пирамиды видимости) для отсечения чанков
    BoundingFrustum viewFrustum;
    BoundingFrustum::CreateFromMatrix(viewFrustum, XMLoadFloat4x4(&m_projectionMatrix));

    // Трансформируем Frustum в мировое пространство (World Space)
    // Для этого нужна инвертированная матрица вида !!!!
    XMMATRIX invView = XMMatrixInverse(nullptr, V);
    viewFrustum.Transform(m_cameraFrustum, invView);

    // Переключение Wireframe (Tab)
    static bool tabPressed = false;
    if (GetAsyncKeyState(VK_TAB) & 0x8000) {
        if (!tabPressed) {
            m_isWireframe = !m_isWireframe;
            m_renderer->SetWireframe(m_isWireframe);
            tabPressed = true;
        }
    }
    else {
        tabPressed = false;
    }
}

void GammaEngine::UpdateTitleStats() {
    m_fps = m_frameCount;
    m_mspf = 1000.0f / (m_fps > 0 ? m_fps : 1);
    std::string title = "Gamma Engine | FPS: " + std::to_string(m_fps);
    m_window->SetTitle(title);
}

void GammaEngine::Render() {
    // Очистка буферов (Цвет + Глубина)
    m_renderer->Clear(0.1f, 0.15f, 0.25f, 1.0f);
    m_renderer->BindDefaultDepthState();

    // Подготовка матриц
    XMMATRIX view = XMLoadFloat4x4(&m_viewMatrix);
    XMMATRIX proj = XMLoadFloat4x4(&m_projectionMatrix);

    // Настройка конвейера (Шейдеры и Глобальные текстуры)
    if (m_useLegacyRender) {
        if (m_shaderLegacy) m_shaderLegacy->Bind();
    }
    else {
        if (m_shader) m_shader->Bind();
        // Биндинг массива текстур террейна
        if (m_levelTextureManager) {
            ID3D11ShaderResourceView* srv = m_levelTextureManager->GetSRV();
            if (srv) m_renderer->GetContext()->PSSetShaderResources(0, 1, &srv);
        }
    }

    // Предварительный расчет квадрата дистанции (чтобы избежать sqrt в циклах)
    float renderDistSq = m_renderDistance * m_renderDistance;

    // Рендер Чанков (Terrain Pass)
    int chunksDrawn = 0;
    for (const auto& chunk : m_chunks) {
        // Передаем параметры оптимизации:
        // - m_cameraFrustum: для проверки попадания в конус камеры
        // - m_camPos + renderDistSq: для проверки дистанции
        // - m_enableCulling: флаг (F1) для включения/отключения оптимизаций
        if (chunk->Render(m_transformBuffer.get(), view, proj,
            m_cameraFrustum, m_camPos, renderDistSq, m_enableCulling))
        {
            chunksDrawn++;
        }
    }

    // 4. Рендер Воды (Water Pass)
    float gameTime = m_timer.GetTotalTime();
    for (const auto& water : m_waterObjects) {
        water->Render(view, proj, m_camPos, gameTime, m_transformBuffer.get(),
            m_cameraFrustum, renderDistSq, m_enableCulling);
    }

    // Интерфейс отладки (Debug UI)
    m_spriteBatch->Begin();

    // Вычисляем координаты чанка, над которым летим
    int curX = (int)std::floor(m_camPos.x / 100.0f);
    int curZ = (int)std::floor(m_camPos.z / 100.0f);

    std::wstring cullStatus = m_enableCulling ? L"ON" : L"OFF";

    std::wstring debugInfo = L"Pos: " + std::to_wstring((int)m_camPos.x) + L", " +
        std::to_wstring((int)m_camPos.y) + L", " + std::to_wstring((int)m_camPos.z) +
        L"\nChunk Grid: [" + std::to_wstring(curX) + L", " + std::to_wstring(curZ) + L"]" +
        L"\nRender Dist (Z/X): " + std::to_wstring((int)m_renderDistance) + L"m" +
        L"\nCulling (F1): " + cullStatus +
        L"\nChunks Visible: " + std::to_wstring(chunksDrawn) + L" / " + std::to_wstring(m_chunks.size()) +
        L"\nWater Objects: " + std::to_wstring(m_waterObjects.size());

    if (m_font) {
        m_font->DrawString(m_spriteBatch.get(), debugInfo.c_str(), XMFLOAT2(11, 11), Colors::Black);
        m_font->DrawString(m_spriteBatch.get(), debugInfo.c_str(), XMFLOAT2(10, 10), Colors::Yellow);
    }

    m_spriteBatch->End();

    m_renderer->Present(false);
}