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
    // --- SYSTEM ---
    System.WindowWidth = 1920;
    System.WindowHeight = 1080;
    System.WindowTitle = "Gamma Engine";
    System.IsWindowed = true;
    System.VSync = false;

    // --- GRAPHICS (GAME PROFILE) ---
    GameProfile.Graphics.RenderDistance = 8000.0f;
    GameProfile.Graphics.FOV = 45.0f;
    GameProfile.Graphics.NearZ = 1.0f;
    GameProfile.Graphics.FarZ = 8000.0f;

    // --- OPTIMIZATION (GAME PROFILE) ---
    GameProfile.Optimization.EnableOcclusionCulling = false;
    GameProfile.Optimization.EnableFrustumCulling = true;
    GameProfile.Optimization.EnableSizeCulling = true;
    GameProfile.Optimization.EnableZPrepass = true;
    GameProfile.Optimization.MinPixelSize = 5.0f;

    // --- LODs (GAME PROFILE) ---
    GameProfile.Lod.Enabled = true;
    GameProfile.Lod.Dist1 = 100.0f; GameProfile.Lod.Dist2 = 250.0f; GameProfile.Lod.Dist3 = 1000.0f;

    GameProfile.FloraLod.Enabled = true;
    GameProfile.FloraLod.Dist1 = 50.0f; GameProfile.FloraLod.Dist2 = 150.0f; GameProfile.FloraLod.Dist3 = 400.0f;

    GameProfile.TerrainLod.Enabled = true;
    GameProfile.TerrainLod.Dist1 = 350.0f; GameProfile.TerrainLod.Dist2 = 800.0f;
    GameProfile.TerrainLod.Step0 = 1; GameProfile.TerrainLod.Step1 = 2; GameProfile.TerrainLod.Step2 = 4;
    GameProfile.TerrainLod.EnableRVT = false;
    GameProfile.TerrainLod.RVTNearBlendDistance = 30.0f;
    GameProfile.TerrainLod.RVTResolution = 2048;
    GameProfile.TerrainLod.RVTCascadeCount = 3;
    GameProfile.TerrainLod.RVTCascade0Coverage = 200.0f;
    GameProfile.TerrainLod.RVTCascade1Coverage = 800.0f;
    GameProfile.TerrainLod.RVTCascade2Coverage = 4500.0f;
    GameProfile.TerrainLod.RVTCascade3Coverage = 16000.0f;

    // --- SHADOWS (GAME PROFILE) ---
    GameProfile.Shadows.Enabled = true;
    GameProfile.Shadows.Resolution = 1024;
    GameProfile.Shadows.CascadeCount = 3;
    GameProfile.Shadows.Splits[0] = 30.0f;
    GameProfile.Shadows.Splits[1] = 100.0f;
    GameProfile.Shadows.Splits[2] = 400.0f;
    GameProfile.Shadows.SoftShadows = true;
    GameProfile.Shadows.UpdateEveryFrame = true;
    GameProfile.Shadows.EnableShadowFrustumCulling = true;
    GameProfile.Shadows.EnableShadowAlphaCulling = true;
    GameProfile.Shadows.EnableShadowSizeCulling = true;
    GameProfile.Shadows.MaxShadowDistance = 500.0f;
    GameProfile.Shadows.MinShadowSize = 2.0f;
    GameProfile.Shadows.ShadowAlphaCullingDistance = 100.0f;
    GameProfile.Shadows.ShadowSizeCullingDistance = 30.0f;

    // --- LIGHTING (GAME PROFILE) ---
    GameProfile.Lighting.MaxLightDistance = 200.0f;

    AutoExposure.Enabled = true;
    AutoExposure.MinExposure = 0.1f;
    AutoExposure.MaxExposure = 3.0f;
    AutoExposure.SpeedUp = 3.0f;
    AutoExposure.SpeedDown = 1.0f;


    // По умолчанию редактор получает копию игровых настроек графики
    EditorProfile = GameProfile;



    // --- GLOBAL SETTINGS (Одинаковые для всех) ---
    Wind.Speed = 1.0f;
    Wind.TrunkBendAmplitude = 0.02f;
    Wind.RootStiffness = 0.2f;
    Wind.LeafFlutterSpeed = 4.0f;
    Wind.LeafFlutterAmplitude = 0.005f;
    Wind.LeafMicroTurbulence = 5.0f;

    Water.QualityLevel = 3;
    Water.EnableTessellation = true; Water.TessellationMaxFactor = 64.0f; Water.TessellationMaxDist = 2000.0f;
    Water.WaveCascades = 4;
    Water.EnableSSR = true; Water.EnableFoam = true; Water.EnableDepthAbsorption = true; Water.EnableRefraction = true;
    Water.GlobalWaveScale = 1.0f;

    VolumetricLighting.Enabled = true; VolumetricLighting.HalfResolution = true;
    VolumetricLighting.BlurRadius = 2.0f; VolumetricLighting.QualitySteps = 32;
    VolumetricLighting.Density = 0.05f; VolumetricLighting.GAnisotropy = 0.75f; VolumetricLighting.RayLength = 100.0f;

    PostProcess.EnableLensFlares = true;
    Bloom.Enabled = true; Bloom.Threshold = 1.0f; Bloom.Intensity = 0.05f;
    Logging.Render = true; Logging.Texture = false; Logging.Terrain = false; Logging.Physics = false; Logging.System = true;

    // --- INPUTS ---
    KeyBindings = {
        { "MoveForward", 'W', GammaHash("MoveForward") },     { "MoveBackward", 'S', GammaHash("MoveBackward") },
        { "MoveLeft", 'A', GammaHash("MoveLeft") },           { "MoveRight", 'D', GammaHash("MoveRight") },
        { "FlyUp", 'E', GammaHash("FlyUp") },                 { "FlyDown", 'Q', GammaHash("FlyDown") },
        { "Jump", VK_SPACE, GammaHash("Jump") },              { "Sprint", VK_SHIFT, GammaHash("Sprint") },
        { "ToggleCulling", VK_F1, GammaHash("ToggleCulling") },{ "ToggleDebugCam", VK_F2, GammaHash("ToggleDebugCam") },
        { "ToggleLODs", VK_F3, GammaHash("ToggleLODs") },     { "ToggleHZB", VK_F4, GammaHash("ToggleHZB") },
        { "ToggleWireframe", VK_TAB, GammaHash("ToggleWireframe") },
        { "RenderDistUp", 'X', GammaHash("RenderDistUp") },   { "RenderDistDown", 'Z', GammaHash("RenderDistDown") },
        { "Test", 'K', GammaHash("Test") }
    };

    EditorKeyBindings = {
        { "EditorMoveForward", 'W', GammaHash("EditorMoveForward") }, { "EditorMoveBackward", 'S', GammaHash("EditorMoveBackward") },
        { "EditorMoveLeft", 'A', GammaHash("EditorMoveLeft") },       { "EditorMoveRight", 'D', GammaHash("EditorMoveRight") },
        { "EditorFlyUp", 'E', GammaHash("EditorFlyUp") },             { "EditorFlyDown", 'Q', GammaHash("EditorFlyDown") }
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
            System.WindowWidth = sys.value("WindowWidth", System.WindowWidth);
            System.WindowHeight = sys.value("WindowHeight", System.WindowHeight);
            System.WindowTitle = sys.value("WindowTitle", System.WindowTitle);
            System.VSync = sys.value("VSync", System.VSync);

#ifdef GAMMA_EDITOR
            System.IsWindowed = true;
            System.VSync = false;
#else
            System.IsWindowed = sys.value("Windowed", System.IsWindowed);
#endif
        }

        if (j.contains("AutoExposure")) {
            auto& ae = j["AutoExposure"];
            AutoExposure.Enabled = ae.value("Enabled", AutoExposure.Enabled);
            AutoExposure.MinExposure = ae.value("MinExposure", AutoExposure.MinExposure);
            AutoExposure.MaxExposure = ae.value("MaxExposure", AutoExposure.MaxExposure);
            AutoExposure.SpeedUp = ae.value("SpeedUp", AutoExposure.SpeedUp);
            AutoExposure.SpeedDown = ae.value("SpeedDown", AutoExposure.SpeedDown);
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
            GameProfile.Graphics.RenderDistance = gfx.value("RenderDistance", GameProfile.Graphics.RenderDistance);
            GameProfile.Graphics.FOV = gfx.value("FOV", GameProfile.Graphics.FOV);
            GameProfile.Graphics.NearZ = gfx.value("NearZ", GameProfile.Graphics.NearZ);
            GameProfile.Graphics.FarZ = gfx.value("FarZ", GameProfile.Graphics.FarZ);

            if (gfx.contains("LOD")) {
                auto& lod = gfx["LOD"];
                GameProfile.Lod.Enabled = lod.value("Enabled", GameProfile.Lod.Enabled);
                GameProfile.Lod.Dist1 = lod.value("Distance1", GameProfile.Lod.Dist1);
                GameProfile.Lod.Dist2 = lod.value("Distance2", GameProfile.Lod.Dist2);
                GameProfile.Lod.Dist3 = lod.value("Distance3", GameProfile.Lod.Dist3);
            }
            if (gfx.contains("FloraLOD")) {
                auto& flod = gfx["FloraLOD"];
                GameProfile.FloraLod.Enabled = flod.value("Enabled", GameProfile.FloraLod.Enabled);
                GameProfile.FloraLod.Dist1 = flod.value("Distance1", GameProfile.FloraLod.Dist1);
                GameProfile.FloraLod.Dist2 = flod.value("Distance2", GameProfile.FloraLod.Dist2);
                GameProfile.FloraLod.Dist3 = flod.value("Distance3", GameProfile.FloraLod.Dist3);
            }
            if (gfx.contains("TerrainLOD")) {
                auto& tlod = gfx["TerrainLOD"];
                GameProfile.TerrainLod.Enabled = tlod.value("Enabled", GameProfile.TerrainLod.Enabled);
                GameProfile.TerrainLod.Dist1 = tlod.value("Distance1", GameProfile.TerrainLod.Dist1);
                GameProfile.TerrainLod.Dist2 = tlod.value("Distance2", GameProfile.TerrainLod.Dist2);
                GameProfile.TerrainLod.Step0 = tlod.value("Step0", GameProfile.TerrainLod.Step0);
                GameProfile.TerrainLod.Step1 = tlod.value("Step1", GameProfile.TerrainLod.Step1);
                GameProfile.TerrainLod.Step2 = tlod.value("Step2", GameProfile.TerrainLod.Step2);

                GameProfile.TerrainLod.EnableRVT = tlod.value("EnableRVT", GameProfile.TerrainLod.EnableRVT);
                GameProfile.TerrainLod.RVTNearBlendDistance = tlod.value("RVTNearBlendDistance", GameProfile.TerrainLod.RVTNearBlendDistance);
                GameProfile.TerrainLod.RVTResolution = tlod.value("RVTResolution", GameProfile.TerrainLod.RVTResolution);
                GameProfile.TerrainLod.RVTCascadeCount = tlod.value("RVTCascadeCount", GameProfile.TerrainLod.RVTCascadeCount);
                GameProfile.TerrainLod.RVTCascade0Coverage = tlod.value("RVTCascade0Coverage", GameProfile.TerrainLod.RVTCascade0Coverage);
                GameProfile.TerrainLod.RVTCascade1Coverage = tlod.value("RVTCascade1Coverage", GameProfile.TerrainLod.RVTCascade1Coverage);
                GameProfile.TerrainLod.RVTCascade2Coverage = tlod.value("RVTCascade2Coverage", GameProfile.TerrainLod.RVTCascade2Coverage);
                GameProfile.TerrainLod.RVTCascade3Coverage = tlod.value("RVTCascade3Coverage", GameProfile.TerrainLod.RVTCascade3Coverage);
            }
        }

        if (j.contains("Lighting")) {
            auto& l = j["Lighting"];
            GameProfile.Lighting.MaxLightDistance = l.value("MaxLightDistance", GameProfile.Lighting.MaxLightDistance);
        }

        if (j.contains("Shadows")) {
            auto& sh = j["Shadows"];
            GameProfile.Shadows.Enabled = sh.value("Enabled", GameProfile.Shadows.Enabled);
            GameProfile.Shadows.Resolution = sh.value("Resolution", GameProfile.Shadows.Resolution);
            GameProfile.Shadows.CascadeCount = sh.value("CascadeCount", GameProfile.Shadows.CascadeCount);
            GameProfile.Shadows.SoftShadows = sh.value("SoftShadows", GameProfile.Shadows.SoftShadows);
            GameProfile.Shadows.UpdateEveryFrame = sh.value("UpdateEveryFrame", GameProfile.Shadows.UpdateEveryFrame);
            GameProfile.Shadows.EnableShadowFrustumCulling = sh.value("EnableShadowFrustumCulling", GameProfile.Shadows.EnableShadowFrustumCulling);
            GameProfile.Shadows.EnableShadowAlphaCulling = sh.value("EnableShadowAlphaCulling", GameProfile.Shadows.EnableShadowAlphaCulling);
            GameProfile.Shadows.EnableShadowSizeCulling = sh.value("EnableShadowSizeCulling", GameProfile.Shadows.EnableShadowSizeCulling);
            GameProfile.Shadows.MaxShadowDistance = sh.value("MaxShadowDistance", GameProfile.Shadows.MaxShadowDistance);
            GameProfile.Shadows.MinShadowSize = sh.value("MinShadowSize", GameProfile.Shadows.MinShadowSize);
            GameProfile.Shadows.ShadowAlphaCullingDistance = sh.value("ShadowAlphaCullingDistance", GameProfile.Shadows.ShadowAlphaCullingDistance);
            GameProfile.Shadows.ShadowSizeCullingDistance = sh.value("ShadowSizeCullingDistance", GameProfile.Shadows.ShadowSizeCullingDistance);

            if (sh.contains("Splits") && sh["Splits"].is_array()) {
                GameProfile.Shadows.Splits[0] = sh["Splits"].size() > 0 ? (float)sh["Splits"][0] : 30.0f;
                GameProfile.Shadows.Splits[1] = sh["Splits"].size() > 1 ? (float)sh["Splits"][1] : 100.0f;
                GameProfile.Shadows.Splits[2] = sh["Splits"].size() > 2 ? (float)sh["Splits"][2] : 400.0f;
            }
        }

        if (j.contains("Optimization")) {
            auto& opt = j["Optimization"];
            GameProfile.Optimization.EnableOcclusionCulling = opt.value("EnableOcclusionCulling", GameProfile.Optimization.EnableOcclusionCulling);
            GameProfile.Optimization.EnableFrustumCulling = opt.value("EnableFrustumCulling", GameProfile.Optimization.EnableFrustumCulling);
            GameProfile.Optimization.EnableSizeCulling = opt.value("EnableSizeCulling", GameProfile.Optimization.EnableSizeCulling);
            GameProfile.Optimization.EnableZPrepass = opt.value("EnableZPrepass", GameProfile.Optimization.EnableZPrepass);
            GameProfile.Optimization.MinPixelSize = opt.value("MinPixelSize", GameProfile.Optimization.MinPixelSize);
        }

        // --- ВАЖНО: Копируем игровые настройки в редактор ---
        // (Позже мы добавим чтение отдельного блока "EditorGraphics" из JSON)
        EditorProfile = GameProfile;

        // --- НОВОЕ: Загружаем 100% настроек редактора ---
        if (j.contains("EditorProfile")) {
            auto& edProf = j["EditorProfile"];

            if (edProf.contains("Graphics")) {
                EditorProfile.Graphics.RenderDistance = edProf["Graphics"].value("RenderDistance", EditorProfile.Graphics.RenderDistance);
                EditorProfile.Graphics.FOV = edProf["Graphics"].value("FOV", EditorProfile.Graphics.FOV);
                EditorProfile.Graphics.NearZ = edProf["Graphics"].value("NearZ", EditorProfile.Graphics.NearZ);
                EditorProfile.Graphics.FarZ = edProf["Graphics"].value("FarZ", EditorProfile.Graphics.FarZ);
            }

            if (edProf.contains("Lighting")) {
                EditorProfile.Lighting.MaxLightDistance = edProf["Lighting"].value("MaxLightDistance", EditorProfile.Lighting.MaxLightDistance);

            }

            if (edProf.contains("Optimization")) {
                EditorProfile.Optimization.EnableOcclusionCulling = edProf["Optimization"].value("EnableOcclusionCulling", EditorProfile.Optimization.EnableOcclusionCulling);
                EditorProfile.Optimization.EnableFrustumCulling = edProf["Optimization"].value("EnableFrustumCulling", EditorProfile.Optimization.EnableFrustumCulling);
                EditorProfile.Optimization.EnableSizeCulling = edProf["Optimization"].value("EnableSizeCulling", EditorProfile.Optimization.EnableSizeCulling);
                EditorProfile.Optimization.EnableZPrepass = edProf["Optimization"].value("EnableZPrepass", EditorProfile.Optimization.EnableZPrepass);
                EditorProfile.Optimization.MinPixelSize = edProf["Optimization"].value("MinPixelSize", EditorProfile.Optimization.MinPixelSize);
            }
            if (edProf.contains("Shadows")) {
                EditorProfile.Shadows.Enabled = edProf["Shadows"].value("Enabled", EditorProfile.Shadows.Enabled);
                EditorProfile.Shadows.Resolution = edProf["Shadows"].value("Resolution", EditorProfile.Shadows.Resolution);
                EditorProfile.Shadows.CascadeCount = edProf["Shadows"].value("CascadeCount", EditorProfile.Shadows.CascadeCount);
                EditorProfile.Shadows.SoftShadows = edProf["Shadows"].value("SoftShadows", EditorProfile.Shadows.SoftShadows);
                EditorProfile.Shadows.UpdateEveryFrame = edProf["Shadows"].value("UpdateEveryFrame", EditorProfile.Shadows.UpdateEveryFrame);
                EditorProfile.Shadows.EnableShadowFrustumCulling = edProf["Shadows"].value("EnableShadowFrustumCulling", EditorProfile.Shadows.EnableShadowFrustumCulling);
                EditorProfile.Shadows.EnableShadowAlphaCulling = edProf["Shadows"].value("EnableShadowAlphaCulling", EditorProfile.Shadows.EnableShadowAlphaCulling);
                EditorProfile.Shadows.EnableShadowSizeCulling = edProf["Shadows"].value("EnableShadowSizeCulling", EditorProfile.Shadows.EnableShadowSizeCulling);
                EditorProfile.Shadows.MaxShadowDistance = edProf["Shadows"].value("MaxShadowDistance", EditorProfile.Shadows.MaxShadowDistance);
                EditorProfile.Shadows.MinShadowSize = edProf["Shadows"].value("MinShadowSize", EditorProfile.Shadows.MinShadowSize);
                EditorProfile.Shadows.ShadowAlphaCullingDistance = edProf["Shadows"].value("ShadowAlphaCullingDistance", EditorProfile.Shadows.ShadowAlphaCullingDistance);
                EditorProfile.Shadows.ShadowSizeCullingDistance = edProf["Shadows"].value("ShadowSizeCullingDistance", EditorProfile.Shadows.ShadowSizeCullingDistance);

                if (edProf["Shadows"].contains("Splits") && edProf["Shadows"]["Splits"].is_array()) {
                    EditorProfile.Shadows.Splits[0] = edProf["Shadows"]["Splits"].size() > 0 ? (float)edProf["Shadows"]["Splits"][0] : 30.0f;
                    EditorProfile.Shadows.Splits[1] = edProf["Shadows"]["Splits"].size() > 1 ? (float)edProf["Shadows"]["Splits"][1] : 100.0f;
                    EditorProfile.Shadows.Splits[2] = edProf["Shadows"]["Splits"].size() > 2 ? (float)edProf["Shadows"]["Splits"][2] : 400.0f;
                }
            }
            if (edProf.contains("LOD")) {
                EditorProfile.Lod.Enabled = edProf["LOD"].value("Enabled", EditorProfile.Lod.Enabled);
                EditorProfile.Lod.Dist1 = edProf["LOD"].value("Distance1", EditorProfile.Lod.Dist1);
                EditorProfile.Lod.Dist2 = edProf["LOD"].value("Distance2", EditorProfile.Lod.Dist2);
                EditorProfile.Lod.Dist3 = edProf["LOD"].value("Distance3", EditorProfile.Lod.Dist3);
            }
            if (edProf.contains("FloraLOD")) {
                EditorProfile.FloraLod.Enabled = edProf["FloraLOD"].value("Enabled", EditorProfile.FloraLod.Enabled);
                EditorProfile.FloraLod.Dist1 = edProf["FloraLOD"].value("Distance1", EditorProfile.FloraLod.Dist1);
                EditorProfile.FloraLod.Dist2 = edProf["FloraLOD"].value("Distance2", EditorProfile.FloraLod.Dist2);
                EditorProfile.FloraLod.Dist3 = edProf["FloraLOD"].value("Distance3", EditorProfile.FloraLod.Dist3);
            }
            if (edProf.contains("TerrainLOD")) {
                EditorProfile.TerrainLod.Enabled = edProf["TerrainLOD"].value("Enabled", EditorProfile.TerrainLod.Enabled);
                EditorProfile.TerrainLod.Dist1 = edProf["TerrainLOD"].value("Distance1", EditorProfile.TerrainLod.Dist1);
                EditorProfile.TerrainLod.Dist2 = edProf["TerrainLOD"].value("Distance2", EditorProfile.TerrainLod.Dist2);
                EditorProfile.TerrainLod.Step0 = edProf["TerrainLOD"].value("Step0", EditorProfile.TerrainLod.Step0);
                EditorProfile.TerrainLod.Step1 = edProf["TerrainLOD"].value("Step1", EditorProfile.TerrainLod.Step1);
                EditorProfile.TerrainLod.Step2 = edProf["TerrainLOD"].value("Step2", EditorProfile.TerrainLod.Step2);
                EditorProfile.TerrainLod.EnableRVT = edProf["TerrainLOD"].value("EnableRVT", EditorProfile.TerrainLod.EnableRVT);
                EditorProfile.TerrainLod.RVTNearBlendDistance = edProf["TerrainLOD"].value("RVTNearBlendDistance", EditorProfile.TerrainLod.RVTNearBlendDistance);
                EditorProfile.TerrainLod.RVTResolution = edProf["TerrainLOD"].value("RVTResolution", EditorProfile.TerrainLod.RVTResolution);
                EditorProfile.TerrainLod.RVTCascadeCount = edProf["TerrainLOD"].value("RVTCascadeCount", EditorProfile.TerrainLod.RVTCascadeCount);
                EditorProfile.TerrainLod.RVTCascade0Coverage = edProf["TerrainLOD"].value("RVTCascade0Coverage", EditorProfile.TerrainLod.RVTCascade0Coverage);
                EditorProfile.TerrainLod.RVTCascade1Coverage = edProf["TerrainLOD"].value("RVTCascade1Coverage", EditorProfile.TerrainLod.RVTCascade1Coverage);
                EditorProfile.TerrainLod.RVTCascade2Coverage = edProf["TerrainLOD"].value("RVTCascade2Coverage", EditorProfile.TerrainLod.RVTCascade2Coverage);
                EditorProfile.TerrainLod.RVTCascade3Coverage = edProf["TerrainLOD"].value("RVTCascade3Coverage", EditorProfile.TerrainLod.RVTCascade3Coverage);
            }
        }


        if (j.contains("Wind")) {
            auto& w = j["Wind"];
            Wind.Speed = w.value("Speed", Wind.Speed);
            Wind.TrunkBendAmplitude = w.value("TrunkBendAmplitude", Wind.TrunkBendAmplitude);
            Wind.RootStiffness = w.value("RootStiffness", Wind.RootStiffness);
            Wind.LeafFlutterSpeed = w.value("LeafFlutterSpeed", Wind.LeafFlutterSpeed);
            Wind.LeafFlutterAmplitude = w.value("LeafFlutterAmplitude", Wind.LeafFlutterAmplitude);
            Wind.LeafMicroTurbulence = w.value("LeafMicroTurbulence", Wind.LeafMicroTurbulence);
        }

        if (j.contains("Water")) {
            auto& w = j["Water"];
            Water.QualityLevel = w.value("QualityLevel", Water.QualityLevel);
            Water.EnableTessellation = w.value("EnableTessellation", Water.EnableTessellation);
            Water.TessellationMaxFactor = w.value("TessellationMaxFactor", Water.TessellationMaxFactor);
            Water.TessellationMaxDist = w.value("TessellationMaxDistance", Water.TessellationMaxDist);
            Water.WaveCascades = w.value("WaveCascades", Water.WaveCascades);
            Water.EnableSSR = w.value("EnableSSR", Water.EnableSSR);
            Water.EnableFoam = w.value("EnableFoam", Water.EnableFoam);
            Water.EnableDepthAbsorption = w.value("EnableDepthAbsorption", Water.EnableDepthAbsorption);
            Water.GlobalWaveScale = w.value("GlobalWaveScale", Water.GlobalWaveScale);
            Water.EnableRefraction = w.value("EnableRefraction", Water.EnableRefraction);
        }

        if (j.contains("VolumetricLighting")) {
            auto& vl = j["VolumetricLighting"];
            VolumetricLighting.Enabled = vl.value("Enabled", VolumetricLighting.Enabled);
            VolumetricLighting.HalfResolution = vl.value("HalfResolution", VolumetricLighting.HalfResolution);
            VolumetricLighting.BlurRadius = vl.value("BlurRadius", VolumetricLighting.BlurRadius);
            VolumetricLighting.QualitySteps = vl.value("QualitySteps", VolumetricLighting.QualitySteps);
            VolumetricLighting.Density = vl.value("Density", VolumetricLighting.Density);
            VolumetricLighting.GAnisotropy = vl.value("GAnisotropy", VolumetricLighting.GAnisotropy);
            VolumetricLighting.RayLength = vl.value("RayLength", VolumetricLighting.RayLength);
        }

        if (j.contains("Bloom")) {
            auto& bl = j["Bloom"];
            Bloom.Enabled = bl.value("Enabled", Bloom.Enabled);
            Bloom.Threshold = bl.value("Threshold", Bloom.Threshold);
            Bloom.Intensity = bl.value("Intensity", Bloom.Intensity);
        }

        if (j.contains("PostProcess")) {
            PostProcess.EnableLensFlares = j["PostProcess"].value("EnableLensFlares", PostProcess.EnableLensFlares);
        }

        if (j.contains("Input")) {
            auto& inp = j["Input"];
            for (auto it = inp.begin(); it != inp.end(); ++it) {
                std::string actName = it.key();
                int code = ParseKeyCode(it.value().get<std::string>());
                auto bIt = std::find_if(KeyBindings.begin(), KeyBindings.end(), [&](const KeyBinding& b) { return b.actionName == actName; });
                if (bIt != KeyBindings.end()) {
                    bIt->keyCode = code;
                }
                else {
                    KeyBindings.push_back({ actName, code, GammaHash(actName.c_str()) });
                }
            }
        }

        if (j.contains("EditorInput")) {
            auto& edInp = j["EditorInput"];
            for (auto& bind : EditorKeyBindings) {
                if (edInp.contains(bind.actionName)) {
                    int code = ParseKeyCode(edInp[bind.actionName].get<std::string>());
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
        {"VK_MBUTTON", VK_MBUTTON}, {"MMB", VK_MBUTTON},
        { "VK_UP", VK_UP }, {"UP", VK_UP},
        {"VK_DOWN", VK_DOWN}, {"DOWN", VK_DOWN},
        {"VK_LEFT", VK_LEFT}, {"LEFT", VK_LEFT},
        {"VK_RIGHT", VK_RIGHT}, {"RIGHT", VK_RIGHT}
    };

    auto it = keyMap.find(upperKey);
    if (it != keyMap.end()) return it->second;
    return 0;
}

std::string EngineConfig::KeyCodeToString(int keyCode) {
    if (keyCode >= 'A' && keyCode <= 'Z') return std::string(1, (char)keyCode);
    if (keyCode >= '0' && keyCode <= '9') return std::string(1, (char)keyCode);
    switch (keyCode) {
    case VK_SPACE: return "SPACE";
    case VK_SHIFT: return "SHIFT";
    case VK_CONTROL: return "CTRL";
    case VK_ESCAPE: return "ESC";
    case VK_TAB: return "TAB";
    case VK_RETURN: return "ENTER";
    case VK_UP: return "VK_UP";
    case VK_DOWN: return "VK_DOWN";
    case VK_LEFT: return "VK_LEFT";
    case VK_RIGHT: return "VK_RIGHT";
    case VK_F1: return "F1"; case VK_F2: return "F2";
    case VK_F3: return "F3"; case VK_F4: return "F4";
    case VK_LBUTTON: return "LMB";
    case VK_RBUTTON: return "RMB";
    case VK_MBUTTON: return "MMB";
    default: return std::to_string(keyCode);
    }
}

bool EngineConfig::Save(const std::string& filename) {
    json j;
    std::ifstream inFile(filename);
    if (inFile.is_open()) {
        try { inFile >> j; }
        catch (...) {}
        inFile.close();
    }

    // Сохраняем Lighting
    j["Lighting"]["MaxLightDistance"] = GameProfile.Lighting.MaxLightDistance;


    // Сохраняем System
    j["System"]["WindowWidth"] = System.WindowWidth;
    j["System"]["WindowHeight"] = System.WindowHeight;
    j["System"]["WindowTitle"] = System.WindowTitle;
    j["System"]["Windowed"] = System.IsWindowed;
    j["System"]["VSync"] = System.VSync;

    // Сохраняем Graphics (GameProfile)
    j["Graphics"]["RenderDistance"] = GameProfile.Graphics.RenderDistance;
    j["Graphics"]["FOV"] = GameProfile.Graphics.FOV;
    j["Graphics"]["NearZ"] = GameProfile.Graphics.NearZ;
    j["Graphics"]["FarZ"] = GameProfile.Graphics.FarZ;

    // Сохраняем Optimization (GameProfile)
    j["Optimization"]["EnableOcclusionCulling"] = GameProfile.Optimization.EnableOcclusionCulling;
    j["Optimization"]["EnableFrustumCulling"] = GameProfile.Optimization.EnableFrustumCulling;
    j["Optimization"]["EnableSizeCulling"] = GameProfile.Optimization.EnableSizeCulling;
    j["Optimization"]["EnableZPrepass"] = GameProfile.Optimization.EnableZPrepass;
    j["Optimization"]["MinPixelSize"] = GameProfile.Optimization.MinPixelSize;


    j["AutoExposure"]["Enabled"] = AutoExposure.Enabled;
    j["AutoExposure"]["MinExposure"] = AutoExposure.MinExposure;
    j["AutoExposure"]["MaxExposure"] = AutoExposure.MaxExposure;
    j["AutoExposure"]["SpeedUp"] = AutoExposure.SpeedUp;
    j["AutoExposure"]["SpeedDown"] = AutoExposure.SpeedDown;

    // Сохраняем Shadows (GameProfile)
    j["Shadows"]["Enabled"] = GameProfile.Shadows.Enabled;
    j["Shadows"]["Resolution"] = GameProfile.Shadows.Resolution;
    j["Shadows"]["CascadeCount"] = GameProfile.Shadows.CascadeCount;
    j["Shadows"]["SoftShadows"] = GameProfile.Shadows.SoftShadows;
    j["Shadows"]["UpdateEveryFrame"] = GameProfile.Shadows.UpdateEveryFrame;
    j["Shadows"]["EnableShadowFrustumCulling"] = GameProfile.Shadows.EnableShadowFrustumCulling;
    j["Shadows"]["EnableShadowAlphaCulling"] = GameProfile.Shadows.EnableShadowAlphaCulling;
    j["Shadows"]["EnableShadowSizeCulling"] = GameProfile.Shadows.EnableShadowSizeCulling;
    j["Shadows"]["MaxShadowDistance"] = GameProfile.Shadows.MaxShadowDistance;
    j["Shadows"]["MinShadowSize"] = GameProfile.Shadows.MinShadowSize;
    j["Shadows"]["ShadowAlphaCullingDistance"] = GameProfile.Shadows.ShadowAlphaCullingDistance;
    j["Shadows"]["ShadowSizeCullingDistance"] = GameProfile.Shadows.ShadowSizeCullingDistance;


    // Сохраняем EditorProfile
    j["EditorProfile"]["Graphics"]["RenderDistance"] = EditorProfile.Graphics.RenderDistance;
    j["EditorProfile"]["Graphics"]["FOV"] = EditorProfile.Graphics.FOV;
    j["EditorProfile"]["Graphics"]["NearZ"] = EditorProfile.Graphics.NearZ;
    j["EditorProfile"]["Graphics"]["FarZ"] = EditorProfile.Graphics.FarZ;

    j["EditorProfile"]["Optimization"]["EnableOcclusionCulling"] = EditorProfile.Optimization.EnableOcclusionCulling;
    j["EditorProfile"]["Optimization"]["EnableFrustumCulling"] = EditorProfile.Optimization.EnableFrustumCulling;
    j["EditorProfile"]["Optimization"]["EnableSizeCulling"] = EditorProfile.Optimization.EnableSizeCulling;
    j["EditorProfile"]["Optimization"]["EnableZPrepass"] = EditorProfile.Optimization.EnableZPrepass;
    j["EditorProfile"]["Optimization"]["MinPixelSize"] = EditorProfile.Optimization.MinPixelSize;

    j["EditorProfile"]["Shadows"]["Enabled"] = EditorProfile.Shadows.Enabled;
    j["EditorProfile"]["Shadows"]["Resolution"] = EditorProfile.Shadows.Resolution;
    j["EditorProfile"]["Shadows"]["CascadeCount"] = EditorProfile.Shadows.CascadeCount;
    j["EditorProfile"]["Shadows"]["SoftShadows"] = EditorProfile.Shadows.SoftShadows;
    j["EditorProfile"]["Shadows"]["UpdateEveryFrame"] = EditorProfile.Shadows.UpdateEveryFrame;
    j["EditorProfile"]["Shadows"]["EnableShadowFrustumCulling"] = EditorProfile.Shadows.EnableShadowFrustumCulling;
    j["EditorProfile"]["Shadows"]["EnableShadowAlphaCulling"] = EditorProfile.Shadows.EnableShadowAlphaCulling;
    j["EditorProfile"]["Shadows"]["EnableShadowSizeCulling"] = EditorProfile.Shadows.EnableShadowSizeCulling;
    j["EditorProfile"]["Shadows"]["MaxShadowDistance"] = EditorProfile.Shadows.MaxShadowDistance;
    j["EditorProfile"]["Shadows"]["MinShadowSize"] = EditorProfile.Shadows.MinShadowSize;
    j["EditorProfile"]["Shadows"]["ShadowAlphaCullingDistance"] = EditorProfile.Shadows.ShadowAlphaCullingDistance;
    j["EditorProfile"]["Shadows"]["ShadowSizeCullingDistance"] = EditorProfile.Shadows.ShadowSizeCullingDistance;
    j["EditorProfile"]["Shadows"]["Splits"] = { EditorProfile.Shadows.Splits[0], EditorProfile.Shadows.Splits[1], EditorProfile.Shadows.Splits[2] };

    j["EditorProfile"]["LOD"]["Enabled"] = EditorProfile.Lod.Enabled;
    j["EditorProfile"]["LOD"]["Distance1"] = EditorProfile.Lod.Dist1;
    j["EditorProfile"]["LOD"]["Distance2"] = EditorProfile.Lod.Dist2;
    j["EditorProfile"]["LOD"]["Distance3"] = EditorProfile.Lod.Dist3;

    j["EditorProfile"]["FloraLOD"]["Enabled"] = EditorProfile.FloraLod.Enabled;
    j["EditorProfile"]["FloraLOD"]["Distance1"] = EditorProfile.FloraLod.Dist1;
    j["EditorProfile"]["FloraLOD"]["Distance2"] = EditorProfile.FloraLod.Dist2;
    j["EditorProfile"]["FloraLOD"]["Distance3"] = EditorProfile.FloraLod.Dist3;

    j["EditorProfile"]["TerrainLOD"]["Enabled"] = EditorProfile.TerrainLod.Enabled;
    j["EditorProfile"]["TerrainLOD"]["Distance1"] = EditorProfile.TerrainLod.Dist1;
    j["EditorProfile"]["TerrainLOD"]["Distance2"] = EditorProfile.TerrainLod.Dist2;
    j["EditorProfile"]["TerrainLOD"]["Step0"] = EditorProfile.TerrainLod.Step0;
    j["EditorProfile"]["TerrainLOD"]["Step1"] = EditorProfile.TerrainLod.Step1;
    j["EditorProfile"]["TerrainLOD"]["Step2"] = EditorProfile.TerrainLod.Step2;
    j["EditorProfile"]["TerrainLOD"]["EnableRVT"] = EditorProfile.TerrainLod.EnableRVT;
    j["EditorProfile"]["TerrainLOD"]["RVTNearBlendDistance"] = EditorProfile.TerrainLod.RVTNearBlendDistance;
    j["EditorProfile"]["TerrainLOD"]["RVTResolution"] = EditorProfile.TerrainLod.RVTResolution;
    j["EditorProfile"]["TerrainLOD"]["RVTCascadeCount"] = EditorProfile.TerrainLod.RVTCascadeCount;
    j["EditorProfile"]["TerrainLOD"]["RVTCascade0Coverage"] = EditorProfile.TerrainLod.RVTCascade0Coverage;
    j["EditorProfile"]["TerrainLOD"]["RVTCascade1Coverage"] = EditorProfile.TerrainLod.RVTCascade1Coverage;
    j["EditorProfile"]["TerrainLOD"]["RVTCascade2Coverage"] = EditorProfile.TerrainLod.RVTCascade2Coverage;
    j["EditorProfile"]["TerrainLOD"]["RVTCascade3Coverage"] = EditorProfile.TerrainLod.RVTCascade3Coverage;

    j["EditorProfile"]["Lighting"]["MaxLightDistance"] = EditorProfile.Lighting.MaxLightDistance;

    // Сохраняем Инпуты
    for (const auto& bind : KeyBindings) {
        j["Input"][bind.actionName] = KeyCodeToString(bind.keyCode);
    }
    for (const auto& bind : EditorKeyBindings) {
        j["EditorInput"][bind.actionName] = KeyCodeToString(bind.keyCode);
    }

    std::ofstream outFile(filename);
    if (outFile.is_open()) {
        outFile << j.dump(4);
        return true;
    }
    return false;
}

void EngineConfig::ResetToDefaults() {
    SetupDefaults();
    bInputBindingsChanged = true;
}