//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// StaticModel.h
// ================================================================================

#define _CRT_SECURE_NO_WARNINGS
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE

#pragma warning(push)
#pragma warning(disable : 4267)
#pragma warning(disable : 4018)
#pragma warning(disable : 4996)

#include "../Resources/tiny_gltf.h"

#pragma warning(pop)

#include "StaticModel.h"
#include "ModelManager.h"
#include "../Core/Logger.h"
#include "../Core/ResourceManager.h"
#include <filesystem>

namespace fs = std::filesystem;
using namespace DirectX;

bool LoadImageDataCallback(tinygltf::Image* image, const int image_idx,
    std::string* err, std::string* warn,
    int req_width, int req_height,
    const unsigned char* bytes, int size,
    void* user_data)
{
    return true;
}

StaticModel::StaticModel(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context)
{
}

StaticModel::~StaticModel()
{
}

bool StaticModel::Load(const std::string& path)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;

    loader.SetImageLoader(LoadImageDataCallback, nullptr);

    std::string err, warn;
    bool ret = false;

    std::string fullPath;
    if (!ModelManager::Get().ResolvePath(path, fullPath))
    {
        Logger::Error(LogCategory::General, "GLTF Not Found: " + path);
        return false;
    }

    if (fullPath.find(".glb") != std::string::npos)
    {
        ret = loader.LoadBinaryFromFile(&model, &err, &warn, fullPath);
    }
    else
    {
        ret = loader.LoadASCIIFromFile(&model, &err, &warn, fullPath);
    }

    if (!warn.empty()) Logger::Warn(LogCategory::General, "GLTF Warn: " + warn);
    if (!err.empty()) Logger::Error(LogCategory::General, "GLTF Error: " + err);
    if (!ret) return false;

    // Очистка перед новой загрузкой
    m_vertices.clear();
    m_indices.clear();
    m_parts.clear();

    // Загрузка текстур через ResourceManager
    LoadTextures(model);

    // Обработка узлов сцены
    const tinygltf::Scene& scene = model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];
    XMMATRIX rootTransform = XMMatrixIdentity();

    for (size_t i = 0; i < scene.nodes.size(); ++i)
    {
        ProcessNode(model, model.nodes[scene.nodes[i]], rootTransform);
    }

    // Создание Vertex Buffer
    if (!m_vertices.empty())
    {
        DirectX::BoundingSphere::CreateFromPoints(m_boundingSphere, m_vertices.size(),
            (const DirectX::XMFLOAT3*)&m_vertices[0].Pos,
            sizeof(SimpleVertex));

        D3D11_BUFFER_DESC vbd = { 0 };
        vbd.ByteWidth = sizeof(SimpleVertex) * (UINT)m_vertices.size();
        vbd.Usage = D3D11_USAGE_IMMUTABLE;
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vd = { m_vertices.data(), 0, 0 };
        m_device->CreateBuffer(&vbd, &vd, m_vertexBuffer.GetAddressOf());

        m_vertices.clear();
        m_vertices.shrink_to_fit();
    }

    // Создание Index Buffer
    if (!m_indices.empty())
    {
        D3D11_BUFFER_DESC ibd = { 0 };
        ibd.ByteWidth = sizeof(uint32_t) * (UINT)m_indices.size();
        ibd.Usage = D3D11_USAGE_IMMUTABLE;
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA id = { m_indices.data(), 0, 0 };
        m_device->CreateBuffer(&ibd, &id, m_indexBuffer.GetAddressOf());

        // Очищаем оперативную память
        m_indices.clear();
        m_indices.shrink_to_fit();
    }

    return true;
}

void StaticModel::LoadTextures(const tinygltf::Model& model)
{
    m_materialTextures.resize(model.materials.size(), nullptr);
    std::string baseTexPath = "Assets/NewModels/Textures/";

    for (size_t i = 0; i < model.materials.size(); ++i)
    {
        const auto& mat = model.materials[i];
        int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
        if (texIndex >= 0 && texIndex < model.textures.size())
        {
            int imgIndex = model.textures[texIndex].source;
            if (imgIndex >= 0 && imgIndex < model.images.size())
            {
                std::string uri = model.images[imgIndex].uri;
                std::string filename = fs::path(uri).filename().string();
                std::string finalPath = baseTexPath + filename;

                // Используем Async загрузчик
                m_materialTextures[i] = ResourceManager::Get().LoadTextureAsync(finalPath);
            }
        }
    }
}

bool StaticModel::ProcessNode(const tinygltf::Model& model, const tinygltf::Node& node,
    const DirectX::XMMATRIX& parentTransform)
{
    XMMATRIX localTransform = XMMatrixIdentity();
    if (node.translation.size() == 3)
    {
        localTransform *= XMMatrixTranslation((float)node.translation[0],
            (float)node.translation[1],
            (float)node.translation[2]);
    }
    if (node.rotation.size() == 4)
    {
        XMVECTOR q = XMVectorSet((float)node.rotation[0],
            (float)node.rotation[1],
            (float)node.rotation[2],
            (float)node.rotation[3]);
        localTransform *= XMMatrixRotationQuaternion(q);
    }
    if (node.scale.size() == 3)
    {
        localTransform *= XMMatrixScaling((float)node.scale[0],
            (float)node.scale[1],
            (float)node.scale[2]);
    }
    if (node.matrix.size() == 16)
    {
        XMFLOAT4X4 matFloats;
        for (int i = 0; i < 16; ++i) matFloats.m[i / 4][i % 4] = (float)node.matrix[i];
        localTransform = XMLoadFloat4x4(&matFloats);
    }
    XMMATRIX worldTransform = localTransform * parentTransform;

    bool invertWinding = false;
    XMVECTOR det = XMMatrixDeterminant(worldTransform);
    if (XMVectorGetX(det) < 0.0f)
    {
        invertWinding = true;
    }

    if (node.mesh >= 0)
    {
        const auto& mesh = model.meshes[node.mesh];
        for (const auto& primitive : mesh.primitives)
        {
            ProcessPrimitive(model, primitive, worldTransform, invertWinding);
        }
    }
    for (int childId : node.children)
    {
        ProcessNode(model, model.nodes[childId], worldTransform);
    }
    return true;
}

