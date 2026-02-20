#include "../Core/Prerequisites.h"

#include "GpuTypes.h"

#include <map>

#include <vector>



class StaticModel;

class GpuCullingSystem;

class DX11Renderer;



struct BatchGpuResources {

    ComPtr<ID3D11Buffer> inputBuffer;

    ComPtr<ID3D11ShaderResourceView> inputSRV;

    ComPtr<ID3D11Buffer> visibleBuffer;

    ComPtr<ID3D11UnorderedAccessView> visibleUAV;

    ComPtr<ID3D11ShaderResourceView> visibleSRV;

    ComPtr<ID3D11Buffer> argsBuffer;

    size_t currentCapacity = 0;

};



class GpuInstanceBatcher {

public:

    GpuInstanceBatcher(ID3D11Device* device, ID3D11DeviceContext* context);

    ~GpuInstanceBatcher() = default;



    void BeginFrame();

    void AddInstance(StaticModel* model, const InstanceData& data);



    // --- ИЗМЕНЕНИЕ: Добавляем hzbSRV в аргументы ---

    void RenderBatches(GpuCullingSystem* culler, DX11Renderer* renderer, ID3D11ShaderResourceView* hzbSRV);



private:

    struct Batch {

        std::vector<InstanceData> instances;

        BatchGpuResources gpuResources;

    };



    ID3D11Device* m_device;

    ID3D11DeviceContext* m_context;

    std::map<StaticModel*, Batch> m_batches;

};