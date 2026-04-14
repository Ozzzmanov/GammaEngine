//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GammaModule.h
// Система безопасной динамической загрузки и Hot Reloading для игровой логики.
// Строго разделена макросами для Editor/Debug и Release сборок.
// ================================================================================
#pragma once
#include "Prerequisites.h"
#include "../API/GammaAPI.h"

/**
 * @class GammaModule
 * @brief Управляет жизненным циклом загружаемой DLL игровой логики.
 */
class GammaModule {
public:
    GammaModule();
    ~GammaModule();

    void Load();
    void Unload();

    /// @brief Обновляет DLL, если обнаружена пересборка. Доступно только в Editor/Debug.
    void ReloadIfNeeded(GammaState* state, float deltaTime);
    void SafeReload(GammaState* state);

    void SafeInit(GammaState* state);
    void SafeUpdate(GammaState* state, float deltaTime);

private:
#ifdef GAMMA_USE_HOT_RELOAD
    /// @brief Проверяет, заблокирован ли файл другим процессом (например, линкером MSVC).
    bool IsFileLocked(const std::string& filePath) const;
#endif

private:
    HMODULE m_dllHandle = nullptr;
    bool m_isValid = false;

    GammaInitFunc m_initFunc = nullptr;
    GammaUpdateFunc m_updateFunc = nullptr;
    GammaReloadFunc m_reloadFunc = nullptr;

    // Данные для Hot Reload компилируются ТОЛЬКО если он включен (экономия памяти в релизе)
#ifdef GAMMA_USE_HOT_RELOAD
    std::filesystem::file_time_type m_lastWriteTime;
    float m_reloadTimer = 0.0f;
    const float RELOAD_POLL_INTERVAL = 0.5f; // Частота проверки диска (в секундах)
    bool m_isAwaitingReload = false;         // Флаг ожидания завершения компиляции
#endif
};