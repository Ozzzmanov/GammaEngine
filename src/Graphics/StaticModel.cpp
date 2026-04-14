//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// StaticModel.cpp
// ================================================================================

#define _CRT_SECURE_NO_WARNINGS

// FIXME: TINYGLTF_IMPLEMENTATION сильно замедляет компиляцию этого файла.
// Лучше создать отдельный файл 'tiny_gltf_impl.cpp' с этим макросом, чтобы библиотека
// компилировалась только один раз.
#ifndef TINYGLTF_IMPLEMENTATION
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_STB_IMAGE       
#endif

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

// Игнорируем загрузку картинок в оперативную память
static bool LoadImageDataCallback(tinygltf::Image* image, const int image_idx,
    std::string* err, std::string* warn,
    int req_width, int req_height,
    const unsigned char* bytes, int size,
    void* user_data)
{
    UNUSED(image); UNUSED(image_idx); UNUSED(err); UNUSED(warn);
    UNUSED(req_width); UNUSED(req_height); UNUSED(bytes); UNUSED(size); UNUSED(user_data);
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
        GAMMA_LOG_ERROR(LogCategory::General, "GLTF Not Found: " + path);
        return false;
    }

    if (fullPath.find(".glb") != std::string::npos) {
        ret = loader.LoadBinaryFromFile(&model, &err, &warn, fullPath);
    }
    else {
        ret = loader.LoadASCIIFromFile(&model, &err, &warn, fullPath);
    }

    if (!warn.empty()) GAMMA_LOG_WARN(LogCategory::General, "GLTF Warn: " + warn);
    if (!err.empty())  GAMMA_LOG_ERROR(LogCategory::General, "GLTF Error: " + err);
    if (!ret) return false;

    m_vertices.clear();
    m_indices.clear();
    m_parts.clear();

    const tinygltf::Scene& scene = model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];
    XMMATRIX rootTransform = XMMatrixIdentity();

    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        ProcessNode(model, model.nodes[scene.nodes[i]], rootTransform);
    }

    if (!m_vertices.empty()) {
        DirectX::BoundingSphere::CreateFromPoints(m_boundingSphere, m_vertices.size(),
            reinterpret_cast<const DirectX::XMFLOAT3*>(&m_vertices[0].Pos),
            sizeof(SimpleVertex));
    }

    // Отправляем геометрию в глобальный кэш GPU
    ModelManager::Get().AllocateStaticGeometry(m_vertices, m_indices, m_parts);

    // Очищаем оперативную память
    m_vertices.clear();
    m_vertices.shrink_to_fit();
    m_indices.clear();
    m_indices.shrink_to_fit();

    return true;
}

bool StaticModel::ProcessNode(const tinygltf::Model& model, const tinygltf::Node& node, const DirectX::XMMATRIX& parentTransform)
{
    XMMATRIX localTransform = XMMatrixIdentity();

    if (node.translation.size() == 3) {
        localTransform *= XMMatrixTranslation((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]);
    }
    if (node.rotation.size() == 4) {
        XMVECTOR q = XMVectorSet((float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3]);
        localTransform *= XMMatrixRotationQuaternion(q);
    }
    if (node.scale.size() == 3) {
        localTransform *= XMMatrixScaling((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]);
    }
    if (node.matrix.size() == 16) {
        XMFLOAT4X4 matFloats;
        for (int i = 0; i < 16; ++i) matFloats.m[i / 4][i % 4] = (float)node.matrix[i];
        localTransform = XMLoadFloat4x4(&matFloats);
    }

    XMMATRIX worldTransform = localTransform * parentTransform;

    bool invertWinding = false;
    XMVECTOR det = XMMatrixDeterminant(worldTransform);
    if (XMVectorGetX(det) < 0.0f) {
        invertWinding = true;
    }

    if (node.mesh >= 0) {
        const auto& mesh = model.meshes[node.mesh];
        for (const auto& primitive : mesh.primitives) {
            ProcessPrimitive(model, primitive, worldTransform, invertWinding);
        }
    }

    for (int childId : node.children) {
        ProcessNode(model, model.nodes[childId], worldTransform);
    }
    return true;
}

