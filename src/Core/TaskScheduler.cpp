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

    // Оставляем 1 ядро для основного потока (Main Thread), остальные отдаем под воркеры
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
        catch (const std::exception& e) {
            Logger::Error(LogCategory::System, std::string("Task Exception: ") + e.what());
        }
        catch (...) {
            Logger::Error(LogCategory::System, "Unknown Task Exception");
        }

        if (task.Callback) {
            std::lock_guard<std::mutex> lock(m_mainMutex);
            m_mainThreadQueue.push(task.Callback);
        }
    }
}

void TaskScheduler::ProcessMainThreadCallbacks(float maxTimeSeconds) {
    auto startTime = std::chrono::high_resolution_clock::now();
    long long maxDurationNs = static_cast<long long>(maxTimeSeconds * 1'000'000'000.0f);

    while (true) {
        std::function<void()> callback;
        {
            std::unique_lock<std::mutex> lock(m_mainMutex);
            if (m_mainThreadQueue.empty()) return;

            callback = std::move(m_mainThreadQueue.front());
            m_mainThreadQueue.pop();
        }

        if (callback) {
            try {
                callback();
            }
            catch (...) {
                Logger::Error(LogCategory::System, "MainThread Callback Error");
            }
        }

        // Проверяем, не вышли ли мы за лимит времени кадра
        auto currTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(currTime - startTime).count();

        if (duration > maxDurationNs) {
            break; // Остаток доделаем в следующем кадре, чтобы не ронять FPS
        }
    }
}