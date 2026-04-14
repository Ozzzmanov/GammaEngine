//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GammaAPI.h
// Контракт между Ядром (EXE) и Игровой Логикой (DLL).
// !!! ВАЖНО: ЗАПРЕЩАЕТСЯ ИСПОЛЬЗОВАТЬ STL (std::vector, std::string) В ЭТОМ ФАЙЛЕ!
// Используйте только базовые типы (float, int, bool) и POD-структуры (DirectXMath).
// ================================================================================
#pragma once

#include <DirectXMath.h>
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

#ifdef GAMMA_USE_HOT_RELOAD
#ifdef GAMMA_LOGIC_EXPORT
#define GAMMA_API extern "C" __declspec(dllexport)
#else
#define GAMMA_API extern "C" __declspec(dllimport)
#endif
#else
#define GAMMA_API 
#endif

// ХЭШ СТРОК (Вычисляется при компиляции!)
constexpr uint32_t GammaHash(const char* str) {
    uint32_t hash = 2166136261u;
    for (int i = 0; str[i] != '\0'; ++i) {
        hash ^= static_cast<uint8_t>(str[i]);
        hash *= 16777619u;
    }
    return hash;
}

// Данные конкретного экшена
struct InputActionData {
    uint32_t nameHash; // Хэш имени экшена
    bool isDown;       // Зажата ли кнопка
    bool isTriggered;  // Нажата ли в этом кадре (Tap)
};

// Контейнер инпутов
struct GammaInputState {
    float mouseDeltaX;
    float mouseDeltaY;
    float mouseScroll;

    InputActionData actions[64]; // Поддерживаем до 64 кастомных экшенов
    int actionCount;
};

// Данные для рендера солнца (POD - безопасно)
struct SunData {
    float pitch;
    float yaw;
    Vector3 color;
    float intensity;
    float angularSize;
    float godRaysDensity;
    Vector3 direction;
};

// Игровое окружение (POD - безопасно)
struct EnvironmentData {
    float timeOfDay;
    float timeSpeed;
    float humidity;

    Vector3 zenithColor;
    Vector3 horizonColor;

    SunData sun;
};

// Настройки движка для чтения из скриптов (POD - безопасно)
struct GammaConfigData {
    bool volumetricEnabled;
    float volumetricDensity;
    float windSpeed;
};

// Главная память логики
// Память под нее выделяется в EXE (Core) и передается в DLL по указателю.
struct GammaState {
    bool isInitialized;
    EnvironmentData env;
    GammaConfigData config;

    GammaInputState input;

    // !!! Пример правильного использования массивов вместо std::vector:
    // int maxPlayers;
    // PlayerData players[32]; 
};

// Сигнатуры API
GAMMA_API void GammaInit(GammaState* state);
GAMMA_API void GammaUpdate(GammaState* state, float deltaTime);
GAMMA_API void GammaOnReload(GammaState* state);

typedef void (*GammaInitFunc)(GammaState*);
typedef void (*GammaUpdateFunc)(GammaState*, float);
typedef void (*GammaReloadFunc)(GammaState*);