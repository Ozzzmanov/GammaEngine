//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GammaEngine.h
// Главный класс движка. Управляет подсистемами, ресурсами и игровым циклом.
// ================================================================================

#pragma once

// --- Core Includes ---
#include "Core/Prerequisites.h" 
#include "Core/Window.h"
#include "Core/Timer.h"

// --- Graphics & Math ---
#include <DirectXMath.h>
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <DirectXCollision.h>

// --- Forward Declarations ---
class DX11Renderer;
class Shader;
class SpaceSettings;
class Chunk;
class LevelTextureManager;
class WaterVLO;
template <typename T> class ConstantBuffer;
struct CB_VS_Transform;

// --- Constants ---
#define DEFAULT_WINDOW_WIDTH  1920
#define DEFAULT_WINDOW_HEIGHT 1080

class GammaEngine {
public:
    GammaEngine();
    ~GammaEngine();

    // Инициализация всех подсистем.
    bool Initialize();

    // Запуск основного цикла приложения.
    void Run();

    //Загрузка водного объекта (VLO) по UID.
    void LoadVLOByUID(const std::string& uid, const std::string& type,
        const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& scale);

private:
    // --- Internal Loop Methods ---
    void Update();
    void Render();

    // --- Resource Loading ---
    void LoadLocation(const std::string& folderPath);
    bool ParseGridCoordinates(const std::string& filename, int& outX, int& outZ);

    void UpdateTitleStats();

private:
    // --- Core Systems ---
    std::unique_ptr<Window>       m_window;
    std::unique_ptr<DX11Renderer> m_renderer;
    Timer                         m_timer;

    // --- Graphics Pipeline ---
    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Shader> m_shaderLegacy;
    bool                    m_useLegacyRender = true;

    std::unique_ptr<ConstantBuffer<CB_VS_Transform>> m_transformBuffer;

    // --- 2D / UI ---
    std::unique_ptr<DirectX::CommonStates> m_states;
    std::unique_ptr<DirectX::SpriteBatch>  m_spriteBatch;
    std::unique_ptr<DirectX::SpriteFont>   m_font;

    // --- World Data ---
    std::unique_ptr<SpaceSettings>       m_spaceSettings;
    std::unique_ptr<LevelTextureManager> m_levelTextureManager;
    std::vector<std::unique_ptr<Chunk>>  m_chunks;

    // --- Water System ---
    std::vector<std::shared_ptr<WaterVLO>> m_waterObjects;
    std::map<std::string, std::shared_ptr<WaterVLO>> m_globalWaterStorage;

    // --- Camera  ---
    DirectX::XMFLOAT3   m_camPos = { 0.0f, 50.0f, 0.0f };
    float               m_camPitch = 0.0f;
    float               m_camYaw = 0.0f;
    DirectX::XMFLOAT4X4 m_viewMatrix;
    DirectX::XMFLOAT4X4 m_projectionMatrix;
    DirectX::BoundingFrustum m_cameraFrustum; // Пирамида видимости

    // --- Optimization Flags ---
    bool m_enableCulling = true; // По умолчанию включено для оптимизации
    float m_renderDistance = 8000.0f; // Дальность прорисовки в метрах (по умолчанию 8000)

    // --- Input State ---
    POINT m_prevMousePos = { 0, 0 };
    bool  m_firstMouse = true;
    bool  m_isWireframe = false;

    // --- Performance Stats ---
    int   m_frameCount = 0;
    float m_timeElapsed = 0.0f;
    int   m_fps = 0;
    float m_mspf = 0.0f;
};