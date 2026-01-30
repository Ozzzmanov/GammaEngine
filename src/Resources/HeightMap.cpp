
//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
#include "HeightMap.h"
#include "../Core/Logger.h"
#include "stb_image.h"
#include <cstring>
#include <algorithm>
#include <cmath>

#pragma pack(push, 1)
struct HeightMapHeader {
    uint32_t magic;         // 00: "hmp\0"
    uint32_t width;         // 04:
    uint32_t height;        // 08
    uint32_t compression;   // 12
    uint32_t version;       // 16: 
    float    minHeight;     // 20
    float    maxHeight;     // 24
    uint32_t pad;           // 28
};
#pragma pack(pop)

const uint32_t QUANTISED_PNG_MAGIC = 0x71706e67; // 'qpng'

bool HeightMap::LoadPackedHeight(
    const unsigned char* buffer, int len,
    std::vector<float>& outHeights,
    int& outWidth, int& outHeight,
    float quantizationLevel)
{
    if (len < sizeof(HeightMapHeader)) return false;

    const HeightMapHeader* header = reinterpret_cast<const HeightMapHeader*>(buffer);

    outWidth = header->width;
    outHeight = header->height;

    // Смещаемся к данным
    const unsigned char* dataPtr = buffer + sizeof(HeightMapHeader);
    int remainingLen = len - sizeof(HeightMapHeader);

    const unsigned char* pngData = dataPtr;
    int pngLen = remainingLen;
    bool isQpng = false;

    if (remainingLen >= 4) {
        uint32_t magic = *reinterpret_cast<const uint32_t*>(dataPtr);
        if (magic == QUANTISED_PNG_MAGIC) {
            pngData += 4;
            pngLen -= 4;
            isQpng = true;
        }
    }

    int channels;
    int pngW, pngH;

    bool is16Bit = stbi_is_16_bit_from_memory(pngData, pngLen);

    unsigned char* pixels8 = nullptr;
    unsigned short* pixels16 = nullptr;

    if (is16Bit) {
        pixels16 = stbi_load_16_from_memory(pngData, pngLen, &pngW, &pngH, &channels, 1);
    }
    else {
        int reqChannels = (header->version == 4) ? 4 : 1;
        pixels8 = stbi_load_from_memory(pngData, pngLen, &pngW, &pngH, &channels, reqChannels);
    }

    if (!pixels8 && !pixels16) {
        Logger::Error(LogCategory::Terrain, "Failed to decode HeightMap PNG.");
        return false;
    }

    outWidth = pngW;
    outHeight = pngH;   
    outHeights.resize(outWidth * outHeight);

    if (pixels16) {
        float minH = header->minHeight;
        float range = header->maxHeight - minH;
        for (int i = 0; i < outWidth * outHeight; ++i) {
            float ratio = pixels16[i] / 65535.0f;
            outHeights[i] = minH + (ratio * range);
        }
        stbi_image_free(pixels16);
    }
    else if (pixels8) {
        if (header->version == 4) {
            const int32_t* p32 = reinterpret_cast<const int32_t*>(pixels8);
            for (int i = 0; i < outWidth * outHeight; ++i) {
                outHeights[i] = static_cast<float>(p32[i]) * quantizationLevel;
            }
        }
        else {
            // Fallback 8-bit Relative
            float minH = header->minHeight;
            float range = header->maxHeight - minH;
            for (int i = 0; i < outWidth * outHeight; ++i) {
                float ratio = pixels8[i] / 255.0f;
                outHeights[i] = minH + (ratio * range);
            }
        }
        stbi_image_free(pixels8);
    }


    return true;
}

bool HeightMap::LoadNormalsFromMemory(
    const unsigned char* buffer, int len,
    std::vector<ColorRGB>& outNormals,
    int& outWidth, int& outHeight)
{
    int channels;
    unsigned char* data = stbi_load_from_memory(buffer, len, &outWidth, &outHeight, &channels, 3);
    if (!data) return false;
    outNormals.resize(outWidth * outHeight);
    std::memcpy(outNormals.data(), data, outWidth * outHeight * 3);
    stbi_image_free(data);
    return true;
}