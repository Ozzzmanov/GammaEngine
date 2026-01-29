//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Terrain.cpp
// ================================================================================

#include "Terrain.h"
#include "../Resources/HeightMap.h"
#include "../Resources/stb_image.h"
#include "../Core/Logger.h"
#include "../Resources/BwPackedSection.h"
#include "../Core/ResourceManager.h"
#include <fstream>
#include <numeric>
#include <algorithm>
#include <map>
#include <filesystem>

namespace fs = std::filesystem;

// --- ZLib Helpers ---
extern "C" char* stbi_zlib_decode_malloc(const char* buffer, int len, int* outlen);

static std::vector<uint8_t> DecodeZLib(const char* data, int len) {
    int outLen = 0;
    char* decoded = stbi_zlib_decode_malloc(data, len, &outLen);
    if (!decoded) {
        std::vector<char> buffer(len + 2);
        buffer[0] = 0x78; buffer[1] = (char)0x9C;
        memcpy(buffer.data() + 2, data, len);
        decoded = stbi_zlib_decode_malloc(buffer.data(), static_cast<int>(buffer.size()), &outLen);
    }
    std::vector<uint8_t> result;
    if (decoded) {
        result.resize(outLen);
        memcpy(result.data(), decoded, outLen);
        stbi_image_free(decoded);
    }
    return result;
}

static const unsigned char* GetPngData(const std::vector<char>& fileData, size_t nameOffset, size_t fileSize, int& outMaxLen) {
    size_t searchEnd = (std::min)(fileSize, nameOffset + 512);
    for (size_t i = nameOffset; i <= searchEnd - 8; ++i) {
        if ((uint8_t)fileData[i] == 0x89 && (uint8_t)fileData[i + 1] == 0x50 && (uint8_t)fileData[i + 2] == 0x4E) {
            outMaxLen = (int)(fileSize - i);
            return reinterpret_cast<const unsigned char*>(&fileData[i]);
        }
    }
    return nullptr;
}

Terrain::Terrain(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context)
{
    // Буфер констант создаем как DEFAULT (обновляем 1 раз)
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(LayerConstants);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;
    m_device->CreateBuffer(&bd, nullptr, m_layerBuffer.GetAddressOf());
}

size_t Terrain::FindPattern(const std::vector<char>& data, const std::string& pattern, size_t startOffset) {
    if (startOffset >= data.size()) return std::string::npos;
    auto it = std::search(data.begin() + startOffset, data.end(), pattern.begin(), pattern.end());
    return (it != data.end()) ? std::distance(data.begin(), it) : std::string::npos;
}

bool Terrain::TryLoadLayerData(const char* dataPtr, size_t maxLen, TerrainLayer& outLayer) {
    if (maxLen < sizeof(BlendHeader)) return false;
    const uint8_t* m = (const uint8_t*)dataPtr;
    if (m[0] != 'b' || m[1] != 'l' || m[2] != 'd') return false;

    const BlendHeader* header = reinterpret_cast<const BlendHeader*>(dataPtr);
    if (header->width_ == 0 || header->width_ > 8192) return false;

    outLayer.width = header->width_;
    outLayer.height = header->height_;
    outLayer.uProj = header->uProjection_;
    outLayer.vProj = header->vProjection_;

    const char* ptr = dataPtr + sizeof(BlendHeader);
    size_t remaining = maxLen - sizeof(BlendHeader);

    if (remaining < 4) return false;
    uint32_t nameLen = *reinterpret_cast<const uint32_t*>(ptr);
    ptr += 4; remaining -= 4;

    if (nameLen == 0 || nameLen > 1024 || remaining < nameLen) return false;
    outLayer.textureName.assign(ptr, nameLen);
    outLayer.textureName.erase(std::remove(outLayer.textureName.begin(), outLayer.textureName.end(), '\0'), outLayer.textureName.end());
    ptr += nameLen; remaining -= nameLen;

    if (header->version_ == 2) {
        int w, h, ch;
        unsigned char* pixels = stbi_load_from_memory((const unsigned char*)ptr, static_cast<int>(remaining), &w, &h, &ch, 1);
        if (pixels) {
            outLayer.blendData.resize(w * h);
            memcpy(outLayer.blendData.data(), pixels, w * h);
            stbi_image_free(pixels);
        }
        else {
            outLayer.blendData.resize(outLayer.width * outLayer.height, 255);
        }
    }
    else {
        if (remaining >= (size_t)(outLayer.width * outLayer.height)) {
            outLayer.blendData.resize(outLayer.width * outLayer.height);
            memcpy(outLayer.blendData.data(), ptr, outLayer.width * outLayer.height);
        }
        else {
            outLayer.blendData.resize(outLayer.width * outLayer.height, 255);
        }
    }
    return true;
}

