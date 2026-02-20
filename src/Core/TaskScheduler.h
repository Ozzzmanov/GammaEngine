//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// TaskScheduler.h
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

class TaskScheduler {
public:
    static TaskScheduler& Get() {
        static TaskScheduler instance;
        return instance;
    }

    void Initialize();
    void Shutdown();
    void Submit(const std::function<void()>& work, const std::function<void()>& callback = nullptr);

    // Добавляем аргумент maxTimeSeconds (по умолчанию 10мс) FIX ME сделать нормально.
    void ProcessMainThreadCallbacks(float maxTimeSeconds = 0.010f);

private:
    TaskScheduler() = default;
    ~TaskScheduler() = default;
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    void WorkerThreadLoop();

private:
    std::vector<std::thread> m_workers;

    std::queue<Task> m_backgroundQueue;
    std::mutex m_bgMutex;
    std::condition_variable m_cv;

    std::queue<std::function<void()>> m_mainThreadQueue;
    std::mutex m_mainMutex;

    std::atomic<bool> m_isRunning{ false };
};