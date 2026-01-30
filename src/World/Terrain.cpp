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

// ================================================================================
// ZIP HELPERS
// ================================================================================

struct ZipEntryInfo {
    const unsigned char* dataPtr = nullptr;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint16_t compressionMethod = 0; // 0 = Store, 8 = Deflate
};

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

static bool GetZipEntryInfo(const std::vector<char>& fileData, size_t nameOffset, ZipEntryInfo& outInfo) {
    if (nameOffset < 30) return false;
    
    size_t headerStart = nameOffset - 30;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(fileData.data());

    // Сигнатура Local File Header: 0x04034b50 (PK\03\04)
    if (p[headerStart] != 0x50 || p[headerStart+1] != 0x4B || 
        p[headerStart+2] != 0x03 || p[headerStart+3] != 0x04) {
        return false;
    }

    outInfo.compressionMethod = *reinterpret_cast<const uint16_t*>(&p[headerStart + 8]);
    outInfo.compressedSize = *reinterpret_cast<const uint32_t*>(&p[headerStart + 18]);
    outInfo.uncompressedSize = *reinterpret_cast<const uint32_t*>(&p[headerStart + 22]);

    uint16_t nameLen = *reinterpret_cast<const uint16_t*>(&p[headerStart + 26]);
    uint16_t extraLen = *reinterpret_cast<const uint16_t*>(&p[headerStart + 28]);

    size_t dataStart = headerStart + 30 + nameLen + extraLen;
    
    if (dataStart + outInfo.compressedSize > fileData.size()) return false;

    outInfo.dataPtr = &p[dataStart];
    return true;
}

// ================================================================================
// TERRAIN CLASS IMPLEMENTATION
// ================================================================================

Terrain::Terrain(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context)
{
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
    const BlendHeader* header = reinterpret_cast<const BlendHeader*>(dataPtr);

    // Проверка magic 'bld\0'
    if (((uint8_t*)&header->magic_)[0] != 'b') return false;

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
        } else {
            outLayer.blendData.resize(outLayer.width * outLayer.height, 0);
        }
    } else {
        if (remaining >= (size_t)(outLayer.width * outLayer.height)) {
            outLayer.blendData.resize(outLayer.width * outLayer.height);
            memcpy(outLayer.blendData.data(), ptr, outLayer.width * outLayer.height);
        } else {
            outLayer.blendData.resize(outLayer.width * outLayer.height, 0);
        }
    }
    return true;
}

void Terrain::LoadHoles(const std::vector<char>& fileData) {
    std::string pattern = "terrain2/holes";
    size_t nameOffset = FindPattern(fileData, pattern, 0);

    std::vector<uint8_t> texturePixels;
    int width = 0;
    int height = 0;
    bool loaded = false;

    const uint8_t MAGIC_H_O_L_0[] = { 0x68, 0x6F, 0x6C, 0x00 };

    if (nameOffset != std::string::npos) {
        size_t searchStart = nameOffset + pattern.length();
        size_t searchEnd = std::min(fileData.size(), searchStart + 256);

        size_t pizOffset = std::string::npos;
        for (size_t i = searchStart; i < searchEnd - 4; ++i) {
            if (fileData[i] == 'p' && fileData[i + 1] == 'i' && fileData[i + 2] == 'z' && fileData[i + 3] == '!') {
                pizOffset = i;
                break;
            }
        }

        if (pizOffset != std::string::npos) {
            size_t zlibStart = pizOffset + 8;

            size_t exactZlibOffset = std::string::npos;
            for (size_t k = pizOffset + 4; k < std::min(fileData.size(), pizOffset + 20); ++k) {
                if ((uint8_t)fileData[k] == 0x78) {
                    exactZlibOffset = k;
                    break;
                }
            }

            if (exactZlibOffset != std::string::npos) {
                int maxCompressedLen = (int)(fileData.size() - exactZlibOffset);
                std::vector<uint8_t> decompressed = DecodeZLib(fileData.data() + exactZlibOffset, maxCompressedLen);

                if (!decompressed.empty() && decompressed.size() >= sizeof(HolesHeader)) {
                    const HolesHeader* header = reinterpret_cast<const HolesHeader*>(decompressed.data());
                    const uint8_t* m = (const uint8_t*)&header->magic_;

                    bool magicMatch = (m[0] == 0x68 && m[1] == 0x6F && m[2] == 0x6C);

                    if (magicMatch) {
                        width = header->width_;
                        height = header->height_;

                        // Данные битмаски идут сразу после заголовка
                        const uint8_t* bitsData = decompressed.data() + sizeof(HolesHeader);

                        uint32_t stride = (header->width_ / 8) + ((header->width_ & 3) ? 1 : 0);

                        if (decompressed.size() >= sizeof(HolesHeader) + stride * height) {
                            texturePixels.resize(width * height);

                            for (int y = 0; y < height; ++y) {
                                for (int x = 0; x < width; ++x) {
                                    int byteIndex = y * stride + (x / 8);
                                    int bitIndex = x & 7;

                                    uint8_t val = bitsData[byteIndex];

                                    bool isHole = (val & (1 << bitIndex)) != 0;

                                    texturePixels[y * width + x] = isHole ? 0 : 255;
                                }
                            }
                            loaded = true;
                            // Logger::Info(LogCategory::Terrain, "Holes Loaded");
                        }
                    }
                    else {
                        // Logger::Warn(LogCategory::Terrain, "Holes found but Magic mismatch: " 
                        //    + std::to_string(m[0]) + " " + std::to_string(m[1]) + " " + std::to_string(m[2]));
                    }
                }
            }
        }
    }

    if (!loaded || texturePixels.empty()) {
        width = 8;
        height = 8;
        texturePixels.resize(width * height, 255); // 255 = Земля
    }

    // Создание текстуры
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = texturePixels.data();
    initData.SysMemPitch = width;

    // Reset ComPtrs
    if (m_holeTexture) m_holeTexture.Reset();
    if (m_holeTextureView) m_holeTextureView.Reset();

    m_device->CreateTexture2D(&desc, &initData, m_holeTexture.GetAddressOf());
    if (m_holeTexture) {
        m_device->CreateShaderResourceView(m_holeTexture.Get(), nullptr, m_holeTextureView.GetAddressOf());
    }
}

