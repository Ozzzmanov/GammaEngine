//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
//=================================================================================
// TerrainBaker.cpp
// ================================================================================

#include "TerrainBaker.h"
#include "../Core/Logger.h"
#include "../Resources/HeightMap.h"
#include "../Resources/stb_image.h"
#include <DirectXTex.h>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

// Вспомогательные структуры
struct ZipEntryInfo {
    const unsigned char* dataPtr = nullptr;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint16_t compressionMethod = 0;
};

#pragma pack(push, 1)
struct BlendHeader {
    uint32_t magic_; uint32_t width_; uint32_t height_; uint32_t bpp_;
    DirectX::XMFLOAT4 uProjection_; DirectX::XMFLOAT4 vProjection_;
    uint32_t version_; uint32_t pad_[3];
};

struct HolesHeader {
    uint32_t magic_; uint32_t width_; uint32_t height_; uint32_t version_;
};
#pragma pack(pop)

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

static size_t FindPattern(const std::vector<char>& data, const std::string& pattern, size_t startOffset) {
    if (startOffset >= data.size()) return std::string::npos;
    auto it = std::search(data.begin() + startOffset, data.end(), pattern.begin(), pattern.end());
    return (it != data.end()) ? std::distance(data.begin(), it) : std::string::npos;
}

