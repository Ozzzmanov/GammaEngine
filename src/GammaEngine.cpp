//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
#include "GammaEngine.h"
#include "Core/Logger.h"
#include <sstream>
#include <cmath>

GammaEngine::GammaEngine() {
    m_window = std::make_unique<Window>();
    m_renderer = std::make_unique<DX11Renderer>();
}

GammaEngine::~GammaEngine() {}

bool GammaEngine::Initialize() {
    Logger::Info(LogCategory::General, "Starting Gamma Engine Initialization");


    if (!m_window->Initialize(WINDOW_WIDTH, WINDOW_HEIGHT, "Gamma Engine")) return false;
    if (!m_renderer->Initialize(m_window->GetHWND(), WINDOW_WIDTH, WINDOW_HEIGHT)) return false;

    // Инициализация UI
    Logger::Info(LogCategory::General, "Initializing UI...");
    m_states = std::make_unique<CommonStates>(m_renderer->GetDevice());
    m_spriteBatch = std::make_unique<SpriteBatch>(m_renderer->GetContext());
    try {
        m_font = std::make_unique<SpriteFont>(m_renderer->GetDevice(), L"Assets/Fonts/consolas.spritefont");
    }
    catch (...) {
        Logger::Error(LogCategory::General, "Font not found! Check Assets/Fonts/consolas.spritefont");
        return false;
    }

    // Инициализация Шейдера
    Logger::Info(LogCategory::General, "Loading Terrain Shader...");
    m_shader = std::make_unique<Shader>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_shader->Load(L"Assets/Shaders/Terrain.hlsl")) return false;

    // Инициализация Constant Buffer
    m_transformBuffer = std::make_unique<ConstantBuffer<CB_VS_Transform>>(m_renderer->GetDevice(), m_renderer->GetContext());
    if (!m_transformBuffer->Initialize()) return false;

    // Настройка Проекции
    XMMATRIX P = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)WINDOW_WIDTH / WINDOW_HEIGHT, 0.1f, 20000.0f);
    XMStoreFloat4x4(&m_projectionMatrix, P);

    // Загрузка Мира
    LoadLocation("Assets/tunguska2");

    Logger::Info(LogCategory::General, "Initialization Complete");
    return true;
}

bool GammaEngine::ParseGridCoordinates(const std::string& filename, int& outX, int& outZ) {
    if (filename.length() < 9) return false;
    try {
        // Формат: xxxxzzzzo.chunk (hex)
        // Пример: fffeffffo.chunk -> x=-2, z=-1 (в short)
        std::string hexX = filename.substr(0, 4);
        std::string hexZ = filename.substr(4, 4);

        unsigned int uX, uZ;
        std::stringstream ssX, ssZ;
        ssX << std::hex << hexX; ssX >> uX;
        ssZ << std::hex << hexZ; ssZ >> uZ;

        // Кастуем в short, чтобы получить отрицательные значения
        outX = (short)uX;
        outZ = (short)uZ;
        return true;
    }
    catch (...) { return false; }
}

void GammaEngine::LoadLocation(const std::string& folderPath) {
    if (!fs::exists(folderPath)) {
        Logger::Error(LogCategory::General, "Location folder not found: ");

        return;
    }

    m_spaceSettings = std::make_unique<SpaceSettings>();
    std::string settingsPath = folderPath + "/space.settings";
    SpaceParams currentParams;

    if (fs::exists(settingsPath)) {
        if (m_spaceSettings->Load(settingsPath)) {
            currentParams = m_spaceSettings->GetParams();
            m_camPos = currentParams.startPosition;
            m_camPos.y += 20.0f; // Поднимаем камеру
        }
    }
    else {
        currentParams.blendMapSize = 128; // Дефолт
    }

    Logger::Info(LogCategory::General, "Loading Chunks from: " + folderPath);
    int count = 0;

    for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
        if (entry.is_directory()) continue;

        std::string filename = entry.path().filename().string();
        std::string ext = entry.path().extension().string();

        if (ext == ".chunk" && filename.find("o.chunk") != std::string::npos) {
            int gx, gz;
            if (ParseGridCoordinates(filename, gx, gz)) {
                auto chunk = std::make_unique<Chunk>(m_renderer->GetDevice(), m_renderer->GetContext());
                if (chunk->Load(entry.path().string(), gx, gz, currentParams)) {
                    m_chunks.push_back(std::move(chunk));
                    count++;
                }
            }
        }
    }
    Logger::Info(LogCategory::General, "Total Chunks Loaded: " + std::to_string(count));
}

