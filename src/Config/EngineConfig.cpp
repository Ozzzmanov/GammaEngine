//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// EngineConfig.cpp
// ================================================================================
#include "EngineConfig.h"
#include "../Core/Logger.h"
#include <Windows.h>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <Resources/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

EngineConfig::EngineConfig() {
    SetupDefaults();
}

void EngineConfig::SetupDefaults() {
    WindowWidth = 1920;
    WindowHeight = 1080;
    WindowTitle = "Gamma Engine";
    IsWindowed = true;
    VSync = false;

    Textures.MaxResolution = 4096;
    Textures.UseBC7Compression = false;
    Textures.AnisotropyLevel = 16;
    Textures.TerrainTextureSize = 1024;
    Textures.UseTextureArrays = false;

    RenderDistance = 8000.0f;
    FOV = 45.0f;
    NearZ = 1.0f;
    FarZ = 8000.0f;
    AnisotropyLevel = 16;
    MipLODBias = -0.5f;

    EnableOcclusionCulling = false;
    EnableFrustumCulling = true;
    EnableSizeCulling = true;
    MinPixelSize = 5.0f;
    EnableZPrepass = true;
    UseStaticBatching = true;
    OcclusionAlgorithm = "HZB";

    MouseSensitivity = 0.003f;
    InvertY = false;

    Lod.Enabled = true;
    Lod.Dist1 = 100.0f;
    Lod.Dist2 = 250.0f;
    Lod.Dist3 = 1000.0f;

    Logging.Render = true;
    Logging.Texture = false;
    Logging.Terrain = false;
    Logging.Physics = false;
    Logging.System = true;

    KeyBindings = {
        { "MoveForward", 'W' },     { "MoveBackward", 'S' },
        { "MoveLeft", 'A' },        { "MoveRight", 'D' },
        { "FlyUp", 'E' },           { "FlyDown", 'Q' },
        { "Jump", VK_SPACE },       { "Sprint", VK_SHIFT },
        { "ToggleCulling", VK_F1 }, { "ToggleDebugCam", VK_F2 },
        { "ToggleLODs", VK_F3 },    { "ToggleHZB", VK_F4 },
        { "ToggleWireframe", VK_TAB },
        { "RenderDistUp", 'X' },    { "RenderDistDown", 'Z' }
    };
}

