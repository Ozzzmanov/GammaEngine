//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Класс GammaEngine.
// Главный класс движка. Управляет циклом обновления, рендерингом,
// загрузкой мира и вводом пользователя.
// ================================================================================

#pragma once
#include "Core/Window.h"
#include "Core/Timer.h"
#include "Graphics/DX11Renderer.h"
#include "Graphics/Shader.h"
#include "Graphics/ConstantBuffer.h"
#include "Resources/SpaceSettings.h"
#include "World/Chunk.h"

// Размер окна
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

class GammaEngine {
public:
    GammaEngine();
    ~GammaEngine();

    // Инициализация всех подсистем
    bool Initialize();

    // Запуск главного цикла
    void Run();

private:
    void Update();
    void Render();

    // Загрузка локации из папки
    void LoadLocation(const std::string& folderPath);

    // Обновление статистики в заголовке окна
    void UpdateTitleStats();

    // Парсинг координат из имени файла (xxxxzzzz.chunk)
    bool ParseGridCoordinates(const std::string& filename, int& outX, int& outZ);

    // --- Подсистемы ---
    std::unique_ptr<Window> m_window;
    std::unique_ptr<DX11Renderer> m_renderer;
    Timer m_timer;

    // --- Графика ---
    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<ConstantBuffer<CB_VS_Transform>> m_transformBuffer;

    // --- DirectXTK (UI) ---
    std::unique_ptr<CommonStates> m_states;
    std::unique_ptr<SpriteBatch> m_spriteBatch;
    std::unique_ptr<SpriteFont> m_font;

    // --- Мир ---
    std::unique_ptr<SpaceSettings> m_spaceSettings;
    std::vector<std::unique_ptr<Chunk>> m_chunks;

    // --- Камера ---
    DirectX::XMFLOAT3 m_camPos = { 0.0f, 50.0f, 0.0f }; // Начало повыше
    float m_camPitch = 0.0f;
    float m_camYaw = 0.0f;
    DirectX::XMFLOAT4X4 m_viewMatrix;
    DirectX::XMFLOAT4X4 m_projectionMatrix;

    // --- Ввод ---
    POINT m_prevMousePos = { 0, 0 };
    bool m_firstMouse = true;
    bool m_isWireframe = false;

    // --- Статистика ---
    int m_frameCount = 0;
    float m_timeElapsed = 0.0f;
    int m_fps = 0;
    float m_mspf = 0.0f;
};