#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <d3d11.h>
#include "../Graphics/ConstantBuffer.h"

struct MaterialConfig {
    std::vector<std::string> macros;
    int textureSlots[5]; // [0]=Diffuse, [1]=Normal, etc.

    MaterialConfig() {
        for (int i = 0; i < 5; ++i) textureSlots[i] = -1;
    }
};

struct CB_Material {
    DirectX::XMFLOAT4 BaseColor;
    float AlphaCutoff;
    float SpecularPower;
    float Padding[2];
};

class MaterialSystem {
public:
    static MaterialSystem& Get() {
        static MaterialSystem instance;
        return instance;
    }

    // 1. Инициализация DirectX ресурсов (вызывать 1 раз при старте)
    void Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx);

    // 2. Загрузка JSON для конкретной локации (вызывать перед загрузкой мира)
    // levelPath - путь к папке, где лежат materials.json и models.json (например "Assets/outlands2")
    void LoadLevelData(const std::string& levelPath);

    MaterialConfig GetConfig(const std::string& modelPath, size_t loadedTextureCount);
    void BindMaterialBuffer();

private:
    std::map<std::string, std::vector<std::string>> m_variantsDB;
    std::map<std::string, std::string> m_manifestDB;

    std::unique_ptr<ConstantBuffer<CB_Material>> m_cbMaterial;
};