//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// TaskScheduler.h
// Система планирования задач (Job System). 
// Распределяет тяжелую работу (загрузка файлов, запекание) по свободным ядрам CPU.
// ================================================================================
#pragma once
#include "Prerequisites.h"
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

struct Task {
    std::function<void()> Work;
    std::function<void()> Callback;
};

/**
 * @class TaskScheduler
 * @brief Менеджер фоновых потоков (Thread Pool).
 */
class TaskScheduler {
public:
    static TaskScheduler& Get() {
        static TaskScheduler instance;
        return instance;
    }

    void Initialize();
    void Shutdown();

    /// @brief Добавляет задачу в очередь фоновых потоков.
    /// @param work Тяжелая работа (выполняется в фоне).
    /// @param callback Коллбэк (выполняется в основном потоке после завершения work).
    void Submit(const std::function<void()>& work, const std::function<void()>& callback = nullptr);

    /// @brief Обрабатывает завершенные фоновые задачи (коллбэки). Вызывается из основного потока (Main).
    /// @param maxTimeSeconds Максимальное время на обработку в текущем кадре (защита от фризов).
    void ProcessMainThreadCallbacks(float maxTimeSeconds = 0.010f);

    size_t GetWorkerCount() const { return m_workers.size(); }

    size_t GetPendingBackgroundTasks() {
        std::lock_guard<std::mutex> lock(m_bgMutex);
        return m_backgroundQueue.size();
    }

    size_t GetPendingMainCallbacks() {
        std::lock_guard<std::mutex> lock(m_mainMutex);
        return m_mainThreadQueue.size();
    }

private:
    TaskScheduler() = default;
    ~TaskScheduler() = default;
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    void WorkerThreadLoop();

private:
    std::vector<std::thread> m_workers;

    // FIXME: Заменить std::queue + std::mutex на Lock-Free очереди (например, moodycamel::ConcurrentQueue) 
    // для достижения топ производительности и минимизации блокировок (contention).
    std::queue<Task> m_backgroundQueue;
    std::mutex m_bgMutex;
    std::condition_variable m_cv;

    std::queue<std::function<void()>> m_mainThreadQueue;
    std::mutex m_mainMutex;

    std::atomic<bool> m_isRunning{ false };
};