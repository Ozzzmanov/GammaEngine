//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Класс HeightMap.
// Отвечает за декодирование карт высот, включая специфичный формат QPNG (Quantized PNG),
// используемый в движках типа BigWorld, а также загрузку карт нормалей.
// ================================================================================

#pragma once
#include <vector>
#include <string>

// Структура для хранения цвета (нормали)
struct ColorRGB {
    unsigned char r, g, b;
};

class HeightMap {
public:
    // Загружает карту высот из буфера памяти.
    // Поддерживает QPNG (int32 упакованный в RGBA) и обычный PNG (float/byte).
    static bool LoadPackedHeight(
        const unsigned char* buffer, int len,
        std::vector<float>& outHeights,
        int& outWidth, int& outHeight,
        float quantizationLevel = 0.001f); // Множитель для QPNG

    // Загружает карту нормалей из буфера памяти.
    static bool LoadNormalsFromMemory(
        const unsigned char* buffer, int len,
        std::vector<ColorRGB>& outNormals,
        int& outWidth, int& outHeight);
};