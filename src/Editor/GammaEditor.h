#pragma once
#include "../Core/Prerequisites.h"

#ifdef GAMMA_EDITOR
#include "EditorProfiler.h"
#include "EditorPanel.h"
#include "Panels/ViewportPanel.h"

class Window;
class DX11Renderer;
struct GammaState;

class GammaEditor {
public:
    GammaEditor() = default;
    ~GammaEditor() = default;

    bool Initialize(Window* window, DX11Renderer* renderer);
    void Render(GammaState* gameState, ID3D11ShaderResourceView* gameSRV, const FrameProfilerStats& stats);
    void Shutdown();

    // Запрашиваем состояние у главного вьюпорта (для блокировки инпута в GammaEngine.cpp)
    bool IsMainViewportHovered() const;

private:
    void SetupDockSpace();
    void DrawMenuBar();

    Window* m_window = nullptr;
    DX11Renderer* m_renderer = nullptr;

    std::unique_ptr<EditorProfiler> m_profiler;

    std::vector<std::shared_ptr<EditorPanel>> m_panels;
    std::shared_ptr<ViewportPanel> m_mainViewport; // Быстрый доступ к главному окну
};
#endif // GAMMA_EDITOR