//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
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
}

void Timer::Tick() {
    __int64 currTime;
    QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

    __int64 deltaCounts = currTime - m_prevTime;
    m_prevTime = currTime;

    if (deltaCounts < 0) deltaCounts = 0;

    // Берем сырую дельту 
    m_deltaTime = (float)(deltaCounts * m_secondsPerCount);

    // FIX ME переделать. ВАЖНО: Защита от "Спирали смерти" и провалов под текстуры!
    // Если кадр длился дольше 1/10 секунды (зависание, перетаскивание окна),
    // мы жестко обрезаем дельту, чтобы физика и движение не взорвались.
    if (m_deltaTime > 0.1f) {
        m_deltaTime = 0.1f;
    }

    // Общее время считаем точно от старта
    m_totalTime = (float)((currTime - m_startTime) * m_secondsPerCount);
}