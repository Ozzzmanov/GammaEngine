//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// Timer.h
// Высокоточный таймер на основе QueryPerformanceCounter. 
// Выдает чистую сырую дельту (Raw Delta) с защитой от подвисаний.
// ================================================================================
#pragma once
#include "Prerequisites.h"

// Структура для профилировщика Editor
struct FrameProfilerStats {
    float UpdateTimeMs = 0.0f;
    float RenderPipelineTimeMs = 0.0f;
    float EditorTimeMs = 0.0f;
    float PresentTimeMs = 0.0f;
    int   FPS = 0;
};

class Timer {
public:
    Timer();

    void Tick();
    float GetDeltaTime() const { return m_deltaTime; }
    float GetTotalTime() const { return m_totalTime; }

    /// @brief Сброс таймера (вызывать после тяжелой загрузки уровня/сцены, чтобы избежать огромной дельты)
    void Reset();

private:
    double  m_secondsPerCount;
    __int64 m_prevTime;
    __int64 m_startTime;

    float m_deltaTime;
    float m_totalTime;
};