//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// LevelTextureManager.cpp
// ================================================================================
#include "LevelTextureManager.h"
#include "../Core/Logger.h"

LevelTextureManager::LevelTextureManager(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context)
{
    m_textureArray = std::make_unique<TextureArray>(device, context);
}

void LevelTextureManager::RegisterTexture(const std::string& name) {
    // Пропускаем пустые строки или строки, состоящие только из пробелов/нулей
    // FIXME: Сделать более строгую структуру/проверку имен текстур на этапе парсинга.
    if (name.empty() || name.find_first_not_of(" \t\r\n\0") == std::string::npos) {
        return;
    }

    if (m_nameToIndexMap.find(name) == m_nameToIndexMap.end()) {
        int index = static_cast<int>(m_uniqueTextureNames.size());
        m_uniqueTextureNames.push_back(name);
        m_nameToIndexMap[name] = index;
    }
}

int LevelTextureManager::RegisterMaterial(const std::string& name, const DirectX::XMFLOAT4& uProj, const DirectX::XMFLOAT4& vProj) {
    if (name.empty() || name.find_first_not_of(" \t\r\n\0") == std::string::npos) return 0;

    // Сначала регистрируем саму текстуру, чтобы получить её индекс в Texture2DArray
    int texIndex = 0;
    auto it = m_nameToIndexMap.find(name);
    if (it == m_nameToIndexMap.end()) {
        texIndex = static_cast<int>(m_uniqueTextureNames.size());
        m_uniqueTextureNames.push_back(name);
        m_nameToIndexMap[name] = texIndex;
    }
    else {
        texIndex = it->second;
    }

    // FIXME O(N) Поиск дубликатов материалов. 
    // Для небольшого количества слоев (до 200) работает быстро, но при росте числа уникальных 
    // тайлингов станет узким местом загрузки. Лучше использовать std::unordered_map с кастомным хэшом.
    for (size_t i = 0; i < m_materials.size(); ++i) {
        if (m_materials[i].DiffuseIndex == texIndex &&
            m_materials[i].UProj.x == uProj.x && m_materials[i].UProj.y == uProj.y &&
            m_materials[i].VProj.x == vProj.x && m_materials[i].VProj.y == vProj.y)
        {
            return static_cast<int>(i); // Такой материал уже есть
        }
    }

    // Создаем новый глобальный материал
    TerrainMaterial mat = {};
    mat.UProj = uProj;
    mat.VProj = vProj;
    mat.DiffuseIndex = static_cast<uint32_t>(texIndex);
    mat.Padding = { 0.0f, 0.0f, 0.0f };

    int matIndex = static_cast<int>(m_materials.size());
    m_materials.push_back(mat);

    return matIndex;
}

int LevelTextureManager::GetTextureIndex(const std::string& name) const {
    auto it = m_nameToIndexMap.find(name);
    if (it != m_nameToIndexMap.end()) {
        return it->second;
    }
    return 0;
}

bool LevelTextureManager::BuildArray() {
    if (m_uniqueTextureNames.empty()) {
        GAMMA_LOG_WARN(LogCategory::Texture, "No textures to build array.");
        return false;
    }

    // Создание буфера материалов на GPU
    if (!m_materials.empty()) {
        D3D11_BUFFER_DESC matDesc = {};
        matDesc.Usage = D3D11_USAGE_IMMUTABLE;
        matDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        matDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        matDesc.ByteWidth = static_cast<UINT>(sizeof(TerrainMaterial) * m_materials.size());
        matDesc.StructureByteStride = sizeof(TerrainMaterial);

        D3D11_SUBRESOURCE_DATA matInit = { m_materials.data(), 0, 0 };
        HR_CHECK(m_device->CreateBuffer(&matDesc, &matInit, m_materialBuffer.ReleaseAndGetAddressOf()), "Failed to create Terrain Material Buffer");

        D3D11_SHADER_RESOURCE_VIEW_DESC matSrvDesc = {};
        matSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        matSrvDesc.Buffer.NumElements = static_cast<UINT>(m_materials.size());
        HR_CHECK(m_device->CreateShaderResourceView(m_materialBuffer.Get(), &matSrvDesc, m_materialSRV.ReleaseAndGetAddressOf()), "Failed to create Terrain Material SRV");
    }

    // FIXME: Синхронное чтение текстур.
    // Метод TextureArray::Initialize делает загрузку с диска и ресайз в основном потоке.
    // Это вызовет фриз загрузки сцены. Необходимо заменить на асинхронную загрузку или оффлайн запекание.
    return m_textureArray->Initialize(m_uniqueTextureNames, 1024, 1024);
}