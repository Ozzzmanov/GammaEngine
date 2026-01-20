//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
#include "TextureArray.h"
#include "../Core/Logger.h"
#include "../Graphics/DDSTextureLoader.h"
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

TextureArray::TextureArray(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
}

ID3D11Texture2D* TextureArray::CreateSolidTexture(int width, int height, DXGI_FORMAT format, uint32_t color) {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    // Генерируем массив пикселей
    std::vector<uint32_t> pixels(width * height, color);

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = width * sizeof(uint32_t);

    ID3D11Texture2D* tex = nullptr;
    // Используем m_device текущего класса
    HRESULT hr = m_device->CreateTexture2D(&desc, &initData, &tex);

    if (FAILED(hr)) {
        return nullptr;
    }
    return tex;
}

bool TextureArray::Initialize(const std::vector<std::string>& filenames) {
    if (filenames.empty()) return false;

    std::vector<ID3D11Texture2D*> sourceTextures(filenames.size(), nullptr);
    D3D11_TEXTURE2D_DESC targetDesc = {};
    bool hasTarget = false;

    Logger::Info(LogCategory::Texture, "--- TextureArray Init Start (" + std::to_string(filenames.size()) + " textures) ---");

    // Загрузка
    for (size_t i = 0; i < filenames.size(); ++i) {
        fs::path path(filenames[i]);
        if (path.extension() != ".dds") path.replace_extension(".dds");

        std::wstring loadPath = path.wstring();
        // Пытаемся найти файл
        if (!fs::exists(path)) {
            path = "Assets" / path;
            if (fs::exists(path)) loadPath = path.wstring();
            else {
                Logger::Warn("Texture Not Found: " + filenames[i]);
            }
        }

        ComPtr<ID3D11Resource> res;
        // Используем параметры флагов 
        HRESULT hr = DirectX::CreateDDSTextureFromFileEx(
            m_device.Get(), loadPath.c_str(), 0,
            D3D11_USAGE_STAGING, 0,
            D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE,
            0, DirectX::DDS_LOADER_DEFAULT,
            res.GetAddressOf(), nullptr
        );

        if (SUCCEEDED(hr)) {
            ComPtr<ID3D11Texture2D> tex;
            res.As(&tex);
            D3D11_TEXTURE2D_DESC desc;
            tex->GetDesc(&desc);

            if (!hasTarget) {
                // Первая успешная текстура задает стандарт размера
                targetDesc = desc;
                hasTarget = true;
                sourceTextures[i] = tex.Detach();
                Logger::Info(LogCategory::Texture, "  [MASTER] " + filenames[i] + " (" + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + ")");
            }
            else {
                // Проверяем размер
                if (desc.Width != targetDesc.Width || desc.Height != targetDesc.Height) {
                    Logger::Error(LogCategory::Texture, "  [MISMATCH] " + filenames[i]);

                    // Важно: мы НЕ сохраняем эту текстуру, слот останется nullptr
                    sourceTextures[i] = nullptr;
                }
                else {
                    // Все ок
                    sourceTextures[i] = tex.Detach();
                }
            }
        }
        else {
            Logger::Error(LogCategory::Texture, "  [FAIL] Failed to load DDS: " + filenames[i]);
            sourceTextures[i] = nullptr;
        }
    }

    if (!hasTarget) {
        Logger::Error("CRITICAL: No valid textures loaded for array!");

        return false;
    }

    // Создаем массив
    D3D11_TEXTURE2D_DESC arrayDesc = targetDesc;
    arrayDesc.ArraySize = static_cast<UINT>(filenames.size());
    arrayDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    arrayDesc.Usage = D3D11_USAGE_DEFAULT;
    arrayDesc.CPUAccessFlags = 0;
    arrayDesc.MiscFlags = 0;

    HRESULT hr = m_device->CreateTexture2D(&arrayDesc, nullptr, m_arrayTexture.GetAddressOf());
    if (FAILED(hr)) {
        Logger::Error("Failed to create Texture Array Object");
        return false;
    }

    // Создаем Magenta Placeholder (Розовая заглушка) нужного размера
    ID3D11Texture2D* placeholder = CreateSolidTexture(targetDesc.Width, targetDesc.Height, targetDesc.Format, 0xFFFF00FF);

    // Копируем данные
    for (UINT i = 0; i < sourceTextures.size(); ++i) {
        ID3D11Texture2D* src = sourceTextures[i];

        // Если текстура битая или не загрузилась - берем розовую заглушку
        if (!src) src = placeholder;

        if (src) {
            for (UINT mip = 0; mip < targetDesc.MipLevels; ++mip) {
                m_context->CopySubresourceRegion(
                    m_arrayTexture.Get(),
                    D3D11CalcSubresource(mip, i, targetDesc.MipLevels),
                    0, 0, 0,
                    src,
                    mip,
                    nullptr
                );
            }
        }

        // Чистим ресурсы
        if (sourceTextures[i]) sourceTextures[i]->Release();
    }

    if (placeholder) placeholder->Release();

    // SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = arrayDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = arrayDesc.MipLevels;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = arrayDesc.ArraySize;

    hr = m_device->CreateShaderResourceView(m_arrayTexture.Get(), &srvDesc, m_srv.GetAddressOf());

    Logger::Info("--- TextureArray Init Complete ---");
    return SUCCEEDED(hr);
}