#include "Resources/HeightMap.h"
#include "Core/stb_image.h"

#include <vector>
#include <cstring>
#include <iostream>
#include <algorithm> 

#pragma pack(push, 1)
struct HeightMapHeader {
    uint32_t magic;       // 0x00
    uint32_t compression; // 0x04
    uint32_t width;       // 0x08
    uint32_t height;      // 0x0C
    float    minHeight;   // 0x10 (version 0x500 используется float напрямую)
    float    maxHeight;   // 0x14
    uint32_t version;     // 0x18
};
#pragma pack(pop)

// Магическое число 'qpng' (0x71706e67)
const uint32_t QUANTISED_PNG_VERSION = 0x71706e67;
// Квантование: 1 единица int32 = 1 миллиметр
const float QUANTISATION_LEVEL = 0.001f;

bool HeightMap::LoadPackedHeight(
    const unsigned char* buffer, int len,
    std::vector<float>& outHeights,
    int& outWidth, int& outHeight,
    float /*ignored_scale*/)
{
    // 1. Проверка размера буфера (хотя бы на заголовок)
    if (len < sizeof(HeightMapHeader)) return false;

    const HeightMapHeader* header = reinterpret_cast<const HeightMapHeader*>(buffer);

    // Данные начинаются сразу после заголовка
    const unsigned char* dataPtr = buffer + sizeof(HeightMapHeader);
    int remainingLen = len - sizeof(HeightMapHeader);

    // Проверка на сжатие QPNG ('qpng')
    // Если осталось меньше 4 байт, читать нечего
    if (remainingLen < 4) return false;

    uint32_t magic = *reinterpret_cast<const uint32_t*>(dataPtr);

    // Если нашли магическое число QPNG, значит это современный формат (int32 упакованный в PNG)
    if (magic == QUANTISED_PNG_VERSION) {
        // Пропускаем 4 байта магии 'qpng'
        const unsigned char* pngData = dataPtr + 4;
        int pngLen = remainingLen - 4;

        int channels;
        // Форсируем 4 канала (RGBA), так как int32 занимает 4 байта.
        // stbi прочитает байты R, G, B, A, которые составляют int32.
        unsigned char* pixels = stbi_load_from_memory(pngData, pngLen, &outWidth, &outHeight, &channels, 4);

        if (!pixels) {
            std::cout << "[HeightMap] Failed to decode QPNG image." << std::endl;
            return false;
        }

        outHeights.resize(outWidth * outHeight);

        // Интерпретируем сырые байты пикселей как массив 32-битных знаковых чисел
        const int32_t* intPixels = reinterpret_cast<const int32_t*>(pixels);

        for (int i = 0; i < outWidth * outHeight; ++i) {
            // Читаем int32 (это высота в миллиметрах)
            int32_t quantizedHeight = intPixels[i];

            // Переводим в метры: умножаем на 0.001
            outHeights[i] = static_cast<float>(quantizedHeight) * QUANTISATION_LEVEL;
        }

        stbi_image_free(pixels);
        return true;
    }
    else {
        // Если это не QPNG, пробуем загрузить как обычный PNG (старый формат или raw float)
        std::cout << "[HeightMap] Warning: Not a QPNG format. Trying legacy load..." << std::endl;

        int channels;
        unsigned char* pixels = stbi_load_from_memory(buffer, len, &outWidth, &outHeight, &channels, 0); // Авто-каналы

        if (!pixels) {
            // Если и так не вышло, пробуем найти PNG сигнатуру перебором (для старых версий)
            for (int i = 0; i < std::min(len, 256); ++i) {
                if (buffer[i] == 0x89 && buffer[i + 1] == 0x50) {
                    pixels = stbi_load_from_memory(buffer + i, len - i, &outWidth, &outHeight, &channels, 0);
                    break;
                }
            }
        }

        if (!pixels) return false;

        outHeights.resize(outWidth * outHeight);
        for (int i = 0; i < outWidth * outHeight; ++i) {
            // Грубая аппроксимация для legacy
            outHeights[i] = (float)pixels[i * channels] * 0.01f;
        }
        stbi_image_free(pixels);
        return true;
    }
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