bool StaticModel::ProcessPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const DirectX::XMMATRIX& transform, bool invertWinding)
{
    size_t vertexCount = 0;
    if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
        vertexCount = model.accessors[primitive.attributes.at("POSITION")].count;
    }
    else {
        return false;
    }

    // Безопасное извлечение атрибутов с учетом Stride и Component Type
    auto extractAttribute = [&](const std::string& attrName, size_t index, XMFLOAT4 defaultVal = { 0, 0, 0, 1 }) -> XMFLOAT4 {
        if (primitive.attributes.find(attrName) == primitive.attributes.end()) {
            return defaultVal;
        }

        const auto& accessor = model.accessors[primitive.attributes.at(attrName)];
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        const auto& buffer = model.buffers[bufferView.buffer];

        size_t stride = accessor.ByteStride(bufferView);
        const uint8_t* pData = &buffer.data[bufferView.byteOffset + accessor.byteOffset] + (index * stride);

        int numComponents = 1;
        if (accessor.type == TINYGLTF_TYPE_VEC2) numComponents = 2;
        else if (accessor.type == TINYGLTF_TYPE_VEC3) numComponents = 3;
        else if (accessor.type == TINYGLTF_TYPE_VEC4) numComponents = 4;

        float result[4] = { defaultVal.x, defaultVal.y, defaultVal.z, defaultVal.w };

        for (int c = 0; c < numComponents; ++c) {
            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                result[c] = reinterpret_cast<const float*>(pData)[c];
            }
            else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                if (accessor.normalized || attrName == "COLOR_0") result[c] = pData[c] / 255.0f;
                else result[c] = static_cast<float>(pData[c]);
            }
            else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                uint16_t val = reinterpret_cast<const uint16_t*>(pData)[c];
                if (accessor.normalized || attrName == "COLOR_0") result[c] = val / 65535.0f;
                else result[c] = static_cast<float>(val);
            }
        }
        return XMFLOAT4(result[0], result[1], result[2], result[3]);
        };

    ModelPart part;
    part.startIndex = static_cast<UINT>(m_indices.size());
    part.baseVertex = static_cast<int>(m_vertices.size());
    part.materialIndex = primitive.material;

    std::string albedoName = "";
    std::string mraoName = "";
    std::string normalName = "";

    // FIXME: Хардкод пути. Папка должна подтягиваться из конфига или относительного пути GLTF.
    std::string baseTexPath = "Assets/NewModels/Textures/";

    if (primitive.material >= 0 && primitive.material < model.materials.size()) {
        const auto& mat = model.materials[primitive.material];

        // Albedo
        int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
        if (texIndex >= 0 && texIndex < model.textures.size()) {
            int imgIndex = model.textures[texIndex].source;
            if (imgIndex >= 0 && imgIndex < model.images.size()) {
                albedoName = baseTexPath + fs::path(model.images[imgIndex].uri).filename().string();
            }
        }
        // MRAO
        int mraoIndex = mat.pbrMetallicRoughness.metallicRoughnessTexture.index;
        if (mraoIndex >= 0 && mraoIndex < model.textures.size()) {
            int imgIndex = model.textures[mraoIndex].source;
            if (imgIndex >= 0 && imgIndex < model.images.size()) {
                mraoName = baseTexPath + fs::path(model.images[imgIndex].uri).filename().string();
            }
        }
        // Normal
        int normIndex = mat.normalTexture.index;
        if (normIndex >= 0 && normIndex < model.textures.size()) {
            int imgIndex = model.textures[normIndex].source;
            if (imgIndex >= 0 && imgIndex < model.images.size()) {
                normalName = baseTexPath + fs::path(model.images[imgIndex].uri).filename().string();
            }
        }
    }

    // Регистрируем материал в Глобальном Менеджере
    ModelManager::Get().RegisterMaterial(albedoName, mraoName, normalName, part.bucketIndex, part.sliceIndex);

    // ИЗВЛЕЧЕНИЕ ВЕРШИН
    for (size_t i = 0; i < vertexCount; ++i) {
        SimpleVertex v;
        ZeroMemory(&v, sizeof(SimpleVertex));

        XMFLOAT4 pos = extractAttribute("POSITION", i, { 0.0f, 0.0f, 0.0f, 1.0f });
        XMFLOAT4 norm = extractAttribute("NORMAL", i, { 0.0f, 1.0f, 0.0f, 0.0f });
        XMFLOAT4 tex = extractAttribute("TEXCOORD_0", i, { 0.0f, 0.0f, 0.0f, 0.0f });
        XMFLOAT4 col = extractAttribute("COLOR_0", i, { 1.0f, 1.0f, 1.0f, 1.0f });
        XMFLOAT4 tang = extractAttribute("TANGENT", i, { 1.0f, 0.0f, 0.0f, 1.0f });

        // Трансформации
        XMVECTOR posVec = XMVectorSet(pos.x, pos.y, pos.z, 1.0f);
        posVec = XMVector3Transform(posVec, transform);
        XMStoreFloat3(&v.Pos, posVec);

        XMVECTOR normVec = XMVectorSet(norm.x, norm.y, norm.z, 0.0f);
        normVec = XMVector3TransformNormal(normVec, transform);
        normVec = XMVector3Normalize(normVec);
        XMStoreFloat3(&v.Normal, normVec);

        XMVECTOR tangVec = XMVectorSet(tang.x, tang.y, tang.z, 0.0f);
        tangVec = XMVector3TransformNormal(tangVec, transform);
        tangVec = XMVector3Normalize(tangVec);
        XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&v.Tangent), tangVec);
        v.Tangent.w = tang.w; // Знак битангенса

        // Копирование UV и Цвета
        v.Tex = { tex.x, tex.y };
        v.Color = { col.x, col.y, col.z }; // Поддержка Vertex Colors (напр. запеченный Ambient Occlusion)

        m_vertices.push_back(v);
    }

    // ИЗВЛЕЧЕНИЕ ИНДЕКСОВ
    if (primitive.indices >= 0) {
        const auto& accessor = model.accessors[primitive.indices];
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        const auto& buffer = model.buffers[bufferView.buffer];

        part.indexCount = static_cast<UINT>(accessor.count);
        const uint8_t* pInd = &buffer.data[bufferView.byteOffset + accessor.byteOffset];

        std::vector<uint32_t> tempIndices;
        tempIndices.reserve(accessor.count);

        for (size_t i = 0; i < accessor.count; ++i) {
            uint32_t index = 0;
            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                index = *reinterpret_cast<const uint16_t*>(pInd + i * 2);
            }
            else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                index = *reinterpret_cast<const uint32_t*>(pInd + i * 4);
            }
            else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                index = *reinterpret_cast<const uint8_t*>(pInd + i);
            }
            tempIndices.push_back(index);
        }

        // Инверсия треугольников при необходимости
        for (size_t i = 0; i < tempIndices.size(); i += 3) {
            if (i + 2 < tempIndices.size()) {
                if (!invertWinding) {
                    m_indices.push_back(tempIndices[i + 0]);
                    m_indices.push_back(tempIndices[i + 1]);
                    m_indices.push_back(tempIndices[i + 2]);
                }
                else {
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