bool StaticModel::ProcessPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive,
    const DirectX::XMMATRIX& transform, bool invertWinding)
{
    const float* pPos = nullptr;
    const float* pNorm = nullptr;
    const float* pTex = nullptr;
    size_t vertexCount = 0;

    if (primitive.attributes.find("POSITION") != primitive.attributes.end())
    {
        const auto& accessor = model.accessors[primitive.attributes.at("POSITION")];
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        const auto& buffer = model.buffers[bufferView.buffer];
        pPos = reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);
        vertexCount = accessor.count;
    }
    if (primitive.attributes.find("NORMAL") != primitive.attributes.end())
    {
        const auto& accessor = model.accessors[primitive.attributes.at("NORMAL")];
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        const auto& buffer = model.buffers[bufferView.buffer];
        pNorm = reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);
    }
    if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end())
    {
        const auto& accessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        const auto& buffer = model.buffers[bufferView.buffer];
        pTex = reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);
    }

    ModelPart part;
    part.startIndex = (UINT)m_indices.size();
    part.baseVertex = (int)m_vertices.size();
    part.materialIndex = primitive.material;

    if (primitive.material >= 0 && primitive.material < m_materialTextures.size())
    {
        part.texture = m_materialTextures[primitive.material];
    }
    else
    {
        // Fallback texture
        part.texture = ResourceManager::Get().LoadTextureAsync("Assets/Texture/default.dds");
    }

    for (size_t i = 0; i < vertexCount; ++i)
    {
        SimpleVertex v;
        XMVECTOR posVec = XMVectorSet(pPos[i * 3 + 0], pPos[i * 3 + 1], pPos[i * 3 + 2], 1.0f);

        posVec = XMVector3Transform(posVec, transform);
        XMStoreFloat3(&v.Pos, posVec);

        v.Color = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);

        if (pNorm)
        {
            XMVECTOR normVec = XMVectorSet(pNorm[i * 3 + 0], pNorm[i * 3 + 1], pNorm[i * 3 + 2], 0.0f);
            normVec = XMVector3TransformNormal(normVec, transform);
            normVec = XMVector3Normalize(normVec);
            XMStoreFloat3(&v.Normal, normVec);
        }
        else
        {
            v.Normal = { 0, 1, 0 };
        }

        if (pTex)
        {
            v.Tex = { pTex[i * 2 + 0], pTex[i * 2 + 1] };
        }
        else
        {
            v.Tex = { 0, 0 };
        }
        m_vertices.push_back(v);
    }

    if (primitive.indices >= 0)
    {
        const auto& accessor = model.accessors[primitive.indices];
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        const auto& buffer = model.buffers[bufferView.buffer];

        part.indexCount = (UINT)accessor.count;
        const uint8_t* pInd = &buffer.data[bufferView.byteOffset + accessor.byteOffset];

        std::vector<uint32_t> tempIndices;
        tempIndices.reserve(accessor.count);

        for (size_t i = 0; i < accessor.count; ++i)
        {
            uint32_t index = 0;
            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
            {
                index = *reinterpret_cast<const uint16_t*>(pInd + i * 2);
            }
            else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
            {
                index = *reinterpret_cast<const uint32_t*>(pInd + i * 4);
            }
            else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
            {
                index = *reinterpret_cast<const uint8_t*>(pInd + i);
            }
            tempIndices.push_back(index);
        }

        for (size_t i = 0; i < tempIndices.size(); i += 3)
        {
            if (i + 2 < tempIndices.size())
            {
                if (!invertWinding)
                {
                    m_indices.push_back(tempIndices[i + 0]);
                    m_indices.push_back(tempIndices[i + 1]);
                    m_indices.push_back(tempIndices[i + 2]);
                }
                else
                {
                    m_indices.push_back(tempIndices[i + 0]);
                    m_indices.push_back(tempIndices[i + 2]); // SWAP
                    m_indices.push_back(tempIndices[i + 1]);
                }
            }
        }
    }

    m_parts.push_back(part);
    return true;
}

void StaticModel::Render()
{
    if (!m_vertexBuffer || !m_indexBuffer) return;

    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (const auto& part : m_parts)
    {
        if (part.texture)
        {
            ID3D11ShaderResourceView* srv = part.texture->Get();
            m_context->PSSetShaderResources(0, 1, &srv);
        }
        m_context->DrawIndexed(part.indexCount, part.startIndex, part.baseVertex);
    }
}

void StaticModel::BindGeometry()
{
    if (!m_vertexBuffer || !m_indexBuffer) return;

    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void StaticModel::RenderIndirect(ID3D11Buffer* argsBuffer, UINT argsOffset)
{
    if (!m_vertexBuffer || !m_indexBuffer) return;

    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Мы рисуем каждую часть (ModelPart) отдельно.
    if (!m_parts.empty())
    {
        const auto& part = m_parts[0]; 

        if (part.texture)
        {
            ID3D11ShaderResourceView* srv = part.texture->Get();
            m_context->PSSetShaderResources(0, 1, &srv);
        }

        m_context->DrawIndexedInstancedIndirect(argsBuffer, argsOffset);
    }
}