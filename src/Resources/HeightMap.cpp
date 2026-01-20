//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Реализация HeightMap.
// Содержит логику парсинга заголовков QPNG и использования stbi_load_from_memory.
// ================================================================================

#include "HeightMap.h"
#include "stb_image.h"
#include <iostream>
#include <cstring>
#include <algorithm>

// Заголовок формата QPNG (Quantized PNG)
#pragma pack(push, 1)
struct HeightMapHeader {
    uint32_t magic;       // Должно быть 0
    uint32_t compression;
    uint32_t width;
    uint32_t height;
    float    minHeight;
    float    maxHeight;
    uint32_t version;
};
#pragma pack(pop)

// Магическое число 'qpng' (0x71706e67)
const uint32_t QUANTISED_PNG_MAGIC = 0x71706e67;

bool HeightMap::LoadPackedHeight(
    const unsigned char* buffer, int len,
    std::vector<float>& outHeights,
    int& outWidth, int& outHeight,
    float quantizationLevel)
{
    // 1. Минимальная проверка длины
    if (len < sizeof(HeightMapHeader)) return false;

    // Считываем заголовок
    const HeightMapHeader* header = reinterpret_cast<const HeightMapHeader*>(buffer);

    // Данные начинаются после заголовка
    const unsigned char* dataPtr = buffer + sizeof(HeightMapHeader);
    int remainingLen = len - sizeof(HeightMapHeader);

    // Проверяем наличие 'qpng' сигнатуры
    if (remainingLen < 4) return false;
    uint32_t magic = *reinterpret_cast<const uint32_t*>(dataPtr);

    // --- ВЕТКА 1: QPNG (32-битная точность) ---
    if (magic == QUANTISED_PNG_MAGIC) {
        // Пропускаем 4 байта сигнатуры 'qpng'
        const unsigned char* pngData = dataPtr + 4;
        int pngLen = remainingLen - 4;

        int channels;
        // Загружаем как RGBA (4 канала), так как int32 занимает 4 байта
        unsigned char* pixels = stbi_load_from_memory(pngData, pngLen, &outWidth, &outHeight, &channels, 4);

        if (!pixels) {
            std::cout << "[HeightMap] Error: Failed to decode QPNG data." << std::endl;
            return false;
        }

        outHeights.resize(outWidth * outHeight);

        // Интерпретируем массив байт как массив int32
        const int32_t* intPixels = reinterpret_cast<const int32_t*>(pixels);

        for (int i = 0; i < outWidth * outHeight; ++i) {
            // Читаем квантованное значение
            int32_t quantizedHeight = intPixels[i];

            // Преобразуем в float, умножая на шаг квантования (обычно 1 мм = 0.001)
            outHeights[i] = static_cast<float>(quantizedHeight) * quantizationLevel;
        }

        stbi_image_free(pixels);
        return true;
    }
    // --- ВЕТКА 2: ОБЫЧНЫЙ PNG (Legacy / Fallback) ---
    else {
        std::cout << "[HeightMap] Warning: Not a QPNG format. Using legacy fallback." << std::endl;

        int channels;
        // Пробуем загрузить "как есть"
        unsigned char* pixels = stbi_load_from_memory(buffer, len, &outWidth, &outHeight, &channels, 0);

        // Если не вышло, пробуем найти PNG заголовок внутри данных (brute-force)
        if (!pixels) {
            for (int i = 0; i < std::min(len, 256); ++i) {
                // Сигнатура PNG: 89 50 4E 47
                if (buffer[i] == 0x89 && buffer[i + 1] == 0x50) {
                    pixels = stbi_load_from_memory(buffer + i, len - i, &outWidth, &outHeight, &channels, 0);
                    break;
                }
            }
        }

        if (!pixels) return false;

        outHeights.resize(outWidth * outHeight);
        // Конвертируем 8-битный цвет в высоту (очень грубо, только для совместимости)
        for (int i = 0; i < outWidth * outHeight; ++i) {
            outHeights[i] = static_cast<float>(pixels[i * channels]) * 0.01f;
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
    // Принудительно загружаем 3 канала (RGB)
    unsigned char* data = stbi_load_from_memory(buffer, len, &outWidth, &outHeight, &channels, 3);

    if (!data) return false;

    outNormals.resize(outWidth * outHeight);

    // Копируем данные напрямую в вектор
    std::memcpy(outNormals.data(), data, outWidth * outHeight * 3);

    stbi_image_free(data);
    return true;
}