//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// EngineConfig.h
// ================================================================================
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "../Core/Prerequisites.h"

struct KeyBinding {
    std::string actionName;
    int keyCode;
};

struct LogSettings {
    bool Render;
    bool Texture;
    bool Terrain;
    bool Physics;
    bool System;
};

struct LodSettings {
    bool Enabled;
    float Dist1;
    float Dist2;
    float Dist3;
};

struct TextureSettings {
    int MaxResolution;
    bool UseBC7Compression;
    int AnisotropyLevel;
    int TerrainTextureSize;
    bool UseTextureArrays;
};

class EngineConfig {
public:
    static EngineConfig& Get() {
        static EngineConfig instance;
        return instance;
    }

    bool Load(const std::string& filename);

    int WindowWidth;
    int WindowHeight;
    std::string WindowTitle;
    bool IsWindowed;
    bool VSync;

    float RenderDistance;
    float FOV;
    float NearZ;
    float FarZ;
    int AnisotropyLevel;
    float MipLODBias;

    bool EnableOcclusionCulling;
    bool EnableFrustumCulling;
    bool EnableSizeCulling;
    float MinPixelSize;
    bool EnableZPrepass;

    bool UseStaticBatching;
    std::string OcclusionAlgorithm;

    float MouseSensitivity;
    bool InvertY;

    TextureSettings Textures;
    LogSettings Logging;
    LodSettings Lod;

    std::vector<KeyBinding> KeyBindings;

private:
    EngineConfig();
    ~EngineConfig() = default;

    void SetupDefaults();
    int ParseKeyCode(const std::string& keyStr);
};