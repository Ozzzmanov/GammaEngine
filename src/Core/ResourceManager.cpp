//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// ResourceManager.cpp
// Реализация менеджера ресурсов.
// ================================================================================

#include "ResourceManager.h"
#include "Logger.h"
#include <DirectXTex.h> 
#include "../Graphics/DDSTextureLoader.h"
#include <WICTextureLoader.h>
#include <algorithm>

#ifdef _DEBUG
#pragma comment(lib, "DirectXTex.lib")
#else
#pragma comment(lib, "DirectXTex.lib")
#endif

// --- Хелперы ---

static std::string SanitizeCacheName(std::string path) {
    std::replace(path.begin(), path.end(), '/', '_');
    std::replace(path.begin(), path.end(), '\\', '_');
    std::replace(path.begin(), path.end(), ':', '_');
    return path;
}

static std::string DXGIFormatToString(DXGI_FORMAT fmt) {
    switch (fmt) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_BC1_UNORM: return "BC1_UNORM";
    case DXGI_FORMAT_BC2_UNORM: return "BC2_UNORM";
    case DXGI_FORMAT_BC3_UNORM: return "BC3_UNORM";
    case DXGI_FORMAT_BC5_UNORM: return "BC5_UNORM";
    default: return "Unknown (" + std::to_string((int)fmt) + ")";
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void ResourceManager::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    m_device = device;
    m_context = context;

    if (!fs::exists("Cache")) {
        fs::create_directory("Cache");
        Logger::Info(LogCategory::System, "ResourceManager: Created Cache directory.");
    }

    // Инициализация COM для WIC (загрузка PNG/JPG)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // S_FALSE означает, что COM уже инициализирован, это нормально
    if (FAILED(hr)) {
        Logger::Warn(LogCategory::System, "CoInitializeEx failed or already initialized.");
    }
}

// ============================================================================
// PATH RESOLUTION (DRY OPTIMIZATION)
// ============================================================================

bool ResourceManager::ResolveTexturePath(const std::string& inputPath, fs::path& outPath) {
    fs::path p(inputPath);

    // Прямая проверка (абсолютный или относительный путь как есть)
    if (fs::exists(p)) {
        outPath = p;
        return true;
    }

    // Проверка внутри папки Assets/
    fs::path assetsPath = "Assets" / p;
    if (fs::exists(assetsPath)) {
        outPath = assetsPath;
        return true;
    }

    // Автоматическая замена .tga -> .dds
    if (p.extension() == ".tga") {
        fs::path ddsPath = p;
        ddsPath.replace_extension(".dds");

        if (fs::exists(ddsPath)) {
            outPath = ddsPath;
            return true;
        }

        fs::path assetsDdsPath = "Assets" / ddsPath;
        if (fs::exists(assetsDdsPath)) {
            outPath = assetsDdsPath;
            return true;
        }
    }

    // Рекурсивный поиск в Assets
    // Используем только если предыдущие быстрые проверки провалились.
    std::string filename = p.filename().string();
    std::string filenameDDS = p.filename().replace_extension(".dds").string();

    try {
        for (const auto& entry : fs::recursive_directory_iterator("Assets")) {
            if (!entry.is_regular_file()) continue;

            std::string entryName = entry.path().filename().string();

            // Ищем совпадение по имени файла
            if (entryName == filename || entryName == filenameDDS) {
                outPath = entry.path();
                return true;
            }
        }
    }
    catch (...) {
        // Ошибки доступа к файлам
        return false;
    }

    return false;
}

// ============================================================================
// PUBLIC API
// ============================================================================

ID3D11ShaderResourceView* ResourceManager::GetOrCacheTexture(const std::string& originalPath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Определяем реальный путь к исходнику
    fs::path sourcePath;
    bool sourceFound = ResolveTexturePath(originalPath, sourcePath);

    // Формируем имя кэша
    std::string flatName = SanitizeCacheName(originalPath);
    // Убираем расширение из имени кэша, чтобы добавить свое
    size_t lastDot = flatName.find_last_of('.');
    if (lastDot != std::string::npos) flatName = flatName.substr(0, lastDot);

    fs::path cachePath = fs::path("Cache") / (flatName + "_bc3.dds");

    // Проверяем RAM кэш (по пути кэш-файла)
    if (m_memoryCache.find(cachePath.string()) != m_memoryCache.end()) {
        return m_memoryCache[cachePath.string()].Get();
    }

    // Проверяем валидность дискового кэша
    if (!IsCacheValid(sourcePath, cachePath)) {
        if (sourceFound) {
            // Конвертируем и сохраняем в кэш
            if (!ProcessAndCacheTexture(sourcePath, cachePath)) {
                Logger::Error(LogCategory::Texture, "Failed to process texture: " + sourcePath.string());
                return nullptr;
            }
        }
        else {
            Logger::Warn(LogCategory::Texture, "Texture NOT FOUND (GetOrCache): " + originalPath);
            return nullptr;
        }
    }

    // Грузим из кэша
    return LoadFromCache(cachePath);
}

