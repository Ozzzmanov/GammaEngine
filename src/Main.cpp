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

int main() {
    // Старт логгера
    Logger::Initialize();

    // Настройки логгера
    Logger::SetCategoryEnabled(LogCategory::Texture, true);
    Logger::SetCategoryEnabled(LogCategory::Terrain, true);
    Logger::SetCategoryEnabled(LogCategory::Render, true);

    Logger::Info(LogCategory::System, "Engine Starting...");

    // Создаем движок
    GammaEngine engine;

    if (engine.Initialize()) {
        engine.Run();
    }
    else {
        Logger::Error(LogCategory::System, "Engine initialization failed. Exiting.");
    }

    Logger::Info(LogCategory::System, "Engine Shutdown.");

    // Остановка логгера перед выходом
    Logger::Shutdown();

    return 0;
}