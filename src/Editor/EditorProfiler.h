//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// EditorProfiler.h
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"

#ifdef GAMMA_EDITOR
#include "../Core/Timer.h"
#include <vector>

class EditorProfiler {
public:
    EditorProfiler() = default;
    ~EditorProfiler() = default;

    void Initialize(ID3D11Device* device);

    // Единый метод обновления и отрисовки
    void UpdateAndRender(const FrameProfilerStats& stats);

private:
    void CollectSystemMetrics();

    ID3D11Device* m_device = nullptr;

    // Графики (храним последние 240 кадров = 4 сек при 60fps)
    static const int MAX_HISTORY = 240;
    std::vector<float> m_fpsHistory;
    std::vector<float> m_frameTimeHistory;

    // Системные ресурсы
    float m_sysInfoTimer = 1.0f;
    float m_ramUsageMb = 0.0f;
    float m_ramTotalMb = 0.0f;   // <--- Общий объем ОЗУ
    float m_vramUsageMb = 0.0f;
    float m_vramBudgetMb = 0.0f;
};
#endif // GAMMA_EDITOR