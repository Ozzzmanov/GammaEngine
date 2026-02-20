#include "GpuInstanceBatcher.h"

#include "StaticModel.h"

#include "GpuCullingSystem.h" 

#include "DX11Renderer.h"



GpuInstanceBatcher::GpuInstanceBatcher(ID3D11Device* device, ID3D11DeviceContext* context)

    : m_device(device), m_context(context)

{

}



void GpuInstanceBatcher::BeginFrame() {

    for (auto& pair : m_batches) {

        pair.second.instances.clear();

    }

}



void GpuInstanceBatcher::AddInstance(StaticModel* model, const InstanceData& data) {

    if (model) {

        m_batches[model].instances.push_back(data);

    }

}



void GpuInstanceBatcher::RenderBatches(GpuCullingSystem* culler, DX11Renderer* renderer, ID3D11ShaderResourceView* hzbSRV) {

    auto context = renderer->GetContext();



    for (auto& pair : m_batches) {

        StaticModel* model = pair.first;

        auto& batch = pair.second;

        const auto& instances = batch.instances;



        if (instances.empty()) continue;



        const auto& mainPart = model->GetPart(0);



        // 1. CULLING (GPU) - Передаем HZB внутрь

        culler->PerformCulling(

            batch.gpuResources,

            instances,

            mainPart.indexCount,

            mainPart.startIndex,

            mainPart.baseVertex,

            hzbSRV // <--- Передаем

        );



        // 2. RENDER SETUP

        ID3D11ShaderResourceView* allDataSRV = batch.gpuResources.inputSRV.Get();

        ID3D11ShaderResourceView* visibleIndicesSRV = batch.gpuResources.visibleSRV.Get();



        ID3D11ShaderResourceView* vsSRVs[] = { allDataSRV, visibleIndicesSRV };

        context->VSSetShaderResources(1, 2, vsSRVs);



        model->BindGeometry();



        // 3. DRAW LOOP

        for (size_t i = 0; i < model->GetPartCount(); ++i) {

            const auto& part = model->GetPart(i);



            if (part.texture) {

                ID3D11ShaderResourceView* tex = part.texture->Get();

                context->PSSetShaderResources(0, 1, &tex);

            }



            IndirectCommand cmd;

            cmd.IndexCountPerInstance = part.indexCount;

            cmd.InstanceCount = 0;

            cmd.StartIndexLocation = part.startIndex;

            cmd.BaseVertexLocation = part.baseVertex;

            cmd.StartInstanceLocation = 0;



            context->UpdateSubresource(batch.gpuResources.argsBuffer.Get(), 0, nullptr, &cmd, 0, 0);

            context->CopyStructureCount(batch.gpuResources.argsBuffer.Get(), 4, batch.gpuResources.visibleUAV.Get());



            context->DrawIndexedInstancedIndirect(batch.gpuResources.argsBuffer.Get(), 0);

        }



        // 4. CLEANUP

        ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };

        context->VSSetShaderResources(1, 2, nullSRVs);

    }

}