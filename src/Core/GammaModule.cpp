//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GammaModule.cpp
// ================================================================================
#include "GammaModule.h"
#include "Logger.h"

// FIXME: В будущем заменить на std::filesystem::current_path() или передавать путь из конфига, 
// чтобы не зависеть от Win32 API GetModuleFileNameA напрямую.
static std::string GetExeDir() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path().string() + "\\";
}

GammaModule::GammaModule() {}

GammaModule::~GammaModule() {
    Unload();
}

void GammaModule::Load() {
    std::string dllSource = GetExeDir() + "GammaDemo.dll";

#ifndef GAMMA_USE_HOT_RELOAD
    // ================================================================================
    // RELEASE MODE: Максимальная производительность. 
    // ================================================================================
    // Грузим DLL напрямую, без создания временных файлов и проверок диска.
    m_dllHandle = LoadLibraryA(dllSource.c_str());
    if (m_dllHandle) {
        m_initFunc = (GammaInitFunc)GetProcAddress(m_dllHandle, "GammaInit");
        m_updateFunc = (GammaUpdateFunc)GetProcAddress(m_dllHandle, "GammaUpdate");
        m_reloadFunc = (GammaReloadFunc)GetProcAddress(m_dllHandle, "GammaOnReload");

        m_isValid = (m_initFunc != nullptr && m_updateFunc != nullptr);

        if (m_isValid) {
            Logger::Info(LogCategory::System, "GammaDemo.dll statically linked (Release Mode). Security: MAXIMUM.");
        }
        else {
            Logger::Error(LogCategory::System, "Failed to find Gamma API signatures in Release DLL.");
        }
    }
    else {
        Logger::Error(LogCategory::System, "LoadLibraryA failed in Release. File not found: " + dllSource);
    }

#else
    // ================================================================================
    // DEBUG / EDITOR MODE: Включен Hot Reload.
    // ================================================================================
    // Копируем во временный файл, чтобы не блокировать Visual Studio при пересборке DLL.
    std::string dllTemp = GetExeDir() + "GammaDemo_temp.dll";
    std::string pdbSource = GetExeDir() + "GammaDemo.pdb";
    std::string pdbTemp = GetExeDir() + "GammaDemo_temp.pdb";

    std::error_code ec;
    if (!std::filesystem::exists(dllSource, ec)) {
        Logger::Warn(LogCategory::System, "GammaDemo.dll not found at: " + dllSource);
        return;
    }

    m_lastWriteTime = std::filesystem::last_write_time(dllSource, ec);

    std::filesystem::copy_file(dllSource, dllTemp, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return;

    // PDB копируем без проверки ошибок, так как его отсутствие не критично для работы
    std::filesystem::copy_file(pdbSource, pdbTemp, std::filesystem::copy_options::overwrite_existing, ec);

    m_dllHandle = LoadLibraryA(dllTemp.c_str());
    if (m_dllHandle) {
        m_initFunc = (GammaInitFunc)GetProcAddress(m_dllHandle, "GammaInit");
        m_updateFunc = (GammaUpdateFunc)GetProcAddress(m_dllHandle, "GammaUpdate");
        m_reloadFunc = (GammaReloadFunc)GetProcAddress(m_dllHandle, "GammaOnReload");

        m_isValid = (m_initFunc != nullptr && m_updateFunc != nullptr);

        if (m_isValid) {
            Logger::Info(LogCategory::System, "GammaDemo.dll Loaded and Linked Successfully!");
        }
        else {
            Logger::Error(LogCategory::System, "Failed to find Gamma API signatures in DLL.");
        }
    }
    else {
        Logger::Error(LogCategory::System, "LoadLibraryA failed to load " + dllTemp);
    }
#endif
}

void GammaModule::Unload() {
    if (m_dllHandle) {
        FreeLibrary(m_dllHandle);
        m_dllHandle = nullptr;
    }
    m_initFunc = nullptr;
    m_updateFunc = nullptr;
    m_reloadFunc = nullptr;
    m_isValid = false;
}

#ifdef GAMMA_USE_HOT_RELOAD
bool GammaModule::IsFileLocked(const std::string& filePath) const {
    // Пытаемся открыть файл с эксклюзивным доступом.
    // Если линкер MSVC всё ещё пишет в файл, CreateFile вернет INVALID_HANDLE_VALUE.
    HANDLE handle = CreateFileA(
        filePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE) {
        return true;
    }

    CloseHandle(handle);
    return false;
}
#endif

void GammaModule::ReloadIfNeeded(GammaState* state, float deltaTime) {
#ifdef GAMMA_USE_HOT_RELOAD
    m_reloadTimer += deltaTime;

    // Ограничиваем частоту опроса диска, если мы не находимся в активном ожидании сборки.
    if (m_reloadTimer < RELOAD_POLL_INTERVAL && !m_isAwaitingReload) {
        return;
    }
    m_reloadTimer = 0.0f;

    std::string dllSource = GetExeDir() + "GammaDemo.dll";
    std::string pdbSource = GetExeDir() + "GammaDemo.pdb";
    std::error_code ec;

    if (!std::filesystem::exists(dllSource, ec)) return;

    auto currentTime = std::filesystem::last_write_time(dllSource, ec);

    if (currentTime > m_lastWriteTime || m_isAwaitingReload) {

        // Проверяем, не удерживается ли файл компилятором
        if (IsFileLocked(dllSource) || IsFileLocked(pdbSource)) {
            m_isAwaitingReload = true;
            return; // Пропускаем кадр, движок продолжает работать без фризов
        }

        // Файлы свободны - выполняем горячую замену
        m_isAwaitingReload = false;
        Logger::Info(LogCategory::System, "--- HOT RELOADING GammaDemo.dll ---");

        Unload();
        Load();
        SafeReload(state);
    }
#else
    UNUSED(state);
    UNUSED(deltaTime);
#endif
}

// ================================================================================
// SEH ОБЕРТКИ (Structured Exception Handling)
// Перехватывают жесткие краши внутри DLL (access violation, divide by zero и т.д.),
// не позволяя им убить процесс редактора или движка.
// ================================================================================

static bool CallInitSafe(GammaInitFunc func, GammaState* state) {
    __try {
        func(state);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool CallUpdateSafe(GammaUpdateFunc func, GammaState* state, float dt) {
    __try {
        func(state, dt);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool CallReloadSafe(GammaReloadFunc func, GammaState* state) {
    __try {
        func(state);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void GammaModule::SafeInit(GammaState* state) {
    if (!m_isValid || !m_initFunc) return;

    if (!CallInitSafe(m_initFunc, state)) {
        Logger::Error(LogCategory::System, "CRITICAL ERROR: GammaDemo.dll crashed during GammaInit()!");
        m_isValid = false;
    }
}

void GammaModule::SafeUpdate(GammaState* state, float deltaTime) {
    if (!m_isValid || !m_updateFunc) return;

    if (!CallUpdateSafe(m_updateFunc, state, deltaTime)) {
        Logger::Error(LogCategory::System, "CRITICAL ERROR: GammaDemo.dll crashed during GammaUpdate()! Logic frozen.");
        m_isValid = false;
    }
}

void GammaModule::SafeReload(GammaState* state) {
    if (!m_isValid || !m_reloadFunc) return;

    if (!CallReloadSafe(m_reloadFunc, state)) {
        Logger::Error(LogCategory::System, "CRITICAL ERROR: GammaDemo.dll crashed during GammaOnReload()!");
        m_isValid = false;
    }
}