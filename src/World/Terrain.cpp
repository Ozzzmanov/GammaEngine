//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
#include "Terrain.h"
#include "../Resources/HeightMap.h"
#include "../Resources/stb_image.h"
#include "../Core/Logger.h"
#include "../Resources/BwPackedSection.h"

#include <fstream>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>
#include <memory>
#include <cstring>
#include <map>
#include <filesystem>

namespace fs = std::filesystem;

#undef min
#undef max

extern "C" char* stbi_zlib_decode_malloc(const char* buffer, int len, int* outlen);

// Хелпер для ZLib
std::vector<uint8_t> DecodeZLib(const char* data, int len) {
    int outLen = 0;
    char* decoded = stbi_zlib_decode_malloc(data, len, &outLen);

    if (!decoded) {
        // Попытка добавить заголовок, если он был обрезан
        std::vector<char> buffer(len + 2);
        buffer[0] = 0x78;
        buffer[1] = (char)0x9C;
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

// Хелперы для PNG
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

//  Terrain Implementation

Terrain::Terrain(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context)
{
    m_textureArray = std::make_unique<TextureArray>(device, context);

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(LayerConstants);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_device->CreateBuffer(&bd, nullptr, m_layerBuffer.GetAddressOf());
    if (FAILED(hr)) Logger::Error("CRITICAL: Failed to create Terrain Layer Constant Buffer!");
}

size_t Terrain::FindPattern(const std::vector<char>& data, const std::string& pattern, size_t startOffset) {
    if (startOffset >= data.size()) return std::string::npos;
    auto it = std::search(data.begin() + startOffset, data.end(), pattern.begin(), pattern.end());
    return (it != data.end()) ? std::distance(data.begin(), it) : std::string::npos;
}

bool Terrain::TryLoadLayerData(const char* dataPtr, size_t maxLen, TerrainLayer& outLayer) {
    if (maxLen < sizeof(BlendHeader)) return false;

    const uint8_t* m = (const uint8_t*)dataPtr;
    // Проверка сигнатуры "bld"
    if (m[0] != 'b' || m[1] != 'l' || m[2] != 'd') return false;

    const BlendHeader* header = reinterpret_cast<const BlendHeader*>(dataPtr);
    if (header->width_ == 0 || header->width_ > 8192 || header->height_ == 0 || header->height_ > 8192) return false;

    outLayer.width = header->width_;
    outLayer.height = header->height_;
    outLayer.uProj = header->uProjection_;
    outLayer.vProj = header->vProjection_;

    const char* ptr = dataPtr + sizeof(BlendHeader);
    size_t remaining = maxLen - sizeof(BlendHeader);

    // Чтение имени текстуры
    if (remaining < 4) return false;
    uint32_t nameLen = *reinterpret_cast<const uint32_t*>(ptr);
    ptr += 4; remaining -= 4;

    if (nameLen == 0 || nameLen > 1024 || remaining < nameLen) return false;

    outLayer.textureName.assign(ptr, nameLen);
    // Удаляем null-терминаторы
    outLayer.textureName.erase(std::remove(outLayer.textureName.begin(), outLayer.textureName.end(), '\0'), outLayer.textureName.end());

    ptr += nameLen; remaining -= nameLen;

    // Чтение данных смешивания (PNG или RAW)
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

void Terrain::ParseLayers(const std::vector<char>& fileData) {
    m_layers.clear();
    m_layerTextureNames.clear();

    // Map для сортировки слоев, если найдены явные номера
    std::map<int, TerrainLayer> tempLayers;
    int sequentialIndex = 0;

    const std::string sectionName = "terrain2/layer";
    size_t offset = 0;

    Logger::Info("  Scanning for layers (Safe Order Mode)...");

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

            bool isHeader = false;
            bool isPacked = false;

            // Проверка сигнатур
            if (ptr[0] == 'b' && ptr[1] == 'l' && ptr[2] == 'd') isHeader = true;
            else if (*reinterpret_cast<const uint32_t*>(ptr) == PACKED_SECTION_MAGIC) { isHeader = true; isPacked = true; }

            if (isHeader) {
                // Ищем цифру (номер слоя) перед заголовком
                if (s > 0) {
                    std::string gap(fileData.data() + dataStartBase, s);
                    for (char c : gap) {
                        if (c >= '0' && c <= '9') {
                            try {
                                std::string numStr; numStr += c;
                                foundLayerIndex = std::stoi(numStr);
                            }
                            catch (...) {}
                            break;
                        }
                    }
                }

                // Загружаем данные
                if (isPacked) {
                    auto ps = BwPackedSection::Create(ptr, bytesLeft);
                    if (ps) {
                        std::vector<uint8_t> blob = ps->GetBlob();
                        if (!blob.empty()) {
                            if (TryLoadLayerData((const char*)blob.data(), blob.size(), layer)) loaded = true;
                        }
                    }
                }
                else {
                    // RAW
                    if (TryLoadLayerData(ptr, bytesLeft, layer)) loaded = true;
                }
            }

            if (loaded) {
                std::string idxStr = (foundLayerIndex != -1) ? std::to_string(foundLayerIndex) : ("Seq" + std::to_string(sequentialIndex));
                Logger::Info("    Found Layer [" + idxStr + "] at shift " + std::to_string(s));
                break;
            }
        }

        // ZLIB Rescue Fallback
        if (!loaded) {
            for (int s = 0; s < (std::min)((int)remainingBytes, 64); ++s) {
                const char* ptr = fileData.data() + dataStartBase + s;
                // Тут используем локальную DecodeZLib
                std::vector<uint8_t> inflated = DecodeZLib(ptr, static_cast<int>(std::min((size_t)(remainingBytes - s), (size_t)65536)));
                if (!inflated.empty()) {
                    if (TryLoadLayerData((const char*)inflated.data(), inflated.size(), layer)) {
                        loaded = true;
                        Logger::Info("    [ZLIB-Rescue] Layer found at shift " + std::to_string(s));
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

    // Переносим в вектор
    for (auto const& [idx, layer] : tempLayers) {
        m_layers.push_back(layer);
        m_layerTextureNames.push_back(layer.textureName);
    }

    // --- FALLBACK НА LOD ---
    if (m_layers.empty()) {
        std::string lodTexName = "lodTexture.dds";
        size_t pos = FindPattern(fileData, "lodTexture.dds", 0);
        if (pos == std::string::npos) pos = FindPattern(fileData, "terrain2/lodTexture.dds", 0);

        fs::path cdataPath(m_currentCDataPath);
        fs::path folder = cdataPath.parent_path();
        fs::path fullPath = folder / lodTexName;

        if (!fs::exists(fullPath)) {
            if (fs::exists("Assets/" + lodTexName)) fullPath = "Assets/" + lodTexName;
        }

        Logger::Info("  No layers found. Using Fallback LOD: " + fullPath.string());

        TerrainLayer lodLayer;
        lodLayer.textureName = fullPath.string();
        lodLayer.width = 64;
        lodLayer.height = 64;
        lodLayer.blendData.resize(64 * 64, 255);

        m_layers.push_back(lodLayer);
        m_layerTextureNames.push_back(lodLayer.textureName);
    }
}

// Создание карт смешивания
void Terrain::CreateCombinedBlendMap() {
    int w = 128;
    int h = 128;

    if (!m_layers.empty() && m_layers[0].textureName.find("lodTexture.dds") == std::string::npos) {
        w = m_layers[0].width;
        h = m_layers[0].height;
    }
    else if (m_spaceParams.blendMapSize > 0) {
        w = m_spaceParams.blendMapSize;
        h = w;
    }

    std::vector<uint32_t> pixels1(w * h, 0);
    std::vector<uint32_t> pixels2(w * h, 0);

    if (m_layers.empty()) {
        std::fill(pixels1.begin(), pixels1.end(), 0x000000FF);
    }
    else {
        int numLayersToPack = std::min((int)m_layers.size(), 8);

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                uint8_t ch1[4] = { 0, 0, 0, 0 };
                uint8_t ch2[4] = { 0, 0, 0, 0 };

                for (int i = 0; i < numLayersToPack; ++i) {
                    const auto& layer = m_layers[i];
                    if (layer.blendData.empty()) continue;

                    int lx = (x * layer.width) / w;
                    int ly = (y * layer.height) / h;
                    int srcIdx = std::min(ly, layer.height - 1) * layer.width + std::min(lx, layer.width - 1);
                    uint8_t val = layer.blendData[srcIdx];

                    if (i == 0)      ch1[0] = val; // Blue
                    else if (i == 1) ch1[1] = val; // Green
                    else if (i == 2) ch1[2] = val; // Red
                    else if (i == 3) ch1[3] = val; // Alpha
                    else if (i == 4) ch2[2] = val; // Red (Map2)
                    else if (i == 5) ch2[1] = val; // Green (Map2)
                    else if (i == 6) ch2[0] = val; // Blue (Map2)
                    else if (i == 7) ch2[3] = val; // Alpha (Map2)
                }

                pixels1[y * w + x] = (ch1[3] << 24) | (ch1[2] << 16) | (ch1[1] << 8) | ch1[0];
                pixels2[y * w + x] = (ch2[3] << 24) | (ch2[2] << 16) | (ch2[1] << 8) | ch2[0];
            }
        }
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData1 = {};
    initData1.pSysMem = pixels1.data();
    initData1.SysMemPitch = w * 4;
    m_device->CreateTexture2D(&desc, &initData1, m_blendMap1.ReleaseAndGetAddressOf());
    m_device->CreateShaderResourceView(m_blendMap1.Get(), nullptr, m_blendMapView1.ReleaseAndGetAddressOf());

    D3D11_SUBRESOURCE_DATA initData2 = {};
    initData2.pSysMem = pixels2.data();
    initData2.SysMemPitch = w * 4;
    m_device->CreateTexture2D(&desc, &initData2, m_blendMap2.ReleaseAndGetAddressOf());
    m_device->CreateShaderResourceView(m_blendMap2.Get(), nullptr, m_blendMapView2.ReleaseAndGetAddressOf());
}

bool Terrain::Initialize(const std::string& cdataFile, const SpaceParams& spaceParams) {
    Logger::Info("Initializing Terrain: " + cdataFile);
    m_spaceParams = spaceParams;
    m_currentCDataPath = cdataFile;

    std::ifstream file(cdataFile, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Logger::Error("Failed to open .cdata file");
        return false;
    }
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> fileData(fileSize);
    file.read(fileData.data(), fileSize);
    file.close();

    // Парсинг слоев
    ParseLayers(fileData);

    if (!m_layerTextureNames.empty()) {
        if (!m_textureArray->Initialize(m_layerTextureNames)) {
            Logger::Error("Failed to initialize TextureArray!");
        }
    }

    CreateCombinedBlendMap();

    // Высоты
    std::vector<float> heights;
    int wH = 32, dH = 32;

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
            if (HeightMap::LoadPackedHeight(chunkStart, chunkLen, heights, wH, dH, 0.001f)) {
                Logger::Info("  Heights Loaded: " + std::to_string(wH) + "x" + std::to_string(dH));
            }
        }
    }

    if (heights.empty()) {
        wH = 32; dH = 32;
        heights.resize(wH * dH, 0.0f);
    }
    else {
        auto result = std::minmax_element(heights.begin(), heights.end());
        m_minHeight = *result.first;
        m_maxHeight = *result.second;
    }

    // Номали
    std::vector<ColorRGB> normalsRaw;
    int wN = 0, dN = 0;
    size_t nOffset = FindPattern(fileData, "terrain2/normals", 0);
    if (nOffset != std::string::npos) {
        int maxLen;
        const unsigned char* pngData = GetPngData(fileData, nOffset, fileSize, maxLen);
        if (pngData) HeightMap::LoadNormalsFromMemory(pngData, maxLen, normalsRaw, wN, dN);
    }

    // Гемоетрия
    std::vector<SimpleVertex> vertices(wH * dH);
    std::vector<uint32_t> indices;

    const float CHUNK_SIZE = 100.0f;
    float dx = CHUNK_SIZE / (float)(wH > 1 ? wH - 1 : 1);
    float dz = CHUNK_SIZE / (float)(dH > 1 ? dH - 1 : 1);
    float widthOffset = (wH - 1) * dx * 0.5f;
    float depthOffset = (dH - 1) * dz * 0.5f;

    bool hasNormals = !normalsRaw.empty() && (wN == wH) && (dN == dH);

    for (int z = 0; z < dH; ++z) {
        for (int x = 0; x < wH; ++x) {
            SimpleVertex& v = vertices[z * wH + x];
            v.Pos = Vector3(x * dx - widthOffset, heights[z * wH + x], z * dz - depthOffset);
            v.Color = Vector3(1, 1, 1);
            v.Tex = Vector2((float)x / (wH - 1), (float)z / (dH - 1));

            if (hasNormals) {
                ColorRGB rgb = normalsRaw[z * wN + x];
                v.Normal = Vector3((rgb.r / 255.0f) * 2 - 1, (rgb.b / 255.0f) * 2 - 1, (rgb.g / 255.0f) * 2 - 1);
            }
            else {
                v.Normal = Vector3(0, 1, 0);
            }
        }
    }

    for (int z = 0; z < dH - 1; ++z) {
        for (int x = 0; x < wH - 1; ++x) {
            uint32_t bl = z * wH + x;
            uint32_t br = z * wH + (x + 1);
            uint32_t tl = (z + 1) * wH + x;
            uint32_t tr = (z + 1) * wH + (x + 1);

            indices.push_back(bl); indices.push_back(tl); indices.push_back(tr);
            indices.push_back(bl); indices.push_back(tr); indices.push_back(br);
        }
    }
    m_indexCount = (UINT)indices.size();

    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.ByteWidth = sizeof(SimpleVertex) * (UINT)vertices.size();
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vData = { vertices.data(), 0, 0 };
    m_device->CreateBuffer(&vbd, &vData, m_vertexBuffer.GetAddressOf());

    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.ByteWidth = sizeof(uint32_t) * (UINT)indices.size();
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iData = { indices.data(), 0, 0 };
    m_device->CreateBuffer(&ibd, &iData, m_indexBuffer.GetAddressOf());

    return true;
}

void Terrain::Render() {
    ID3D11ShaderResourceView* arraySRV = m_textureArray->GetSRV();
    if (arraySRV) m_context->PSSetShaderResources(0, 1, &arraySRV);

    ID3D11ShaderResourceView* blends[] = { m_blendMapView1.Get(), m_blendMapView2.Get() };
    m_context->PSSetShaderResources(1, 2, blends);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_layerBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        LayerConstants* data = (LayerConstants*)mapped.pData;

        data->TextureIndices[0] = DirectX::XMINT4(0, 0, 0, 0);
        data->TextureIndices[1] = DirectX::XMINT4(0, 0, 0, 0);

        memset(data->UProj, 0, sizeof(data->UProj));
        memset(data->VProj, 0, sizeof(data->VProj));

        int numLayers = (std::min)((int)m_layers.size(), 8);

        for (int i = 0; i < numLayers; ++i) {
            int vecIdx = i / 4;
            int targetComp = 0;

            if (i == 0) targetComp = 2; // z
            else if (i == 1) targetComp = 1; // y
            else if (i == 2) targetComp = 0; // x
            else if (i == 3) targetComp = 3; // w
            else if (i == 4) targetComp = 0; // x (Red)
            else if (i == 5) targetComp = 1; // y (Green)
            else if (i == 6) targetComp = 2; // z (Blue)
            else if (i == 7) targetComp = 3; // w (Alpha)

            int* indicesPtr = (int*)&data->TextureIndices[vecIdx];
            indicesPtr[targetComp] = i;

            if (m_layers[i].textureName.find("lodTexture.dds") != std::string::npos) {
                float scale = 0.01f;
                float offX = -m_spaceParams.startPosition.x * scale;
                float offZ = -m_spaceParams.startPosition.z * scale;
                data->UProj[i] = DirectX::XMFLOAT4(scale, 0, 0, offX);
                data->VProj[i] = DirectX::XMFLOAT4(0, 0, scale, offZ);
            }
            else {
                data->UProj[i] = m_layers[i].uProj;
                data->VProj[i] = m_layers[i].vProj;
            }
        }
        m_context->Unmap(m_layerBuffer.Get(), 0);
    }
    m_context->PSSetConstantBuffers(1, 1, m_layerBuffer.GetAddressOf());

    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->DrawIndexed(m_indexCount, 0, 0);
}