ID3D11ShaderResourceView* ResourceManager::GetTextureRaw(const std::string& originalPath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Ищем файл
    fs::path sourcePath;
    if (!ResolveTexturePath(originalPath, sourcePath)) {
        Logger::Warn(LogCategory::Texture, "Texture NOT FOUND (Raw): " + originalPath);
        return nullptr;
    }

    // Проверяем RAM кэш (здесь ключом является сам путь к исходнику)
    std::string key = sourcePath.string();
    if (m_memoryCache.find(key) != m_memoryCache.end()) {
        return m_memoryCache[key].Get();
    }

    // Загружаем напрямую
    ComPtr<ID3D11ShaderResourceView> srv;
    ID3D11Resource* res = nullptr;
    HRESULT hr = E_FAIL;

    // Сначала пробуем как DDS
    hr = DirectX::CreateDDSTextureFromFile(m_device, sourcePath.wstring().c_str(), &res, srv.GetAddressOf());

    // Если не вышло (или это не DDS), пробуем WIC (PNG/JPG/TGA)
    if (FAILED(hr)) {
        hr = DirectX::CreateWICTextureFromFile(m_device, sourcePath.wstring().c_str(), &res, srv.GetAddressOf());
    }

    if (FAILED(hr)) {
        Logger::Error(LogCategory::Texture, "Failed to load raw texture: " + sourcePath.string());
        return nullptr;
    }

    SAFE_RELEASE(res);
    m_memoryCache[key] = srv;
    return srv.Get();
}

// ============================================================================
// INTERNAL LOGIC (CACHE & PROCESS)
// ============================================================================

bool ResourceManager::IsCacheValid(const fs::path& source, const fs::path& cache) {
    if (!fs::exists(cache)) return false;
    // Если исходника нет (например, он был удален или запакован), но кэш есть - верим кэшу
    if (!fs::exists(source)) return true;

    auto timeSource = fs::last_write_time(source);
    auto timeCache = fs::last_write_time(cache);
    return timeCache >= timeSource;
}

ID3D11ShaderResourceView* ResourceManager::LoadFromCache(const fs::path& cachePath) {
    ComPtr<ID3D11ShaderResourceView> srv;
    ID3D11Resource* res = nullptr;

    HRESULT hr = DirectX::CreateDDSTextureFromFile(m_device, cachePath.wstring().c_str(), &res, srv.GetAddressOf());

    if (FAILED(hr)) {
        Logger::Warn(LogCategory::Texture, "Cache corrupted, deleting: " + cachePath.string());
        fs::remove(cachePath);
        return nullptr;
    }

    SAFE_RELEASE(res);
    m_memoryCache[cachePath.string()] = srv;
    return srv.Get();
}

bool ResourceManager::ProcessAndCacheTexture(const fs::path& source, const fs::path& cache) {
    std::wstring wSource = source.wstring();
    std::string ext = source.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // --- SMART FAST-COPY CHECK ---
    // Если исходник уже DDS и имеет правильный формат и размеры, просто копируем его.
    if (ext == ".dds") {
        DirectX::TexMetadata meta;
        if (SUCCEEDED(DirectX::GetMetadataFromDDSFile(wSource.c_str(), DirectX::DDS_FLAGS_NONE, meta))) {
            bool sizeMatch = (meta.width == TARGET_WIDTH && meta.height == TARGET_HEIGHT);
            bool formatMatch = (meta.format == TARGET_FORMAT);
            bool mipsMatch = (meta.mipLevels > 1);

            if (sizeMatch && formatMatch && mipsMatch) {
                try {
                    fs::copy_file(source, cache, fs::copy_options::overwrite_existing);
                    return true;
                }
                catch (...) {}
            }
            else {
                // Логируем конвертацию для отладки
                std::stringstream ss;
                ss << "[CONVERT] " << source.filename().string()
                    << " (" << meta.width << "x" << meta.height << " " << DXGIFormatToString(meta.format) << ")"
                    << " -> (" << TARGET_WIDTH << "x" << TARGET_HEIGHT << " " << DXGIFormatToString(TARGET_FORMAT) << ")";
                Logger::Info(LogCategory::Texture, ss.str());
            }
        }
    }

    // --- FULL CONVERSION PIPELINE ---
    DirectX::ScratchImage image;
    HRESULT hr = E_FAIL;

    // Загрузка
    if (ext == ".dds")      hr = DirectX::LoadFromDDSFile(wSource.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
    else if (ext == ".tga") hr = DirectX::LoadFromTGAFile(wSource.c_str(), nullptr, image);
    else                    hr = DirectX::LoadFromWICFile(wSource.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, image);

    if (FAILED(hr)) return false;

    // Декомпрессия (если исходник сжат)
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

    // Ресайз
    DirectX::ScratchImage resized;
    DirectX::TEX_FILTER_FLAGS filter = static_cast<DirectX::TEX_FILTER_FLAGS>(RESIZE_FILTER);

    if (uncompressed.GetMetadata().width != TARGET_WIDTH || uncompressed.GetMetadata().height != TARGET_HEIGHT) {
        hr = DirectX::Resize(uncompressed.GetImages(), uncompressed.GetImageCount(), uncompressed.GetMetadata(), TARGET_WIDTH, TARGET_HEIGHT, filter, resized);
        if (FAILED(hr)) return false;
    }
    else {
        resized = std::move(uncompressed);
    }

    // Генерация MipMaps
    DirectX::ScratchImage mipChain;
    hr = DirectX::GenerateMipMaps(resized.GetImages(), resized.GetImageCount(), resized.GetMetadata(), filter, 0, mipChain);
    if (FAILED(hr)) mipChain = std::move(resized);

    // Компрессия
    DirectX::ScratchImage compressed;
    hr = DirectX::Compress(mipChain.GetImages(), mipChain.GetImageCount(), mipChain.GetMetadata(), TARGET_FORMAT, DirectX::TEX_COMPRESS_DEFAULT, 1.0f, compressed);
    if (FAILED(hr)) return false;

    // Сохранение
    hr = DirectX::SaveToDDSFile(compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(), DirectX::DDS_FLAGS_NONE, cache.wstring().c_str());

    return SUCCEEDED(hr);
}