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

    Logger::Info(LogCategory::System, "Initializing Terrain Texture Arrays (Max 2048 Chunks)...");

    // Высоты
    CreateTextureArray(TerrainBaker::HEIGHTMAP_SIZE, TerrainBaker::HEIGHTMAP_SIZE, DXGI_FORMAT_R32_FLOAT, m_heightArray, m_heightArraySRV);

    // Дыры
    CreateTextureArray(TerrainBaker::HOLEMAP_SIZE, TerrainBaker::HOLEMAP_SIZE, DXGI_FORMAT_R8_UNORM, m_holeArray, m_holeArraySRV);

    // Индексы (UINT формат, так как храним целые числа от 0 до 255)
    // ВАЖНО: Используем UNORM, так как Baker сохранил как R8G8B8A8_UNORM (0.0 - 1.0), 
    // в шейдере мы просто умножим на 255 и скастуем в uint.
    CreateTextureArray(TerrainBaker::BLENDMAP_SIZE, TerrainBaker::BLENDMAP_SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, m_indexArray, m_indexArraySRV);

    // 4. Веса
    CreateTextureArray(TerrainBaker::BLENDMAP_SIZE, TerrainBaker::BLENDMAP_SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, m_weightArray, m_weightArraySRV);

    Logger::Info(LogCategory::System, "Terrain Arrays created successfully.");
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

int TerrainArrayManager::AllocateSlice() {
    if (m_nextFreeSlice >= MAX_CHUNKS) {
        Logger::Error(LogCategory::Terrain, "Terrain Array is FULL! Max 2048 chunks reached.");
        return 0;
    }
    int allocated = m_nextFreeSlice;
    m_nextFreeSlice++;
    return allocated;
}

void TerrainArrayManager::CopyToSlice(ID3D11Texture2D* srcTex, ID3D11Texture2D* dstArray, int sliceIndex) {
    if (!srcTex || !dstArray) return;
    D3D11_TEXTURE2D_DESC dstDesc;
    dstArray->GetDesc(&dstDesc);
    UINT dstSubresource = D3D11CalcSubresource(0, sliceIndex, dstDesc.MipLevels);
    m_context->CopySubresourceRegion(dstArray, dstSubresource, 0, 0, 0, srcTex, 0, nullptr);
}

bool TerrainArrayManager::LoadChunkIntoArray(int gridX, int gridZ, int sliceIndex) {
    if (sliceIndex < 0 || sliceIndex >= MAX_CHUNKS) return false;

    auto loadAndCopy = [&](const std::wstring& path, ID3D11Texture2D* dstArray) {
        if (!dstArray) return;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return;

        ComPtr<ID3D11Resource> res;
        HRESULT hr = DirectX::CreateDDSTextureFromFile(m_device, path.c_str(), res.GetAddressOf(), nullptr);

        if (SUCCEEDED(hr) && res) {
            ComPtr<ID3D11Texture2D> tex;
            if (SUCCEEDED(res.As(&tex)) && tex) {
                CopyToSlice(tex.Get(), dstArray, sliceIndex);
            }
        }
        };

    loadAndCopy(TerrainBaker::GetCachePath("H", gridX, gridZ), m_heightArray.Get());
    loadAndCopy(TerrainBaker::GetCachePath("O", gridX, gridZ), m_holeArray.Get());
    loadAndCopy(TerrainBaker::GetCachePath("I", gridX, gridZ), m_indexArray.Get());
    loadAndCopy(TerrainBaker::GetCachePath("W", gridX, gridZ), m_weightArray.Get());

    return true;
}