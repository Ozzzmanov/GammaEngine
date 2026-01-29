//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Logger.cpp
// ================================================================================

#include "Logger.h"
#include <iostream>
#include <Windows.h>
#include <iomanip>
#include <sstream>
#include <ctime>

// Статические члены
std::mutex              Logger::m_mutex;
std::condition_variable Logger::m_cv;
std::queue<LogMessage>  Logger::m_queue;
std::thread             Logger::m_worker;
std::atomic<bool>       Logger::m_isRunning = false;
std::unordered_map<LogCategory, bool> Logger::m_categoryFilter;
std::ofstream           Logger::m_logFile;

void Logger::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_isRunning) return;

    // Настройки по умолчанию
    m_categoryFilter[LogCategory::General] = true;
    m_categoryFilter[LogCategory::Render]  = true;
    m_categoryFilter[LogCategory::System]  = true;
    m_categoryFilter[LogCategory::Texture] = true;
    m_categoryFilter[LogCategory::Terrain] = true;
    m_categoryFilter[LogCategory::Physics] = true;

    // Открываем файл (перезапись при каждом запуске)
    m_logFile.open("GammaEngine.log", std::ios::out | std::ios::trunc);
    
    m_isRunning = true;
    m_worker = std::thread(WorkerThread);
}

void Logger::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_isRunning) return;
        m_isRunning = false;
    }
    
    // Будим поток, чтобы он завершился
    m_cv.notify_one();

    if (m_worker.joinable()) {
        m_worker.join();
    }

    if (m_logFile.is_open()) {
        m_logFile.close();
    }
}

void Logger::SetCategoryEnabled(LogCategory category, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_categoryFilter[category] = enabled;
}

void Logger::Log(LogLevel level, LogCategory category, const std::string& message) {
    // Быстрая проверка фильтра (без блокировки, если возможно, но для безопасности лучше с ней)
    // Здесь мы блокируем только для проверки фильтра и push, это очень быстро.
    std::unique_lock<std::mutex> lock(m_mutex);
    
    if (m_categoryFilter.find(category) == m_categoryFilter.end() || !m_categoryFilter[category]) {
        return;
    }

    LogMessage msg;
    msg.Level = level;
    msg.Category = category;
    msg.Text = message;
    msg.Timestamp = GetCurrentTimeStr();

    m_queue.push(msg);
    
    lock.unlock();
    m_cv.notify_one();
}

void Logger::WorkerThread() {
    while (true) {
        LogMessage msg;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            
            // Ждем, пока что-то появится или пока не скажут выходить
            m_cv.wait(lock, [] { return !m_queue.empty() || !m_isRunning; });

            if (!m_isRunning && m_queue.empty()) {
                break; // Выход из цикла
            }

            if (m_queue.empty()) continue;

            msg = m_queue.front();
            m_queue.pop();
        }

        // --- Форматирование вывода ---
        std::stringstream ss;
        ss << "[" << msg.Timestamp << "] " 
           << GetLevelPrefix(msg.Level) << " " 
           << GetCategoryPrefix(msg.Category) << " " 
           << msg.Text << "\n";

        std::string finalStr = ss.str();

        // Вывод в VS Output Window (Очень удобно для отладки)
        OutputDebugStringA(finalStr.c_str());

        // Вывод в стандартную консоль (если есть)
        std::cout << finalStr;

        // Вывод в файл
        if (m_logFile.is_open()) {
            m_logFile << finalStr;
            m_logFile.flush(); // Важно для сохранения при крашах
        }
    }
}

std::string Logger::GetCategoryPrefix(LogCategory category) {
    switch (category) {
        case LogCategory::General: return "[GEN]";
        case LogCategory::Render:  return "[GFX]";
        case LogCategory::Texture: return "[TEX]";
        case LogCategory::Terrain: return "[MAP]";
        case LogCategory::Physics: return "[PHY]";
        case LogCategory::System:  return "[SYS]";
        default:                   return "[UNK]";
    }
}

std::string Logger::GetLevelPrefix(LogLevel level) {
    switch (level) {
        case LogLevel::Info:    return "[INFO]";
        case LogLevel::Warning: return "[WARN]";
        case LogLevel::Error:   return "[ERR ]";
        default:                return "[INFO]";
    }
}

std::string Logger::GetCurrentTimeStr() {
    // Получение времени (thread-safe в C++11 и выше для localtime_s)
    std::time_t now = std::time(nullptr);
    std::tm tmNow;
    localtime_s(&tmNow, &now);

    std::stringstream ss;
    ss << std::put_time(&tmNow, "%H:%M:%S");
    return ss.str();
}