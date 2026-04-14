//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// RenderPipeline.h
// Главный конвейер графики. Выстраивает порядок проходов (Z-Prepass, Shadows, 
// Opaque, Water, PostProcess) и управляет глобальными состояниями рендера.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <memory>
#include <vector>
#include <SpriteBatch.h>
#include <CommonStates.h>
#include "RenderTarget.h"
#include "ClipmapRvtManager.h"
#include "../Resources/RvtBaker.h"
#include "../API/GammaAPI.h"

class DX11Renderer;
class Camera;
class WaterVLO;
class LevelTextureManager;
class TerrainArrayManager;
class StaticGpuScene;
class TerrainGpuScene;
class HzbManager;
class GpuCullingSystem;
class Shader;
class FloraGpuScene;
class ShadowManager;
class WorldEnvironment;
class PostProcessManager;
template <typename T> class ConstantBuffer;
struct CB_VS_Transform;

__declspec(align(16)) struct CB_PS_Lighting {
    DirectX::XMMATRIX InvViewProj;
    DirectX::XMMATRIX LightViewProj[4];
    DirectX::XMMATRIX ViewMatrix;
    DirectX::XMFLOAT4 CascadeSplits;
    DirectX::XMFLOAT3 CameraPos;
    float CascadeCount;

    DirectX::XMFLOAT4 LightSettings;
    DirectX::XMFLOAT4 ExtraSettings;
};

struct RenderSettings {
    float RenderDistance = 8000.0f;
    bool EnableCulling = true;
    bool EnableLODs = true;
    bool EnableZPrepass = true;
    bool FreezeCulling = false;
    bool IsWireframe = false;
    bool ShowHzbDebug = false;
};

struct RenderStats {
    int GpuManaged = 1;
};

class RenderPipeline {
public:
    RenderPipeline(DX11Renderer* renderer);
    ~RenderPipeline();

    bool Initialize();

    StaticGpuScene* GetStaticScene() const { return m_staticScene.get(); }
    TerrainGpuScene* GetTerrainScene() const { return m_terrainGpuScene.get(); }
    TerrainArrayManager* GetTerrainArrayManager() const { return m_terrainArrayManager.get(); }
    FloraGpuScene* GetFloraScene() const { return m_floraScene.get(); }

    ID3D11Buffer* GetInstanceIdBuffer() const { return m_instanceIdBuffer.Get(); }

    RenderStats RenderFrame(
        Camera* camera,
        const EnvironmentData& env,
        const std::vector<std::shared_ptr<WaterVLO>>& waterObjects,
        LevelTextureManager* levelTexMgr,
        float gameTime,
        const RenderSettings& settings
    );

    ID3D11ShaderResourceView* GetFinalGameSRV() const { return m_finalGameRT ? m_finalGameRT->GetSRV() : nullptr; }

private:
    void PassShadows(float gameTime, Camera* camera, const RenderSettings& settings);
    void PassGPUCulling(const RenderSettings& settings, float windowHeight);
    void PassZPrepass(const RenderSettings& settings, LevelTextureManager* levelTexMgr);
    void PassOpaque(LevelTextureManager* levelTexMgr);
    void PassLighting(const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& proj, const DirectX::XMFLOAT3& camPos);
    void PassWater(const std::vector<std::shared_ptr<WaterVLO>>& waterObjects, const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& proj, const DirectX::XMFLOAT3& camPos, float gameTime, const DirectX::BoundingFrustum& cullFrustum, float renderDistSq, bool enableCulling);
    void PassHZB(float windowWidth, float windowHeight);
    void RenderHzbDebug();

private:
    DX11Renderer* m_renderer;

    std::unique_ptr<ShadowManager>       m_shadowManager;
    std::unique_ptr<Shader>              m_shadowModelShader;
    std::unique_ptr<Shader>              m_shadowFloraShader;
    std::unique_ptr<Shader>              m_shadowTerrainShader;
    ComPtr<ID3D11SamplerState>           m_shadowSampler;

    std::unique_ptr<TerrainArrayManager> m_terrainArrayManager;
    std::unique_ptr<HzbManager>          m_hzbManager;
    std::unique_ptr<GpuCullingSystem>    m_gpuCuller;

    std::unique_ptr<StaticGpuScene>      m_staticScene;
    std::unique_ptr<TerrainGpuScene>     m_terrainGpuScene;
    std::unique_ptr<FloraGpuScene>       m_floraScene;

    // FIXME: Хардкод загрузки шейдеров напрямую через файлы .hlsl. 
    // Нужно перенести управление в ShaderManager с кэшированием бинарников (.cso).
    std::unique_ptr<Shader> m_terrainShader;
    std::unique_ptr<Shader> m_modelShader;
    std::unique_ptr<Shader> m_floraShader;
    std::unique_ptr<Shader> m_waterShaderLow;
    std::unique_ptr<Shader> m_waterShaderUltra;

    std::unique_ptr<ConstantBuffer<CB_VS_Transform>>  m_transformBuffer;
    ComPtr<ID3D11Buffer>                              m_instanceIdBuffer;
    std::unique_ptr<ConstantBuffer<CB_GlobalWeather>> m_weatherBuffer;

    std::unique_ptr<DirectX::SpriteBatch>  m_spriteBatch;
    std::unique_ptr<DirectX::CommonStates> m_states;

    std::unique_ptr<RenderTarget> m_gbufferAlbedo;
    std::unique_ptr<RenderTarget> m_gbufferNormal;
    std::unique_ptr<RenderTarget> m_gbufferMRAO;
    std::unique_ptr<RenderTarget> m_volumetricRT;

    std::unique_ptr<ConstantBuffer<CB_PS_Lighting>> m_lightBuffer;
    std::unique_ptr<Shader> m_deferredLightShader;
    std::unique_ptr<Shader> m_volumetricShader;

    std::unique_ptr<PostProcessManager> m_postProcess;
    std::unique_ptr<RenderTarget>       m_sceneHDR;
    std::unique_ptr<RenderTarget>       m_refractionHDR;

    std::unique_ptr<ClipmapRvtManager>  m_clipmapManager;
    std::unique_ptr<RvtBaker>           m_rvtBaker;

    std::unique_ptr<RenderTarget>       m_finalGameRT;

    DirectX::XMFLOAT4X4 m_cullView;
    DirectX::XMFLOAT4X4 m_cullProj;
    DirectX::XMFLOAT4X4 m_prevView;
    DirectX::XMFLOAT4X4 m_prevProj;

    bool m_isFirstFrame = true;
    bool m_shadowsBaked = false;
};