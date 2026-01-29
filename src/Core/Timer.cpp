//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Timer.cpp
// ================================================================================

#include "Timer.h"

Timer::Timer()
    : m_secondsPerCount(0.0), m_prevTime(0), m_startTime(0),
    m_deltaTime(0.0f), m_totalTime(0.0f), m_sampleIndex(0)
{
    __int64 countsPerSec;
    QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
    m_secondsPerCount = 1.0 / (double)countsPerSec;

    Reset();
}

void Timer::Reset() {
    QueryPerformanceCounter((LARGE_INTEGER*)&m_startTime);
    m_prevTime = m_startTime;
    m_totalTime = 0.0f;
    m_deltaTime = 0.0f;

    // Заполняем буфер средним значением (например 60 FPS)
    for (int i = 0; i < MAX_SAMPLE_COUNT; ++i) {
        m_deltaBuffer[i] = 1.0f / 60.0f;
    }
}

void Timer::Tick() {
    __int64 currTime;
    QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

    __int64 deltaCounts = currTime - m_prevTime;
    m_prevTime = currTime;

    if (deltaCounts < 0) deltaCounts = 0;

    // Сырая дельта
    float rawDelta = (float)(deltaCounts * m_secondsPerCount);

    // Сглаживание
    m_deltaBuffer[m_sampleIndex] = rawDelta;
    m_sampleIndex = (m_sampleIndex + 1) % MAX_SAMPLE_COUNT;

    float sum = 0.0f;
    for (int i = 0; i < MAX_SAMPLE_COUNT; ++i) {
        sum += m_deltaBuffer[i];
    }
    m_deltaTime = sum / MAX_SAMPLE_COUNT;

    // Общее время считаем точно от старта, чтобы не накапливать погрешность float
    m_totalTime = (float)((currTime - m_startTime) * m_secondsPerCount);
}