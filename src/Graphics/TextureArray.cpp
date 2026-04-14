//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// TextureArray.cpp
// ================================================================================
#include "TextureArray.h"
#include "../Core/Logger.h"
#include "../Core/ResourceManager.h"
#include <filesystem>
#include <DDSTextureLoader.h>
#include <algorithm>

namespace fs = std::filesystem;

TextureArray::TextureArray(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
}

bool TextureArray::Initialize(const std::vector<std::string>& filenames, int targetW, int targetH) {
    if (filenames.empty()) return false;

    D3D11_TEXTURE2D_DESC arrayDesc = {};
    arrayDesc.Width = (targetW > 0) ? targetW : DEFAULT_RESOLUTION;
    arrayDesc.Height = (targetH > 0) ? targetH : DEFAULT_RESOLUTION;
    arrayDesc.MipLevels = 1;
    arrayDesc.ArraySize = static_cast<UINT>(filenames.size());
    arrayDesc.Format = DXGI_FORMAT_BC2_UNORM;
    arrayDesc.SampleDesc.Count = 1;
    arrayDesc.Usage = D3D11_USAGE_DEFAULT;
    arrayDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    arrayDesc.CPUAccessFlags = 0;
    arrayDesc.MiscFlags = 0;

    std::vector<ComPtr<ID3D11Texture2D>> sourceTextures(filenames.size());
    bool formatFound = false;

    // ПЕРВАЯ ПОПЫТКА ЗАГРУЗКИ СЛОЕВ
    for (size_t i = 0; i < filenames.size(); ++i) {
        if (filenames[i].empty()) continue;

        ID3D11ShaderResourceView* srv = ResourceManager::Get().GetOrCacheTexture(filenames[i], arrayDesc.Width, arrayDesc.Height);
        if (srv) {
            ComPtr<ID3D11Resource> res;
            srv->GetResource(&res);

            ComPtr<ID3D11Texture2D> tex;
            if (SUCCEEDED(res.As(&tex))) {
                D3D11_TEXTURE2D_DESC desc;
                tex->GetDesc(&desc);

                // Захватываем формат и мип-уровни от первой успешной текстуры правильного размера
                if (!formatFound && desc.Width == arrayDesc.Width && desc.Height == arrayDesc.Height) {
                    arrayDesc.Format = desc.Format;
                    arrayDesc.MipLevels = desc.MipLevels;
                    formatFound = true;
                }
                sourceTextures[i] = tex;
            }
        }
    }

    HRESULT hr = m_device->CreateTexture2D(&arrayDesc, nullptr, m_arrayTexture.GetAddressOf());
    if (FAILED(hr)) {
        GAMMA_LOG_ERROR(LogCategory::Texture, "Failed to create Texture2DArray!");
        return false;
    }

    // 2. ЗАПОЛНЕНИЕ МАССИВА И СПАСАТЕЛЬНЫЙ КРУГ
    for (UINT i = 0; i < sourceTextures.size(); ++i) {
        ID3D11Texture2D* src = sourceTextures[i].Get();

        if (src) {
            D3D11_TEXTURE2D_DESC srcDesc;
            src->GetDesc(&srcDesc);

            // FIXME: Операция Спасения (Rescue Resize).
            // Если текстура не совпадает по размеру, мы заставляем ResourceManager сделать ресайз на лету.
            // Это очень медленная операция для основного потока. В идеале ассеты должны быть подготовлены заранее.
            if (srcDesc.Width != arrayDesc.Width || srcDesc.Height != arrayDesc.Height) {
                GAMMA_LOG_WARN(LogCategory::Texture, "Texture mismatch at slot " + std::to_string(i) + ". Forcing rescue resize...");

                ID3D11ShaderResourceView* rescuedSrv = ResourceManager::Get().GetOrCacheTexture(filenames[i], arrayDesc.Width, arrayDesc.Height);
                if (rescuedSrv) {
                    ComPtr<ID3D11Resource> res;
                    rescuedSrv->GetResource(&res);

                    ComPtr<ID3D11Texture2D> rescuedTex;
                    if (SUCCEEDED(res.As(&rescuedTex))) {
                        rescuedTex->GetDesc(&srcDesc);
                        if (srcDesc.Width == arrayDesc.Width && srcDesc.Height == arrayDesc.Height) {
                            src = rescuedTex.Get();
                        }
                    }
                }
            }

            // Копируем текстуру в слой массива (со всеми мип-уровнями)
            if (srcDesc.Width == arrayDesc.Width && srcDesc.Height == arrayDesc.Height && srcDesc.Format == arrayDesc.Format) {
                UINT mipsToCopy = std::min(arrayDesc.MipLevels, srcDesc.MipLevels);
                for (UINT mip = 0; mip < mipsToCopy; ++mip) {
                    m_context->CopySubresourceRegion(
                        m_arrayTexture.Get(), D3D11CalcSubresource(mip, i, arrayDesc.MipLevels),
                        0, 0, 0, src, mip, nullptr
                    );
                }
            }
            else {
                GAMMA_LOG_ERROR(LogCategory::Texture, "Rescue failed for: " + filenames[i] + ". Using empty layer.");
            }
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

    return SUCCEEDED(hr);
}

bool TextureArray::LoadFromBakedFile(const std::string& filePath) {
    std::wstring wPath(filePath.begin(), filePath.end());

    // Освобождаем старые ресурсы
    m_arrayTexture.Reset();
    m_srv.Reset();

    // Загружаем готовый массив (со всеми мипами и слоями) за ОДИН вызов напрямую в VRAM
    ID3D11Resource* res = nullptr;
    HRESULT hr = DirectX::CreateDDSTextureFromFile(m_device.Get(), wPath.c_str(), &res, m_srv.GetAddressOf());

    if (SUCCEEDED(hr) && res) {
        res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(m_arrayTexture.GetAddressOf()));
        res->Release();
        return true;
    }

    GAMMA_LOG_ERROR(LogCategory::Texture, "Failed to load baked array: " + filePath);
    return false;
}