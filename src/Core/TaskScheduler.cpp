//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// TaskScheduler.cpp
// ================================================================================
#include "TaskScheduler.h"
#include "Logger.h"

void TaskScheduler::Initialize() {
    if (m_isRunning) return;
    m_isRunning = true;

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads > 1) numThreads--;
    if (numThreads < 1) numThreads = 1;

    Logger::Info(LogCategory::System, "TaskScheduler: Starting " + std::to_string(numThreads) + " worker threads.");

    for (unsigned int i = 0; i < numThreads; ++i) {
        m_workers.emplace_back(&TaskScheduler::WorkerThreadLoop, this);
    }
}

void TaskScheduler::Shutdown() {
    if (!m_isRunning) return;

    {
        std::lock_guard<std::mutex> lock(m_bgMutex);
        m_isRunning = false;
    }
    m_cv.notify_all();

    for (auto& worker : m_workers) {
        if (worker.joinable()) worker.join();
    }
    m_workers.clear();
    Logger::Info(LogCategory::System, "TaskScheduler: Shutdown complete.");
}

void TaskScheduler::Submit(const std::function<void()>& work, const std::function<void()>& callback) {
    if (!work) return;
    {
        std::lock_guard<std::mutex> lock(m_bgMutex);
        m_backgroundQueue.push({ work, callback });
    }
    m_cv.notify_one();
}

void TaskScheduler::WorkerThreadLoop() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_bgMutex);
            m_cv.wait(lock, [this] { return !m_backgroundQueue.empty() || !m_isRunning; });

            if (!m_isRunning && m_backgroundQueue.empty()) break;
            if (m_backgroundQueue.empty()) continue;

            task = m_backgroundQueue.front();
            m_backgroundQueue.pop();
        }

        try {
            task.Work();
        }
        catch (...) { Logger::Error(LogCategory::System, "Task Exception"); }

        if (task.Callback) {
            std::lock_guard<std::mutex> lock(m_mainMutex);
            m_mainThreadQueue.push(task.Callback);
        }
    }
}

// РЕАЛИЗАЦИЯ С ТАЙМЕРОМ
void TaskScheduler::ProcessMainThreadCallbacks(float maxTimeSeconds) {
    auto startTime = std::chrono::high_resolution_clock::now();
    long long maxDurationNs = (long long)(maxTimeSeconds * 1'000'000'000.0);

    while (true) {
        std::function<void()> callback;
        {
            std::unique_lock<std::mutex> lock(m_mainMutex);
            if (m_mainThreadQueue.empty()) return; // Очередь пуста, выходим

            callback = std::move(m_mainThreadQueue.front());
            m_mainThreadQueue.pop();
        }

        if (callback) {
            try { callback(); }
            catch (...) { Logger::Error(LogCategory::System, "MainThread Callback Error"); }
        }

        // Проверяем время
        auto currTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(currTime - startTime).count();

        // Если превысили лимит времени - выходим, остальное доделаем в следующем кадре
        if (duration > maxDurationNs) {
            break;
        }
    }
}