void Terrain::ParseLayers(const std::vector<char>& fileData, std::vector<TerrainLayer>& outLayers) {
    outLayers.clear();
    std::map<int, TerrainLayer> tempLayers;
    int sequentialIndex = 0;
    const std::string sectionName = "terrain2/layer";
    size_t offset = 0;

    while (true) {
        offset = FindPattern(fileData, sectionName, offset);
        if (offset == std::string::npos) break;

        size_t dataStartBase = offset + sectionName.length();
        size_t remainingBytes = fileData.size() - dataStartBase;

        bool loaded = false;
        TerrainLayer layer;
        int foundLayerIndex = -1;
        int maxScan = (std::min)((int)remainingBytes, 80);

        for (int s = 0; s < maxScan; ++s) {
            const char* ptr = fileData.data() + dataStartBase + s;
            size_t bytesLeft = remainingBytes - s;
            if (bytesLeft < 16) break;

            bool isHeader = (ptr[0] == 'b' && ptr[1] == 'l' && ptr[2] == 'd');
            bool isPacked = (*reinterpret_cast<const uint32_t*>(ptr) == PACKED_SECTION_MAGIC);

            if (isHeader || isPacked) {
                if (s > 0) {
                    for (int k = 1; k <= s; k++) {
                        char c = fileData[dataStartBase + s - k];
                        if (isdigit((unsigned char)c)) {
                            try { foundLayerIndex = c - '0'; }
                            catch (...) {}
                            break;
                        }
                    }
                }

                if (isPacked) {
                    auto ps = BwPackedSection::Create(ptr, bytesLeft);
                    if (ps) {
                        std::vector<uint8_t> blob = ps->GetBlob();
                        if (!blob.empty() && TryLoadLayerData((const char*)blob.data(), blob.size(), layer)) loaded = true;
                    }
                }
                else {
                    if (TryLoadLayerData(ptr, bytesLeft, layer)) loaded = true;
                }
            }
            if (loaded) break;
        }

        if (!loaded) {
            for (int s = 0; s < (std::min)((int)remainingBytes, 64); ++s) {
                const char* ptr = fileData.data() + dataStartBase + s;
                std::vector<uint8_t> inflated = DecodeZLib(ptr, static_cast<int>(std::min((size_t)(remainingBytes - s), (size_t)65536)));
                if (!inflated.empty()) {
                    if (TryLoadLayerData((const char*)inflated.data(), inflated.size(), layer)) {
                        loaded = true;
                        break;
                    }
                }
            }
        }

        if (loaded) {
            if (foundLayerIndex != -1) {
                tempLayers[foundLayerIndex] = layer;
                if (foundLayerIndex >= sequentialIndex) sequentialIndex = foundLayerIndex + 1;
            }
            else {
                tempLayers[sequentialIndex++] = layer;
            }
        }
        offset += 1;
    }

    for (auto const& [idx, layer] : tempLayers) {
        outLayers.push_back(layer);
    }

    if (outLayers.empty()) {
        fs::path fullPath = "Assets/lodTexture.dds";
        TerrainLayer lodLayer;
        lodLayer.textureName = fullPath.string();
        lodLayer.width = 64; lodLayer.height = 64;
        lodLayer.blendData.resize(64 * 64, 255);
        outLayers.push_back(lodLayer);
    }
}

void Terrain::CreateCombinedBlendMap(const std::vector<TerrainLayer>& layers) {
    int w = 128, h = 128;
    if (!layers.empty() && layers[0].textureName.find("lodTexture.dds") == std::string::npos) {
        w = layers[0].width; h = layers[0].height;
    }
    else if (m_spaceParams.blendMapSize > 0) {
        w = m_spaceParams.blendMapSize; h = w;
    }

    std::vector<uint32_t> pixels[6];
    for (int i = 0; i < 6; ++i) pixels[i].resize(w * h, 0);

    int numLayersToPack = std::min((int)layers.size(), 24);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t channels[6][4] = { {0} };

            for (int i = 0; i < numLayersToPack; ++i) {
                const auto& layer = layers[i];
                if (layer.blendData.empty()) continue;

                int lx = (x * layer.width) / w;
                int ly = (y * layer.height) / h;
                int srcIdx = std::min(ly, layer.height - 1) * layer.width + std::min(lx, layer.width - 1);

                int mapIdx = i / 4;
                int chIdx = i % 4;
                channels[mapIdx][chIdx] = layer.blendData[srcIdx];
            }

            for (int m = 0; m < 6; ++m) {
                pixels[m][y * w + x] =
                    (channels[m][3] << 24) | (channels[m][2] << 16) |
                    (channels[m][1] << 8) | (channels[m][0]);
            }
        }
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    for (int i = 0; i < 6; ++i) {
        D3D11_SUBRESOURCE_DATA d = { pixels[i].data(), (UINT)(w * 4), 0 };
        m_device->CreateTexture2D(&desc, &d, m_blendMaps[i].ReleaseAndGetAddressOf());
        m_device->CreateShaderResourceView(m_blendMaps[i].Get(), nullptr, m_blendMapViews[i].ReleaseAndGetAddressOf());
    }
}

