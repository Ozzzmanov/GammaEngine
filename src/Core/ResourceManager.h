//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// ResourceManager.h
// Менеджер ресурсов. Отвечает за асинхронную загрузку, кэширование и 
// конвертацию (запекание) текстур на лету.
// ================================================================================
#pragma once
#include "Prerequisites.h"
#include <shared_mutex>
#include <unordered_map>
#include "../Graphics/Texture.h"
#include <DirectXTex.h>
#include <atomic>

/**
 * @class ResourceManager
 * @brief Глобальный кэш и загрузчик ассетов (Singleton).
 */
class ResourceManager {
public:
    static ResourceManager& Get() {
        static ResourceManager instance;
        return instance;
    }

    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    /// @brief Асинхронно загружает текстуру. Возвращает заглушку (placeholder), пока файл грузится.
    std::shared_ptr<Texture> LoadTextureAsync(const std::string& path, int targetW = 0, int targetH = 0);

    /// @brief Синхронно загружает текстуру, блокируя поток до завершения.
    ID3D11ShaderResourceView* LoadTextureSync(const std::string& path, int targetW = 0, int targetH = 0);

    void SetAsyncMode(bool enabled) { m_isAsyncEnabled = enabled; }
    bool IsAsyncMode() const { return m_isAsyncEnabled; }

    bool ResolveTexturePath(const std::string& inputPath, fs::path& outPath);
    bool LoadRawImage(const fs::path& path, DirectX::ScratchImage& outImage);
    bool HasAlpha(const std::string& path);

    ID3D11ShaderResourceView* GetOrCacheTexture(const std::string& path, int targetW = 0, int targetH = 0);
    ID3D11ShaderResourceView* GetTextureRaw(const std::string& path, int targetW = 0, int targetH = 0);

private:
    ResourceManager() = default;
    ~ResourceManager() = default;

    void RebuildFileCache();
    bool IsCacheValid(const fs::path& source, const fs::path& cache);
    ID3D11ShaderResourceView* LoadFromCache(const fs::path& cachePath);
    bool ProcessAndCacheTexture(const fs::path& source, const fs::path& cache, int targetW, int targetH);

private:
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textureCache;
    std::unordered_map<std::string, ComPtr<ID3D11ShaderResourceView>> m_memoryCache;
    std::unordered_map<std::string, fs::path> m_fileRegistry;
    std::unordered_map<std::string, bool> m_alphaCache;

    std::shared_mutex m_mutex;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    ComPtr<ID3D11ShaderResourceView> m_placeholderSRV;

    std::atomic<bool> m_isAsyncEnabled{ true };
    std::atomic<int> m_pendingUploads{ 0 };

    // FIXME: Перенести эти параметры в EngineConfig (Optimization/Streaming)
    const int MAX_PENDING_UPLOADS = 5;
    const DXGI_FORMAT COMPRESSION_FORMAT = DXGI_FORMAT_BC2_UNORM;
};