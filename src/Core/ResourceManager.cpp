//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// ResourceManager.cpp
// ================================================================================

#include "ResourceManager.h"
#include "Logger.h"
#include "TaskScheduler.h"
#include <DirectXTex.h> 
#include "../Graphics/DDSTextureLoader.h"
#include <WICTextureLoader.h>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <sstream>

#ifdef _DEBUG
#pragma comment(lib, "DirectXTex.lib")
#else
#pragma comment(lib, "DirectXTex.lib")
#endif

namespace fs = std::filesystem;

static std::string SanitizeCacheName(std::string path) {
    std::replace(path.begin(), path.end(), '/', '_');
    std::replace(path.begin(), path.end(), '\\', '_');
    std::replace(path.begin(), path.end(), ':', '_');
    return path;
}

void ResourceManager::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    m_device = device;
    m_context = context;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)hr;

    if (!fs::exists("Cache")) {
        fs::create_directory("Cache");
        Logger::Info(LogCategory::System, "ResourceManager: Created Cache directory.");
    }

    uint32_t placeholderColor = 0xFF00FFFF;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 1; desc.Height = 1; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initData = { &placeholderColor, 4, 0 };
    ComPtr<ID3D11Texture2D> tex;
    device->CreateTexture2D(&desc, &initData, tex.GetAddressOf());
    device->CreateShaderResourceView(tex.Get(), nullptr, m_placeholderSRV.GetAddressOf());

    RebuildFileCache();
}

void ResourceManager::RebuildFileCache() {
    Logger::Info(LogCategory::System, "Building File Registry...");
    m_fileRegistry.clear();

    if (fs::exists("Assets")) {
        try {
            for (const auto& entry : fs::recursive_directory_iterator("Assets")) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
                    m_fileRegistry[filename] = entry.path();
                }
            }
        }
        catch (...) {}
    }
    Logger::Info(LogCategory::System, "File Registry: " + std::to_string(m_fileRegistry.size()) + " files.");
}

std::shared_ptr<Texture> ResourceManager::LoadTextureAsync(const std::string& rawPath) {
    fs::path sourcePath;
    std::string cacheKey;

    // ЧТЕНИЕ (Много потоков одновременно ---
    {
        std::shared_lock<std::shared_mutex> sharedLock(m_mutex);

        if (!ResolveTexturePath(rawPath, sourcePath)) {
            auto dummy = std::make_shared<Texture>(rawPath);
            dummy->SetSRV(m_placeholderSRV.Get());
            dummy->SetLoaded(false);
            return dummy;
        }

        cacheKey = sourcePath.string();

        if (m_textureCache.find(cacheKey) != m_textureCache.end()) {
            return m_textureCache[cacheKey];
        }
    }

    // ЗАПИСЬ (Double-Checked Locking) ---
    {
        std::unique_lock<std::shared_mutex> uniqueLock(m_mutex);

        // Проверяем еще раз: вдруг другой поток уже добавил текстуру, пока мы ждали unique_lock
        if (m_textureCache.find(cacheKey) != m_textureCache.end()) {
            return m_textureCache[cacheKey];
        }

        // Если текстура была загружена синхронно (например, террейном)
        if (m_memoryCache.find(cacheKey) != m_memoryCache.end()) {
            auto reusedTexture = std::make_shared<Texture>(rawPath);
            reusedTexture->SetSRV(m_memoryCache[cacheKey].Get());
            reusedTexture->SetLoaded(true);
            m_textureCache[cacheKey] = reusedTexture;
            return reusedTexture;
        }

        // Создаем новую заглушку и СРАЗУ кладем в кэш
        auto newTexture = std::make_shared<Texture>(rawPath);
        newTexture->SetSRV(m_placeholderSRV.Get());
        newTexture->SetLoaded(false);
        m_textureCache[cacheKey] = newTexture;
    }

    auto newTexture = m_textureCache[cacheKey];

    // Формируем путь к кэшу
    std::string flatName = SanitizeCacheName(cacheKey);
    size_t lastDot = flatName.find_last_of('.');
    if (lastDot != std::string::npos) flatName = flatName.substr(0, lastDot);
    fs::path cachePath = fs::path("Cache") / (flatName + "_bc3.dds");

    if (m_isAsyncEnabled) {
        TaskScheduler::Get().Submit(
            // ФОНОВЫЙ ПОТОК
            [this, sourcePath, cachePath]() {
                CoInitializeEx(nullptr, COINIT_MULTITHREADED);

                while (m_pendingUploads >= MAX_PENDING_UPLOADS) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                m_pendingUploads++;

                if (!IsCacheValid(sourcePath, cachePath)) {
                    Logger::Info(LogCategory::Texture, "Baking cache: " + sourcePath.filename().string());
                    if (!ProcessAndCacheTexture(sourcePath, cachePath)) {
                        Logger::Error(LogCategory::Texture, "Baking failed: " + sourcePath.string());
                    }
                }
                m_pendingUploads--;
            },
            // ГЛАВНЫЙ ПОТОК (Callback)
            [this, sourcePath, cachePath, cacheKey, newTexture]() {
                ComPtr<ID3D11ShaderResourceView> srv;
                ID3D11Resource* res = nullptr;
                HRESULT hr = E_FAIL;

                if (fs::exists(cachePath)) {
                    hr = DirectX::CreateDDSTextureFromFile(m_device, cachePath.wstring().c_str(), &res, srv.GetAddressOf());
                }
                if (FAILED(hr)) {
                    hr = DirectX::CreateWICTextureFromFile(m_device, sourcePath.wstring().c_str(), &res, srv.GetAddressOf());
                }

                if (SUCCEEDED(hr)) {
                    SAFE_RELEASE(res);

                    newTexture->SetSRV(srv.Get());
                    newTexture->SetLoaded(true);

                    // Синхронизируем сырой кэш под эксклюзивной блокировкой
                    std::unique_lock<std::shared_mutex> lock(m_mutex);
                    m_memoryCache[cacheKey] = srv;
                }
            }
        );
    }
    else {
        // СИНХРОННЫЙ РЕЖИМ (Без блокировок на время запекания!)
        if (!IsCacheValid(sourcePath, cachePath)) {
            Logger::Info(LogCategory::Texture, "Baking cache (SYNC): " + sourcePath.filename().string());
            ProcessAndCacheTexture(sourcePath, cachePath);
        }

        ComPtr<ID3D11ShaderResourceView> srv;
        ID3D11Resource* res = nullptr;
        if (SUCCEEDED(DirectX::CreateDDSTextureFromFile(m_device, cachePath.wstring().c_str(), &res, srv.GetAddressOf()))) {
            SAFE_RELEASE(res);
            newTexture->SetSRV(srv.Get());
            newTexture->SetLoaded(true);

            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_memoryCache[cacheKey] = srv;
        }
    }

    return newTexture;
}

