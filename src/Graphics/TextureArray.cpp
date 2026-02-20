//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// TextureArray.cpp
// ================================================================================
#include "TextureArray.h"
#include "../Core/Logger.h"
#include "../Graphics/DDSTextureLoader.h"
#include <filesystem>
#include <vector>
#include "../Core/ResourceManager.h"

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

    // Вектор для исходных текстур
    std::vector<ComPtr<ID3D11Texture2D>> sourceTextures(filenames.size());
    D3D11_TEXTURE2D_DESC targetDesc = {};
    bool hasTarget = false;

    Logger::Info(LogCategory::Texture, "--- TextureArray Init Start (" + std::to_string(filenames.size()) + " textures) ---");

    for (size_t i = 0; i < filenames.size(); ++i) {
        // Используем ResourceManager, который сделает Resize и компрессию в кэш.
        ID3D11ShaderResourceView* srv = ResourceManager::Get().GetOrCacheTexture(filenames[i]);

        if (srv) {
            ComPtr<ID3D11Resource> res;
            srv->GetResource(&res);

            ComPtr<ID3D11Texture2D> tex;
            if (SUCCEEDED(res.As(&tex))) {
                D3D11_TEXTURE2D_DESC desc;
                tex->GetDesc(&desc);

                if (!hasTarget) {
                    targetDesc = desc;
                    hasTarget = true;
                    Logger::Info(LogCategory::Texture, "  [MASTER] " + filenames[i] + " (" + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + ")");
                    sourceTextures[i] = tex;
                }
                else {
                    if (desc.Width != targetDesc.Width || desc.Height != targetDesc.Height) {
                        Logger::Error(LogCategory::Texture, "  [MISMATCH] " + filenames[i] + " (Expected " + std::to_string(targetDesc.Width) + ", got " + std::to_string(desc.Width) + ")");
                        sourceTextures[i] = nullptr;
                    }
                    else {
                        sourceTextures[i] = tex;
                    }
                }
            }
        }
        else {
            Logger::Error(LogCategory::Texture, "  [FAIL] Failed to load/cache: " + filenames[i]);
            sourceTextures[i] = nullptr;
        }
    }

    if (!hasTarget) {
        Logger::Error(LogCategory::Texture, "CRITICAL: No valid textures loaded for array!");
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
    if (FAILED(hr)) return false;

    // Создаем розовую заглушку
    ComPtr<ID3D11Texture2D> placeholder;
    placeholder.Attach(CreateSolidTexture(targetDesc.Width, targetDesc.Height, targetDesc.Format, 0xFFFF00FF));

    // Копируем данные в массив
    for (UINT i = 0; i < sourceTextures.size(); ++i) {
        ID3D11Texture2D* src = sourceTextures[i].Get();

        // Пропускаем пустые текстуры
        if (!src) {
            Logger::Error(LogCategory::Texture, "TextureArray: Slot " + std::to_string(i) + " is NULL. Skipping.");
            continue;
        }

        D3D11_TEXTURE2D_DESC srcDesc;
        src->GetDesc(&srcDesc);

        // Запрещаем копирование текстур разного размера или формата. FIXME сделать надежную защиту.
        if (srcDesc.Width != targetDesc.Width ||
            srcDesc.Height != targetDesc.Height ||
            srcDesc.Format != targetDesc.Format)
        {
            Logger::Error(LogCategory::Texture, "TextureArray: Mismatch at slot " + std::to_string(i) +
                ". Expected " + std::to_string(targetDesc.Width) + "x" + std::to_string(targetDesc.Height) +
                ", got " + std::to_string(srcDesc.Width) + "x" + std::to_string(srcDesc.Height) + ". SKIPPING COPY!");
            continue;
        }

        // Безопасное копирование
        UINT mipsToCopy = std::min(targetDesc.MipLevels, srcDesc.MipLevels);
        for (UINT mip = 0; mip < mipsToCopy; ++mip) {
            m_context->CopySubresourceRegion(
                m_arrayTexture.Get(), D3D11CalcSubresource(mip, i, targetDesc.MipLevels),
                0, 0, 0, src, mip, nullptr
            );
        }
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = arrayDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = arrayDesc.MipLevels;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = arrayDesc.ArraySize;

    hr = m_device->CreateShaderResourceView(m_arrayTexture.Get(), &srvDesc, m_srv.GetAddressOf());

    Logger::Info(LogCategory::Texture, "--- TextureArray Init Complete ---");
    return SUCCEEDED(hr);
}