//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
#include "Timer.h"

Timer::Timer()
    : m_secondsPerCount(0.0), m_prevTime(0), m_deltaTime(0.0f), m_totalTime(0.0f)
{
    __int64 countsPerSec;
    QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
    m_secondsPerCount = 1.0 / (double)countsPerSec;

    QueryPerformanceCounter((LARGE_INTEGER*)&m_prevTime);
}

void Timer::Tick() {
    __int64 currTime;
    QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

    __int64 deltaCounts = currTime - m_prevTime;
    m_prevTime = currTime;

    // Защита от отрицательных значений (если процессор скачет между ядрами)
    if (deltaCounts < 0) deltaCounts = 0;

    m_deltaTime = (float)(deltaCounts * m_secondsPerCount);
    m_totalTime += m_deltaTime;
}