static bool GetZipEntryInfo(const std::vector<char>& fileData, size_t nameOffset, ZipEntryInfo& outInfo) {
    if (nameOffset < 30) return false;
    size_t headerStart = nameOffset - 30;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(fileData.data());

    // Сигнатура PK\03\04
    if (p[headerStart] != 0x50 || p[headerStart + 1] != 0x4B || p[headerStart + 2] != 0x03 || p[headerStart + 3] != 0x04) return false;

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

void TerrainBaker::Initialize() {
    if (!fs::exists("Cache/Terrain")) {
        fs::create_directories("Cache/Terrain");
        Logger::Info(LogCategory::System, "Created Cache/Terrain directory for Baking.");
    }
}

std::wstring TerrainBaker::GetCachePath(const std::string& prefix, int gridX, int gridZ, int index) {
    std::wstringstream ss;
    ss << L"Cache/Terrain/" << std::wstring(prefix.begin(), prefix.end()) << L"_" << gridX << L"_" << gridZ;
    if (index >= 0) ss << L"_" << index;

    if (prefix == "M") ss << L".meta";
    else ss << L".dds";

    return ss.str();
}

bool TerrainBaker::IsChunkBaked(int gridX, int gridZ) {
    auto checkFile = [](const std::wstring& path) {
        std::error_code ec;
        return fs::exists(path, ec) && fs::file_size(path, ec) > 0;
        };

    return checkFile(GetCachePath("H", gridX, gridZ)) &&
        checkFile(GetCachePath("O", gridX, gridZ)) &&
        checkFile(GetCachePath("M", gridX, gridZ)) &&
        checkFile(GetCachePath("I", gridX, gridZ)) &&
        checkFile(GetCachePath("W", gridX, gridZ));
}

bool TerrainBaker::BakeChunk(const std::string& cdataFilePath, int gridX, int gridZ) {
    std::ifstream file(cdataFilePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> fileData(fileSize);
    file.read(fileData.data(), fileSize);
    file.close();

    Logger::Info(LogCategory::System, "Baking Smart Terrain Chunk: " + std::to_string(gridX) + "x" + std::to_string(gridZ));

    // ВЫСОТЫ (HEIGHTMAP)
    std::string heightNames[] = { "terrain2/heights1", "terrain2/heights2", "terrain2/heightshmp", "terrain2/heights" };
    const unsigned char* finalDataPtr = nullptr;
    int finalDataLen = 0;
    std::vector<uint8_t> decompressedData;

    for (const auto& name : heightNames) {
        size_t offset = FindPattern(fileData, name, 0);
        if (offset != std::string::npos) {
            ZipEntryInfo info;
            if (GetZipEntryInfo(fileData, offset, info)) {
                if (info.compressionMethod == 8) {
                    decompressedData = DecodeZLib((const char*)info.dataPtr, info.compressedSize);
                    finalDataPtr = decompressedData.data();
                    finalDataLen = (int)decompressedData.size();
                }
                else {
                    finalDataPtr = info.dataPtr;
                    finalDataLen = info.compressedSize;
                }
                break;
            }
        }
    }

    std::vector<float> heights;
    int hw = 0, hh = 0;
    if (finalDataPtr && finalDataLen > 0) {
        HeightMap::LoadPackedHeight(finalDataPtr, finalDataLen, heights, hw, hh, 0.001f);
    }
    if (heights.empty() || hw == 0 || hh == 0) {
        hw = 37; hh = 37;
        heights.resize(hw * hh, 0.0f);
    }

    DirectX::ScratchImage heightImg;
    heightImg.Initialize2D(DXGI_FORMAT_R32_FLOAT, hw, hh, 1, 1);
    memcpy(heightImg.GetPixels(), heights.data(), heights.size() * sizeof(float));

    DirectX::ScratchImage resizedHeight;
    if (SUCCEEDED(DirectX::Resize(heightImg.GetImages(), heightImg.GetImageCount(), heightImg.GetMetadata(), HEIGHTMAP_SIZE, HEIGHTMAP_SIZE, DirectX::TEX_FILTER_DEFAULT, resizedHeight))) {
        DirectX::SaveToDDSFile(resizedHeight.GetImages(), resizedHeight.GetImageCount(), resizedHeight.GetMetadata(), DirectX::DDS_FLAGS_NONE, GetCachePath("H", gridX, gridZ).c_str());
    }
    else {
        DirectX::SaveToDDSFile(heightImg.GetImages(), heightImg.GetImageCount(), heightImg.GetMetadata(), DirectX::DDS_FLAGS_NONE, GetCachePath("H", gridX, gridZ).c_str());
    }


    // ДЫРЫ (HOLEMAP)
    size_t holeOffset = FindPattern(fileData, "terrain2/holes", 0);
    std::vector<uint8_t> holePixels;
    int holeW = 8, holeH = 8;
    bool holeLoaded = false;

    if (holeOffset != std::string::npos) {
        size_t searchStart = holeOffset + 14;
        size_t searchEnd = std::min(fileData.size(), searchStart + 256);
        size_t pizOffset = std::string::npos;
        for (size_t i = searchStart; i < searchEnd - 4; ++i) {
            if (fileData[i] == 'p' && fileData[i + 1] == 'i' && fileData[i + 2] == 'z' && fileData[i + 3] == '!') { pizOffset = i; break; }
        }

        if (pizOffset != std::string::npos) {
            size_t exactZlibOffset = std::string::npos;
            for (size_t k = pizOffset + 4; k < std::min(fileData.size(), pizOffset + 20); ++k) {
                if ((uint8_t)fileData[k] == 0x78) { exactZlibOffset = k; break; }
            }
            if (exactZlibOffset != std::string::npos) {
                std::vector<uint8_t> decHoles = DecodeZLib(fileData.data() + exactZlibOffset, (int)(fileData.size() - exactZlibOffset));
                if (decHoles.size() >= sizeof(HolesHeader)) {
                    const HolesHeader* hh = reinterpret_cast<const HolesHeader*>(decHoles.data());
                    if (((uint8_t*)&hh->magic_)[0] == 0x68) { // 'hol'
                        holeW = hh->width_; holeH = hh->height_;
                        const uint8_t* bitsData = decHoles.data() + sizeof(HolesHeader);
                        uint32_t stride = (holeW / 8) + ((holeW & 3) ? 1 : 0);
                        if (decHoles.size() >= sizeof(HolesHeader) + stride * holeH) {
                            holePixels.resize(holeW * holeH);
                            for (int y = 0; y < holeH; ++y) {
                                for (int x = 0; x < holeW; ++x) {
                                    bool isHole = (bitsData[y * stride + (x / 8)] & (1 << (x & 7))) != 0;
                                    holePixels[y * holeW + x] = isHole ? 0 : 255;
                                }
                            }
                            holeLoaded = true;
                        }
                    }
                }
            }
        }
    }

    if (!holeLoaded || holePixels.empty()) {
        holeW = 8; holeH = 8;
        holePixels.resize(holeW * holeH, 255);
    }

    DirectX::ScratchImage holeImg;
    holeImg.Initialize2D(DXGI_FORMAT_R8_UNORM, holeW, holeH, 1, 1);
    memcpy(holeImg.GetPixels(), holePixels.data(), holePixels.size());

    DirectX::ScratchImage resizedHole;
    if (SUCCEEDED(DirectX::Resize(holeImg.GetImages(), holeImg.GetImageCount(), holeImg.GetMetadata(), HOLEMAP_SIZE, HOLEMAP_SIZE, DirectX::TEX_FILTER_POINT, resizedHole))) {
        DirectX::SaveToDDSFile(resizedHole.GetImages(), resizedHole.GetImageCount(), resizedHole.GetMetadata(), DirectX::DDS_FLAGS_NONE, GetCachePath("O", gridX, gridZ).c_str());
    }
    else {
        DirectX::SaveToDDSFile(holeImg.GetImages(), holeImg.GetImageCount(), holeImg.GetMetadata(), DirectX::DDS_FLAGS_NONE, GetCachePath("O", gridX, gridZ).c_str());
    }

    // ПАРСИНГ И СЖАТИЕ СЛОЕВ (COMPACTION)
    struct ExtractedLayer {
        std::string name;
        DirectX::XMFLOAT4 uProj;
        DirectX::XMFLOAT4 vProj;
        std::vector<uint8_t> pixels;
        int w, h;
    };
    std::vector<ExtractedLayer> validLayers;

    size_t offset = 0;
    while (validLayers.size() < 24) { // ДО 24 СЛОЕВ FIXME можно уменьшить/увеличить если необходимо из конфига.
        offset = FindPattern(fileData, "terrain2/layer", offset);
        if (offset == std::string::npos) break;

        ZipEntryInfo info;
        if (GetZipEntryInfo(fileData, offset, info)) {
            std::vector<uint8_t> buffer = (info.compressionMethod == 8) ? DecodeZLib((const char*)info.dataPtr, info.compressedSize) : std::vector<uint8_t>(info.dataPtr, info.dataPtr + info.compressedSize);

            if (buffer.size() > sizeof(BlendHeader)) {
                const BlendHeader* h = reinterpret_cast<const BlendHeader*>(buffer.data());
                const char* ptr = (const char*)buffer.data() + sizeof(BlendHeader);
                size_t remaining = buffer.size() - sizeof(BlendHeader);

                if (remaining >= 4) {
                    uint32_t nameLen = *reinterpret_cast<const uint32_t*>(ptr);
                    ptr += 4; remaining -= 4;

                    if (nameLen > 0 && nameLen <= 1024 && remaining >= nameLen) {
                        std::string texName(ptr, nameLen);
                        texName.erase(std::remove(texName.begin(), texName.end(), '\0'), texName.end());
                        ptr += nameLen; remaining -= nameLen;

                        ExtractedLayer layer;
                        layer.name = texName;
                        layer.uProj = h->uProjection_;
                        layer.vProj = h->vProjection_;
                        layer.w = h->width_;
                        layer.h = h->height_;

                        if (h->version_ == 2) {
                            int imgW, imgH, ch;
                            unsigned char* stbiPixels = stbi_load_from_memory((const unsigned char*)ptr, (int)remaining, &imgW, &imgH, &ch, 1);
                            if (stbiPixels) {
                                layer.w = imgW; layer.h = imgH;
                                layer.pixels.assign(stbiPixels, stbiPixels + imgW * imgH);
                                stbi_image_free(stbiPixels);
                            }
                        }
                        else {
                            if (remaining >= (size_t)(layer.w * layer.h)) {
                                layer.pixels.assign(ptr, ptr + layer.w * layer.h);
                            }
                        }

                        // Сохраняем непустые слои
                        if (!layer.pixels.empty() &&
                            layer.name.find("darken") == std::string::npos &&
                            layer.name.find("black") == std::string::npos)
                        {
                            validLayers.push_back(layer);
                        }
                    }
                }
            }
        }
        offset += 14;
    }

    // Заглушка, если чанк полностью пустой
    if (validLayers.empty()) {
        ExtractedLayer dummy;
        dummy.name = "Assets/lodTexture.dds";
        dummy.w = BLENDMAP_SIZE; dummy.h = BLENDMAP_SIZE;
        dummy.pixels.resize(BLENDMAP_SIZE * BLENDMAP_SIZE, 255);
        dummy.uProj = { 0.01f, 0, 0, 0 };
        dummy.vProj = { 0, 0, 0.01f, 0 };
        validLayers.push_back(dummy);
    }

    // УПАКОВКА МАСОК (INDEX & WEIGHT MAPS) - PATH A
    DirectX::ScratchImage indexImg;
    indexImg.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, BLENDMAP_SIZE, BLENDMAP_SIZE, 1, 1);

    DirectX::ScratchImage weightImg;
    weightImg.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, BLENDMAP_SIZE, BLENDMAP_SIZE, 1, 1);

    uint8_t* idxDest = indexImg.GetPixels();
    uint8_t* wtDest = weightImg.GetPixels();

    struct LayerWeight {
        int index;
        float weight;
    };

    for (int y = 0; y < BLENDMAP_SIZE; ++y) {
        for (int x = 0; x < BLENDMAP_SIZE; ++x) {
            std::vector<LayerWeight> pixelWeights;
            pixelWeights.reserve(validLayers.size());

            // Собираем веса всех слоев для этого пикселя
            for (int i = 0; i < (int)validLayers.size(); ++i) {
                const auto& l = validLayers[i];
                int lx = std::min((x * l.w) / BLENDMAP_SIZE, l.w - 1);
                int ly = std::min((y * l.h) / BLENDMAP_SIZE, l.h - 1);
                float w = l.pixels[ly * l.w + lx] / 255.0f;

                if (w > 0.005f) { // Отсекаем микро-мусор
                    pixelWeights.push_back({ i, w });
                }
            }

            // Сортируем по убыванию веса (самые сильные текстуры первыми)
            std::sort(pixelWeights.begin(), pixelWeights.end(), [](const LayerWeight& a, const LayerWeight& b) {
                return a.weight > b.weight;
                });

            // Берем топ4 слоя
            int topIndices[4] = { 0, 0, 0, 0 };
            float topWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float weightSum = 0.0f;

            int count = std::min<int>(4, (int)pixelWeights.size());
            for (int i = 0; i < count; ++i) {
                topIndices[i] = pixelWeights[i].index;
                topWeights[i] = pixelWeights[i].weight;
                weightSum += topWeights[i];
            }

            // Нормализуем веса, чтобы в сумме они давали 1.0 (255)
            if (weightSum > 0.001f) {
                for (int i = 0; i < 4; ++i) {
                    topWeights[i] = topWeights[i] / weightSum;
                }
            }
            else {
                topWeights[0] = 1.0f; // Если пусто - рисуем базовый слой (0)
                topIndices[0] = 0;
            }

            // Записываем в пиксели
            int byteIdx = (y * BLENDMAP_SIZE + x) * 4;

            // Индексы слоев (храним в R8G8B8A8, в шейдере умножим на 255)
            idxDest[byteIdx + 0] = (uint8_t)topIndices[0];
            idxDest[byteIdx + 1] = (uint8_t)topIndices[1];
            idxDest[byteIdx + 2] = (uint8_t)topIndices[2];
            idxDest[byteIdx + 3] = (uint8_t)topIndices[3];

            // Веса слоев (0 - 255)
            wtDest[byteIdx + 0] = (uint8_t)(topWeights[0] * 255.0f);
            wtDest[byteIdx + 1] = (uint8_t)(topWeights[1] * 255.0f);
            wtDest[byteIdx + 2] = (uint8_t)(topWeights[2] * 255.0f);
            wtDest[byteIdx + 3] = (uint8_t)(topWeights[3] * 255.0f);
        }
    }

    // Сохраняем две текстуры
    DirectX::SaveToDDSFile(indexImg.GetImages(), indexImg.GetImageCount(), indexImg.GetMetadata(), DirectX::DDS_FLAGS_NONE, GetCachePath("I", gridX, gridZ).c_str());
    DirectX::SaveToDDSFile(weightImg.GetImages(), weightImg.GetImageCount(), weightImg.GetMetadata(), DirectX::DDS_FLAGS_NONE, GetCachePath("W", gridX, gridZ).c_str());

    // СОХРАНЯЕМ МЕТАДАННЫЕ .META
    ChunkMetaHeader header;
    header.magic = 0x54454D54; // 'TMET'
    header.numLayers = (uint32_t)validLayers.size();

    std::wstring metaPath = GetCachePath("M", gridX, gridZ);
    std::ofstream metaFile(metaPath, std::ios::binary);
    if (metaFile.is_open()) {
        metaFile.write((const char*)&header, sizeof(ChunkMetaHeader));
        for (const auto& l : validLayers) {
            LayerMeta lmeta = {};
            strncpy_s(lmeta.textureName, l.name.c_str(), sizeof(lmeta.textureName) - 1);
            lmeta.uProj = l.uProj;
            lmeta.vProj = l.vProj;
            metaFile.write((const char*)&lmeta, sizeof(LayerMeta));
        }
        metaFile.close();
        Logger::Info(LogCategory::System, "Meta file saved for chunk: " + std::to_string(gridX) + "x" + std::to_string(gridZ));
    }
    else {
        Logger::Error(LogCategory::System, "CRITICAL: Failed to WRITE meta file!");
        return false;
    }

    return true;
}