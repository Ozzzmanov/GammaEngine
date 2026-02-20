//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GpuCullingSystem.h
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
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
        bool enableLODs,
        bool enableFrustum,
        bool enableSize,
        bool enableOcclusion,
        float screenHeight,
        DirectX::XMFLOAT2 hzbSize,
        float renderDistance 
    );

    void PerformCulling(
        ID3D11ShaderResourceView* instancesSRV,
        ID3D11ShaderResourceView* metaDataSRV,
        ID3D11ShaderResourceView* hzbSRV,
        ID3D11UnorderedAccessView* visibleIndicesUAV,
        ID3D11UnorderedAccessView* indirectArgsUAV,
        uint32_t numInstances
    );

private:
    void CreateConstantBuffer();

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    std::unique_ptr<ComputeShader> m_cullingShader;

    ComPtr<ID3D11SamplerState> m_pointClampSampler;

    // СТРОГО 288 БАЙТ (16 * 18)
    struct CullingParams {
        DirectX::XMFLOAT4X4 View;           // 64
        DirectX::XMFLOAT4X4 Projection;     // 64
        DirectX::XMFLOAT4X4 PrevView;       // 64  
        DirectX::XMFLOAT4X4 PrevProjection; // 64  
        DirectX::XMFLOAT4 FrustumPlanes[6]; // 96
        DirectX::XMFLOAT3 CameraPos;        // 12
        uint32_t NumInstances;              // 4
        float LODDist1Sq;                   // 4
        float LODDist2Sq;                   // 4
        float LODDist3Sq;                   // 4
        uint32_t EnableLODs;                // 4
        uint32_t EnableFrustum;             // 4
        uint32_t EnableOcclusion;           // 4
        uint32_t EnableSizeCulling;         // 4
        float MinPixelSizeSq;               // 4
        float ScreenHeight;                 // 4
        DirectX::XMFLOAT2 HZBSize;          // 8
        float Padding;                      // 4 
    };

    ComPtr<ID3D11Buffer> m_cullingParamsCB;
    CullingParams m_cachedFrameData;
};