ID3D11ShaderResourceView* ResourceManager::LoadTextureSync(const std::string& rawPath) {
    return GetTextureRaw(rawPath);
}

ID3D11ShaderResourceView* ResourceManager::GetOrCacheTexture(const std::string& originalPath) {
    fs::path sourcePath;
    std::string cacheKey;

    // --- ЧТЕНИЕ ---
    {
        std::shared_lock<std::shared_mutex> sharedLock(m_mutex);
        if (!ResolveTexturePath(originalPath, sourcePath)) return nullptr;

        cacheKey = sourcePath.string();
        if (m_memoryCache.find(cacheKey) != m_memoryCache.end()) {
            return m_memoryCache[cacheKey].Get();
        }
    }

    // --- КОНВЕРТАЦИЯ (Параллельно для разных текстур) ---
    std::string flatName = SanitizeCacheName(cacheKey);
    size_t lastDot = flatName.find_last_of('.');
    if (lastDot != std::string::npos) flatName = flatName.substr(0, lastDot);
    fs::path cachePath = fs::path("Cache") / (flatName + "_bc3.dds");

    if (!IsCacheValid(sourcePath, cachePath)) {
        if (!ProcessAndCacheTexture(sourcePath, cachePath)) {
            return nullptr;
        }
    }

    // --- ЗАПИСЬ ---
    auto srv = LoadFromCache(cachePath);
    if (srv) {
        std::unique_lock<std::shared_mutex> uniqueLock(m_mutex);
        if (m_memoryCache.find(cacheKey) == m_memoryCache.end()) {
            m_memoryCache[cacheKey] = srv;
        }
        else {
            // Другой поток уже загрузил эту текстуру!
            srv->Release();
            srv = m_memoryCache[cacheKey].Get();
        }
    }

    return srv;
}

ID3D11ShaderResourceView* ResourceManager::GetTextureRaw(const std::string& originalPath) {
    return GetOrCacheTexture(originalPath);
}

bool ResourceManager::LoadRawImage(const fs::path& path, DirectX::ScratchImage& outImage) {
    std::wstring wPath = path.wstring();
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    HRESULT hr = E_FAIL;
    if (ext == ".dds") hr = DirectX::LoadFromDDSFile(wPath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, outImage);
    else if (ext == ".tga") hr = DirectX::LoadFromTGAFile(wPath.c_str(), nullptr, outImage);
    else hr = DirectX::LoadFromWICFile(wPath.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, outImage);

    return SUCCEEDED(hr);
}

