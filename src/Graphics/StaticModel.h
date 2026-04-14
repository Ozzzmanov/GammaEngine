//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// StaticModel.h
// Парсер и контейнер для статических 3D-моделей
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <string>
#include <vector>
#include <d3d11.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include "Texture.h"

namespace tinygltf {
    class Model;
    class Node;
    class Primitive;
}

/**
 * @struct ModelPart
 * @brief Описывает один сабмеш статической модели.
 */
struct ModelPart {
    UINT indexCount;
    UINT startIndex;
    int baseVertex;
    int materialIndex;
    int bucketIndex; // Индекс мега-текстуры (Texture2DArray) в ModelManager
    int sliceIndex;  // Индекс слоя внутри Texture2DArray
};

/**
 * @class StaticModel
 * @brief Загрузчик GLTF/GLB для статических объектов. Выгружает геометрию в глобальный кэш.
 */
class StaticModel {
public:
    StaticModel(ID3D11Device* device, ID3D11DeviceContext* context);
    ~StaticModel();

    /// @brief Загружает модель из GLTF/GLB файла через tinygltf.
    bool Load(const std::string& path);

    /// @brief Мгновенная инициализация из закэшированных данных (Zero-parsing).
    void LoadFromCache(const DirectX::BoundingSphere& sphere, const std::vector<ModelPart>& parts) {
        m_boundingSphere = sphere;
        m_parts = parts;
    }

    size_t GetPartCount() const { return m_parts.size(); }
    const ModelPart& GetPart(size_t index) const { return m_parts[index]; }
    const DirectX::BoundingSphere& GetBoundingSphere() const { return m_boundingSphere; }

private:
    bool ProcessNode(const tinygltf::Model& model, const tinygltf::Node& node, const DirectX::XMMATRIX& parentTransform);
    bool ProcessPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const DirectX::XMMATRIX& transform, bool invertWinding);

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    std::vector<SimpleVertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<ModelPart> m_parts;

    DirectX::BoundingSphere m_boundingSphere;
};