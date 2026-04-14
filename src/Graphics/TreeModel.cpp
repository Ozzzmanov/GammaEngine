//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// TreeModel.cpp
// ================================================================================

#define _CRT_SECURE_NO_WARNINGS
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_STB_IMAGE

#include "TreeModel.h"
#include "ModelManager.h"
#include "../Core/Logger.h"
#include "../Core/ResourceManager.h"
#include "../Graphics/GpuTypes.h"
#include <filesystem>
#include <DirectXPackedVector.h>
#include <algorithm>

#pragma warning(push)
#pragma warning(disable : 4267)
#pragma warning(disable : 4018)
#pragma warning(disable : 4996)
#include "../Resources/tiny_gltf.h"
#pragma warning(pop)

namespace fs = std::filesystem;
using namespace DirectX;

// Перехватываем загрузку изображений в tinygltf и игнорируем ее. 
// Мы грузим текстуры напрямую через GPU ResourceManager. Это экономит сотни МБ RAM.
static bool TreeLoadImageDataCallback(tinygltf::Image* image, const int image_idx,
    std::string* err, std::string* warn,
    int req_width, int req_height,
    const unsigned char* bytes, int size,
    void* user_data)
{
    UNUSED(image); UNUSED(image_idx); UNUSED(err); UNUSED(warn);
    UNUSED(req_width); UNUSED(req_height); UNUSED(bytes); UNUSED(size); UNUSED(user_data);
    return true;
}

static bool IsTargetLOD(const std::string& name, int targetLod) {
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), ::toupper);

    if (targetLod == 1) return n.find("LOD1") != std::string::npos;
    if (targetLod == 2) return n.find("LOD2") != std::string::npos;
    if (targetLod == 3) return n.find("LOD3") != std::string::npos;

    // По умолчанию ищем LOD0 или ноды без явного суффикса LOD
    return (n.find("LOD0") != std::string::npos) ||
        (n.find("LOD1") == std::string::npos && n.find("LOD2") == std::string::npos && n.find("LOD3") == std::string::npos);
}

TreeModel::TreeModel(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context)
{
}

bool TreeModel::Load(const std::string& path, int targetLod)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;

    loader.SetImageLoader(TreeLoadImageDataCallback, nullptr);

    std::string err, warn;
    bool ret = false;

    std::string fullPath;
    if (!ModelManager::Get().ResolvePath(path, fullPath)) {
        GAMMA_LOG_ERROR(LogCategory::General, "Tree GLTF Not Found: " + path);
        return false;
    }

    if (fullPath.find(".glb") != std::string::npos) {
        ret = loader.LoadBinaryFromFile(&model, &err, &warn, fullPath);
    }
    else {
        ret = loader.LoadASCIIFromFile(&model, &err, &warn, fullPath);
    }

    if (!warn.empty()) GAMMA_LOG_WARN(LogCategory::General, "Tree GLTF Warn: " + warn);
    if (!err.empty())  GAMMA_LOG_ERROR(LogCategory::General, "Tree GLTF Error: " + err);
    if (!ret) return false;

    m_vertices.clear();
    m_indices.clear();
    m_opaqueParts.clear();
    m_alphaParts.clear();

    const tinygltf::Scene& scene = model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];
    XMMATRIX rootTransform = XMMatrixIdentity();

    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        const auto& node = model.nodes[scene.nodes[i]];
        std::string nodeName = node.name;

        if (node.mesh >= 0) {
            nodeName += "_" + model.meshes[node.mesh].name;
        }

        // Фильтруем ноды по нужному LOD
        if (IsTargetLOD(nodeName, targetLod)) {
            GAMMA_LOG_INFO(LogCategory::General, "Loaded LOD " + std::to_string(targetLod) + " from Node: " + nodeName);
            ProcessNode(model, node, rootTransform);
        }
    }

    // Если запрошенного LOD нет в файле
    if (m_vertices.empty()) {
        return false;
    }

    DirectX::BoundingSphere::CreateFromPoints(m_boundingSphere, m_vertices.size(),
        reinterpret_cast<const DirectX::XMFLOAT3*>(&m_vertices[0].Pos),
        sizeof(TreeVertex));

    // Отправляем геометрию в глобальный буфер
    ModelManager::Get().AllocateFloraGeometry(m_vertices, m_indices, m_opaqueParts, m_alphaParts);

    // Очищаем RAM, данные уже на GPU
    m_vertices.clear();
    m_vertices.shrink_to_fit();
    m_indices.clear();
    m_indices.shrink_to_fit();

    return true;
}