bool EngineConfig::Load(const std::string& filename) {
    if (!fs::exists(filename)) {
        Logger::Warn(LogCategory::System, "Config '" + filename + "' not found. Using Defaults.");
        return false;
    }

    std::ifstream file(filename);
    if (!file.is_open()) return false;

    try {
        json j;
        file >> j;

        if (j.contains("System")) {
            auto& sys = j["System"];
            WindowWidth = sys.value("WindowWidth", WindowWidth);
            WindowHeight = sys.value("WindowHeight", WindowHeight);
            WindowTitle = sys.value("WindowTitle", WindowTitle);
            IsWindowed = sys.value("Windowed", IsWindowed);
            VSync = sys.value("VSync", VSync);
        }

        if (j.contains("Textures")) {
            auto& tex = j["Textures"];
            Textures.MaxResolution = tex.value("MaxResolution", Textures.MaxResolution);
            Textures.UseBC7Compression = tex.value("UseBC7Compression", Textures.UseBC7Compression);
            Textures.AnisotropyLevel = tex.value("ForceAnisotropy", Textures.AnisotropyLevel);
            Textures.TerrainTextureSize = tex.value("TerrainTextureSize", Textures.TerrainTextureSize);
            Textures.UseTextureArrays = tex.value("UseTextureArrays", Textures.UseTextureArrays);
        }

        if (j.contains("Logging")) {
            auto& log = j["Logging"];
            Logging.Render = log.value("Render", Logging.Render);
            Logging.Texture = log.value("Texture", Logging.Texture);
            Logging.Terrain = log.value("Terrain", Logging.Terrain);
            Logging.Physics = log.value("Physics", Logging.Physics);
            Logging.System = log.value("System", Logging.System);
        }

        if (j.contains("Graphics")) {
            auto& gfx = j["Graphics"];
            RenderDistance = gfx.value("RenderDistance", RenderDistance);
            FOV = gfx.value("FOV", FOV);
            NearZ = gfx.value("NearZ", NearZ);
            FarZ = gfx.value("FarZ", FarZ);
            AnisotropyLevel = gfx.value("AnisotropyLevel", AnisotropyLevel);
            MipLODBias = gfx.value("MipLODBias", MipLODBias);

            if (gfx.contains("LOD")) {
                auto& lod = gfx["LOD"];
                Lod.Enabled = lod.value("Enabled", Lod.Enabled);
                Lod.Dist1 = lod.value("Distance1", Lod.Dist1);
                Lod.Dist2 = lod.value("Distance2", Lod.Dist2);
                Lod.Dist3 = lod.value("Distance3", Lod.Dist3);
            }
        }

        if (j.contains("Optimization")) {
            auto& opt = j["Optimization"];
            EnableOcclusionCulling = opt.value("EnableOcclusionCulling", EnableOcclusionCulling);
            EnableFrustumCulling = opt.value("EnableFrustumCulling", EnableFrustumCulling);
            EnableSizeCulling = opt.value("EnableSizeCulling", EnableSizeCulling);
            MinPixelSize = opt.value("MinPixelSize", MinPixelSize);
            EnableZPrepass = opt.value("EnableZPrepass", EnableZPrepass);
            OcclusionAlgorithm = opt.value("OcclusionAlgorithm", OcclusionAlgorithm);
            UseStaticBatching = opt.value("UseStaticBatching", UseStaticBatching);
        }

        if (j.contains("Input")) {
            auto& inp = j["Input"];
            MouseSensitivity = inp.value("MouseSensitivity", MouseSensitivity);
            InvertY = inp.value("InvertY", InvertY);

            for (auto& bind : KeyBindings) {
                if (inp.contains(bind.actionName)) {
                    int code = ParseKeyCode(inp[bind.actionName].get<std::string>());
                    if (code != 0) bind.keyCode = code;
                }
            }
        }
    }
    catch (...) {
        return false;
    }
    return true;
}

int EngineConfig::ParseKeyCode(const std::string& keyStr) {
    std::string upperKey = keyStr;
    std::transform(upperKey.begin(), upperKey.end(), upperKey.begin(), ::toupper);

    if (upperKey.empty()) return 0;

    if (upperKey.length() == 1) {
        char c = upperKey[0];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) return (int)c;
    }

    static const std::unordered_map<std::string, int> keyMap = {
        {"VK_SPACE", VK_SPACE}, {"SPACE", VK_SPACE},
        {"VK_SHIFT", VK_SHIFT}, {"SHIFT", VK_SHIFT},
        {"VK_CONTROL", VK_CONTROL}, {"CTRL", VK_CONTROL},
        {"VK_ESCAPE", VK_ESCAPE}, {"ESC", VK_ESCAPE},
        {"VK_TAB", VK_TAB}, {"TAB", VK_TAB},
        {"VK_RETURN", VK_RETURN}, {"ENTER", VK_RETURN},
        {"VK_F1", VK_F1}, {"F1", VK_F1},
        {"VK_F2", VK_F2}, {"F2", VK_F2},
        {"VK_F3", VK_F3}, {"F3", VK_F3},
        {"VK_F4", VK_F4}, {"F4", VK_F4},
        {"VK_LBUTTON", VK_LBUTTON}, {"LMB", VK_LBUTTON},
        {"VK_RBUTTON", VK_RBUTTON}, {"RMB", VK_RBUTTON},
        {"VK_MBUTTON", VK_MBUTTON}, {"MMB", VK_MBUTTON}
    };

    auto it = keyMap.find(upperKey);
    if (it != keyMap.end()) return it->second;

    return 0;
}