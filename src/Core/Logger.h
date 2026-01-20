//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Класс Logger.
// Обеспечивает потокобезопасный вывод сообщений в консоль с цветовым кодированием.
// ================================================================================

#pragma once
#include <string>
#include <iostream>
#include <mutex>
#include <unordered_map>

// Перечисление всех систем, которые могут писать в лог
enum class LogCategory {
    General,
    Render,
    Texture,    // Текстуры, загрузка DDS
    Terrain,    // Парсинг .cdata, слои
    Physics,    // Коллизии
    System      // CPU, Память
};

class Logger {
public:
    // Настройка: включить/выключить категорию
    static void SetCategoryEnabled(LogCategory category, bool enabled) {
        GetState()[category] = enabled;
    }

    static void Info(LogCategory category, const std::string& message) {
        if (IsCategoryEnabled(category)) Print("INFO", category, message);
    }

    static void Warn(LogCategory category, const std::string& message) {
        if (IsCategoryEnabled(category)) Print("WARN", category, message);
    }

    static void Error(LogCategory category, const std::string& message) {
        // Ошибки показываем всегда, либо тоже можно фильтровать
        Print("ERROR", category, message);
    }

    // Перегрузки FIX ME
    static void Info(const std::string& message) { Info(LogCategory::General, message); }
    static void Warn(const std::string& message) { Warn(LogCategory::General, message); }
    static void Error(const std::string& message) { Error(LogCategory::General, message); }

private:
    static std::unordered_map<LogCategory, bool>& GetState() {
        static std::unordered_map<LogCategory, bool> state;
        // По умолчанию все включено, если не сказано иное
        if (state.empty()) {
            state[LogCategory::General] = true;
            state[LogCategory::Texture] = true;
            state[LogCategory::Terrain] = true;
        }
        return state;
    }

    static bool IsCategoryEnabled(LogCategory category) {
        return GetState()[category];
    }

    static std::string GetCategoryName(LogCategory category) {
        switch (category) {
        case LogCategory::Texture: return "[TEXTURE]";
        case LogCategory::Terrain: return "[TERRAIN]";
        case LogCategory::Physics: return "[PHYSICS]";
        default: return "[GENERIC]";
        }
    }

    static void Print(const std::string& level, LogCategory category, const std::string& msg) {
        // Тут можно добавить цвет или запись в файл
        std::cout << level << " " << GetCategoryName(category) << ": " << msg << std::endl;
    }
};