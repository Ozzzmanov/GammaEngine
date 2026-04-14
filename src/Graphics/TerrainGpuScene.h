//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// TerrainGpuScene.h
// Управляет GPU-Driven рендерингом ландшафта. 
// Использует Compute Shader для Frustum/Occlusion/LOD куллинга и 
// Indirect Drawing, чтобы рисовать тысячи чанков за один Draw Call.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "../Graphics/LevelTextureManager.h"
#include "../Resources/TerrainArrayManager.h"
#include <DirectXCollision.h>
#include <vector>
#include <memory>

class ComputeShader;

/**
 * @struct ChunkGpuData
 * @brief Данные одного чанка, отправляемые в StructuredBuffer для Compute Shader.
 */
struct ChunkGpuData {
    DirectX::XMFLOAT3 WorldPos;
    uint32_t          ArraySlice;
    uint32_t          MaterialIndices[24];
};

/**
 * @struct CullingConstants
 * @brief Параметры для Compute Shader куллинга.
 * Строго выровнено по 16 байт во избежание сюрпризов от HLSL.
 */
__declspec(align(16)) struct CullingConstants {
    DirectX::XMFLOAT4X4 ViewProj;            // 64 bytes
    DirectX::XMFLOAT4X4 PrevViewProj;        // 64 bytes
    DirectX::XMFLOAT4   FrustumPlanes[6];    // 96 bytes
    DirectX::XMFLOAT3   CameraPos;           // 12 bytes 
    uint32_t            NumChunks;           // 4 bytes  (16 bytes aligned)
    DirectX::XMFLOAT2   HZBSize;             // 8 bytes 
    float               MaxDistanceSq;       // 4 bytes
    uint32_t            EnableFrustum;       // 4 bytes  (16 bytes aligned)
    uint32_t            EnableOcclusion;     // 4 bytes 
    float               LOD1_DistSq;         // 4 bytes  
    float               LOD2_DistSq;         // 4 bytes
    uint32_t            Pad;                 // 4 bytes  (16 bytes aligned)
};

/**
 * @class TerrainGpuScene
 * @brief Менеджер косвенной отрисовки (Indirect Draw) и GPU куллинга ландшафта.
 */
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
        const DirectX::BoundingFrustum& frustum,
        const DirectX::XMFLOAT3& cameraPos,
        ID3D11ShaderResourceView* hzbSRV,
        DirectX::XMFLOAT2 hzbSize,
        bool enableFrustum, bool enableOcclusion,
        bool enableLODs, float renderDistance
    );

    void BindGeometry(ID3D11Buffer* instanceIdBuffer);

    void BindResources(
        TerrainArrayManager* arrayMgr,
        LevelTextureManager* texManager,
        ID3D11ShaderResourceView* diffuseArraySRV,
        ID3D11ShaderResourceView* rvtAlbedoSRV
    );

    void BindShadowResources(int cascadeIndex, TerrainArrayManager* arrayMgr);

    // Геттеры
    ID3D11Buffer* GetIndirectArgsBuffer()  const { return m_indirectArgsBuffer.Get(); }
    ID3D11ShaderResourceView* GetChunkDataSRV()        const { return m_chunkDataSRV.Get(); }
    ID3D11ShaderResourceView* GetChunkSliceLookupSRV() const { return m_chunkSliceLookupSRV.Get(); }
    const std::vector<ChunkGpuData>& GetCpuChunkData() const { return m_cpuChunkData; }

private:
    void CreateGrid();

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    std::vector<ChunkGpuData>         m_cpuChunkData;
    std::vector<DirectX::BoundingBox> m_cpuAABBs;

    std::unique_ptr<ComputeShader> m_cullingShader;
    ComPtr<ID3D11Buffer>           m_cullingCB;
    ComPtr<ID3D11SamplerState>     m_samplerPointClamp;

    // Геометрия базового патча (Grid)
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_indexCount = 0;
    UINT m_lodIndexCounts[3] = {};
    UINT m_lodIndexOffsets[3] = {};

    // Culling SRV (Входные AABB)
    ComPtr<ID3D11Buffer>             m_cullingDataBuffer;
    ComPtr<ID3D11ShaderResourceView> m_cullingDataSRV;

    // Chunk data (Материалы, Срезы)
    ComPtr<ID3D11Buffer>             m_chunkDataBuffer;
    ComPtr<ID3D11ShaderResourceView> m_chunkDataSRV;

    // Lookup: [gx+256, gz+256] → ArraySlice
    ComPtr<ID3D11Texture2D>          m_chunkSliceLookup;
    ComPtr<ID3D11ShaderResourceView> m_chunkSliceLookupSRV;

    // Выходные буферы для Compute Shader
    ComPtr<ID3D11Buffer>              m_visibleIndicesBuffer;
    ComPtr<ID3D11ShaderResourceView>  m_visibleIndicesSRV;
    ComPtr<ID3D11UnorderedAccessView> m_visibleIndicesUAV;

    // Аргументы для DrawIndexedInstancedIndirect
    ComPtr<ID3D11Buffer>              m_indirectArgsBuffer;
    ComPtr<ID3D11Buffer>              m_indirectArgsResetBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_indirectArgsUAV;

    // FIXME: Хардкод на 3 каскада теней. Нужно переделать на std::vector, 
    // чтобы поддерживать динамическое изменение настроек из EngineConfig (1-4 каскада).
    ComPtr<ID3D11Buffer>              m_shadowVisibleIndicesBuffer[3];
    ComPtr<ID3D11ShaderResourceView>  m_shadowVisibleIndicesSRV[3];
    ComPtr<ID3D11UnorderedAccessView> m_shadowVisibleIndicesUAV[3];
    ComPtr<ID3D11Buffer>              m_shadowIndirectArgsBuffer[3];
    ComPtr<ID3D11UnorderedAccessView> m_shadowIndirectArgsUAV[3];
};