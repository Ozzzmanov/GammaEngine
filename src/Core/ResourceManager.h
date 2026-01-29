//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// ResourceManager.h
// Менеджер ресурсов. Управляет кэшированием, поиском и конвертацией текстур.
// ================================================================================

#pragma once
#include "Prerequisites.h"
#include <mutex>
#include <unordered_map>

class ResourceManager {
public:
    static ResourceManager& Get() {
        static ResourceManager instance;
        return instance;
    }

    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    // Умная загрузка: ищет файл, конвертирует в DDS (BC2/BC3), кэширует в RAM и на диске.
    // Возвращает готовый SRV для TextureArray.
    ID3D11ShaderResourceView* GetOrCacheTexture(const std::string& originalPath);

    // "Сырая" загрузка: грузит файл как есть (DDS/PNG/JPG/TGA).
    ID3D11ShaderResourceView* GetTextureRaw(const std::string& originalPath);

private:
    ResourceManager() = default;
    ~ResourceManager() = default;
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // --- Внутренняя логика ---

    // Пытается найти реальный путь к файлу (проверяет Assets/, замену расширений и т.д.)
    bool ResolveTexturePath(const std::string& inputPath, fs::path& outPath);

    bool IsCacheValid(const fs::path& source, const fs::path& cache);
    bool ProcessAndCacheTexture(const fs::path& source, const fs::path& cache);
    ID3D11ShaderResourceView* LoadFromCache(const fs::path& cachePath);

    // --- Данные ---
    std::unordered_map<std::string, ComPtr<ID3D11ShaderResourceView>> m_memoryCache;
    std::mutex m_mutex;

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Константы конвертации
    const size_t TARGET_WIDTH = 1024;
    const size_t TARGET_HEIGHT = 1024;
    const DXGI_FORMAT TARGET_FORMAT = DXGI_FORMAT_BC2_UNORM;

    // Фильтр ресайза (0x300000 = Cubic)
    const static uint32_t RESIZE_FILTER = 0x300000;
};