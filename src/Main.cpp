//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗ 
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Точка входа в приложение.

#include "GammaEngine.h"

int main() {
    // Создаем экземпляр движка на стеке
    GammaEngine engine;

    // Логирование
    Logger::SetCategoryEnabled(LogCategory::Texture, false);
    Logger::SetCategoryEnabled(LogCategory::Terrain, false);
    Logger::SetCategoryEnabled(LogCategory::Render, false);

    // Попытка инициализации
    if (engine.Initialize()) {
        // Запуск основного цикла
        engine.Run();
    }

    return 0;
}