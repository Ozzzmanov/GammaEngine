//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Main.cpp
// Точка входа в приложение (Entry Point)
// ================================================================================
#include "GammaEngine.h"
#include "Core/Logger.h"
#include "Config/EngineConfig.h"
#include <exception>

int main() {
    Logger::Initialize();

    // Загрузка конфигурации из JSON
    if (!EngineConfig::Get().Load("engine.json")) {
        Logger::Warn(LogCategory::System, "Config file 'engine.json' not found or invalid. Using defaults.");
    }
    const auto& cfg = EngineConfig::Get();

    // Применение настроек логирования
    Logger::SetCategoryEnabled(LogCategory::Render, cfg.Logging.Render);
    Logger::SetCategoryEnabled(LogCategory::Texture, cfg.Logging.Texture);
    Logger::SetCategoryEnabled(LogCategory::Terrain, cfg.Logging.Terrain);
    Logger::SetCategoryEnabled(LogCategory::Physics, cfg.Logging.Physics);
    Logger::SetCategoryEnabled(LogCategory::System, cfg.Logging.System);

    Logger::Info(LogCategory::System, "--- Engine Config Loaded ---");
    Logger::Info(LogCategory::System, "Window: " + std::to_string(cfg.System.WindowWidth) + "x" + std::to_string(cfg.System.WindowHeight));

    try {
        GammaEngine engine;

        if (engine.Initialize()) {
            engine.Run();
        }
        else {
            Logger::Error(LogCategory::System, "Engine initialization failed. Exiting.");
        }
    }
    catch (const std::exception& e) {
        // FIXME: В будущем добавить вывод краш-дампа (CrashDump) для релизных билдов
        Logger::Error(LogCategory::System, std::string("Fatal Error: ") + e.what());
    }
    catch (...) {
        Logger::Error(LogCategory::System, "Unknown Fatal Error occurred!");
    }

    Logger::Info(LogCategory::System, "Engine Shutdown.");
    Logger::Shutdown();

    return 0;
}