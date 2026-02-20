//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// TerrainGpuScene.h
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "../Resources/TerrainArrayManager.h"
#include <vector>

class LevelTextureManager;
class ComputeShader;

// 112 байт
struct ChunkGpuData {
    DirectX::XMFLOAT3 WorldPos;
    uint32_t ArraySlice;
    uint32_t MaterialIndices[24];
};

class TerrainGpuScene {
public:
    TerrainGpuScene(ID3D11Device* device, ID3D11DeviceContext* context);
    ~TerrainGpuScene();

    void Initialize();
    void AddChunk(const ChunkGpuData& data, const DirectX::BoundingBox& aabb);
    void BuildGpuBuffers(LevelTextureManager* texManager);

    void PerformCulling(
        const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj,
        const DirectX::XMMATRIX& prevView, const DirectX::XMMATRIX& prevProj, 
        const DirectX::BoundingFrustum& frustum, const DirectX::XMFLOAT3& cameraPos,
        ID3D11ShaderResourceView* hzbSRV, DirectX::XMFLOAT2 hzbSize,
        bool enableFrustum, bool enableOcclusion, float renderDistance  
    );

    void BindGeometry();
    void BindResources(TerrainArrayManager* arrayMgr, ID3D11ShaderResourceView* diffuseArraySRV);

    ID3D11Buffer* GetIndirectArgsBuffer() const { return m_indirectArgsBuffer.Get(); }
    ID3D11ShaderResourceView* GetVisibleIndicesSRV() const { return m_visibleIndicesSRV.Get(); }
    ID3D11ShaderResourceView* GetChunkDataSRV() const { return m_chunkDataSRV.Get(); }

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    std::unique_ptr<ComputeShader> m_cullingShader;

    std::vector<ChunkGpuData> m_cpuChunkData;
    std::vector<DirectX::BoundingBox> m_cpuAABBs;

    ComPtr<ID3D11Buffer> m_chunkDataBuffer;
    ComPtr<ID3D11ShaderResourceView> m_chunkDataSRV;

    ComPtr<ID3D11Buffer> m_visibleIndicesBuffer;
    ComPtr<ID3D11ShaderResourceView> m_visibleIndicesSRV;
    ComPtr<ID3D11UnorderedAccessView> m_visibleIndicesUAV;

    ComPtr<ID3D11Buffer> m_indirectArgsBuffer;
    ComPtr<ID3D11Buffer> m_indirectArgsResetBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_indirectArgsUAV;

    ComPtr<ID3D11ShaderResourceView> m_cullingDataSRV;

    // Глобальные материалы
    ComPtr<ID3D11Buffer> m_materialBuffer;
    ComPtr<ID3D11ShaderResourceView> m_materialSRV;

    void CreateGrid();

    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_indexCount = 0;

    struct CullingConstants {
        DirectX::XMFLOAT4X4 ViewProj;
        DirectX::XMFLOAT4X4 PrevViewProj; // Матрица прошлого кадра
        DirectX::XMFLOAT4 FrustumPlanes[6];
        DirectX::XMFLOAT3 CameraPos;
        uint32_t NumChunks;
        DirectX::XMFLOAT2 HZBSize;
        float MaxDistanceSq;              // Квадрат дистанции отрисовки
        uint32_t EnableFrustum;
        uint32_t EnableOcclusion;
        float Padding[3];                 // Выравнивание структуры до 16 байт
    };

    ComPtr<ID3D11Buffer> m_cullingCB;
    ComPtr<ID3D11SamplerState> m_samplerPointClamp;
};