void Terrain::UpdateLayerConstants(const std::vector<TerrainLayer>& layers, LevelTextureManager* texManager) {
    LayerConstants data;
    memset(&data, 0, sizeof(LayerConstants));

    int numLayers = (std::min)((int)layers.size(), 24);

    for (int i = 0; i < numLayers; ++i) {
        if (!m_useLegacy && texManager) {
            int vecIdx = i / 4;
            int localCh = i % 4;
            int targetComp = 0;
            if (localCh == 0) targetComp = 2; // B
            else if (localCh == 1) targetComp = 1; // G
            else if (localCh == 2) targetComp = 0; // R
            else if (localCh == 3) targetComp = 3; // A

            int* indicesPtr = (int*)&data.TextureIndices[vecIdx];
            indicesPtr[targetComp] = texManager->GetTextureIndex(layers[i].textureName);
        }

        if (layers[i].textureName.find("lodTexture.dds") != std::string::npos) {
            float scale = 0.01f;
            float offX = -m_spaceParams.startPosition.x * scale;
            float offZ = -m_spaceParams.startPosition.z * scale;
            data.UProj[i] = DirectX::XMFLOAT4(scale, 0, 0, offX);
            data.VProj[i] = DirectX::XMFLOAT4(0, 0, scale, offZ);
        }
        else {
            data.UProj[i] = layers[i].uProj;
            data.VProj[i] = layers[i].vProj;
        }
    }

    m_context->UpdateSubresource(m_layerBuffer.Get(), 0, nullptr, &data, 0, 0);
}