bool TreeModel::ProcessNode(const tinygltf::Model& model, const tinygltf::Node& node, const DirectX::XMMATRIX& parentTransform)
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
        invertWinding = true; // Защита от вывернутых наизнанку мешей при отрицательном масштабе (Scale -1)
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

bool TreeModel::ProcessPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const DirectX::XMMATRIX& transform, bool invertWinding)
{
    size_t vertexCount = 0;
    if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
        vertexCount = model.accessors[primitive.attributes.at("POSITION")].count;
    }
    else {
        return false; // Без позиций меш рендерить невозможно
    }

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
                else result[c] = (float)pData[c];
            }
            else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                uint16_t val = reinterpret_cast<const uint16_t*>(pData)[c];
                if (accessor.normalized || attrName == "COLOR_0") result[c] = val / 65535.0f;
                else result[c] = (float)val;
            }
        }
        return XMFLOAT4(result[0], result[1], result[2], result[3]);
        };

    TreePart part;
    part.startIndex = (UINT)m_indices.size();
    part.baseVertex = (int)m_vertices.size();
    part.materialIndex = primitive.material;
    part.isOpaque = true;

    std::string albedoName = "";
    std::string mraoName = "";
    std::string normalName = "";

    // FIXME: Хардкод пути. Директория должна прокидываться из глобального конфига
    // или определяться относительно местоположения загружаемого GLTF файла.
    std::string baseTexPath = "Assets/GammaTrees/Textures/";

    if (primitive.material >= 0 && primitive.material < model.materials.size()) {
        const auto& mat = model.materials[primitive.material];

        // Истинная проверка на альфу по стандарту GLTF
        if (mat.alphaMode == "MASK" || mat.alphaMode == "BLEND") {
            part.isOpaque = false;
        }

        // FIXME: Эвристика по именам материалов это хрупкий костыль.
        // При правильном экспорте ассетов (из Blender/Maya) mat.alphaMode всегда будет "MASK" для листьев.
        // Этот блок стоит вырезать после того, как пайплайн подготовки ассетов будет настроен.
        std::string matName = mat.name;
        std::transform(matName.begin(), matName.end(), matName.begin(), ::tolower);

        if (matName.find("leaf") != std::string::npos || matName.find("leaves") != std::string::npos ||
            matName.find("frond") != std::string::npos || matName.find("pine") != std::string::npos ||
            matName.find("needle") != std::string::npos)
        {
            part.isOpaque = false;
        }

        if (matName.find("bark") != std::string::npos || matName.find("trunk") != std::string::npos ||
            matName.find("wood") != std::string::npos)
        {
            part.isOpaque = true;
        }

        // Парсинг текстур
        int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
        if (texIndex >= 0 && texIndex < model.textures.size()) {
            int imgIndex = model.textures[texIndex].source;
            if (imgIndex >= 0 && imgIndex < model.images.size()) {
                albedoName = baseTexPath + fs::path(model.images[imgIndex].uri).filename().string();
            }
        }

        int mraoIndex = mat.pbrMetallicRoughness.metallicRoughnessTexture.index;
        if (mraoIndex >= 0 && mraoIndex < model.textures.size()) {
            int imgIndex = model.textures[mraoIndex].source;
            if (imgIndex >= 0 && imgIndex < model.images.size()) {
                mraoName = baseTexPath + fs::path(model.images[imgIndex].uri).filename().string();
            }
        }

        int normIndex = mat.normalTexture.index;
        if (normIndex >= 0 && normIndex < model.textures.size()) {
            int imgIndex = model.textures[normIndex].source;
            if (imgIndex >= 0 && imgIndex < model.images.size()) {
                normalName = baseTexPath + fs::path(model.images[imgIndex].uri).filename().string();
            }
        }
    }

    // Регистрация материала для GPU Bindless Texturing
    ModelManager::Get().RegisterMaterial(albedoName, mraoName, normalName, part.bucketIndex, part.sliceIndex);

    // УПАКОВКА ВЕРШИН
    for (size_t i = 0; i < vertexCount; ++i) {
        TreeVertex v;
        ZeroMemory(&v, sizeof(TreeVertex));

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

        XMVECTOR tangVec = XMVectorSet(tang.x, tang.y, tang.z, 0.0f);
        tangVec = XMVector3TransformNormal(tangVec, transform);
        tangVec = XMVector3Normalize(tangVec);

        // УПАКОВКА НОРМАЛИ (Формат 10-10-10-2, -1.0..1.0 мапится в 0..1023)
        uint32_t nx = (uint32_t)(std::clamp(XMVectorGetX(normVec) * 0.5f + 0.5f, 0.0f, 1.0f) * 1023.0f);
        uint32_t ny = (uint32_t)(std::clamp(XMVectorGetY(normVec) * 0.5f + 0.5f, 0.0f, 1.0f) * 1023.0f);
        uint32_t nz = (uint32_t)(std::clamp(XMVectorGetZ(normVec) * 0.5f + 0.5f, 0.0f, 1.0f) * 1023.0f);
        v.Normal = nx | (ny << 10) | (nz << 20);

        // ИДЕАЛЬНЫЕ UV (Без потерь, float32)
        v.UV = DirectX::XMFLOAT2(tex.x, tex.y);

        // УПАКОВКА ЦВЕТА (8-8-8-8, Vertex Colors для анимации ветра)
        uint32_t r = (uint32_t)(std::clamp(col.x, 0.0f, 1.0f) * 255.0f);
        uint32_t g = (uint32_t)(std::clamp(col.y, 0.0f, 1.0f) * 255.0f);
        uint32_t b = (uint32_t)(std::clamp(col.z, 0.0f, 1.0f) * 255.0f);
        uint32_t a = (uint32_t)(std::clamp(col.w, 0.0f, 1.0f) * 255.0f);
        v.WindColor = r | (g << 8) | (b << 16) | (a << 24);

        // УПАКОВКА ТАНГЕНСА (10-10-10-2, где W - знак битангенса)
        uint32_t tx = (uint32_t)(std::clamp(XMVectorGetX(tangVec) * 0.5f + 0.5f, 0.0f, 1.0f) * 1023.0f);
        uint32_t ty = (uint32_t)(std::clamp(XMVectorGetY(tangVec) * 0.5f + 0.5f, 0.0f, 1.0f) * 1023.0f);
        uint32_t tz = (uint32_t)(std::clamp(XMVectorGetZ(tangVec) * 0.5f + 0.5f, 0.0f, 1.0f) * 1023.0f);
        uint32_t tw = (tang.w > 0.0f) ? 3 : 0; // Маппим знак в 2 бита
        v.TangentPacked = tx | (ty << 10) | (tz << 20) | (tw << 30);

        m_vertices.push_back(v);
    }

    // ЧТЕНИЕ ИНДЕКСОВ
    if (primitive.indices >= 0) {
        const auto& accessor = model.accessors[primitive.indices];
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        const auto& buffer = model.buffers[bufferView.buffer];

        part.indexCount = (UINT)accessor.count;
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

        // Учет выворота нормалей при негативном Scale
        for (size_t i = 0; i < tempIndices.size(); i += 3) {
            if (i + 2 < tempIndices.size()) {
                if (!invertWinding) {
                    m_indices.push_back(tempIndices[i + 0]);
                    m_indices.push_back(tempIndices[i + 1]);
                    m_indices.push_back(tempIndices[i + 2]);
                }
                else {
                    m_indices.push_back(tempIndices[i + 0]);
                    m_indices.push_back(tempIndices[i + 2]);
                    m_indices.push_back(tempIndices[i + 1]);
                }
            }
        }
    }

    if (part.isOpaque) m_opaqueParts.push_back(part);
    else m_alphaParts.push_back(part);

    return true;
}