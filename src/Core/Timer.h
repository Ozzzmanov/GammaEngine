//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Timer.h
// Высокоточный таймер с поддержкой сглаживания дельты.
// ================================================================================

#pragma once
#include "Prerequisites.h"

class Timer {
public:
    Timer();

    void Tick();
    float GetDeltaTime() const { return m_deltaTime; }
    float GetTotalTime() const { return m_totalTime; }

    // Сброс таймера (например, после загрузки уровня)
    void Reset();

private:
    double  m_secondsPerCount;
    __int64 m_prevTime;
    __int64 m_startTime;

    float m_deltaTime;
    float m_totalTime;

    // FIX ME вырезать Сглаживание в приоритет сырой дельты.
    static const int MAX_SAMPLE_COUNT = 50;
    float m_deltaBuffer[MAX_SAMPLE_COUNT];
    int m_sampleIndex = 0;
};