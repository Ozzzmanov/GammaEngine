//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// WaterVLO.cpp
// ================================================================================

#include "WaterVLO.h"
#include "../Core/ResourceManager.h"
#include "../Core/Logger.h"
#include "../Core/DataSection.h"
#include "../Config/EngineConfig.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

WaterVLO::WaterVLO(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context)
{
    m_cbWater = std::make_unique<ConstantBuffer<CB_WaterParams>>(device, context);
    // Буфер динамический, так как Time и CamPos меняются каждый кадр
    m_cbWater->Initialize(true);
}

void WaterVLO::SetWorldPosition(const Vector3& pos) {
    m_position = pos;
    UpdateBoundingBox();
}

void WaterVLO::OverrideSize(Vector2 newSize) {
    if (newSize.x < 1.0f || newSize.y < 1.0f) return;

    m_size = newSize;
    GeneratePatches();
    UpdateBoundingBox();
}

void WaterVLO::UpdateBoundingBox() {
    m_boundingBox.Center = m_position;
    // Делаем солидный запас по высоте (Y = 20.0f), чтобы высокие волны (шторм) не отсекались куллингом
    m_boundingBox.Extents = Vector3(m_size.x * 0.5f, 20.0f, m_size.y * 0.5f);
}

bool WaterVLO::Load(const std::string& vloPath) {
    fs::path p(vloPath);
    m_uid = p.stem().string();
    if (!m_uid.empty() && m_uid[0] == '_') m_uid = m_uid.substr(1);

    DataSectionPtr pSection = DataSection::CreateFromXML(vloPath);
    if (!pSection) return false;

    DataSectionPtr w = pSection->openSection("water");
    if (!w) w = pSection;

    // Читаем базу из XML
    m_position = w->readVector3("position", Vector3(0.0f, 0.0f, 0.0f));
    Vector3 sizeV3 = w->readVector3("size", Vector3(100.0f, 0.0f, 100.0f));
    m_size = Vector2(sizeV3.x, sizeV3.z);
    m_orientation = w->readFloat("orientation", 0.0f);

    // FIXME: Хардкод текстур воды.
    // Текстуры должны читаться из XML файла .vlo, либо из глобального конфига (EngineConfig).
    m_normalMap = ResourceManager::Get().LoadTextureAsync("system/maps/waves2.dds");
    m_foamMap = ResourceManager::Get().LoadTextureAsync("system/maps/water_foam2.dds");
    m_envMap = ResourceManager::Get().LoadTextureAsync("textures/cubemaps/skybox2.dds");

    GeneratePatches();
    UpdateBoundingBox();

    return true;
}

void WaterVLO::LoadDefaults(const std::string& uid) {
    m_uid = uid;

    // FIXME: хардкод текстур.
    m_normalMap = ResourceManager::Get().LoadTextureAsync("system/maps/waves2.dds");
    m_foamMap = ResourceManager::Get().LoadTextureAsync("system/maps/water_foam2.dds");
    m_envMap = ResourceManager::Get().LoadTextureAsync("textures/cubemaps/skybox2.dds");

    GeneratePatches();
    UpdateBoundingBox();
}

