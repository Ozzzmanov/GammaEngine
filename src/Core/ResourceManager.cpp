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

#pragma comment(lib, "DirectXTex.lib")

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

    // FIXME: Хардкод папки Cache. Вынести в настройки проекта/движка.
    if (!fs::exists("Cache")) {
        fs::create_directory("Cache");
        Logger::Info(LogCategory::System, "ResourceManager: Created Cache directory.");
    }

    // Создаем пурпурную текстуру-заглушку (Fallback Texture)
    uint32_t placeholderColor = 0xFFFF00FF; // ABGR (Alpha, Blue, Green, Red) -> Magenta
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 1; desc.Height = 1; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = { &placeholderColor, 4, 0 };
    ComPtr<ID3D11Texture2D> tex;
    m_device->CreateTexture2D(&desc, &initData, tex.GetAddressOf());
    m_device->CreateShaderResourceView(tex.Get(), nullptr, m_placeholderSRV.GetAddressOf());

    RebuildFileCache();
}

void ResourceManager::RebuildFileCache() {
    Logger::Info(LogCategory::System, "Building File Registry...");
    m_fileRegistry.clear();

    // FIXME: Хардкод папки Assets.
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

std::shared_ptr<Texture> ResourceManager::LoadTextureAsync(const std::string& rawPath, int targetW, int targetH) {
    fs::path sourcePath;
    std::string cacheKey;

    {
        std::shared_lock<std::shared_mutex> sharedLock(m_mutex);
        if (!ResolveTexturePath(rawPath, sourcePath)) {
            auto dummy = std::make_shared<Texture>(rawPath);
            dummy->SetSRV(m_placeholderSRV.Get());
            dummy->SetLoaded(false);
            return dummy;
        }

        std::string suffix = (targetW > 0 && targetH > 0) ? "_" + std::to_string(targetW) + "x" + std::to_string(targetH) : "";
        cacheKey = sourcePath.string() + suffix;

        if (m_textureCache.find(cacheKey) != m_textureCache.end()) return m_textureCache[cacheKey];
    }

    {
        std::unique_lock<std::shared_mutex> uniqueLock(m_mutex);
        if (m_textureCache.find(cacheKey) != m_textureCache.end()) return m_textureCache[cacheKey];

        if (m_memoryCache.find(cacheKey) != m_memoryCache.end()) {
            auto reusedTexture = std::make_shared<Texture>(rawPath);
            reusedTexture->SetSRV(m_memoryCache[cacheKey].Get());
            reusedTexture->SetLoaded(true);
            m_textureCache[cacheKey] = reusedTexture;
            return reusedTexture;
        }

        auto newTexture = std::make_shared<Texture>(rawPath);
        newTexture->SetSRV(m_placeholderSRV.Get());
        newTexture->SetLoaded(false);
        m_textureCache[cacheKey] = newTexture;
    }

    auto newTexture = m_textureCache[cacheKey];

    std::string suffix = (targetW > 0 && targetH > 0) ? "_" + std::to_string(targetW) + "x" + std::to_string(targetH) : "";
    std::string flatName = SanitizeCacheName(sourcePath.string()) + suffix;
    size_t lastDot = flatName.find_last_of('.');
    if (lastDot != std::string::npos) flatName = flatName.substr(0, lastDot);
    fs::path cachePath = fs::path("Cache") / (flatName + "_bc2.dds");

    if (m_isAsyncEnabled) {
        TaskScheduler::Get().Submit(
            [this, sourcePath, cachePath, targetW, targetH]() {
                CoInitializeEx(nullptr, COINIT_MULTITHREADED);

                // FIXME: Активное ожидание (Spinlock/Busy-Wait). Возможно заменить на std::counting_semaphore в C++20.
                while (m_pendingUploads >= MAX_PENDING_UPLOADS) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                m_pendingUploads++;

                if (!IsCacheValid(sourcePath, cachePath)) {
                    Logger::Info(LogCategory::Texture, "Baking cache: " + sourcePath.filename().string());
                    ProcessAndCacheTexture(sourcePath, cachePath, targetW, targetH);
                }
                m_pendingUploads--;
            },
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
                    std::unique_lock<std::shared_mutex> lock(m_mutex);
                    m_memoryCache[cacheKey] = srv;
                }
            }
        );
    }
    else {
        if (!IsCacheValid(sourcePath, cachePath)) ProcessAndCacheTexture(sourcePath, cachePath, targetW, targetH);

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

ID3D11ShaderResourceView* ResourceManager::LoadTextureSync(const std::string& rawPath, int targetW, int targetH) {
    return GetOrCacheTexture(rawPath, targetW, targetH);
}

ID3D11ShaderResourceView* ResourceManager::GetOrCacheTexture(const std::string& originalPath, int targetW, int targetH) {
    fs::path sourcePath;
    std::string cacheKey;

    {
        std::shared_lock<std::shared_mutex> sharedLock(m_mutex);
        if (!ResolveTexturePath(originalPath, sourcePath)) return nullptr;

        std::string suffix = (targetW > 0 && targetH > 0) ? "_" + std::to_string(targetW) + "x" + std::to_string(targetH) : "";
        cacheKey = sourcePath.string() + suffix;

        if (m_memoryCache.find(cacheKey) != m_memoryCache.end()) {
            return m_memoryCache[cacheKey].Get();
        }
    }

    std::string suffix = (targetW > 0 && targetH > 0) ? "_" + std::to_string(targetW) + "x" + std::to_string(targetH) : "";
    std::string flatName = SanitizeCacheName(sourcePath.string()) + suffix;
    size_t lastDot = flatName.find_last_of('.');
    if (lastDot != std::string::npos) flatName = flatName.substr(0, lastDot);
    fs::path cachePath = fs::path("Cache") / (flatName + "_bc2.dds");

    if (!IsCacheValid(sourcePath, cachePath)) {
        if (!ProcessAndCacheTexture(sourcePath, cachePath, targetW, targetH)) {
            return nullptr;
        }
    }

    auto srv = LoadFromCache(cachePath);
    if (srv) {
        std::unique_lock<std::shared_mutex> uniqueLock(m_mutex);
        if (m_memoryCache.find(cacheKey) == m_memoryCache.end()) {
            m_memoryCache[cacheKey] = srv;
        }
        else {
            srv->Release();
            srv = m_memoryCache[cacheKey].Get();
        }
    }

    return srv;
}

ID3D11ShaderResourceView* ResourceManager::GetTextureRaw(const std::string& originalPath, int targetW, int targetH) {
    return GetOrCacheTexture(originalPath, targetW, targetH);
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

bool ResourceManager::ProcessAndCacheTexture(const fs::path& source, const fs::path& cache, int targetW, int targetH) {
    std::wstring wSource = source.wstring();
    std::string ext = source.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    DirectX::ScratchImage image;
    HRESULT hr = E_FAIL;

    if (ext == ".dds") hr = DirectX::LoadFromDDSFile(wSource.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
    else if (ext == ".tga") hr = DirectX::LoadFromTGAFile(wSource.c_str(), nullptr, image);
    else hr = DirectX::LoadFromWICFile(wSource.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, image);

    if (FAILED(hr)) return false;

    const DirectX::TexMetadata& meta = image.GetMetadata();

    // Smart Copy (если уже правильный размер и формат)
    if (ext == ".dds" && meta.format == COMPRESSION_FORMAT && meta.mipLevels > 1) {
        if (targetW == 0 || targetH == 0 || (meta.width == targetW && meta.height == targetH)) {
            try { fs::copy_file(source, cache, fs::copy_options::overwrite_existing); return true; }
            catch (...) {}
        }
    }

    DirectX::ScratchImage uncompressed;
    if (DirectX::IsCompressed(meta.format)) {
        hr = DirectX::Decompress(image.GetImages(), image.GetImageCount(), meta, DXGI_FORMAT_R8G8B8A8_UNORM, uncompressed);
        if (FAILED(hr)) return false;
    }
    else {
        if (meta.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
            hr = DirectX::Convert(image.GetImages(), image.GetImageCount(), meta, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, uncompressed);
            if (FAILED(hr)) uncompressed = std::move(image);
        }
        else {
            uncompressed = std::move(image);
        }
    }

    // Ресайз
    DirectX::ScratchImage resized;
    if (targetW > 0 && targetH > 0 && (uncompressed.GetMetadata().width != targetW || uncompressed.GetMetadata().height != targetH)) {
        hr = DirectX::Resize(uncompressed.GetImages(), uncompressed.GetImageCount(), uncompressed.GetMetadata(), targetW, targetH, DirectX::TEX_FILTER_LINEAR, resized);
        if (FAILED(hr)) return false;
    }
    else {
        resized = std::move(uncompressed);
    }

    // Генерация MipMap
    DirectX::ScratchImage mipChain;
    hr = DirectX::GenerateMipMaps(resized.GetImages(), resized.GetImageCount(), resized.GetMetadata(),
        DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_SEPARATE_ALPHA, 0, mipChain);
    if (FAILED(hr)) mipChain = std::move(resized);

    // Компрессия
    DirectX::ScratchImage compressed;
    hr = DirectX::Compress(mipChain.GetImages(), mipChain.GetImageCount(), mipChain.GetMetadata(), COMPRESSION_FORMAT, DirectX::TEX_COMPRESS_DEFAULT, 1.0f, compressed);

    if (FAILED(hr)) {
        hr = DirectX::SaveToDDSFile(mipChain.GetImages(), mipChain.GetImageCount(), mipChain.GetMetadata(), DirectX::DDS_FLAGS_NONE, cache.wstring().c_str());
    }
    else {
        hr = DirectX::SaveToDDSFile(compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(), DirectX::DDS_FLAGS_NONE, cache.wstring().c_str());
    }

    return SUCCEEDED(hr);
}