bool Terrain::Initialize(const std::string& cdataFile, const SpaceParams& spaceParams, LevelTextureManager* texManager, bool onlyScan, bool useLegacy) {
    m_spaceParams = spaceParams;
    m_useLegacy = useLegacy;

    std::ifstream file(cdataFile, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Logger::Error(LogCategory::Terrain, "Failed to open cdata: " + cdataFile);
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> fileData(fileSize);
    file.read(fileData.data(), fileSize);
    file.close();

    std::vector<TerrainLayer> layers;
    ParseLayers(fileData, layers);

    if (onlyScan) {
        if (!useLegacy && texManager) {
            for (const auto& l : layers) texManager->RegisterTexture(l.textureName);
        }
        return true;
    }

    if (useLegacy) {
        m_legacyTextures.assign(24, nullptr);

        int loadCount = std::min((int)layers.size(), 24);
        for (int i = 0; i < loadCount; ++i) {
            m_legacyTextures[i] = ResourceManager::Get().GetTextureRaw(layers[i].textureName);
        }
    }

    CreateCombinedBlendMap(layers);
    UpdateLayerConstants(layers, texManager);

    m_heightMap.clear();
    int localW = 32, localH = 32;
    std::string hNames[] = { "terrain2/heightshmp", "terrain2/heights1", "terrain2/heights" };
    size_t hOffset = std::string::npos;
    for (const auto& n : hNames) {
        hOffset = FindPattern(fileData, n, 0);
        if (hOffset != std::string::npos) break;
    }

    if (hOffset != std::string::npos) {
        int maxLen;
        const unsigned char* pngData = GetPngData(fileData, hOffset, fileSize, maxLen);
        if (pngData) {
            const unsigned char* chunkStart = pngData;
            int chunkLen = maxLen;
            if ((pngData - reinterpret_cast<const unsigned char*>(fileData.data())) >= 32) {
                const uint32_t* potentialMagic = reinterpret_cast<const uint32_t*>(pngData - 4);
                if (*potentialMagic == 0x71706e67) {
                    chunkStart = pngData - 32;
                    chunkLen = maxLen + 32;
                }
            }
            HeightMap::LoadPackedHeight(chunkStart, chunkLen, m_heightMap, localW, localH, 0.001f);
        }
    }

    if (m_heightMap.empty()) {
        localW = 32; localH = 32;
        m_heightMap.resize(localW * localH, 0.0f);
    }
    else {
        auto result = std::minmax_element(m_heightMap.begin(), m_heightMap.end());
        m_minHeight = *result.first;
        m_maxHeight = *result.second;
    }

    m_width = localW;
    m_depth = localH;
    const float CHUNK_SIZE = 100.0f;
    m_spacing = CHUNK_SIZE / (float)(m_width > 1 ? m_width - 1 : 1);

    std::vector<ColorRGB> normalsRaw;
    int wN = 0, dN = 0;
    size_t nOffset = FindPattern(fileData, "terrain2/normals", 0);
    if (nOffset != std::string::npos) {
        int maxLen;
        const unsigned char* pngData = GetPngData(fileData, nOffset, fileSize, maxLen);
        if (pngData) HeightMap::LoadNormalsFromMemory(pngData, maxLen, normalsRaw, wN, dN);
    }
    bool hasNormals = !normalsRaw.empty() && (wN == m_width) && (dN == m_depth);

    std::vector<SimpleVertex> vertices(m_width * m_depth);
    std::vector<uint32_t> indices;
    float widthOffset = (m_width - 1) * m_spacing * 0.5f;
    float depthOffset = (m_depth - 1) * m_spacing * 0.5f;

    for (int z = 0; z < (int)m_depth; ++z) {
        for (int x = 0; x < (int)m_width; ++x) {
            SimpleVertex& v = vertices[z * m_width + x];
            float heightY = m_heightMap[z * m_width + x];
            v.Pos = Vector3(x * m_spacing - widthOffset, heightY, z * m_spacing - depthOffset);
            v.Color = Vector3(1, 1, 1);
            v.Tex = Vector2((float)x / (m_width - 1), (float)z / (m_depth - 1));

            if (hasNormals) {
                ColorRGB rgb = normalsRaw[z * wN + x];
                v.Normal = Vector3((rgb.r / 255.0f) * 2 - 1, (rgb.b / 255.0f) * 2 - 1, (rgb.g / 255.0f) * 2 - 1);
            }
            else {
                v.Normal = Vector3(0, 1, 0);
            }
        }
    }

    for (int z = 0; z < (int)m_depth - 1; ++z) {
        for (int x = 0; x < (int)m_width - 1; ++x) {
            uint32_t bl = z * m_width + x;
            uint32_t br = z * m_width + (x + 1);
            uint32_t tl = (z + 1) * m_width + x;
            uint32_t tr = (z + 1) * m_width + (x + 1);
            indices.push_back(bl); indices.push_back(tl); indices.push_back(tr);
            indices.push_back(bl); indices.push_back(tr); indices.push_back(br);
        }
    }
    m_indexCount = (UINT)indices.size();

    D3D11_BUFFER_DESC vbd = { (UINT)(sizeof(SimpleVertex) * vertices.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA vData = { vertices.data(), 0, 0 };
    m_device->CreateBuffer(&vbd, &vData, m_vertexBuffer.ReleaseAndGetAddressOf());

    D3D11_BUFFER_DESC ibd = { (UINT)(sizeof(uint32_t) * indices.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA iData = { indices.data(), 0, 0 };
    m_device->CreateBuffer(&ibd, &iData, m_indexBuffer.ReleaseAndGetAddressOf());

    return true;
}

void Terrain::Render() {
    ID3D11ShaderResourceView* blends[6];
    for (int i = 0; i < 6; ++i) blends[i] = m_blendMapViews[i].Get();

    if (m_useLegacy) {
        m_context->PSSetShaderResources(0, 24, m_legacyTextures.data());
        m_context->PSSetShaderResources(24, 6, blends);
    }
    else {
        m_context->PSSetShaderResources(1, 6, blends);
    }

    m_context->PSSetConstantBuffers(1, 1, m_layerBuffer.GetAddressOf());

    if (m_indexCount > 0) {
        UINT stride = sizeof(SimpleVertex);
        UINT offset = 0;
        m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
        m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->DrawIndexed(m_indexCount, 0, 0);
    }
}

float Terrain::GetHeight(float worldX, float worldZ) const {
    if (m_heightMap.empty() || m_width == 0) return 0.0f;
    float widthOffset = (m_width - 1) * m_spacing * 0.5f;
    float depthOffset = (m_depth - 1) * m_spacing * 0.5f;
    float localX = (worldX - m_position.x) + widthOffset;
    float localZ = (worldZ - m_position.z) + depthOffset;
    float gridX = localX / m_spacing;
    float gridZ = localZ / m_spacing;

    if (gridX < 0 || gridX >= (m_width - 1) || gridZ < 0 || gridZ >= (m_depth - 1)) return 0.0f;

    int x0 = (int)floor(gridX);
    int z0 = (int)floor(gridZ);
    float tx = gridX - x0;
    float tz = gridZ - z0;

    float h00 = m_heightMap[z0 * m_width + x0];
    float h10 = m_heightMap[z0 * m_width + (x0 + 1)];
    float h01 = m_heightMap[(z0 + 1) * m_width + x0];
    float h11 = m_heightMap[(z0 + 1) * m_width + (x0 + 1)];

    return (h00 * (1 - tx) * (1 - tz) + h10 * tx * (1 - tz) + h01 * (1 - tx) * tz + h11 * tx * tz);
}