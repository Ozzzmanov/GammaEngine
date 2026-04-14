//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// TerrainArrayManager.cpp
// ================================================================================
#include "TerrainArrayManager.h"
#include "TerrainBaker.h"
#include "../Core/Logger.h"
#include <DDSTextureLoader.h>
#include <cmath>
#include <algorithm>
#include <filesystem>

void TerrainArrayManager::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    m_device = device;
    m_context = context;
}

bool TerrainArrayManager::LoadMegaArrays(const std::string& locationName) {
    std::string cleanName = std::filesystem::path(locationName).filename().string();
    std::wstring base = L"Cache/Terrain/" + std::wstring(cleanName.begin(), cleanName.end());

    // FIXME: Загрузка происходит синхронно в основном потоке.
    // 5 огромных Texture2DArray могут весить 100-500 МБ. Это гарантированно вызовет 
    // подвисание (hitch) при загрузке. Нужно перевести загрузку этих DDS в TaskScheduler.
    auto loadArray = [&](const std::wstring& path, ComPtr<ID3D11Texture2D>& tex, ComPtr<ID3D11ShaderResourceView>& srv) {
        ID3D11Resource* res = nullptr;
        HRESULT hr = DirectX::CreateDDSTextureFromFile(m_device, path.c_str(), &res, srv.ReleaseAndGetAddressOf());
        if (SUCCEEDED(hr) && res) {
            res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)tex.ReleaseAndGetAddressOf());
            res->Release();
            return true;
        }
        return false;
        };

    bool success = true;
    success &= loadArray(base + L"_Heights.dds", m_heightArray, m_heightArraySRV);
    success &= loadArray(base + L"_Holes.dds", m_holeArray, m_holeArraySRV);
    success &= loadArray(base + L"_Indices.dds", m_indexArray, m_indexArraySRV);
    success &= loadArray(base + L"_Weights.dds", m_weightArray, m_weightArraySRV);
    success &= loadArray(base + L"_Normals.dds", m_normalArray, m_normalArraySRV);

    if (success) {
        GAMMA_LOG_INFO(LogCategory::System, "Terrain Mega-Arrays loaded instantly!");
    }
    else {
        GAMMA_LOG_ERROR(LogCategory::Terrain, "Failed to load Terrain Mega-Arrays from disk.");
    }
    return success;
}

void TerrainArrayManager::CreateTextureArray(int width, int height, DXGI_FORMAT format, ComPtr<ID3D11Texture2D>& outTex, ComPtr<ID3D11ShaderResourceView>& outSRV) {
    int mipLevels = 1;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = mipLevels;
    desc.ArraySize = MAX_CHUNKS;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HR_CHECK_VOID(m_device->CreateTexture2D(&desc, nullptr, outTex.GetAddressOf()), "Failed to create Texture2DArray");

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = desc.MipLevels;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = MAX_CHUNKS;

    HR_CHECK_VOID(m_device->CreateShaderResourceView(outTex.Get(), &srvDesc, outSRV.GetAddressOf()), "Failed to create Array SRV");
}

void TerrainArrayManager::CopyToSlice(ID3D11Texture2D* srcTex, ID3D11Texture2D* dstArray, int sliceIndex) {
    if (!srcTex || !dstArray) return;

    D3D11_TEXTURE2D_DESC dstDesc;
    dstArray->GetDesc(&dstDesc);

    UINT dstSubresource = D3D11CalcSubresource(0, sliceIndex, dstDesc.MipLevels);
    m_context->CopySubresourceRegion(dstArray, dstSubresource, 0, 0, 0, srcTex, 0, nullptr);
}