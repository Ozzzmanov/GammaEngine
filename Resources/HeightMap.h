#pragma once
#include <vector>
#include <string>

struct ColorRGB {
    unsigned char r, g, b;
};

class HeightMap {
public:
    static bool LoadPackedHeight(
        const unsigned char* buffer, int len,
        std::vector<float>& outHeights,
        int& outWidth, int& outHeight,
        float ignoredScale = 0.01f); 

    static bool LoadNormalsFromMemory(
        const unsigned char* buffer, int len,
        std::vector<ColorRGB>& outNormals,
        int& outWidth, int& outHeight);
};