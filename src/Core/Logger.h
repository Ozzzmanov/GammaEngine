//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Logger.h
// Асинхронный, многопоточный логгер.
// ================================================================================

#pragma once
#include <string>
#include <mutex>
#include <unordered_map>
#include <queue>
#include <thread>
#include <condition_variable>
#include <fstream>
#include <atomic>

enum class LogCategory {
    General,
    Render,
    Texture,
    Terrain,
    Physics,
    System
};

enum class LogLevel {
    Info,
    Warning,
    Error
};

struct LogMessage {
    LogLevel    Level;
    LogCategory Category;
    std::string Text;
    std::string Timestamp;
};

class Logger {
public:
    // Инициализация потока логирования и открытие файла
    static void Initialize();

    // Остановка потока и закрытие файла (вызывать при выходе)
    static void Shutdown();

    // Настройка фильтров
    static void SetCategoryEnabled(LogCategory category, bool enabled);

    // Основные методы записи
    static void Log(LogLevel level, LogCategory category, const std::string& message);

    // Удобные обертки
    static void Info(LogCategory category, const std::string& message) { Log(LogLevel::Info, category, message); }
    static void Warn(LogCategory category, const std::string& message) { Log(LogLevel::Warning, category, message); }
    static void Error(LogCategory category, const std::string& message) { Log(LogLevel::Error, category, message); }

    // Перегрузки для General
    static void Info(const std::string& message) { Info(LogCategory::General, message); }
    static void Warn(const std::string& message) { Warn(LogCategory::General, message); }
    static void Error(const std::string& message) { Error(LogCategory::General, message); }

private:
    // Функция рабочего потока
    static void WorkerThread();

    // Вспомогательные
    static std::string GetCategoryPrefix(LogCategory category);
    static std::string GetLevelPrefix(LogLevel level);
    static std::string GetCurrentTimeStr();

private:
    // Синхронизация
    static std::mutex               m_mutex;
    static std::condition_variable  m_cv;
    static std::queue<LogMessage>   m_queue;
    static std::thread              m_worker;
    static std::atomic<bool>        m_isRunning;

    // Состояние
    static std::unordered_map<LogCategory, bool> m_categoryFilter;
    static std::ofstream            m_logFile;
};