void WaterVLO::GeneratePatches() {
    std::vector<WaterControlPoint> vertices;
    std::vector<uint32_t> indices;

    bool useTessellation = EngineConfig::Get().Water.EnableTessellation;

    // ВНИМАНИЕ: Для тесселяции достаточно крупных патчей по 20м.
    // Но для слабых ПК ( без тесселяции) нам нужна более плотная сетка руками, например 4м.
    float patchSize = useTessellation ? 20.0f : 4.0f;

    int gridX = std::max(1, static_cast<int>(std::ceil(m_size.x / patchSize)));
    int gridZ = std::max(1, static_cast<int>(std::ceil(m_size.y / patchSize)));

    float startX = -m_size.x * 0.5f;
    float startZ = -m_size.y * 0.5f;
    float stepX = m_size.x / static_cast<float>(gridX);
    float stepZ = m_size.y / static_cast<float>(gridZ);

    // Генерируем опорные вершины (одинаково для обоих режимов)
    for (int z = 0; z <= gridZ; ++z) {
        for (int x = 0; x <= gridX; ++x) {
            WaterControlPoint v;
            v.Pos = Vector3(
                m_position.x + startX + x * stepX,
                m_position.y,
                m_position.z + startZ + z * stepZ
            );
            v.UV = Vector2(static_cast<float>(x) / gridX, static_cast<float>(z) / gridZ);
            vertices.push_back(v);
        }
    }

    // Собираем индексы в зависимости от режима
    if (useTessellation) {
        m_topology = D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST;
        for (int z = 0; z < gridZ; ++z) {
            for (int x = 0; x < gridX; ++x) {
                uint32_t v0 = x + z * (gridX + 1);
                uint32_t v1 = (x + 1) + z * (gridX + 1);
                uint32_t v2 = x + (z + 1) * (gridX + 1);
                uint32_t v3 = (x + 1) + (z + 1) * (gridX + 1);

                indices.push_back(v0);
                indices.push_back(v1);
                indices.push_back(v2);
                indices.push_back(v3);
            }
        }
    }
    else {
        m_topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        for (int z = 0; z < gridZ; ++z) {
            for (int x = 0; x < gridX; ++x) {
                uint32_t v0 = x + z * (gridX + 1);             // Bottom-Left
                uint32_t v1 = (x + 1) + z * (gridX + 1);       // Bottom-Right
                uint32_t v2 = x + (z + 1) * (gridX + 1);       // Top-Left
                uint32_t v3 = (x + 1) + (z + 1) * (gridX + 1); // Top-Right

                indices.push_back(v0);
                indices.push_back(v2);
                indices.push_back(v1);

                indices.push_back(v1);
                indices.push_back(v2);
                indices.push_back(v3);
            }
        }
    }

    m_indexCount = static_cast<UINT>(indices.size());

    D3D11_BUFFER_DESC vbd = { static_cast<UINT>(sizeof(WaterControlPoint) * vertices.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA vi = { vertices.data(), 0, 0 };
    m_device->CreateBuffer(&vbd, &vi, m_vertexBuffer.ReleaseAndGetAddressOf());

    D3D11_BUFFER_DESC ibd = { static_cast<UINT>(sizeof(uint32_t) * indices.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA ii = { indices.data(), 0, 0 };
    m_device->CreateBuffer(&ibd, &ii, m_indexBuffer.ReleaseAndGetAddressOf());
}

void WaterVLO::Render(const Matrix& view, const Matrix& proj,
    const Vector3& camPos, float time,
    const DirectX::BoundingFrustum& frustum,
    float renderDistSq,
    bool checkVisibility)
{
    UNUSED(view); // Трансформации биндятся глобально в RenderPipeline
    UNUSED(proj);

    if (m_indexCount == 0) return;

    // CPU Отсечение невидимой воды
    if (checkVisibility) {
        if (!frustum.Intersects(m_boundingBox)) return;

        // Поиск ближайшей точки AABB к камере для точного расчета дистанции
        Vector3 closestPoint;
        closestPoint.x = std::clamp(camPos.x, m_boundingBox.Center.x - m_boundingBox.Extents.x, m_boundingBox.Center.x + m_boundingBox.Extents.x);
        closestPoint.y = std::clamp(camPos.y, m_boundingBox.Center.y - m_boundingBox.Extents.y, m_boundingBox.Center.y + m_boundingBox.Extents.y);
        closestPoint.z = std::clamp(camPos.z, m_boundingBox.Center.z - m_boundingBox.Extents.z, m_boundingBox.Center.z + m_boundingBox.Extents.z);

        if (Vector3::DistanceSquared(camPos, closestPoint) > renderDistSq) return;
    }

    const auto& wConfig = EngineConfig::Get().Water;
    const auto& gfxConfig = EngineConfig::Get().GetActiveProfile().Graphics;

    // Обновляем параметры для шейдера
    CB_WaterParams wp = {};
    wp.DeepColor = Vector4(0.01f, 0.02f, 0.05f, 0.92f); // Почти черная бездна
    wp.ShallowColor = Vector4(0.01f, 0.04f, 0.18f, 0.35f);
    wp.FoamColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

    wp.CamPos = camPos;
    wp.Time = time * 1.3f;
    wp.QualityLevel = wConfig.QualityLevel;
    wp.TessellationFactor = wConfig.EnableTessellation ? wConfig.TessellationMaxFactor : 1.0f;
    wp.TessellationMaxDist = wConfig.TessellationMaxDist;
    wp.DepthAbsorptionScale = 0.25f;
    wp.EnableRefraction = wConfig.EnableRefraction ? 1 : 0;

    // Параметры камеры для линеаризации буфера глубины в шейдере
    wp.ZBufferParams.x = gfxConfig.NearZ;
    wp.ZBufferParams.y = gfxConfig.FarZ;

    // Волны Герстнера (Направление X, Направление Y, Крутизна, Длина волны)
    wp.Waves[0] = Vector4(1.0f, 0.4f, 0.11f, 41.0f);
    wp.Waves[1] = Vector4(-0.7f, 0.8f, 0.08f, 17.0f);
    wp.Waves[2] = Vector4(0.3f, -0.9f, 0.06f, 7.0f);
    wp.Waves[3] = Vector4(-0.4f, -0.5f, 0.04f, 29.0f);

    wp.GlobalWaveScale = wConfig.GlobalWaveScale * 0.65f * m_cfg.WaveAmplitudeMult;

    m_cbWater->UpdateDynamic(wp);

    // Отправляем буфер воды во все этапы рендера
    m_cbWater->BindVS(2); // Слот 2 (0 = Transform, 1 = Light/Weather)
    m_cbWater->BindHS(2);
    m_cbWater->BindDS(2);
    m_cbWater->BindPS(2);

    ID3D11ShaderResourceView* srvs[] = {
          (m_normalMap ? m_normalMap->Get() : nullptr),  // t0
          (m_foamMap ? m_foamMap->Get() : nullptr),  // t1
          (m_envMap ? m_envMap->Get() : nullptr)   // t2
    };
    m_context->PSSetShaderResources(0, 3, srvs);

    m_context->IASetPrimitiveTopology(m_topology);

    UINT stride = sizeof(WaterControlPoint);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

    m_context->DrawIndexed(m_indexCount, 0, 0);
}