void GammaEngine::Update() {
    m_frameCount++;
    m_timeElapsed += m_timer.GetDeltaTime();

    if (m_timeElapsed >= 1.0f) {
        UpdateTitleStats();
        m_frameCount = 0;
        m_timeElapsed = 0.0f;
    }

    // --- Камера (WASD + Мышь) ---
    float speed = 50.0f * m_timer.GetDeltaTime();
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) speed *= 10.0f; // Ускорение

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

    XMStoreFloat3(&m_camPos, camPos);

    // Вращение мышью (ПКМ)
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

        // Ограничение по вертикали
        if (m_camPitch > 1.5f) m_camPitch = 1.5f;
        if (m_camPitch < -1.5f) m_camPitch = -1.5f;

        m_prevMousePos = curMouse;
    }
    else {
        m_firstMouse = true;
    }

    // Создание матрицы вида
    XMMATRIX rotation = XMMatrixRotationRollPitchYaw(m_camPitch, m_camYaw, 0);
    XMVECTOR lookDir = XMVector3TransformCoord(XMVectorSet(0, 0, 1, 0), rotation);
    XMVECTOR target = camPos + lookDir;
    XMMATRIX V = XMMatrixLookAtLH(camPos, target, up);
    XMStoreFloat4x4(&m_viewMatrix, V);

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
}

void GammaEngine::Render() {
    m_renderer->Clear(0.1f, 0.15f, 0.25f, 1.0f); // Небесный цвет
    m_renderer->BindDefaultDepthState();

    // Активируем шейдер
    m_shader->Bind();

    // Загружаем матрицы View и Proj
    XMMATRIX view = XMLoadFloat4x4(&m_viewMatrix);
    XMMATRIX proj = XMLoadFloat4x4(&m_projectionMatrix);

    // Отрисовка чанков
    for (const auto& chunk : m_chunks) {
        // 1. Сохраняем позицию в явную переменную
        XMFLOAT3 pos = chunk->GetPosition();

        // 2. Берем адрес от этой переменной
        XMVECTOR chunkPos = XMLoadFloat3(&pos);

        XMVECTOR camPosVec = XMLoadFloat3(&m_camPos);
        float dist = XMVectorGetX(XMVector3Length(chunkPos - camPosVec));

        /*
        Дальность отрисовки чанков
        if (dist < 1000.0f) {
            chunk->Render(m_transformBuffer.get(), view, proj);
        }
        */
        // Дальность отрисовки чанка выключена
        chunk->Render(m_transformBuffer.get(), view, proj);
    }

    // --- DEBUG UI ---
    m_spriteBatch->Begin();

    // Расчет текущего чанка
    int curX = (int)floor(m_camPos.x / 100.0f);
    int curZ = (int)floor(m_camPos.z / 100.0f);

    std::wstring debugInfo = L"FPS: " + std::to_wstring(m_fps) +
        L"\nPos: " + std::to_wstring((int)m_camPos.x) + L", " + std::to_wstring((int)m_camPos.z) +
        L"\nGrid: [" + std::to_wstring(curX) + L", " + std::to_wstring(curZ) + L"]";

    // Поиск данных текущего чанка
    Chunk* active = nullptr;
    for (const auto& c : m_chunks) {
        if (c->GetGridX() == curX && c->GetGridZ() == curZ) {
            active = c.get();
            break;
        }
    }

    if (active) {
        std::string sName = active->GetName();
        debugInfo += L"\nChunk: " + std::wstring(sName.begin(), sName.end());
        debugInfo += L"\n\n--- Layers (8-Layer Sys) ---";

        auto texs = active->GetTerrainTextures();
        for (size_t i = 0; i < texs.size(); ++i) {
            std::string t = texs[i];
            size_t slash = t.find_last_of("/\\");
            if (slash != std::string::npos) t = t.substr(slash + 1);

            std::wstring wt(t.begin(), t.end());

            debugInfo += L"\n[" + std::to_wstring(i) + L"] " + wt;
        }
    }
    else {
        debugInfo += L"\n[NO CHUNK]";
    }

    m_font->DrawString(m_spriteBatch.get(), debugInfo.c_str(), XMFLOAT2(11, 11), Colors::Black);
    m_font->DrawString(m_spriteBatch.get(), debugInfo.c_str(), XMFLOAT2(10, 10), Colors::Yellow);
    m_spriteBatch->End();

    m_renderer->Present(false); // VSync off
}

void GammaEngine::Run() {
    m_timer.Tick();
    while (m_window->ProcessMessages()) {
        m_timer.Tick();
        Update();
        Render();
    }
}