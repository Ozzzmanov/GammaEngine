//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GammaEngine.h
// Главный класс движка
// ================================================================================

#pragma once

// --- Core Includes ---
#include "Core/Prerequisites.h" 
#include "Core/Window.h"
#include "Core/Timer.h"

// --- Modules ---
#include "Core/InputSystem.h"
#include "GUI/DebugOverlay.h"
#include "Graphics/Camera.h"
#include "World/WorldLoader.h"

// --- Graphics & Math ---
#include <DirectXMath.h>
#include <memory>
#include <vector>
#include <map>

// --- Forward Declarations ---
class DX11Renderer;
class Shader;
class Chunk;
template <typename T> class ConstantBuffer;
struct CB_VS_Transform;

// --- Constants ---
#define DEFAULT_WINDOW_WIDTH  1920
#define DEFAULT_WINDOW_HEIGHT 1080

class GammaEngine {
public:
    GammaEngine();
    ~GammaEngine();

    bool Initialize();
    void Run();

    // Доступ для глобальных колбэков
    WorldLoader* GetWorldLoader() const { return m_worldLoader.get(); }

    // Метод для добавления VLO
    void RegisterVLO(const std::string& uid, const std::string& type,
        const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& scale);

private:
    void Update();
    void Render();

private:
    // --- Core Systems ---
    std::unique_ptr<Window>       m_window;
    std::unique_ptr<DX11Renderer> m_renderer;
    Timer                         m_timer;

    // --- Subsystems ---
    std::unique_ptr<InputSystem>  m_input;
    std::unique_ptr<Camera>       m_camera;
    std::unique_ptr<DebugOverlay> m_debugOverlay;
    std::unique_ptr<WorldLoader>  m_worldLoader;

    // --- Graphics Pipeline ---
    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Shader> m_shaderLegacy;
    bool                    m_useLegacyRender = true;

    std::unique_ptr<ConstantBuffer<CB_VS_Transform>> m_transformBuffer;

    // --- World Data ---
    std::unique_ptr<SpaceSettings>       m_spaceSettings;
    std::unique_ptr<LevelTextureManager> m_levelTextureManager;
    std::vector<std::unique_ptr<Chunk>>  m_chunks;
    std::vector<std::shared_ptr<WaterVLO>> m_waterObjects;

    // --- Performance Stats ---
    int   m_fps = 0;
    int   m_frameCount = 0;
    float m_timeElapsed = 0.0f;
    float m_renderDistance = 8000.0f;
    bool  m_enableCulling = true;
    bool  m_isWireframe = false;
};