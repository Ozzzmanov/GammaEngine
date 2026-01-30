//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Chunk.cpp
// Реализация чанка с Frustum Culling.
// ================================================================================

#include "Chunk.h"
#include "Terrain.h"
#include "../Resources/BwPackedSection.h" 
#include "../Core/Logger.h"
#include <fstream>
#include <vector>
#include <algorithm>

extern void RegisterDetectedVLO(const std::string& uid, const std::string& type, const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& scale);

static inline bool IsGuidChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_';
}

Chunk::Chunk(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
}

Chunk::~Chunk() {}

bool Chunk::Load(const std::string& chunkFilePath, int gridX, int gridZ,
    const SpaceParams& params, LevelTextureManager* texMgr, VegetationManager* vegMgr, bool onlyScan)
{
    m_chunkName = fs::path(chunkFilePath).filename().string();
    m_gridX = gridX;
    m_gridZ = gridZ;
    m_position = DirectX::XMFLOAT3(gridX * 100.0f + 50.0f, 0.0f, gridZ * 100.0f + 50.0f);

    m_boundingBox.Center = m_position;
    m_boundingBox.Extents = DirectX::XMFLOAT3(50.0f, 500.0f, 50.0f);

    std::ifstream file(chunkFilePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);
    file.close();

    PreScanForWater(buffer);

    auto root = BwPackedSection::Create(buffer.data(), size);
    if (root) {
        ScanForVLOs(root);       // Поиск воды
    }

    fs::path cdataPath = fs::path(chunkFilePath).replace_extension(".cdata");
    if (fs::exists(cdataPath)) {
        m_terrain = std::make_unique<Terrain>(m_device, m_context);
        m_terrain->SetPosition(m_position.x, m_position.y, m_position.z);
        bool useLegacy = (texMgr == nullptr);
        m_terrain->Initialize(cdataPath.string(), params, texMgr, onlyScan, useLegacy);

        // Уточняем высоту BoundingBox, если террейн загружен
        float minH = m_terrain->GetMinHeight();
        float maxH = m_terrain->GetMaxHeight();
        float midY = (minH + maxH) * 0.5f;
        float heightExt = (maxH - minH) * 0.5f + 10.0f; // +10м запас
        m_boundingBox.Center.y = midY;
        m_boundingBox.Extents.y = heightExt;
    }

    return true;
}

void Chunk::PreScanForWater(const std::vector<char>& buffer) {
    const char* data = buffer.data();
    size_t size = buffer.size();
    if (size < 40) return;

    for (size_t i = 35; i <= size - 5; ++i) {
        if (data[i] == 'w' && data[i + 1] == 'a' && data[i + 2] == 't' && data[i + 3] == 'e' && data[i + 4] == 'r') {
            size_t start = i - 35;
            int dots = 0;
            bool valid = true;
            for (size_t k = 0; k < 35; ++k) {
                char c = data[start + k];
                if (c == '.') dots++;
                else if (!IsGuidChar(c)) { valid = false; break; }
            }
            if (valid && dots == 3) {
                std::string uid(data + start, 35);
                RegisterDetectedVLO(uid, "water", { 0,0,0 }, { 1,1,1 });
            }
        }
    }
}

void Chunk::ScanForVLOs(std::shared_ptr<BwPackedSection> root) {
    if (!root) return;
    std::vector<ChunkVloInfo> foundVLOs;
    RecursiveVloScan(root, foundVLOs);

    for (const auto& info : foundVLOs) {
        if (info.uid.empty()) continue;
        DirectX::XMFLOAT3 worldPos;
        worldPos.x = m_position.x + info.position.x;
        worldPos.y = info.position.y;
        worldPos.z = m_position.z + info.position.z;
        RegisterDetectedVLO(info.uid, info.type, worldPos, info.scale);
    }
}

void Chunk::RecursiveVloScan(std::shared_ptr<BwPackedSection> section, std::vector<ChunkVloInfo>& outInfos) {
    if (!section) return;

    for (auto child : section->GetChildren()) {
        std::string tagName = child->GetName();
        bool isVloContainer = (tagName == "water" || tagName == "vlo");

        if (isVloContainer) {
            ChunkVloInfo info;
            info.type = (tagName == "water") ? "water" : "";
            info.hasPosition = false;
            info.hasScale = false;

            for (auto prop : child->GetChildren()) {
                std::string propName = prop->GetName();
                const auto& blob = prop->GetBlob();
                size_t len = blob.size();

                if (propName == "uid") {
                    std::string val = prop->GetValueAsString();
                    if (val.length() >= 35) info.uid = val;
                }
                else if (len >= 35 && len <= 40) { // Fallback для UID без имени
                    std::string val = prop->GetValueAsString();
                    int dots = 0;
                    for (char c : val) if (c == '.') dots++;
                    if (dots == 3) info.uid = val;
                }

                if (propName == "position") {
                    info.position = prop->AsVector3();
                    info.hasPosition = true;
                }

                if (propName == "transform" && (len == 48 || len == 64)) {
                    const float* m = reinterpret_cast<const float*>(blob.data());
                    if (len == 48) info.position = { m[9], m[10], m[11] };
                    else           info.position = { m[12], m[13], m[14] };
                    info.hasPosition = true;
                }

                if (propName == "size") {
                    info.scale = prop->AsVector3();
                    info.hasScale = true;
                }
            }

            if (!info.uid.empty()) {
                if (info.type.empty()) info.type = "water";
                outInfos.push_back(info);
            }
        }
        RecursiveVloScan(child, outInfos);
    }
}

// --- РЕНДЕР ---
bool Chunk::Render(ConstantBuffer<CB_VS_Transform>* cb,
    const DirectX::XMMATRIX& view,
    const DirectX::XMMATRIX& proj,
    const DirectX::BoundingFrustum& cameraFrustum,
    const DirectX::XMFLOAT3& camPos,
    float renderDistanceSq,
    bool checkVisibility)
{
    // Отсечение целого Чанка (Chunk Culling)
    if (checkVisibility) {
        float dx = m_position.x - camPos.x;
        float dz = m_position.z - camPos.z;
        float distSq = dx * dx + dz * dz;

        // Если чанк слишком далеко — не рисуем ничего
        if (distSq > renderDistanceSq) return false;

        // Проверка пирамиды видимости (Frustum Culling) по BoundingBox чанка
        if (!cameraFrustum.Intersects(m_boundingBox)) {
            return false;
        }
    }

    bool drawnSomething = false;

    DirectX::XMMATRIX viewT = DirectX::XMMatrixTranspose(view);
    DirectX::XMMATRIX projT = DirectX::XMMatrixTranspose(proj);

    // ---------------------------------------------------------
    // Рендер Ландшафта (Terrain)
    // ---------------------------------------------------------
    if (m_terrain) {
        DirectX::XMMATRIX terrainWorld = DirectX::XMMatrixTranslation(m_position.x, m_position.y, m_position.z);

        CB_VS_Transform data;
        data.World = DirectX::XMMatrixTranspose(terrainWorld);
        data.View = viewT;
        data.Projection = projT;

        cb->UpdateDynamic(data);
        cb->BindVS(0);

        m_terrain->Render();
        drawnSomething = true;
    }

    return drawnSomething;
}