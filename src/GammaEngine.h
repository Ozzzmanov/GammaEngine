//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GammaEngine.h
// Главный класс движка. Управляет жизненным циклом всех подсистем.
// ================================================================================
#pragma once
#include "Core/Prerequisites.h" 
#include "Core/Window.h"
#include "Core/Timer.h"
#include "Core/InputSystem.h"
#include "Graphics/Camera.h"
#include "World/WorldLoader.h"
#include "Graphics/RenderPipeline.h"
#include "Core/GammaModule.h"

#include <memory>
#include <vector>
#include <string>

#ifdef GAMMA_EDITOR
#include "Editor/GammaEditor.h"
#endif

// Forward declarations
class DX11Renderer;
class SpaceSettings;
class LevelTextureManager;
class Chunk;
class WaterVLO;

/**
 * @class GammaEngine
 * @brief Ядро движка. Инициализирует рендер, загружает мир, управляет игровым циклом.
 */
class GammaEngine {
public:
    GammaEngine();
    ~GammaEngine();

    bool Initialize();
    void Run();

    WorldLoader* GetWorldLoader() const { return m_worldLoader.get(); }

    // FIXME: Избавиться от глобального проброса методов. Использовать Event System 
    // или передавать WorldLoader напрямую в парсер файлов.
    void RegisterVLO(const std::string& uid, const std::string& type,
        const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& scale);

private:
    void Update();
    void Render();

private:
    // --- Core Systems ---
    std::unique_ptr<Window>         m_window;
    std::unique_ptr<DX11Renderer>   m_renderer;
    Timer                           m_timer;
    std::unique_ptr<InputSystem>    m_input;

    // --- Game Logic & State ---
    GammaState* m_gameState = nullptr; // FIXME: Завернуть в std::unique_ptr если DLL API позволяет
    GammaModule                     m_gameModule;

    // --- Graphics & World ---
    std::unique_ptr<Camera>               m_camera;
    std::unique_ptr<RenderPipeline>       m_renderPipeline;
    RenderSettings                        m_renderSettings;

    std::unique_ptr<WorldLoader>          m_worldLoader;
    std::unique_ptr<SpaceSettings>        m_spaceSettings;
    std::unique_ptr<LevelTextureManager>  m_levelTextureManager;
    std::vector<std::unique_ptr<Chunk>>   m_chunks;
    std::vector<std::shared_ptr<WaterVLO>> m_waterObjects;

    // --- Metrics ---
    int   m_fps = 0;
    int   m_frameCount = 0;
    float m_timeElapsed = 0.0f;

#ifdef GAMMA_EDITOR
    // --- Editor Systems ---
    // FIXME: В идеале вынести весь этот блок в класс GammaEditorApp : public GammaEngine
    std::unique_ptr<GammaEditor> m_editor;
    FrameProfilerStats           m_profilerStats;
#endif
};