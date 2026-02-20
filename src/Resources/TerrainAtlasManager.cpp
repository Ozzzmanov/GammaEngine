//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// TerrainAtlasManager.cpp
// ================================================================================

// Не используется, возможно в дальнейшем релазиация для очень больших миров.
#include "TerrainAtlasManager.h"
#include "TerrainBaker.h"
#include "../Core/Logger.h"
#include "../Graphics/DDSTextureLoader.h"
#include <cmath>
#include <algorithm>

void TerrainAtlasManager::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    m_device = device;
    m_context = context;

    Logger::Info(LogCategory::System, "Initializing Terrain Virtual Atlases...");

    // Создаем Атлас Высот (64 px * 64 grid = 4096 x 4096)
    CreateAtlasTexture(TerrainBaker::HEIGHTMAP_SIZE, DXGI_FORMAT_R32_FLOAT, m_heightAtlas, m_heightAtlasSRV);

    // Создаем Атлас Дыр (32 px * 64 grid = 2048 x 2048)
    CreateAtlasTexture(TerrainBaker::HOLEMAP_SIZE, DXGI_FORMAT_R8_UNORM, m_holeAtlas, m_holeAtlasSRV);

    // Создаем 6 Атласов Смешивания (128 px * 64 grid = 8192 x 8192)
    for (int i = 0; i < 6; ++i) {
        CreateAtlasTexture(TerrainBaker::BLENDMAP_SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, m_blendAtlas[i], m_blendAtlasSRV[i]);
    }

    Logger::Info(LogCategory::System, "Terrain Atlases created successfully.");
}

void TerrainAtlasManager::CreateAtlasTexture(int tileSize, DXGI_FORMAT format, ComPtr<ID3D11Texture2D>& outTex, ComPtr<ID3D11ShaderResourceView>& outSRV) {
    int atlasSize = tileSize * ATLAS_GRID_SIZE;

    // Считаем мип-уровни только для высот и дыр. Для масок жестко ставим 1.
    int mipLevels = (int)std::floor(std::log2(tileSize)) + 1;
    if (format == DXGI_FORMAT_R8G8B8A8_UNORM) {
        mipLevels = 1;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = atlasSize;
    desc.Height = atlasSize;
    desc.MipLevels = mipLevels;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HR_CHECK_VOID(m_device->CreateTexture2D(&desc, nullptr, outTex.GetAddressOf()), "Failed to create Atlas Texture");
    HR_CHECK_VOID(m_device->CreateShaderResourceView(outTex.Get(), nullptr, outSRV.GetAddressOf()), "Failed to create Atlas SRV");
}

AtlasAllocation TerrainAtlasManager::AllocateTile() {
    // Временный простейший аллокатор (линейный)
    // В будущем тут будет пул освобождаемых ячеек для стриминга
    if (m_nextFreeTileY >= ATLAS_GRID_SIZE) {
        Logger::Error(LogCategory::Terrain, "Atlas is FULL! Need chunk streaming implementation.");
        return AtlasAllocation();
    }

    AtlasAllocation alloc;
    alloc.tileX = m_nextFreeTileX;
    alloc.tileY = m_nextFreeTileY;

    // UV смещение = Индекс тайла * (1.0 / 64)
    alloc.uvScale = 1.0f / (float)ATLAS_GRID_SIZE;
    alloc.uvOffset.x = alloc.tileX * alloc.uvScale;
    alloc.uvOffset.y = alloc.tileY * alloc.uvScale;

    // Сдвигаем указатель
    m_nextFreeTileX++;
    if (m_nextFreeTileX >= ATLAS_GRID_SIZE) {
        m_nextFreeTileX = 0;
        m_nextFreeTileY++;
    }

    return alloc;
}

void TerrainAtlasManager::CopyTextureToAtlas(ID3D11Texture2D* srcTex, ID3D11Texture2D* dstAtlas, int tileX, int tileY, int tileSize, bool isBC) {
    if (!srcTex || !dstAtlas) {
        Logger::Error(LogCategory::Terrain, "CopyTextureToAtlas: Source or Destination Atlas is NULL!");
        return;
    }

    D3D11_TEXTURE2D_DESC srcDesc, dstDesc;
    srcTex->GetDesc(&srcDesc);
    dstAtlas->GetDesc(&dstDesc);

    UINT mips = std::min(srcDesc.MipLevels, dstDesc.MipLevels);

    for (UINT mip = 0; mip < mips; ++mip) {
        UINT mipDiv = 1 << mip;
        UINT mipTileSize = std::max(1u, (UINT)tileSize / mipDiv);

        UINT dstX = tileX * mipTileSize;
        UINT dstY = tileY * mipTileSize;

        if (isBC) {
            if (dstX % 4 != 0 || dstY % 4 != 0 || mipTileSize < 4) {
                break;
            }
        }

        m_context->CopySubresourceRegion(
            dstAtlas, D3D11CalcSubresource(mip, 0, dstDesc.MipLevels),
            dstX, dstY, 0,
            srcTex, D3D11CalcSubresource(mip, 0, srcDesc.MipLevels),
            nullptr
        );
    }
}

bool TerrainAtlasManager::LoadChunkIntoAtlas(int gridX, int gridZ, const AtlasAllocation& alloc) {
    if (!alloc.IsValid()) return false;

    auto loadAndCopy = [&](const std::wstring& path, ID3D11Texture2D* atlas, int tileSize, bool isBC) {
        if (!atlas) return;

        ComPtr<ID3D11Resource> res;
        HRESULT hr = DirectX::CreateDDSTextureFromFile(m_device, path.c_str(), res.GetAddressOf(), nullptr);

        if (SUCCEEDED(hr) && res) {
            ComPtr<ID3D11Texture2D> tex;
            // Проверяем, что ресурс реально является 2D текстурой
            if (SUCCEEDED(res.As(&tex)) && tex) {
                CopyTextureToAtlas(tex.Get(), atlas, alloc.tileX, alloc.tileY, tileSize, isBC);
            }
            else {
                Logger::Warn(LogCategory::Terrain, "Atlas Load Failed (Not a 2D Texture): " + std::string(path.begin(), path.end()));
            }
        }
        else {
            Logger::Warn(LogCategory::Terrain, "Atlas Load Failed (File missing or corrupt): " + std::string(path.begin(), path.end()));
        }
        };

    // Высоты
    loadAndCopy(TerrainBaker::GetCachePath("H", gridX, gridZ), m_heightAtlas.Get(), TerrainBaker::HEIGHTMAP_SIZE, false);

    // Дыры
    loadAndCopy(TerrainBaker::GetCachePath("O", gridX, gridZ), m_holeAtlas.Get(), TerrainBaker::HOLEMAP_SIZE, false);

    // 6 Масок смешивания
    for (int i = 0; i < 6; ++i) {
        loadAndCopy(TerrainBaker::GetCachePath("B", gridX, gridZ, i), m_blendAtlas[i].Get(), TerrainBaker::BLENDMAP_SIZE, false);
    }

    return true;
}