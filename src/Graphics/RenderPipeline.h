//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// RenderPipeline.h
// Главный координатор графического конвейера.
// ================================================================================

#pragma once

#include "../Core/Prerequisites.h"
#include <memory>
#include <vector>
#include <SpriteBatch.h>
#include <CommonStates.h>
#include "RenderTarget.h"

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
template <typename T> class ConstantBuffer;
struct CB_VS_Transform;

__declspec(align(16)) struct CB_PS_Lighting {
    DirectX::XMMATRIX InvViewProj;
    DirectX::XMFLOAT3 CameraPos;
    float Padding;
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

    RenderStats RenderFrame(
        Camera* camera,
        const std::vector<std::shared_ptr<WaterVLO>>& waterObjects,
        LevelTextureManager* levelTexMgr,
        float gameTime,
        const RenderSettings& settings
    );

private:
    void PassGPUCulling(const RenderSettings& settings, float windowHeight);
    void PassZPrepass(const RenderSettings& settings, ID3D11ShaderResourceView* diffuseSRV);
    void PassOpaque(ID3D11ShaderResourceView* diffuseSRV);
    void PassLighting(const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& proj, const DirectX::XMFLOAT3& camPos);
    void PassWater(const std::vector<std::shared_ptr<WaterVLO>>& waterObjects, const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& proj, const DirectX::XMFLOAT3& camPos, float gameTime, const DirectX::BoundingFrustum& cullFrustum, float renderDistSq, bool enableCulling);
    void PassHZB(float windowWidth, float windowHeight);
    void RenderHzbDebug();

private:
    DX11Renderer* m_renderer;

    std::unique_ptr<TerrainArrayManager> m_terrainArrayManager;
    std::unique_ptr<HzbManager>          m_hzbManager;
    std::unique_ptr<GpuCullingSystem>    m_gpuCuller;

    std::unique_ptr<StaticGpuScene>      m_staticScene;
    std::unique_ptr<TerrainGpuScene>     m_terrainGpuScene;

    std::unique_ptr<Shader> m_terrainShader;
    std::unique_ptr<Shader> m_modelShader;
    std::unique_ptr<ConstantBuffer<CB_VS_Transform>> m_transformBuffer;

    std::unique_ptr<DirectX::SpriteBatch> m_spriteBatch;
    std::unique_ptr<DirectX::CommonStates> m_states;

    std::unique_ptr<RenderTarget> m_gbufferAlbedo;
    std::unique_ptr<RenderTarget> m_gbufferNormal;
    std::unique_ptr<ConstantBuffer<CB_PS_Lighting>> m_lightBuffer;
    std::unique_ptr<Shader>       m_deferredLightShader;

    DirectX::XMFLOAT4X4 m_cullView;
    DirectX::XMFLOAT4X4 m_cullProj;
    DirectX::XMFLOAT4X4 m_prevView;
    DirectX::XMFLOAT4X4 m_prevProj;
    bool m_isFirstFrame = true;
};