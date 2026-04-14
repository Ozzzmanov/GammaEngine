//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// TreeModel.h
// Парсер и контейнер для моделей растительности
// Автоматически разделяет меши на глухие (ствол) и прозрачные (листья) для 
// корректного рендера и теней.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <string>
#include <vector>
#include <d3d11.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include "Texture.h"
#include "GpuTypes.h"

namespace tinygltf {
    class Model;
    class Node;
    class Primitive;
}

/**
 * @struct TreePart
 * @brief Описывает один сабмеш дерева (ствол или крону).
 */
struct TreePart {
    UINT indexCount;
    UINT startIndex;
    int baseVertex;
    int materialIndex;
    int bucketIndex; // Индекс мега-текстуры (Texture2DArray) в ModelManager
    int sliceIndex;  // Индекс слоя внутри Texture2DArray
    bool isOpaque;   // true = Ствол/Ветки, false = Листья (Alpha Test)
};

/**
 * @class TreeModel
 * @brief Загрузчик GLTF моделей флоры. Извлекает LODы и пакует вершины.
 */
class TreeModel {
public:
    TreeModel(ID3D11Device* device, ID3D11DeviceContext* context);
    ~TreeModel() = default;

    /// @brief Загружает модель из файла. targetLod позволяет вытащить конкретный уровень детализации.
    bool Load(const std::string& path, int targetLod = 0);

    /// @brief Мгновенная инициализация из закэшированных данных (Zero-parsing).
    void LoadFromCache(const DirectX::BoundingSphere& sphere, const std::vector<TreePart>& opaque, const std::vector<TreePart>& alpha) {
        m_boundingSphere = sphere;
        m_opaqueParts = opaque;
        m_alphaParts = alpha;
    }

    const std::vector<TreePart>& GetOpaqueParts() const { return m_opaqueParts; }
    const std::vector<TreePart>& GetAlphaParts() const { return m_alphaParts; }
    const DirectX::BoundingSphere& GetBoundingSphere() const { return m_boundingSphere; }

private:
    bool ProcessNode(const tinygltf::Model& model, const tinygltf::Node& node, const DirectX::XMMATRIX& parentTransform);
    bool ProcessPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const DirectX::XMMATRIX& transform, bool invertWinding);

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    std::vector<TreeVertex> m_vertices;
    std::vector<uint32_t> m_indices;

    std::vector<TreePart> m_opaqueParts;
    std::vector<TreePart> m_alphaParts;

    DirectX::BoundingSphere m_boundingSphere;
};