//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// FloraGpuScene.h
// Менеджер GPU-Driven рендеринга растительности.
// Автоматически разделяет отрисовку на Opaque (стволы) и Alpha Test (листья) 
// для правильной работы Depth Prepass и теней.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "TreeModel.h"
#include "GpuTypes.h"
#include <vector>
#include <map>

struct TreeLODs {
    TreeModel* Lod0;
    TreeModel* Lod1;
    TreeModel* Lod2;
    bool HideLod1 = false;
    bool HideLod2 = false;
};

struct FloraRenderBatch {
    TreeModel* Model;
    int LocalPartIndex;
    bool IsOpaque;
    UINT StartInstanceOffset;
};

class FloraGpuScene {
public:
    FloraGpuScene(ID3D11Device* device, ID3D11DeviceContext* context);
    ~FloraGpuScene() = default;

    void AddEntity(const TreeLODs& lods, const InstanceData& data);
    void BuildGpuBuffers();

    void RenderOpaque(ID3D11Buffer* instanceIdBuffer);
    void RenderAlpha(ID3D11Buffer* instanceIdBuffer);

    void RenderShadowsOpaque(int cascadeIndex, ID3D11Buffer* instanceIdBuffer);
    void RenderShadowsAlpha(int cascadeIndex, ID3D11Buffer* instanceIdBuffer);

    ID3D11Buffer* GetArgsBuffer() const { return m_indirectArgsBuffer.Get(); }
    ID3D11Buffer* GetArgsResetBuffer() const { return m_indirectArgsResetBuffer.Get(); }

    ID3D11ShaderResourceView* GetInstanceSRV() const { return m_instanceSRV.Get(); }
    ID3D11ShaderResourceView* GetMetaDataSRV() const { return m_metaDataSRV.Get(); }
    ID3D11ShaderResourceView* GetBatchInfoSRV() const { return m_batchInfoSRV.Get(); }

    ID3D11UnorderedAccessView* GetVisibleIndexUAV() const { return m_visibleIndexUAV.Get(); }
    ID3D11UnorderedAccessView* GetIndirectArgsUAV() const { return m_indirectArgsUAV.Get(); }
    UINT GetTotalInstanceCount() const { return m_totalInstanceCount; }

    ID3D11Buffer* GetShadowArgsBuffer(int cascadeIndex) const { return m_shadowIndirectArgsBuffer[cascadeIndex].Get(); }
    ID3D11UnorderedAccessView* GetShadowVisibleIndexUAV(int cascadeIndex) const { return m_shadowVisibleIndexUAV[cascadeIndex].Get(); }
    ID3D11UnorderedAccessView* GetShadowIndirectArgsUAV(int cascadeIndex) const { return m_shadowIndirectArgsUAV[cascadeIndex].Get(); }

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    struct FloraGroupKey {
        TreeModel* Lod0;
        TreeModel* Lod1;
        TreeModel* Lod2;
        bool HideLod1;
        bool HideLod2;

        bool operator<(const FloraGroupKey& other) const {
            if (Lod0 != other.Lod0) return Lod0 < other.Lod0;
            if (Lod1 != other.Lod1) return Lod1 < other.Lod1;
            if (Lod2 != other.Lod2) return Lod2 < other.Lod2;
            if (HideLod1 != other.HideLod1) return HideLod1 < other.HideLod1;
            return HideLod2 < other.HideLod2;
        }
    };

    std::map<FloraGroupKey, std::vector<InstanceData>> m_buildData;

    std::vector<FloraRenderBatch> m_batches;

    std::vector<int> m_opaqueRenderOrder;
    std::vector<int> m_alphaRenderOrder;

    ComPtr<ID3D11Buffer> m_instanceBuffer;
    ComPtr<ID3D11ShaderResourceView> m_instanceSRV;

    ComPtr<ID3D11Buffer> m_metaDataBuffer;
    ComPtr<ID3D11ShaderResourceView> m_metaDataSRV;

    ComPtr<ID3D11Buffer> m_visibleIndexBuffer;
    ComPtr<ID3D11ShaderResourceView> m_visibleIndexSRV;
    ComPtr<ID3D11UnorderedAccessView> m_visibleIndexUAV;

    ComPtr<ID3D11Buffer> m_indirectArgsBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_indirectArgsUAV;
    ComPtr<ID3D11Buffer> m_indirectArgsResetBuffer;

    ComPtr<ID3D11Buffer> m_batchInfoBuffer;
    ComPtr<ID3D11ShaderResourceView> m_batchInfoSRV;

    ComPtr<ID3D11Buffer>              m_shadowVisibleIndexBuffer[3];
    ComPtr<ID3D11ShaderResourceView>  m_shadowVisibleIndexSRV[3];
    ComPtr<ID3D11UnorderedAccessView> m_shadowVisibleIndexUAV[3];

    ComPtr<ID3D11Buffer>              m_shadowIndirectArgsBuffer[3];
    ComPtr<ID3D11UnorderedAccessView> m_shadowIndirectArgsUAV[3];

    UINT m_totalInstanceCount = 0;
};