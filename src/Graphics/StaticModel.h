//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// StaticModel.h
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <string>
#include <vector>
#include <d3d11.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include "Texture.h"

// Forward declaration
namespace tinygltf { class Model; struct Node; struct Primitive; }

struct ModelPart {
    UINT indexCount;
    UINT startIndex;
    int baseVertex;
    int materialIndex;
    std::shared_ptr<Texture> texture;
};

class StaticModel {
public:
    StaticModel(ID3D11Device* device, ID3D11DeviceContext* context);
    ~StaticModel();

    bool Load(const std::string& path);
    void Render();

    void BindGeometry();

    size_t GetPartCount() const { return m_parts.size(); }
    const ModelPart& GetPart(size_t index) const { return m_parts[index]; }

    void RenderIndirect(ID3D11Buffer* argsBuffer, UINT argsOffset = 0);

    const DirectX::BoundingSphere& GetBoundingSphere() const { return m_boundingSphere; }

private:
    bool ProcessNode(const tinygltf::Model& model, const tinygltf::Node& node, const DirectX::XMMATRIX& parentTransform);
    bool ProcessPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const DirectX::XMMATRIX& transform);
    void LoadTextures(const tinygltf::Model& model);
    bool ProcessPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const DirectX::XMMATRIX& transform, bool invertWinding);

    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;

    std::vector<SimpleVertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<ModelPart> m_parts;
    std::vector<std::shared_ptr<Texture>> m_materialTextures;

    DirectX::BoundingSphere m_boundingSphere;
};