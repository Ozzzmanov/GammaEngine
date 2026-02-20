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

#include "Core/Prerequisites.h" 
#include "Core/Window.h"
#include "Core/Timer.h"

// Модули
#include "Core/InputSystem.h"
#include "GUI/DebugOverlay.h"
#include "Graphics/Camera.h"
#include "World/WorldLoader.h"
#include "Graphics/RenderPipeline.h"

#include <memory>
#include <vector>
#include <string>

class DX11Renderer;
class SpaceSettings;
class LevelTextureManager;
class Chunk;
class WaterVLO;

class GammaEngine {
public:
    GammaEngine();
    ~GammaEngine();

    bool Initialize();
    void Run();

    WorldLoader* GetWorldLoader() const { return m_worldLoader.get(); }

    void RegisterVLO(const std::string& uid, const std::string& type,
        const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& scale);

private:
    void Update();
    void Render();

private:
    // Core
    std::unique_ptr<Window>       m_window;
    std::unique_ptr<DX11Renderer> m_renderer;
    Timer                         m_timer;

    // Subsystems
    std::unique_ptr<InputSystem>  m_input;
    std::unique_ptr<Camera>       m_camera;
    std::unique_ptr<DebugOverlay> m_debugOverlay;
    std::unique_ptr<WorldLoader>  m_worldLoader;

    // конвейер
    std::unique_ptr<RenderPipeline> m_renderPipeline;
    RenderSettings                  m_renderSettings;

    // World Data
    std::unique_ptr<SpaceSettings>       m_spaceSettings;
    std::unique_ptr<LevelTextureManager> m_levelTextureManager;
    std::vector<std::unique_ptr<Chunk>>  m_chunks;
    std::vector<std::shared_ptr<WaterVLO>> m_waterObjects;

    // Stats
    int   m_fps = 0;
    int   m_frameCount = 0;
    float m_timeElapsed = 0.0f;
};