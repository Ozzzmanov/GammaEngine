// ================================================================================
// EngineConfig.h
// ================================================================================
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "../Core/Prerequisites.h"
#include "../API/GammaAPI.h"

struct KeyBinding {
    std::string actionName;
    int keyCode;
    uint32_t hash = 0;
};

struct SystemSettings {
    int WindowWidth;
    int WindowHeight;
    std::string WindowTitle;
    bool IsWindowed;
    bool VSync;
};

struct GraphicsSettings {
    float RenderDistance;
    float FOV;
    float NearZ;
    float FarZ;
};

struct OptimizationSettings {
    bool EnableOcclusionCulling;
    bool EnableFrustumCulling;
    bool EnableSizeCulling;
    bool EnableZPrepass;
    float MinPixelSize;
};

struct LogSettings {
    bool Render; bool Texture; bool Terrain; bool Physics; bool System;
};

struct LodSettings {
    bool Enabled; float Dist1; float Dist2; float Dist3;
};

struct WindSettings {
    float Speed; float TrunkBendAmplitude; float RootStiffness;
    float LeafFlutterSpeed; float LeafFlutterAmplitude; float LeafMicroTurbulence;
};

struct ShadowSettings {
    bool Enabled; int Resolution; int CascadeCount; float Splits[3];
    bool SoftShadows; bool UpdateEveryFrame;
    bool EnableShadowFrustumCulling; bool EnableShadowAlphaCulling; bool EnableShadowSizeCulling;
    float MaxShadowDistance; float MinShadowSize;
    float ShadowAlphaCullingDistance; float ShadowSizeCullingDistance;
};

struct TerrainLodSettings {
    bool Enabled; float Dist1; float Dist2; int Step0, Step1, Step2;
    bool EnableRVT; float RVTNearBlendDistance; int RVTResolution; int RVTCascadeCount;
    float RVTCascade0Coverage; float RVTCascade1Coverage; float RVTCascade2Coverage; float RVTCascade3Coverage;
};

struct WaterSettings {
    int QualityLevel; bool EnableTessellation; float TessellationMaxFactor; float TessellationMaxDist;
    int WaveCascades; bool EnableSSR; bool EnableFoam; bool EnableDepthAbsorption;
    float GlobalWaveScale; bool EnableRefraction;
};

struct VolumetricLightingSettings {
    bool Enabled; bool HalfResolution; float BlurRadius; int QualitySteps;
    float Density; float GAnisotropy; float RayLength;
};

struct PostProcessSettings {
    bool EnableLensFlares;
};

struct BloomSettings {
    bool Enabled; float Threshold; float Intensity;
};

struct LightingSettings {
    float MaxLightDistance; // На каком расстоянии отключать мелкий свет
};

struct AutoExposureSettings {
    bool Enabled;
    float MinExposure;  // Насколько темным может стать экран (глядя на солнце)
    float MaxExposure;  // Насколько светлым может стать экран (ночью)
    float SpeedUp;      // Скорость привыкания глаз к свету
    float SpeedDown;    // Скорость привыкания глаз к темноте
};

// --- ПРОФИЛЬ РЕНДЕРА ---
struct RenderProfile {
    GraphicsSettings Graphics;
    OptimizationSettings Optimization;
    ShadowSettings Shadows;
    TerrainLodSettings TerrainLod;
    LodSettings Lod;
    LodSettings FloraLod;
    LightingSettings Lighting;
};

class EngineConfig {
public:
    static EngineConfig& Get() {
        static EngineConfig instance;
        return instance;
    }

    bool UseEditorProfile = true;

    // НОВОЕ: Const и Non-const перегрузки для компилятора!
    RenderProfile& GetActiveProfile() {
#ifdef GAMMA_EDITOR
        return UseEditorProfile ? EditorProfile : GameProfile;
#else
        return GameProfile;
#endif
    }

    const RenderProfile& GetActiveProfile() const {
#ifdef GAMMA_EDITOR
        return UseEditorProfile ? EditorProfile : GameProfile;
#else
        return GameProfile;
#endif
    }

    bool Load(const std::string& filename);
    bool Save(const std::string& filename);
    void ResetToDefaults();
    std::string KeyCodeToString(int keyCode);

    bool bInputBindingsChanged = false;

    SystemSettings System;
    LogSettings Logging;
    WindSettings Wind;
    WaterSettings Water;
    VolumetricLightingSettings VolumetricLighting;
    AutoExposureSettings AutoExposure;
    PostProcessSettings PostProcess;
    BloomSettings Bloom;

    RenderProfile GameProfile;
    RenderProfile EditorProfile;

    std::vector<KeyBinding> KeyBindings;
    std::vector<KeyBinding> EditorKeyBindings;

private:
    EngineConfig();
    ~EngineConfig() = default;

    void SetupDefaults();
    int ParseKeyCode(const std::string& keyStr);
};