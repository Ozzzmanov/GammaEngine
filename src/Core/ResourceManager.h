//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// ResourceManager.h
// ================================================================================
#pragma once
#include "Prerequisites.h"
#include <shared_mutex>
#include <unordered_map>
#include "../Graphics/Texture.h"
#include <DirectXTex.h>
#include <atomic>

class ResourceManager {
public:
    static ResourceManager& Get() {
        static ResourceManager instance;
        return instance;
    }

    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    std::shared_ptr<Texture> LoadTextureAsync(const std::string& path);
    ID3D11ShaderResourceView* LoadTextureSync(const std::string& path);

    // true = Загрузка в фоне (высокий FPS, но видно появление текстур)
    // false = Загрузка сразу (фриз игры, но текстуры появляются мгновенно)
    void SetAsyncMode(bool enabled) { m_isAsyncEnabled = enabled; }
    bool IsAsyncMode() const { return m_isAsyncEnabled; }

    // Внутренние методы
    ID3D11ShaderResourceView* GetOrCacheTexture(const std::string& path);
    ID3D11ShaderResourceView* GetTextureRaw(const std::string& path);

private:
    ResourceManager() = default;
    ~ResourceManager() = default;

    void RebuildFileCache();
    bool ResolveTexturePath(const std::string& inputPath, fs::path& outPath);
    bool LoadRawImage(const fs::path& path, DirectX::ScratchImage& outImage);

    // Методы кэширования
    bool IsCacheValid(const fs::path& source, const fs::path& cache);
    ID3D11ShaderResourceView* LoadFromCache(const fs::path& cachePath);
    bool ProcessAndCacheTexture(const fs::path& source, const fs::path& cache);

private:
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textureCache;
    std::unordered_map<std::string, ComPtr<ID3D11ShaderResourceView>> m_memoryCache; // Кэш сырых SRV
    std::unordered_map<std::string, fs::path> m_fileRegistry;


    std::shared_mutex m_mutex;

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    ComPtr<ID3D11ShaderResourceView> m_placeholderSRV;

    std::atomic<bool> m_isAsyncEnabled{ true };

    // --- КОНТРОЛЬ ПАМЯТИ ---
    std::atomic<int> m_pendingUploads{ 0 };
    // FIX ME: Уменьшено с 20 до 5 для снижения пикового потребления RAM Исправить. 
    const int MAX_PENDING_UPLOADS = 5;

    // Параметры для конвертации, все зависит от ассетов. 
    const int TARGET_WIDTH = 1024;
    const int TARGET_HEIGHT = 1024;
    const DXGI_FORMAT TARGET_FORMAT = DXGI_FORMAT_BC2_UNORM;
    const int RESIZE_FILTER = DirectX::TEX_FILTER_LINEAR; // Используем значение из DirectTex
};