bool ResourceManager::ResolveTexturePath(const std::string& inputPath, fs::path& outPath) {
    fs::path p(inputPath);

    if (fs::exists(p)) {
        try { outPath = fs::canonical(p); }
        catch (...) { outPath = fs::absolute(p); }
        return true;
    }

    std::string filename = p.filename().string();
    std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);

    auto it = m_fileRegistry.find(filename);
    if (it != m_fileRegistry.end()) {
        outPath = it->second;
        return true;
    }

    if (p.extension() == ".tga") {
        std::string ddsName = p.filename().replace_extension(".dds").string();
        std::transform(ddsName.begin(), ddsName.end(), ddsName.begin(), ::tolower);
        auto itDDS = m_fileRegistry.find(ddsName);
        if (itDDS != m_fileRegistry.end()) {
            outPath = itDDS->second;
            return true;
        }
    }
    return false;
}

bool ResourceManager::IsCacheValid(const fs::path& source, const fs::path& cache) {
    if (!fs::exists(cache)) return false;
    if (!fs::exists(source)) return true;
    return fs::last_write_time(cache) >= fs::last_write_time(source);
}

ID3D11ShaderResourceView* ResourceManager::LoadFromCache(const fs::path& cachePath) {
    ComPtr<ID3D11ShaderResourceView> srv;
    ID3D11Resource* res = nullptr;
    HRESULT hr = DirectX::CreateDDSTextureFromFile(m_device, cachePath.wstring().c_str(), &res, srv.GetAddressOf());
    if (FAILED(hr)) {
        fs::remove(cachePath);
        return nullptr;
    }
    SAFE_RELEASE(res);
    return srv.Detach();
}

bool ResourceManager::ProcessAndCacheTexture(const fs::path& source, const fs::path& cache) {
    std::wstring wSource = source.wstring();
    std::string ext = source.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // SMART COPY
    if (ext == ".dds") {
        DirectX::TexMetadata meta;
        if (SUCCEEDED(DirectX::GetMetadataFromDDSFile(wSource.c_str(), DirectX::DDS_FLAGS_NONE, meta))) {
            bool sizeMatch = (meta.width == TARGET_WIDTH && meta.height == TARGET_HEIGHT);
            bool formatMatch = (meta.format == TARGET_FORMAT);
            if (sizeMatch && formatMatch && meta.mipLevels > 1) {
                try { fs::copy_file(source, cache, fs::copy_options::overwrite_existing); return true; }
                catch (...) {}
            }
        }
    }

    // CONVERSION PIPELINE
    DirectX::ScratchImage image;
    HRESULT hr = E_FAIL;

    if (ext == ".dds") hr = DirectX::LoadFromDDSFile(wSource.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
    else if (ext == ".tga") hr = DirectX::LoadFromTGAFile(wSource.c_str(), nullptr, image);
    else hr = DirectX::LoadFromWICFile(wSource.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, image);

    if (FAILED(hr)) return false;

    DirectX::ScratchImage uncompressed;
    if (DirectX::IsCompressed(image.GetMetadata().format)) {
        hr = DirectX::Decompress(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM, uncompressed);
        if (FAILED(hr)) return false;
    }
    else {
        if (image.GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM) {
            hr = DirectX::Convert(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, uncompressed);
            if (FAILED(hr)) uncompressed = std::move(image);
        }
        else {
            uncompressed = std::move(image);
        }
    }

    DirectX::ScratchImage resized;
    if (uncompressed.GetMetadata().width != TARGET_WIDTH || uncompressed.GetMetadata().height != TARGET_HEIGHT) {
        hr = DirectX::Resize(uncompressed.GetImages(), uncompressed.GetImageCount(), uncompressed.GetMetadata(), TARGET_WIDTH, TARGET_HEIGHT, DirectX::TEX_FILTER_LINEAR, resized);
        if (FAILED(hr)) return false;
    }
    else {
        resized = std::move(uncompressed);
    }

    DirectX::ScratchImage mipChain;
    hr = DirectX::GenerateMipMaps(resized.GetImages(), resized.GetImageCount(), resized.GetMetadata(), DirectX::TEX_FILTER_LINEAR, 0, mipChain);
    if (FAILED(hr)) mipChain = std::move(resized);

    DirectX::ScratchImage compressed;
    hr = DirectX::Compress(mipChain.GetImages(), mipChain.GetImageCount(), mipChain.GetMetadata(), TARGET_FORMAT, DirectX::TEX_COMPRESS_DEFAULT, 1.0f, compressed);
    if (FAILED(hr)) return false;

    hr = DirectX::SaveToDDSFile(compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(), DirectX::DDS_FLAGS_NONE, cache.wstring().c_str());

    return SUCCEEDED(hr);
}