void Terrain::ParseLayers(const std::vector<char>& fileData, std::vector<TerrainLayer>& outLayers) {
    outLayers.clear();
    std::map<int, TerrainLayer> tempLayers;
    std::string sectionName = "terrain2/layer";
    size_t offset = 0;

    while (true) {
        offset = FindPattern(fileData, sectionName, offset);
        if (offset == std::string::npos) break;

        ZipEntryInfo info;
        if (GetZipEntryInfo(fileData, offset, info)) {
            int layerIdx = -1;
            if (offset + 14 < fileData.size()) {
                char digit = fileData[offset + 14];
                if (isdigit((unsigned char)digit)) {
                    layerIdx = digit - '0';
                }
            }

            TerrainLayer layer;
            bool loaded = false;
            std::vector<uint8_t> buffer;
            const char* ptr = nullptr;
            size_t len = 0;

            if (info.compressionMethod == 8) {
                buffer = DecodeZLib((const char*)info.dataPtr, info.compressedSize);
                ptr = (const char*)buffer.data();
                len = buffer.size();
            } else {
                ptr = (const char*)info.dataPtr;
                len = info.compressedSize;
            }

            if (ptr && len > 0) {
                if (TryLoadLayerData(ptr, len, layer)) loaded = true;
            }

            if (loaded) {
                if (layerIdx != -1) tempLayers[layerIdx] = layer;
                else tempLayers[(int)tempLayers.size()] = layer;
            }
        }
        offset += sectionName.length();
    }

    for (auto const& [idx, layer] : tempLayers) outLayers.push_back(layer);

    if (outLayers.empty()) {
        TerrainLayer lodLayer;
        lodLayer.textureName = "Assets/lodTexture.dds";
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
                channels[i / 4][i % 4] = layer.blendData[srcIdx];
            }
            for (int m = 0; m < 6; ++m) {
                pixels[m][y * w + x] = (channels[m][3] << 24) | (channels[m][2] << 16) | (channels[m][1] << 8) | (channels[m][0]);
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
            int targetComp = (localCh == 0) ? 2 : (localCh == 1) ? 1 : (localCh == 2) ? 0 : 3;
            int* indicesPtr = (int*)&data.TextureIndices[vecIdx];
            indicesPtr[targetComp] = texManager->GetTextureIndex(layers[i].textureName);
        }
        if (layers[i].textureName.find("lodTexture.dds") != std::string::npos) {
            float scale = 0.01f;
            float offX = -m_spaceParams.startPosition.x * scale;
            float offZ = -m_spaceParams.startPosition.z * scale;
            data.UProj[i] = DirectX::XMFLOAT4(scale, 0, 0, offX);
            data.VProj[i] = DirectX::XMFLOAT4(0, 0, scale, offZ);
        } else {
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

    // ВЫСОТЫ
    m_heightMap.clear();
    int localW = 37, localH = 37;

    std::string potentialNames[] = {
        "terrain2/heights1",
        "terrain2/heights2",
        "terrain2/heightshmp",
        "terrain2/heights"
    };

    std::vector<uint8_t> decompressedData;
    const unsigned char* finalDataPtr = nullptr;
    int finalDataLen = 0;
    std::string loadedName = "";

    for (const auto& name : potentialNames) {
        size_t offset = 0;
        while (true) {
            offset = FindPattern(fileData, name, offset);
            if (offset == std::string::npos) break;

            ZipEntryInfo info;
            if (GetZipEntryInfo(fileData, offset, info)) {
                if (info.compressionMethod == 8) {
                    decompressedData = DecodeZLib((const char*)info.dataPtr, info.compressedSize);
                    if (!decompressedData.empty()) {
                        finalDataPtr = decompressedData.data();
                        finalDataLen = (int)decompressedData.size();
                        loadedName = name;
                        goto FOUND_HEIGHTS;
                    }
                }
                else if (info.compressionMethod == 0) {
                    finalDataPtr = info.dataPtr;
                    finalDataLen = info.compressedSize;
                    loadedName = name;
                    goto FOUND_HEIGHTS;
                }
            }
            offset += name.length();
        }
    }

FOUND_HEIGHTS:
    if (finalDataPtr && finalDataLen > 0) {
        if (!HeightMap::LoadPackedHeight(finalDataPtr, finalDataLen, m_heightMap, localW, localH, 0.001f)) {
            Logger::Error(LogCategory::Terrain, "Decode failed for " + loadedName);
        }
    }

    if (m_heightMap.empty()) {
        localW = 37; localH = 37;
        m_heightMap.resize(localW * localH, 0.0f);
    } else {
        auto result = std::minmax_element(m_heightMap.begin(), m_heightMap.end());
        m_minHeight = *result.first;
        m_maxHeight = *result.second;
    }

    LoadHoles(fileData);

    m_width = localW;
    m_depth = localH;
    const float CHUNK_SIZE = 100.0f;
    m_spacing = CHUNK_SIZE / (float)(m_width > 1 ? m_width - 1 : 1);

    // НОРМАЛИ
    std::vector<ColorRGB> normalsRaw;
    int wN = 0, dN = 0;
    std::vector<uint8_t> decompNormals;
    const unsigned char* finalNormPtr = nullptr;
    int finalNormLen = 0;

    size_t nOffset = 0;
    while (true) {
        nOffset = FindPattern(fileData, "terrain2/normals", nOffset);
        if (nOffset == std::string::npos) break;

        ZipEntryInfo info;
        if (GetZipEntryInfo(fileData, nOffset, info)) {
            if (info.compressionMethod == 8) {
                decompNormals = DecodeZLib((const char*)info.dataPtr, info.compressedSize);
                if (!decompNormals.empty()) {
                    finalNormPtr = decompNormals.data();
                    finalNormLen = (int)decompNormals.size();
                    break;
                }
            }
            else if (info.compressionMethod == 0) {
                finalNormPtr = info.dataPtr;
                finalNormLen = info.compressedSize;
                break;
            }
        }
        nOffset += 16;
    }

    if (finalNormPtr && finalNormLen > 0) {
        HeightMap::LoadNormalsFromMemory(finalNormPtr, finalNormLen, normalsRaw, wN, dN);
    }
    bool hasNormals = !normalsRaw.empty() && (wN == m_width) && (dN == m_depth);

    // ВЕРШИНЫ
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
            } else {
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
        m_context->PSSetShaderResources(0, (UINT)m_legacyTextures.size(), m_legacyTextures.data());
        m_context->PSSetShaderResources(24, 6, blends);
    }
    else {
        m_context->PSSetShaderResources(1, 6, blends);
    }

    m_context->PSSetConstantBuffers(1, 1, m_layerBuffer.GetAddressOf());

    if (m_holeTextureView) {
        m_context->PSSetShaderResources(30, 1, m_holeTextureView.GetAddressOf());
    }

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