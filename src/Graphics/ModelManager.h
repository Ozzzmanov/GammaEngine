//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// ModelManager.h
// Центральный хаб для моделей, геометрии и текстурных бакетов.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "../Graphics/GpuTypes.h"
#include "../Graphics/TextureArray.h"
#include <unordered_map>
#include <string>
#include <memory>

class StaticModel;
class TreeModel;

struct ModelPart;
struct TreePart;

/**
 * @struct TextureBucket
 * @brief Корзина (бакет) для группировки текстур одинакового разрешения в Texture2DArray.
 */
struct TextureBucket {
    std::vector<std::string> albedoNames;
    std::vector<std::string> mraoNames;
    std::vector<std::string> normalNames;

    std::unique_ptr<TextureArray> AlbedoArray;
    std::unique_ptr<TextureArray> MRAOArray;
    std::unique_ptr<TextureArray> NormalArray;
};

class ModelManager {
    friend class GeometryBaker;
public:
    static ModelManager& Get() {
        static ModelManager instance;
        return instance;
    }

    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    // Загрузка и резолв
    std::shared_ptr<StaticModel> GetModel(const std::string& path);
    std::shared_ptr<TreeModel> GetTreeModel(const std::string& path, int targetLod = 0);
    bool ResolvePath(const std::string& input, std::string& outPath);

    // Аллокация геометрии (Mega-Buffers)
    void AllocateStaticGeometry(const std::vector<SimpleVertex>& verts, const std::vector<uint32_t>& inds, std::vector<ModelPart>& parts);
    void AllocateFloraGeometry(const std::vector<TreeVertex>& verts, const std::vector<uint32_t>& inds, std::vector<TreePart>& opaqueParts, std::vector<TreePart>& alphaParts);
    void BuildGlobalBuffers();

    // Управление текстурами (Bindless)
    void RegisterMaterial(const std::string& albedo, const std::string& mrao, const std::string& normal, int& outBucketIndex, int& outSliceIndex);
    void BuildTextureBuckets(const std::string& locationName);
    void PreloadManifest(const std::string& locationName);

    // Геттеры для рендера 
    ID3D11Buffer* GetGlobalStaticVB() const { return m_globalStaticVB.Get(); }
    ID3D11Buffer* GetGlobalStaticIB() const { return m_globalStaticIB.Get(); }
    ID3D11Buffer* GetGlobalFloraVB() const { return m_globalFloraVB.Get(); }
    ID3D11Buffer* GetGlobalFloraIB() const { return m_globalFloraIB.Get(); }

    TextureBucket* GetBucket(int index) { return &m_bucketsList[index]; }
    size_t GetBucketCount() const { return m_bucketsList.size(); }
    const std::vector<TextureBucket>& GetBuckets() const { return m_bucketsList; }

    // Геттеры/Сеттеры для GeometryBaker (Инкапсуляция)
    std::vector<SimpleVertex>& GetStaticVerticesRaw() { return m_allStaticVertices; }
    std::vector<uint32_t>& GetStaticIndicesRaw() { return m_allStaticIndices; }
    std::vector<TreeVertex>& GetFloraVerticesRaw() { return m_allFloraVertices; }
    std::vector<uint32_t>& GetFloraIndicesRaw() { return m_allFloraIndices; }

    std::unordered_map<std::string, std::shared_ptr<StaticModel>>& GetStaticCache() { return m_cache; }
    std::unordered_map<std::string, std::shared_ptr<TreeModel>>& GetFloraCache() { return m_treeCache; }

private:
    ModelManager() = default;
    ~ModelManager() = default;
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    std::unordered_map<std::string, std::shared_ptr<StaticModel>> m_cache;
    std::unordered_map<std::string, std::shared_ptr<TreeModel>>   m_treeCache;

    std::vector<TextureBucket> m_bucketsList;

    // ХРАНИЛИЩА MEGA-BUFFERS
    std::vector<SimpleVertex> m_allStaticVertices;
    std::vector<uint32_t> m_allStaticIndices;

    std::vector<TreeVertex> m_allFloraVertices;
    std::vector<uint32_t> m_allFloraIndices;

    ComPtr<ID3D11Buffer> m_globalStaticVB;
    ComPtr<ID3D11Buffer> m_globalStaticIB;

    ComPtr<ID3D11Buffer> m_globalFloraVB;
    ComPtr<ID3D11Buffer> m_globalFloraIB;
};