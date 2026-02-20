//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// StaticGpuScene.h
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "../Graphics/StaticModel.h"
#include "../Graphics/GpuTypes.h"
#include <vector>
#include <map>

// Группа моделей для одной сущности
struct EntityLODs {
    StaticModel* Lod0;
    StaticModel* Lod1;
    StaticModel* Lod2;
    bool HideLod1 = false;
    bool HideLod2 = false;
};

// Описание части модели для рендера
struct RenderBatch {
    StaticModel* Model;
    int PartIndex;
    UINT StartInstanceOffset;
};

// Константный буфер для передачи смещения батча в шейдер
struct CB_Batch {
    UINT BatchOffset;
    float Padding[3];
};

class StaticGpuScene {
public:
    StaticGpuScene(ID3D11Device* device, ID3D11DeviceContext* context);
    ~StaticGpuScene() = default;

    // сущность со всеми её LOD-ами
    void AddEntity(const EntityLODs& lods, const InstanceData& data);

    void BuildGpuBuffers();
    void Render();

    ID3D11Buffer* GetArgsBuffer() const { return m_indirectArgsBuffer.Get(); }
    ID3D11Buffer* GetArgsResetBuffer() const { return m_indirectArgsResetBuffer.Get(); }

    ID3D11ShaderResourceView* GetInstanceSRV() const { return m_instanceSRV.Get(); }
    ID3D11ShaderResourceView* GetMetaDataSRV() const { return m_metaDataSRV.Get(); }

    ID3D11UnorderedAccessView* GetVisibleIndexUAV() const { return m_visibleIndexUAV.Get(); }
    ID3D11UnorderedAccessView* GetIndirectArgsUAV() const { return m_indirectArgsUAV.Get(); }

    UINT GetTotalInstanceCount() const { return m_totalInstanceCount; }

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    struct EntityGroupKey {
        StaticModel* Lod0;
        StaticModel* Lod1;
        StaticModel* Lod2;
        bool HideLod1; 
        bool HideLod2;

        bool operator<(const EntityGroupKey& other) const {
            if (Lod0 != other.Lod0) return Lod0 < other.Lod0;
            if (Lod1 != other.Lod1) return Lod1 < other.Lod1;
            if (Lod2 != other.Lod2) return Lod2 < other.Lod2;
            if (HideLod1 != other.HideLod1) return HideLod1 < other.HideLod1;
            return HideLod2 < other.HideLod2;
        }
    };

    std::map<EntityGroupKey, std::vector<InstanceData>> m_buildData;
    std::vector<RenderBatch> m_batches;

    ComPtr<ID3D11Buffer> m_instanceBuffer;
    ComPtr<ID3D11ShaderResourceView> m_instanceSRV;

    ComPtr<ID3D11Buffer> m_metaDataBuffer;
    ComPtr<ID3D11ShaderResourceView> m_metaDataSRV;

    ComPtr<ID3D11Buffer> m_visibleIndexBuffer;
    ComPtr<ID3D11ShaderResourceView> m_visibleIndexSRV;
    ComPtr<ID3D11UnorderedAccessView> m_visibleIndexUAV; // Для CS

    std::vector<int> m_renderOrder;

    ComPtr<ID3D11Buffer> m_indirectArgsBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_indirectArgsUAV; // Для CS
    ComPtr<ID3D11Buffer> m_indirectArgsResetBuffer;

    UINT m_totalInstanceCount = 0;

    ComPtr<ID3D11Buffer> m_batchCB;
};