//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GpuCullingSystem.h
// Выполняет отсечение невидимых инстансов на GPU через Compute Shader.
// Поддерживает Frustum Culling, Occlusion Culling (HZB) и Distance/Size Culling.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "../Config/EngineConfig.h"
#include <memory>

class ComputeShader;

class GpuCullingSystem {
public:
    GpuCullingSystem(ID3D11Device* device, ID3D11DeviceContext* context);
    ~GpuCullingSystem();

    void Initialize();

    void UpdateFrameConstants(
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& proj,
        const DirectX::XMMATRIX& prevView,
        const DirectX::XMMATRIX& prevProj,
        const DirectX::BoundingFrustum& frustum,
        const DirectX::XMFLOAT3& cameraPos,
        const LodSettings& lodSettings,
        bool enableLODs,
        bool enableFrustum,
        bool enableSize,
        bool enableOcclusion,
        float screenHeight,
        DirectX::XMFLOAT2 hzbSize,
        float renderDistance,
        float minDistance,
        float maxDistance,
        bool isShadowPass,
        bool enableShadowSizeCulling,
        float shadowSizeCullingDist,
        float minShadowSize,
        bool enableShadowFrustumCulling,
        const DirectX::XMFLOAT4* shadowPlanes
    );

    void PerformCulling(
        ID3D11ShaderResourceView* instancesSRV,
        ID3D11ShaderResourceView* metaDataSRV,
        ID3D11ShaderResourceView* hzbSRV,
        ID3D11ShaderResourceView* batchInfoSRV,
        ID3D11UnorderedAccessView** visibleIndicesUAVs,
        ID3D11UnorderedAccessView** indirectArgsUAVs,
        int numUAVs,
        uint32_t numInstances
    );

private:
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    std::unique_ptr<ComputeShader> m_cullingShader;
    ComPtr<ID3D11SamplerState> m_pointClampSampler;

    __declspec(align(16)) struct CullingParams {
        DirectX::XMFLOAT4X4 View;                   // 64
        DirectX::XMFLOAT4X4 Projection;             // 64
        DirectX::XMFLOAT4X4 PrevView;               // 64  
        DirectX::XMFLOAT4X4 PrevProjection;         // 64  
        DirectX::XMFLOAT4 FrustumPlanes[6];         // 96
        DirectX::XMFLOAT3 CameraPos;                // 12
        uint32_t NumInstances;                      // 4
        float LODDist1Sq;                           // 4
        float LODDist2Sq;                           // 4
        float LODDist3Sq;                           // 4
        uint32_t EnableLODs;                        // 4
        uint32_t EnableFrustum;                     // 4
        uint32_t EnableOcclusion;                   // 4
        uint32_t EnableSizeCulling;                 // 4
        float MinPixelSizeSq;                       // 4
        float ScreenHeight;                         // 4
        DirectX::XMFLOAT2 HZBSize;                  // 8
        float MinDistanceSq;                        // 4
        float MaxDistanceSq;                        // 4
        uint32_t IsShadowPass;                      // 4
        uint32_t EnableShadowSizeCulling;           // 4  
        float ShadowSizeCullingDistSq;              // 4
        float MinShadowSize;                        // 4 
        uint32_t EnableShadowFrustumCulling;        // 4
        DirectX::XMFLOAT2 PaddingCB;                // 8
        DirectX::XMFLOAT4 ShadowPlanes[18];         // 288 
    };

    ComPtr<ID3D11Buffer> m_cullingParamsCB;
    CullingParams m_cachedFrameData;
};