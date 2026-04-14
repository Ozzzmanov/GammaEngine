//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// RvtBaker.h
// Runtime Virtual Texturing (RVT) Baker.
// Отвечает за динамическое запекание геометрии и материалов ландшафта в кэш 
// виртуальной текстуры в рантайме через Compute Shader.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <memory>

class ComputeShader;
class TerrainArrayManager;
class LevelTextureManager;

/**
 * @struct BakeJobParams
 * @brief Параметры для передачи в Compute Shader.
 * Строго выровнено по 16 байтам для безопасности Constant Buffer в DX11.
 */
__declspec(align(16)) struct BakeJobParams {
    float    WorldPosMinX;
    float    WorldPosMinZ;
    uint32_t UpdateWidth;
    uint32_t UpdateHeight;
    float    TexelSize;
    uint32_t Resolution;
    uint32_t CascadeIndex;
    uint32_t Padding;
};

/**
 * @class RvtBaker
 * @brief Управляет диспатчем Compute-шейдера для запекания RVT страниц.
 */
class RvtBaker {
public:
    RvtBaker(ID3D11Device* device, ID3D11DeviceContext* context);
    ~RvtBaker();

    bool Initialize();

    /// @brief Один вызов = один диспатч на всю зону обновления
    void BakeClipmap(
        const BakeJobParams& params,
        TerrainArrayManager* arrayMgr,
        LevelTextureManager* texManager,
        ID3D11ShaderResourceView* chunkSliceLookupSRV, // из TerrainGpuScene
        ID3D11ShaderResourceView* chunkDataSRV,        // StructuredBuffer<ChunkGpuData>
        ID3D11UnorderedAccessView* outAlbedoUAV,
        ID3D11UnorderedAccessView* outNormalUAV
    );

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    std::unique_ptr<ComputeShader> m_bakerShader;
    ComPtr<ID3D11Buffer>           m_bakeParamsCB;
    ComPtr<ID3D11SamplerState>     m_samplerWrap;
    ComPtr<ID3D11SamplerState>     m_samplerClamp;
};