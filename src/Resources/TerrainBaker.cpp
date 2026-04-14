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
#include "../Core/TaskScheduler.h"
#include "../World/WorldLoader.h"
#include <DirectXTex.h>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <map>
#include <cmath>
#include <random>    
#include <algorithm> 

namespace fs = std::filesystem;

const float TerrainBaker::AO_RADIUS_METERS = 60.0f;
const float TerrainBaker::AO_INTENSITY = 2.0f;

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

// Импорт функции из zlib (через stb_image)
extern "C" char* stbi_zlib_decode_malloc(const char* buffer, int len, int* outlen);

static std::vector<uint8_t> DecodeZLib(const char* data, int len) {
    int outLen = 0;
    char* decoded = stbi_zlib_decode_malloc(data, len, &outLen);

    // Если сломалось, пробуем добавить zlib header
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

static void SafeCopyImage(const DirectX::Image* dst, const DirectX::Image* src, size_t rowWidthBytes) {
    for (size_t row = 0; row < src->height; ++row) {
        memcpy(dst->pixels + row * dst->rowPitch, src->pixels + row * src->rowPitch, rowWidthBytes);
    }
}

void TerrainBaker::Initialize() {
    if (!fs::exists("Cache/Terrain")) {
        fs::create_directories("Cache/Terrain");
        GAMMA_LOG_INFO(LogCategory::System, "Created Cache/Terrain directory for Baking.");
    }
}

std::string TerrainBaker::GetCachePrefix(const std::string& locationName) {
    std::string cleanName = fs::path(locationName).filename().string();
    return "Cache/Terrain/" + cleanName;
}

bool TerrainBaker::IsLocationBaked(const std::string& locationName, size_t expectedChunkCount) {
    std::string prefix = GetCachePrefix(locationName);

    if (!fs::exists(prefix + "_Heights.dds") ||
        !fs::exists(prefix + "_Normals.dds") ||
        !fs::exists(prefix + "_Terrain.meta")) return false;

    std::ifstream metaFile(prefix + "_Terrain.meta", std::ios::binary);
    if (!metaFile.is_open()) return false;

    uint32_t magic, count;
    metaFile.read((char*)&magic, sizeof(magic));
    metaFile.read((char*)&count, sizeof(count));

    return magic == 0x4D414745 && count == expectedChunkCount;
}

bool TerrainBaker::BakeLocationInMemory(const std::string& folderPath, const std::vector<ChunkBakeTask>& chunks) {
    size_t numChunks = chunks.size();
    if (numChunks == 0 || numChunks > 2048) return false;

    GAMMA_LOG_INFO(LogCategory::System, "Starting TASK SCHEDULER Megabake for " + std::to_string(numChunks) + " chunks...");

    DirectX::ScratchImage megaHeights, megaHoles, megaIndices, megaWeights, megaNormals;
    megaHeights.Initialize2D(DXGI_FORMAT_R32_FLOAT, HEIGHTMAP_SIZE, HEIGHTMAP_SIZE, numChunks, 1);
    megaHoles.Initialize2D(DXGI_FORMAT_R8_UNORM, HOLEMAP_SIZE, HOLEMAP_SIZE, numChunks, 1);
    megaIndices.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, BLENDMAP_SIZE, BLENDMAP_SIZE, numChunks, 1);
    megaWeights.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, BLENDMAP_SIZE, BLENDMAP_SIZE, numChunks, 1);
    megaNormals.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, HEIGHTMAP_SIZE, HEIGHTMAP_SIZE, numChunks, 1);

    std::vector<UnifiedChunkMeta> metaList(numChunks);
    std::vector<float> vGlobalPhysicsHeights(numChunks * HEIGHTMAP_SIZE * HEIGHTMAP_SIZE, 0.0f);

    // СБОР ВЫСОТ И МАТЕРИАЛОВ (ОДИН ПОТОК)
    int minGx = 999999, maxGx = -999999, minGz = 999999, maxGz = -999999;
    for (const auto& c : chunks) {
        if (c.gx < minGx) minGx = c.gx; if (c.gx > maxGx) maxGx = c.gx;
        if (c.gz < minGz) minGz = c.gz; if (c.gz > maxGz) maxGz = c.gz;
    }

    int gridW = maxGx - minGx + 1;
    int gridH = maxGz - minGz + 1;
    std::vector<int> fastGrid(gridW * gridH, -1);

    for (size_t slice = 0; slice < numChunks; ++slice) {
        int gx = chunks[slice].gx;
        int gz = chunks[slice].gz;
        fastGrid[(gz - minGz) * gridW + (gx - minGx)] = (int)slice;

        metaList[slice].GridX = gx;
        metaList[slice].GridZ = gz;
        metaList[slice].SliceIndex = (int)slice;

        std::ifstream file(chunks[slice].cdataPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) continue;
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<char> fileData(fileSize);
        file.read(fileData.data(), fileSize);
        file.close();

        // ЧТЕНИЕ ВЫСОТ
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
            hw = hh = 37; heights.resize(hw * hh, 0.0f);
        }

        DirectX::ScratchImage tempHeight;
        tempHeight.Initialize2D(DXGI_FORMAT_R32_FLOAT, hw, hh, 1, 1);
        memcpy(tempHeight.GetPixels(), heights.data(), heights.size() * sizeof(float));

        DirectX::ScratchImage resizedHeight;
        DirectX::Resize(tempHeight.GetImages(), tempHeight.GetImageCount(), tempHeight.GetMetadata(), HEIGHTMAP_SIZE, HEIGHTMAP_SIZE, DirectX::TEX_FILTER_DEFAULT, resizedHeight);

        const DirectX::Image* srcH = resizedHeight.GetImage(0, 0, 0);
        SafeCopyImage(megaHeights.GetImage(0, slice, 0), srcH, HEIGHTMAP_SIZE * sizeof(float));

        float* pSrcPhys = (float*)srcH->pixels;
        float cMinH = 10000.0f, cMaxH = -10000.0f;
        for (int i = 0; i < HEIGHTMAP_SIZE * HEIGHTMAP_SIZE; ++i) {
            if (pSrcPhys[i] < cMinH) cMinH = pSrcPhys[i];
            if (pSrcPhys[i] > cMaxH) cMaxH = pSrcPhys[i];
            vGlobalPhysicsHeights[slice * HEIGHTMAP_SIZE * HEIGHTMAP_SIZE + i] = pSrcPhys[i];
        }
        metaList[slice].MinHeight = cMinH;
        metaList[slice].MaxHeight = cMaxH;

        // FIXME: Разбор .cdata для масок дыр и материалов очень хрупкий.
        // Хардкод смещений (pizOffset + 4, holeOffset + 14) может сломаться при малейшем изменении формата.

        // ЧТЕНИЕ ДЫР (HOLES)
        size_t holeOffset = FindPattern(fileData, "terrain2/holes", 0);
        std::vector<uint8_t> holePixels;
        int holeW = 8, holeH = 8;
        bool holeLoaded = false;

        if (holeOffset != std::string::npos) {
            size_t searchStart = holeOffset + 14;
            size_t searchEnd = std::min(fileData.size(), searchStart + 256);
            size_t pizOffset = std::string::npos;

            for (size_t i = searchStart; i < searchEnd - 4; ++i) {
                if (fileData[i] == 'p' && fileData[i + 1] == 'i' && fileData[i + 2] == 'z' && fileData[i + 3] == '!') {
                    pizOffset = i;
                    break;
                }
            }

            if (pizOffset != std::string::npos) {
                size_t exactZlibOffset = std::string::npos;
                for (size_t k = pizOffset + 4; k < std::min(fileData.size(), pizOffset + 20); ++k) {
                    if ((uint8_t)fileData[k] == 0x78) {
                        exactZlibOffset = k;
                        break;
                    }
                }

                if (exactZlibOffset != std::string::npos) {
                    std::vector<uint8_t> decHoles = DecodeZLib(fileData.data() + exactZlibOffset, (int)(fileData.size() - exactZlibOffset));
                    if (decHoles.size() >= sizeof(HolesHeader)) {
                        const HolesHeader* hh_hdr = reinterpret_cast<const HolesHeader*>(decHoles.data());
                        if (((uint8_t*)&hh_hdr->magic_)[0] == 0x68) {
                            holeW = hh_hdr->width_;
                            holeH = hh_hdr->height_;
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

        if (!holeLoaded) {
            holeW = 8; holeH = 8; holePixels.assign(64, 255);
        }

        DirectX::ScratchImage tempHole;
        tempHole.Initialize2D(DXGI_FORMAT_R8_UNORM, holeW, holeH, 1, 1);
        for (size_t r = 0; r < (size_t)holeH; ++r) {
            memcpy(tempHole.GetImage(0, 0, 0)->pixels + r * tempHole.GetImage(0, 0, 0)->rowPitch, &holePixels[r * holeW], holeW);
        }

        DirectX::ScratchImage resizedHole;
        DirectX::Resize(tempHole.GetImages(), tempHole.GetImageCount(), tempHole.GetMetadata(), HOLEMAP_SIZE, HOLEMAP_SIZE, DirectX::TEX_FILTER_POINT, resizedHole);
        SafeCopyImage(megaHoles.GetImage(0, slice, 0), resizedHole.GetImage(0, 0, 0), HOLEMAP_SIZE);

        // ЧТЕНИЕ СЛОЕВ (LAYERS)
        struct ExtractedLayer {
            std::string name;
            DirectX::XMFLOAT4 uProj, vProj;
            std::vector<uint8_t> pixels;
            int w, h;
        };

        std::vector<ExtractedLayer> validLayers;
        size_t m_offset = 0;

        while (validLayers.size() < MAX_TERRAIN_LAYERS) {
            m_offset = FindPattern(fileData, "terrain2/layer", m_offset);
            if (m_offset == std::string::npos) break;

            ZipEntryInfo info;
            if (GetZipEntryInfo(fileData, m_offset, info)) {
                std::vector<uint8_t> buffer = (info.compressionMethod == 8)
                    ? DecodeZLib((const char*)info.dataPtr, info.compressedSize)
                    : std::vector<uint8_t>(info.dataPtr, info.dataPtr + info.compressedSize);

                if (buffer.size() > sizeof(BlendHeader)) {
                    const BlendHeader* h = reinterpret_cast<const BlendHeader*>(buffer.data());
                    const char* ptr = (const char*)buffer.data() + sizeof(BlendHeader);
                    size_t remaining = buffer.size() - sizeof(BlendHeader);

                    if (remaining >= 4) {
                        uint32_t nameLen = *reinterpret_cast<const uint32_t*>(ptr);
                        ptr += 4;
                        remaining -= 4;

                        if (nameLen > 0 && nameLen <= 1024 && remaining >= nameLen) {
                            std::string texName(ptr, nameLen);
                            texName.erase(std::remove(texName.begin(), texName.end(), '\0'), texName.end());
                            ptr += nameLen;
                            remaining -= nameLen;

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
                                    layer.w = imgW;
                                    layer.h = imgH;
                                    layer.pixels.assign(stbiPixels, stbiPixels + imgW * imgH);
                                    stbi_image_free(stbiPixels);
                                }
                            }
                            else if (remaining >= (size_t)(layer.w * layer.h)) {
                                layer.pixels.assign(ptr, ptr + layer.w * layer.h);
                            }

                            if (!layer.pixels.empty() && layer.name.find("darken") == std::string::npos) {
                                validLayers.push_back(layer);
                            }
                        }
                    }
                }
            }
            m_offset += 14;
        }

        if (validLayers.empty()) {
            ExtractedLayer d;
            d.name = "Assets/lodTexture.dds";
            d.w = d.h = BLENDMAP_SIZE;
            d.pixels.assign(BLENDMAP_SIZE * BLENDMAP_SIZE, 255);
            d.uProj = { 0.01f, 0, 0, 0 };
            d.vProj = { 0, 0, 0.01f, 0 };
            validLayers.push_back(d);
        }

        metaList[slice].NumLayers = (uint32_t)validLayers.size();
        for (size_t i = 0; i < validLayers.size(); ++i) {
            strncpy_s(metaList[slice].Layers[i].textureName, validLayers[i].name.c_str(), 127);
            metaList[slice].Layers[i].uProj = validLayers[i].uProj;
            metaList[slice].Layers[i].vProj = validLayers[i].vProj;
        }

        const DirectX::Image* dstI = megaIndices.GetImage(0, slice, 0);
        const DirectX::Image* dstW = megaWeights.GetImage(0, slice, 0);

        for (size_t y = 0; y < BLENDMAP_SIZE; ++y) {
            for (size_t x = 0; x < BLENDMAP_SIZE; ++x) {
                struct LW { int i; float w; };
                std::vector<LW> pw;
                pw.reserve(validLayers.size());

                for (int i = 0; i < (int)validLayers.size(); ++i) {
                    const auto& l = validLayers[i];
                    float w = l.pixels[std::min((int)(y * l.h) / BLENDMAP_SIZE, l.h - 1) * l.w + std::min((int)(x * l.w) / BLENDMAP_SIZE, l.w - 1)] / 255.0f;
                    if (w > 0.005f) pw.push_back({ i, w });
                }

                std::sort(pw.begin(), pw.end(), [](const LW& a, const LW& b) { return a.w > b.w; });

                int ti[4] = { 0,0,0,0 };
                float tw[4] = { 0,0,0,0 };
                float sum = 0;
                int cnt = std::min<int>(4, (int)pw.size());

                for (int i = 0; i < cnt; ++i) {
                    ti[i] = pw[i].i;
                    tw[i] = pw[i].w;
                    sum += tw[i];
                }

                if (sum > 0.001f) {
                    for (int i = 0; i < 4; ++i) tw[i] /= sum;
                }
                else {
                    tw[0] = 1.0f; ti[0] = 0;
                }

                uint8_t* pI = dstI->pixels + y * dstI->rowPitch + x * 4;
                uint8_t* pW = dstW->pixels + y * dstW->rowPitch + x * 4;
                pI[0] = (uint8_t)ti[0]; pI[1] = (uint8_t)ti[1]; pI[2] = (uint8_t)ti[2]; pI[3] = (uint8_t)ti[3];
                pW[0] = (uint8_t)(tw[0] * 255.0f); pW[1] = (uint8_t)(tw[1] * 255.0f); pW[2] = (uint8_t)(tw[2] * 255.0f); pW[3] = (uint8_t)(tw[3] * 255.0f);
            }
        }
    }

    // ПАСС 2: МНОГОПОТОЧНЫЙ РАСЧЕТ AO
    GAMMA_LOG_INFO(LogCategory::System, "[Pass 2/2] TaskScheduler Baking Materials & HBAO...");

    float spacing = 100.0f / (float)(HEIGHTMAP_SIZE - 1);

    std::atomic<int> chunksCompleted{ 0 };
    std::mutex bakeMutex;
    std::condition_variable bakeCV;

    for (size_t slice = 0; slice < numChunks; ++slice) {

        TaskScheduler::Get().Submit([slice, numChunks, spacing, minGx, maxGx, minGz, maxGz, gridW, &chunks, &metaList, &vGlobalPhysicsHeights, &fastGrid, &megaNormals, &megaHoles, &megaIndices, &megaWeights, &chunksCompleted, &bakeMutex, &bakeCV]() {

            auto GetHeightAtWorldPos = [&](float wx, float wz, float defaultH) -> float {
                int gx = (int)std::floor(wx / 100.0f);
                int gz = (int)std::floor(wz / 100.0f);
                if (gx < minGx || gx > maxGx || gz < minGz || gz > maxGz) return defaultH;

                int s = fastGrid[(gz - minGz) * gridW + (gx - minGx)];
                if (s == -1) return defaultH;

                float localX = (wx - (float)gx * 100.0f) / spacing;
                float localZ = (wz - (float)gz * 100.0f) / spacing;
                int lx = std::clamp((int)std::floor(localX), 0, HEIGHTMAP_SIZE - 1);
                int lz = std::clamp((int)std::floor(localZ), 0, HEIGHTMAP_SIZE - 1);

                return vGlobalPhysicsHeights[s * HEIGHTMAP_SIZE * HEIGHTMAP_SIZE + lz * HEIGHTMAP_SIZE + lx];
                };

            auto GetHeightBilinear = [&](float wx, float wz, float defaultH) -> float {
                float gridX = wx / spacing;
                float gridZ = wz / spacing;

                float x0f = std::floor(gridX);
                float z0f = std::floor(gridZ);

                float tx = gridX - x0f;
                float tz = gridZ - z0f;

                float h00 = GetHeightAtWorldPos(x0f * spacing, z0f * spacing, defaultH);
                float h10 = GetHeightAtWorldPos((x0f + 1.0f) * spacing, z0f * spacing, defaultH);
                float h01 = GetHeightAtWorldPos(x0f * spacing, (z0f + 1.0f) * spacing, defaultH);
                float h11 = GetHeightAtWorldPos((x0f + 1.0f) * spacing, (z0f + 1.0f) * spacing, defaultH);

                float h0 = h00 + tx * (h10 - h00);
                float h1 = h01 + tx * (h11 - h01);
                return h0 + tz * (h1 - h0);
                };

            auto Hash21 = [](float x, float y) -> float {
                float dt = x * 12.9898f + y * 78.233f;
                float sn = std::sin(dt) * 43758.5453f;
                return sn - std::floor(sn);
                };

            uint8_t* pNormOut = megaNormals.GetImage(0, slice, 0)->pixels;
            size_t normPitch = megaNormals.GetImage(0, slice, 0)->rowPitch;

            float chunkBaseX = (float)metaList[slice].GridX * 100.0f;
            float chunkBaseZ = (float)metaList[slice].GridZ * 100.0f;

            for (int y = 0; y < HEIGHTMAP_SIZE; ++y) {
                for (int x = 0; x < HEIGHTMAP_SIZE; ++x) {
                    float wx = chunkBaseX + (float)x * spacing;
                    float wz = chunkBaseZ + (float)y * spacing;
                    float centerH = vGlobalPhysicsHeights[slice * HEIGHTMAP_SIZE * HEIGHTMAP_SIZE + y * HEIGHTMAP_SIZE + x];

                    // НОРМАЛИ (SOBEL 3x3)
                    float h00 = GetHeightAtWorldPos(wx - spacing, wz - spacing, centerH);
                    float h10 = GetHeightAtWorldPos(wx, wz - spacing, centerH);
                    float h20 = GetHeightAtWorldPos(wx + spacing, wz - spacing, centerH);

                    float h01 = GetHeightAtWorldPos(wx - spacing, wz, centerH);
                    float h21 = GetHeightAtWorldPos(wx + spacing, wz, centerH);

                    float h02 = GetHeightAtWorldPos(wx - spacing, wz + spacing, centerH);
                    float h12 = GetHeightAtWorldPos(wx, wz + spacing, centerH);
                    float h22 = GetHeightAtWorldPos(wx + spacing, wz + spacing, centerH);

                    float dX = (h20 + 2.0f * h21 + h22) - (h00 + 2.0f * h01 + h02);
                    float dZ = (h02 + 2.0f * h12 + h22) - (h00 + 2.0f * h10 + h20);
                    float dY = 8.0f * spacing;

                    float invLen = 1.0f / sqrt(dX * dX + dY * dY + dZ * dZ);

                    float Nx = -dX * invLen;
                    float Ny = dY * invLen;
                    float Nz = -dZ * invLen;

                    // HBAO
                    float totalOcclusion = 0.0f;
                    int numDirs = 8;
                    int numSteps = 8;
                    float maxRadius = 60.0f;

                    float jitter = Hash21(wx, wz);
                    float angleJitter = Hash21(wx + 0.1f, wz + 0.1f) * 6.2831853f;

                    float startBias = 0.1f + (1.0f - Ny) * 1.5f;
                    float startH = centerH + startBias;

                    for (int d = 0; d < numDirs; ++d) {
                        float angle = angleJitter + (float)d * (6.2831853f / numDirs);
                        float dirX = std::cos(angle);
                        float dirZ = std::sin(angle);

                        float tangentY = (-Nx * dirX - Nz * dirZ) / Ny;
                        float tangentAngle = std::atan2(tangentY, 1.0f);
                        float maxElevationAngle = tangentAngle;

                        for (int s = 0; s < numSteps; ++s) {
                            float t = (s + jitter) / (float)numSteps;
                            float dist = (spacing * 1.5f) + t * t * (maxRadius - spacing * 1.5f);

                            float sH = GetHeightBilinear(wx + dirX * dist, wz + dirZ * dist, startH);
                            float diffH = sH - startH;
                            float elAngle = std::atan2(diffH, dist);

                            if (elAngle > maxElevationAngle) {
                                maxElevationAngle = elAngle;
                            }
                        }

                        float occlusion = std::sin(maxElevationAngle) - std::sin(tangentAngle);
                        totalOcclusion += std::max(0.0f, occlusion);
                    }

                    float ao = 1.0f - std::clamp((totalOcclusion / (float)numDirs) * 1.8f, 0.0f, 1.0f);

                    // Упаковка в [0..255]
                    size_t outOff = y * normPitch + x * 4;
                    pNormOut[outOff + 0] = (uint8_t)(Nx * 0.5f * 255.0f + 127.5f);
                    pNormOut[outOff + 1] = (uint8_t)(Ny * 0.5f * 255.0f + 127.5f);
                    pNormOut[outOff + 2] = (uint8_t)(Nz * 0.5f * 255.0f + 127.5f);
                    pNormOut[outOff + 3] = (uint8_t)(ao * 255.0f);
                }
            }

            // СИНХРОНИЗАЦИЯ ПОТОКОВ
            if (++chunksCompleted == numChunks) {
                std::lock_guard<std::mutex> lock(bakeMutex);
                bakeCV.notify_one();
            }
            });
    }

    // ГЛАВНЫЙ ПОТОК ОЖИДАЕТ ЗАВЕРШЕНИЯ ВСЕХ ЗАДАЧ
    std::unique_lock<std::mutex> lock(bakeMutex);
    bakeCV.wait(lock, [&] { return chunksCompleted == numChunks; });

    // СОХРАНЕНИЕ
    std::string prefix = GetCachePrefix(folderPath);
    auto saveDDS = [](DirectX::ScratchImage& img, const std::string& p) {
        std::wstring wp(p.begin(), p.end());
        DirectX::SaveToDDSFile(img.GetImages(), img.GetImageCount(), img.GetMetadata(), DirectX::DDS_FLAGS_NONE, wp.c_str());
        };

    saveDDS(megaHeights, prefix + "_Heights.dds");
    saveDDS(megaHoles, prefix + "_Holes.dds");
    saveDDS(megaIndices, prefix + "_Indices.dds");
    saveDDS(megaWeights, prefix + "_Weights.dds");
    saveDDS(megaNormals, prefix + "_Normals.dds");

    std::ofstream metaFile(prefix + "_Terrain.meta", std::ios::binary);
    uint32_t magic = 0x4D414745;
    uint32_t count = (uint32_t)numChunks;
    metaFile.write((char*)&magic, sizeof(magic));
    metaFile.write((char*)&count, sizeof(count));
    metaFile.write((char*)metaList.data(), numChunks * sizeof(UnifiedChunkMeta));
    metaFile.close();

    std::ofstream physFile(prefix + "_Physics.bin", std::ios::binary);
    physFile.write((char*)vGlobalPhysicsHeights.data(), vGlobalPhysicsHeights.size() * sizeof(float));
    physFile.close();

    GAMMA_LOG_INFO(LogCategory::System, "TASK SCHEDULER Megabake completed in parallel!");
    return true;
}

// FIXME: Вынести логику HBAO в общую функцию/лямбду, чтобы не дублировать код 
// между BakeLocationInMemory и BakeFromGLevel.
float TerrainBaker::CalculateAOSample(
    const std::vector<float>& globalHeights,
    const std::vector<UnifiedChunkMeta>& metaList,
    const std::map<std::pair<int, int>, int>& gridToSlice,
    int currentSlice, int x, int y, float spacing)
{
    const UnifiedChunkMeta& curMeta = metaList[currentSlice];
    float centerH = globalHeights[currentSlice * HEIGHTMAP_SIZE * HEIGHTMAP_SIZE + y * HEIGHTMAP_SIZE + x];

    float centerX = (float)curMeta.GridX * 100.0f + (float)x * spacing;
    float centerZ = (float)curMeta.GridZ * 100.0f + (float)y * spacing;

    const int numDirections = 8;
    const int numSteps = 4;
    const float stepSizes[4] = { 5.0f, 15.0f, 30.0f, 60.0f };

    float totalOcclusion = 0.0f;

    for (int d = 0; d < numDirections; ++d) {
        float angle = (float)d * (6.2831853f / numDirections);
        float dirX = std::cos(angle);
        float dirZ = std::sin(angle);

        float maxElevationAngle = 0.0f;

        for (int s = 0; s < numSteps; ++s) {
            float dist = stepSizes[s];
            float sampleWorldX = centerX + dirX * dist;
            float sampleWorldZ = centerZ + dirZ * dist;

            int sampleGX = (int)std::floor(sampleWorldX / 100.0f);
            int sampleGZ = (int)std::floor(sampleWorldZ / 100.0f);

            auto it = gridToSlice.find({ sampleGX, sampleGZ });
            if (it == gridToSlice.end()) continue;
            int sampleSlice = it->second;

            float localX = (sampleWorldX - (float)sampleGX * 100.0f) / spacing;
            float localZ = (sampleWorldZ - (float)sampleGZ * 100.0f) / spacing;

            int lx = std::clamp((int)std::floor(localX), 0, HEIGHTMAP_SIZE - 1);
            int lz = std::clamp((int)std::floor(localZ), 0, HEIGHTMAP_SIZE - 1);

            float sampleH = globalHeights[sampleSlice * HEIGHTMAP_SIZE * HEIGHTMAP_SIZE + lz * HEIGHTMAP_SIZE + lx];

            float diffH = sampleH - centerH;
            if (diffH > 0.0f) {
                float elevationAngle = std::atan2(diffH, dist);
                if (elevationAngle > maxElevationAngle) {
                    maxElevationAngle = elevationAngle;
                }
            }
        }
        totalOcclusion += std::sin(maxElevationAngle);
    }

    float avgOcclusion = (totalOcclusion / (float)numDirections) * 1.5f;
    return 1.0f - std::clamp(avgOcclusion, 0.0f, 1.0f);
}

bool TerrainBaker::BakeFromGLevel(const std::string& folderPath, const std::string& glevelPath) {
    GAMMA_LOG_INFO(LogCategory::System, "Starting TaskScheduler Megabake directly from .glevel...");

    std::ifstream file(glevelPath, std::ios::binary);
    if (!file.is_open()) return false;

    GLevelHeader header;
    file.read((char*)&header, sizeof(GLevelHeader));

    size_t numChunks = header.NumChunks;
    if (numChunks == 0 || numChunks > 2048) return false;

    // Читаем String Pool
    std::vector<char> poolData(header.StringPoolSize);
    file.read(poolData.data(), header.StringPoolSize);
    uint32_t strCount = *(uint32_t*)poolData.data();
    std::vector<std::string> stringPool;
    stringPool.reserve(strCount);
    size_t offset = 4;
    for (uint32_t i = 0; i < strCount; ++i) {
        std::string s(poolData.data() + offset);
        stringPool.push_back(s);
        offset += s.length() + 1;
    }

    DirectX::ScratchImage megaHeights, megaHoles, megaIndices, megaWeights, megaNormals;
    megaHeights.Initialize2D(DXGI_FORMAT_R32_FLOAT, HEIGHTMAP_SIZE, HEIGHTMAP_SIZE, numChunks, 1);
    megaHoles.Initialize2D(DXGI_FORMAT_R8_UNORM, HOLEMAP_SIZE, HOLEMAP_SIZE, numChunks, 1);
    megaIndices.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, BLENDMAP_SIZE, BLENDMAP_SIZE, numChunks, 1);
    megaWeights.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, BLENDMAP_SIZE, BLENDMAP_SIZE, numChunks, 1);
    megaNormals.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, HEIGHTMAP_SIZE, HEIGHTMAP_SIZE, numChunks, 1);

    std::vector<UnifiedChunkMeta> metaList(numChunks);
    std::vector<float> vGlobalPhysicsHeights(numChunks * HEIGHTMAP_SIZE * HEIGHTMAP_SIZE, 0.0f);

    // Чтение сырых массивов из .glevel
    for (uint32_t slice = 0; slice < numChunks; ++slice) {
        GChunkHeader chHdr;
        file.read((char*)&chHdr, sizeof(GChunkHeader));

        metaList[slice].GridX = chHdr.GridX;
        metaList[slice].GridZ = chHdr.GridZ;
        metaList[slice].SliceIndex = slice;
        metaList[slice].MinHeight = chHdr.MinHeight;
        metaList[slice].MaxHeight = chHdr.MaxHeight;
        metaList[slice].NumLayers = MAX_TERRAIN_LAYERS;

        // Высоты
        std::vector<uint8_t> tempHeights(HEIGHTMAP_SIZE * HEIGHTMAP_SIZE * sizeof(float));
        file.read((char*)tempHeights.data(), tempHeights.size());

        const DirectX::Image* imgH = megaHeights.GetImage(0, slice, 0);
        size_t rowSizeHeights = HEIGHTMAP_SIZE * sizeof(float);
        for (size_t y = 0; y < HEIGHTMAP_SIZE; ++y) {
            memcpy(imgH->pixels + y * imgH->rowPitch, tempHeights.data() + y * rowSizeHeights, rowSizeHeights);
        }
        memcpy(&vGlobalPhysicsHeights[slice * HEIGHTMAP_SIZE * HEIGHTMAP_SIZE], tempHeights.data(), tempHeights.size());

        // Дыры
        std::vector<uint8_t> holes8x8(64);
        file.read((char*)holes8x8.data(), 64);
        DirectX::ScratchImage tempHole;
        tempHole.Initialize2D(DXGI_FORMAT_R8_UNORM, 8, 8, 1, 1);
        memcpy(tempHole.GetImage(0, 0, 0)->pixels, holes8x8.data(), 64);

        DirectX::ScratchImage resizedHole;
        DirectX::Resize(tempHole.GetImages(), tempHole.GetImageCount(), tempHole.GetMetadata(), HOLEMAP_SIZE, HOLEMAP_SIZE, DirectX::TEX_FILTER_POINT, resizedHole);
        SafeCopyImage(megaHoles.GetImage(0, slice, 0), resizedHole.GetImage(0, 0, 0), HOLEMAP_SIZE);

        // Материалы
#pragma pack(push, 1)
        struct GMaterialData {
            uint16_t MatID;
            uint16_t Pad;
            DirectX::XMFLOAT4 UProj;
            DirectX::XMFLOAT4 VProj;
        };
#pragma pack(pop)

        GMaterialData matData[MAX_TERRAIN_LAYERS];
        file.read((char*)matData, sizeof(matData));

        for (int m = 0; m < MAX_TERRAIN_LAYERS; ++m) {
            std::string texName = "default";
            if (matData[m].MatID < stringPool.size()) {
                texName = stringPool[matData[m].MatID];
            }
            strncpy_s(metaList[slice].Layers[m].textureName, texName.c_str(), sizeof(metaList[slice].Layers[m].textureName) - 1);
            metaList[slice].Layers[m].uProj = matData[m].UProj;
            metaList[slice].Layers[m].vProj = matData[m].VProj;
        }

        // Индексы
        size_t blendMapBytes = BLENDMAP_SIZE * BLENDMAP_SIZE * 4;
        std::vector<uint8_t> tempIndices(blendMapBytes);
        file.read((char*)tempIndices.data(), blendMapBytes);

        const DirectX::Image* imgI = megaIndices.GetImage(0, slice, 0);
        size_t rowSizeBlend = BLENDMAP_SIZE * 4;
        for (size_t y = 0; y < BLENDMAP_SIZE; ++y) {
            memcpy(imgI->pixels + y * imgI->rowPitch, tempIndices.data() + y * rowSizeBlend, rowSizeBlend);
        }

        // Веса
        std::vector<uint8_t> tempWeights(blendMapBytes);
        file.read((char*)tempWeights.data(), blendMapBytes);

        const DirectX::Image* imgW = megaWeights.GetImage(0, slice, 0);
        for (size_t y = 0; y < BLENDMAP_SIZE; ++y) {
            memcpy(imgW->pixels + y * imgW->rowPitch, tempWeights.data() + y * rowSizeBlend, rowSizeBlend);
        }

        // Пропускаем статику и флору
        uint32_t statCount;
        file.read((char*)&statCount, sizeof(uint32_t));
        file.seekg(statCount * sizeof(GEntity), std::ios::cur);

        uint32_t floraCount;
        file.read((char*)&floraCount, sizeof(uint32_t));
        file.seekg(floraCount * sizeof(GEntity), std::ios::cur);
    }
    file.close();

    // ==========================================
    // ПАСС 2: МНОГОПОТОЧНЫЙ РАСЧЕТ AO
    // ==========================================
    GAMMA_LOG_INFO(LogCategory::System, "[Pass 2/2] TaskScheduler Baking HBAO from .glevel data...");

    int minGx = 999999, maxGx = -999999, minGz = 999999, maxGz = -999999;
    for (size_t slice = 0; slice < numChunks; ++slice) {
        if (metaList[slice].GridX < minGx) minGx = metaList[slice].GridX;
        if (metaList[slice].GridX > maxGx) maxGx = metaList[slice].GridX;
        if (metaList[slice].GridZ < minGz) minGz = metaList[slice].GridZ;
        if (metaList[slice].GridZ > maxGz) maxGz = metaList[slice].GridZ;
    }

    int gridW = maxGx - minGx + 1;
    int gridH = maxGz - minGz + 1;
    std::vector<int> fastGrid(gridW * gridH, -1);

    for (size_t slice = 0; slice < numChunks; ++slice) {
        fastGrid[(metaList[slice].GridZ - minGz) * gridW + (metaList[slice].GridX - minGx)] = (int)slice;
    }

    float spacing = 100.0f / (float)(HEIGHTMAP_SIZE - 1);
    std::atomic<int> chunksCompleted{ 0 };
    std::mutex bakeMutex;
    std::condition_variable bakeCV;

    for (size_t slice = 0; slice < numChunks; ++slice) {
        TaskScheduler::Get().Submit([slice, numChunks, spacing, minGx, maxGx, minGz, maxGz, gridW, &metaList, &vGlobalPhysicsHeights, &fastGrid, &megaNormals, &chunksCompleted, &bakeMutex, &bakeCV]() {

            auto GetHeightAtWorldPos = [&](float wx, float wz, float defaultH) -> float {
                int gx = (int)std::floor(wx / 100.0f);
                int gz = (int)std::floor(wz / 100.0f);
                if (gx < minGx || gx > maxGx || gz < minGz || gz > maxGz) return defaultH;

                int s = fastGrid[(gz - minGz) * gridW + (gx - minGx)];
                if (s == -1) return defaultH;

                float localX = (wx - (float)gx * 100.0f) / spacing;
                float localZ = (wz - (float)gz * 100.0f) / spacing;
                int lx = std::clamp((int)std::floor(localX), 0, HEIGHTMAP_SIZE - 1);
                int lz = std::clamp((int)std::floor(localZ), 0, HEIGHTMAP_SIZE - 1);

                return vGlobalPhysicsHeights[s * HEIGHTMAP_SIZE * HEIGHTMAP_SIZE + lz * HEIGHTMAP_SIZE + lx];
                };

            auto GetHeightBilinear = [&](float wx, float wz, float defaultH) -> float {
                float gridX = wx / spacing;
                float gridZ = wz / spacing;
                float x0f = std::floor(gridX);
                float z0f = std::floor(gridZ);
                float tx = gridX - x0f;
                float tz = gridZ - z0f;

                float h00 = GetHeightAtWorldPos(x0f * spacing, z0f * spacing, defaultH);
                float h10 = GetHeightAtWorldPos((x0f + 1.0f) * spacing, z0f * spacing, defaultH);
                float h01 = GetHeightAtWorldPos(x0f * spacing, (z0f + 1.0f) * spacing, defaultH);
                float h11 = GetHeightAtWorldPos((x0f + 1.0f) * spacing, (z0f + 1.0f) * spacing, defaultH);

                float h0 = h00 + tx * (h10 - h00);
                float h1 = h01 + tx * (h11 - h01);
                return h0 + tz * (h1 - h0);
                };

            auto Hash21 = [](float x, float y) -> float {
                float dt = x * 12.9898f + y * 78.233f;
                float sn = std::sin(dt) * 43758.5453f;
                return sn - std::floor(sn);
                };

            uint8_t* pNormOut = megaNormals.GetImage(0, slice, 0)->pixels;
            size_t normPitch = megaNormals.GetImage(0, slice, 0)->rowPitch;

            float chunkBaseX = (float)metaList[slice].GridX * 100.0f;
            float chunkBaseZ = (float)metaList[slice].GridZ * 100.0f;

            for (int y = 0; y < HEIGHTMAP_SIZE; ++y) {
                for (int x = 0; x < HEIGHTMAP_SIZE; ++x) {
                    float wx = chunkBaseX + (float)x * spacing;
                    float wz = chunkBaseZ + (float)y * spacing;
                    float centerH = vGlobalPhysicsHeights[slice * HEIGHTMAP_SIZE * HEIGHTMAP_SIZE + y * HEIGHTMAP_SIZE + x];

                    float h00 = GetHeightAtWorldPos(wx - spacing, wz - spacing, centerH);
                    float h10 = GetHeightAtWorldPos(wx, wz - spacing, centerH);
                    float h20 = GetHeightAtWorldPos(wx + spacing, wz - spacing, centerH);

                    float h01 = GetHeightAtWorldPos(wx - spacing, wz, centerH);
                    float h21 = GetHeightAtWorldPos(wx + spacing, wz, centerH);

                    float h02 = GetHeightAtWorldPos(wx - spacing, wz + spacing, centerH);
                    float h12 = GetHeightAtWorldPos(wx, wz + spacing, centerH);
                    float h22 = GetHeightAtWorldPos(wx + spacing, wz + spacing, centerH);

                    float dX = (h20 + 2.0f * h21 + h22) - (h00 + 2.0f * h01 + h02);
                    float dZ = (h02 + 2.0f * h12 + h22) - (h00 + 2.0f * h10 + h20);
                    float dY = 8.0f * spacing;

                    float invLen = 1.0f / sqrt(dX * dX + dY * dY + dZ * dZ);

                    float Nx = -dX * invLen;
                    float Ny = dY * invLen;
                    float Nz = -dZ * invLen;

                    float totalOcclusion = 0.0f;
                    int numDirs = 8;
                    int numSteps = 8;
                    float maxRadius = 60.0f;

                    float jitter = Hash21(wx, wz);
                    float angleJitter = Hash21(wx + 0.1f, wz + 0.1f) * 6.2831853f;

                    float startBias = 0.1f + (1.0f - Ny) * 1.5f;
                    float startH = centerH + startBias;

                    for (int d = 0; d < numDirs; ++d) {
                        float angle = angleJitter + (float)d * (6.2831853f / numDirs);
                        float dirX = std::cos(angle);
                        float dirZ = std::sin(angle);

                        float tangentY = (-Nx * dirX - Nz * dirZ) / Ny;
                        float tangentAngle = std::atan2(tangentY, 1.0f);
                        float maxElevationAngle = tangentAngle;

                        for (int s = 0; s < numSteps; ++s) {
                            float t = (s + jitter) / (float)numSteps;
                            float dist = (spacing * 1.5f) + t * t * (maxRadius - spacing * 1.5f);

                            float sH = GetHeightBilinear(wx + dirX * dist, wz + dirZ * dist, startH);
                            float diffH = sH - startH;
                            float elAngle = std::atan2(diffH, dist);

                            if (elAngle > maxElevationAngle) {
                                maxElevationAngle = elAngle;
                            }
                        }

                        float occlusion = std::sin(maxElevationAngle) - std::sin(tangentAngle);
                        totalOcclusion += std::max(0.0f, occlusion);
                    }

                    float ao = 1.0f - std::clamp((totalOcclusion / (float)numDirs) * 1.8f, 0.0f, 1.0f);

                    size_t outOff = y * normPitch + x * 4;
                    pNormOut[outOff + 0] = (uint8_t)(Nx * 0.5f * 255.0f + 127.5f);
                    pNormOut[outOff + 1] = (uint8_t)(Ny * 0.5f * 255.0f + 127.5f);
                    pNormOut[outOff + 2] = (uint8_t)(Nz * 0.5f * 255.0f + 127.5f);
                    pNormOut[outOff + 3] = (uint8_t)(ao * 255.0f);
                }
            }

            if (++chunksCompleted == numChunks) {
                std::lock_guard<std::mutex> lock(bakeMutex);
                bakeCV.notify_one();
            }
            });
    }

    std::unique_lock<std::mutex> lock(bakeMutex);
    bakeCV.wait(lock, [&] { return chunksCompleted == numChunks; });

    // СОХРАНЕНИЕ DDS ФАЙЛОВ
    std::string prefix = GetCachePrefix(folderPath);
    auto saveDDS = [](DirectX::ScratchImage& img, const std::string& p) {
        std::wstring wp(p.begin(), p.end());
        DirectX::SaveToDDSFile(img.GetImages(), img.GetImageCount(), img.GetMetadata(), DirectX::DDS_FLAGS_NONE, wp.c_str());
        };

    saveDDS(megaHeights, prefix + "_Heights.dds");
    saveDDS(megaHoles, prefix + "_Holes.dds");
    saveDDS(megaIndices, prefix + "_Indices.dds");
    saveDDS(megaWeights, prefix + "_Weights.dds");
    saveDDS(megaNormals, prefix + "_Normals.dds");

    std::ofstream metaFile(prefix + "_Terrain.meta", std::ios::binary);
    uint32_t magic = 0x4D414745;
    uint32_t count = (uint32_t)numChunks;
    metaFile.write((char*)&magic, sizeof(magic));
    metaFile.write((char*)&count, sizeof(count));
    metaFile.write((char*)metaList.data(), numChunks * sizeof(UnifiedChunkMeta));
    metaFile.close();

    std::ofstream physFile(prefix + "_Physics.bin", std::ios::binary);
    physFile.write((char*)vGlobalPhysicsHeights.data(), vGlobalPhysicsHeights.size() * sizeof(float));
    physFile.close();

    GAMMA_LOG_INFO(LogCategory::System, "GLEVEL Megabake completed successfully!");
    return true;
}