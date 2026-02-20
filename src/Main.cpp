//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Main.cpp
// ================================================================================

#include "GammaEngine.h"
#include "Core/Logger.h"
#include "Config/EngineConfig.h" 

int main() {
    Logger::Initialize();

    // Загрузка конфигурации
    if (!EngineConfig::Get().Load("engine.json")) {
        Logger::Warn(LogCategory::System, "Config file not found or invalid. Using defaults.");
    }
    const auto& cfg = EngineConfig::Get();

    // Применение настроек логирования из конфига
    Logger::SetCategoryEnabled(LogCategory::Render, cfg.Logging.Render);
    Logger::SetCategoryEnabled(LogCategory::Texture, cfg.Logging.Texture);
    Logger::SetCategoryEnabled(LogCategory::Terrain, cfg.Logging.Terrain);
    Logger::SetCategoryEnabled(LogCategory::Physics, cfg.Logging.Physics);
    Logger::SetCategoryEnabled(LogCategory::System, cfg.Logging.System);

    Logger::Info(LogCategory::System, "--- Engine Config Loaded ---");
    Logger::Info(LogCategory::System, "Window: " + std::to_string(cfg.WindowWidth) + "x" + std::to_string(cfg.WindowHeight));
    Logger::Info(LogCategory::System, "Static Batching: " + std::string(cfg.UseStaticBatching ? "ON" : "OFF"));

    // Создаем и запускаем движок
    GammaEngine engine;

    if (engine.Initialize()) {
        engine.Run();
    }
    else {
        Logger::Error(LogCategory::System, "Engine initialization failed. Exiting.");
    }

    Logger::Info(LogCategory::System, "Engine Shutdown.");
    Logger::Shutdown();

    return 0;
}