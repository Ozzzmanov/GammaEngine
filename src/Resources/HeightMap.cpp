//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// HeightMap.cpp
// ================================================================================

#include "HeightMap.h"
#include "../Core/Logger.h"
#include "stb_image.h"

// Формат заголовка QPNG (BigWorld Internal Format)
#pragma pack(push, 1)
struct HeightMapHeader {
    uint32_t magic;      
    uint32_t compression;
    uint32_t width;
    uint32_t height;
    float    minHeight;
    float    maxHeight;
    uint32_t version;
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

    // Читаем заголовок
    const unsigned char* dataPtr = buffer + sizeof(HeightMapHeader);
    int remainingLen = len - sizeof(HeightMapHeader);

    // Проверка на Magic Number QPNG, который может быть сразу за заголовком
    if (remainingLen >= 4) {
        uint32_t magic = *reinterpret_cast<const uint32_t*>(dataPtr);

        // QPNG (Сжатый, Высокая точность) ---
        if (magic == QUANTISED_PNG_MAGIC) {
            const unsigned char* pngData = dataPtr + 4;
            int pngLen = remainingLen - 4;

            int channels;
            // Грузим как RGBA (4 канала), т.к. int32 упакован в пиксели
            unsigned char* pixels = stbi_load_from_memory(pngData, pngLen, &outWidth, &outHeight, &channels, 4);

            if (!pixels) {
                Logger::Error(LogCategory::Terrain, "Failed to decode QPNG data.");
                return false;
            }

            outHeights.resize(outWidth * outHeight);
            const int32_t* intPixels = reinterpret_cast<const int32_t*>(pixels);

            // Конвертация: Int32 -> Float (с учетом масштаба)
            for (int i = 0; i < outWidth * outHeight; ++i) {
                outHeights[i] = static_cast<float>(intPixels[i]) * quantizationLevel;
            }

            stbi_image_free(pixels);
            return true;
        }
    }

    // ОБЫЧНЫЙ PNG (Legacy / Fallback) ---
    // Если magic не совпал, пробуем читать весь буфер как PNG
    int channels;
    unsigned char* pixels = stbi_load_from_memory(buffer, len, &outWidth, &outHeight, &channels, 0);

    // Fallback: иногда PNG смещен
    if (!pixels) {
        for (int i = 0; i < std::min(len, 256); ++i) {
            if (buffer[i] == 0x89 && buffer[i + 1] == 0x50) { // PNG Signature
                pixels = stbi_load_from_memory(buffer + i, len - i, &outWidth, &outHeight, &channels, 0);
                break;
            }
        }
    }

    if (!pixels) {
        // Это нормально, если секция не содержит высот
        return false;
    }

    outHeights.resize(outWidth * outHeight);

    // Для обычного PNG считаем, что это 8-битная или 16-битная карта (грейскейл)
    // Здесь упрощенная логика для 8 бит -> метры
    for (int i = 0; i < outWidth * outHeight; ++i) {
        // Берем только первый канал (R)
        unsigned char val = pixels[i * channels];
        outHeights[i] = static_cast<float>(val) * 0.1f; // Примерный масштаб
    }

    stbi_image_free(pixels);
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

    // Быстрое копирование
    std::memcpy(outNormals.data(), data, outWidth * outHeight * 3);

    stbi_image_free(data);
    return true;
}