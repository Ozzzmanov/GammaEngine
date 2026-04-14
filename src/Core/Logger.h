//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Logger.h
// Асинхронный, многопоточный логгер. Пишет логи в фоновом потоке, 
// не блокируя основной цикл рендера или логики.
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

/**
 * @class Logger
 * @brief Глобальный класс для записи логов в консоль, файл и окно вывода VS.
 */
class Logger {
public:
    static void Initialize();
    static void Shutdown();

    static void SetCategoryEnabled(LogCategory category, bool enabled);
    static void Log(LogLevel level, LogCategory category, const std::string& message);

    // --- Удобные обертки ---
    static void Info(LogCategory category, const std::string& message) { Log(LogLevel::Info, category, message); }
    static void Warn(LogCategory category, const std::string& message) { Log(LogLevel::Warning, category, message); }
    static void Error(LogCategory category, const std::string& message) { Log(LogLevel::Error, category, message); }

    // --- Перегрузки для категории General ---
    static void Info(const std::string& message) { Info(LogCategory::General, message); }
    static void Warn(const std::string& message) { Warn(LogCategory::General, message); }
    static void Error(const std::string& message) { Error(LogCategory::General, message); }

private:
    static void WorkerThread();

    static std::string GetCategoryPrefix(LogCategory category);
    static std::string GetLevelPrefix(LogLevel level);
    static std::string GetCurrentTimeStr();

private:
    // Синхронизация
    static std::mutex               m_mutex;
    static std::condition_variable  m_cv;

    // FIXME: std::queue при огромном потоке логов может съесть много памяти. 
    // В будущем лучше заменить на кольцевой буфер (Ring Buffer) фиксированного размера.
    static std::queue<LogMessage>   m_queue;
    static std::thread              m_worker;
    static std::atomic<bool>        m_isRunning;

    // Состояние
    static std::unordered_map<LogCategory, bool> m_categoryFilter;
    static std::ofstream            m_logFile;
};

// ================================================================================
// МАКРОСЫ ЛОГИРОВАНИЯ
// ================================================================================
// Если мы не в редакторе и не в Debug (то есть чистый Release-билд игры)
// логирование полностью вырезается из бинарника для максимальной производительности.

#if !defined(GAMMA_EDITOR) && !defined(_DEBUG)
#define GAMMA_LOG_INFO(category, msg)  ((void)0)
#define GAMMA_LOG_WARN(category, msg)  ((void)0)
#define GAMMA_LOG_ERROR(category, msg) ((void)0)
#else
#define GAMMA_LOG_INFO(category, msg)  Logger::Info(category, msg)
#define GAMMA_LOG_WARN(category, msg)  Logger::Warn(category, msg)
#define GAMMA_LOG_ERROR(category, msg) Logger::Error(category, msg)
#endif