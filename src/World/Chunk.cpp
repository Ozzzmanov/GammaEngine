//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Chunk.cpp
// ================================================================================

#include "Chunk.h"
#include "Terrain.h"
#include "../Resources/BwPackedSection.h" 
#include "../Core/Logger.h"
#include <fstream>
#include <vector>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

extern void RegisterDetectedVLO(const std::string& uid, const std::string& type, const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& scale);

static inline bool IsGuidChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_';
}

std::string ConvertToNewModelPath(const std::string& oldPath) {
    fs::path p(oldPath);
    std::string filename = p.stem().string();
    std::string newPath = "Assets/NewModels/Models/" + filename + ".gltf";

    if (!fs::exists(newPath)) {
        std::string glbPath = "Assets/NewModels/Models/" + filename + ".glb";
        if (fs::exists(glbPath)) return glbPath;
    }
    return newPath;
}

Chunk::Chunk(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device(device), m_context(context) {
}

Chunk::~Chunk() {}

bool Chunk::Load(const std::string& chunkFilePath, int gridX, int gridZ,
    const SpaceParams& params, LevelTextureManager* texMgr)
{
    m_chunkName = fs::path(chunkFilePath).filename().string();
    m_gridX = gridX; m_gridZ = gridZ;

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
        ScanForVLOs(root);
        ScanForModels(root);
    }

    fs::path cdataPath = fs::path(chunkFilePath).replace_extension(".cdata");
    if (fs::exists(cdataPath)) {
        m_terrain = std::make_unique<Terrain>();
        m_terrain->SetPosition(m_position.x, m_position.y, m_position.z);
        m_terrain->Initialize(gridX, gridZ, texMgr);

        float minH = m_terrain->GetMinHeight();
        float maxH = m_terrain->GetMaxHeight();
        float heightPadding = 50.0f;

        m_boundingBox.Center.y = (minH + maxH) * 0.5f + (heightPadding * 0.5f);
        m_boundingBox.Extents.y = std::max((maxH - minH) * 0.5f, 1.0f) + heightPadding;
    }

    m_boundingBox.Extents.x += 2.0f;
    m_boundingBox.Extents.z += 2.0f;

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
            for (auto prop : child->GetChildren()) {
                std::string propName = prop->GetName();
                const auto& blob = prop->GetBlob();
                size_t len = blob.size();
                if (propName == "uid") info.uid = prop->GetValueAsString();
                else if (len >= 35 && len <= 40 && info.uid.empty()) {
                    std::string val = prop->GetValueAsString();
                    if (val.find("...") == std::string::npos) info.uid = val;
                }
                if (propName == "position") info.position = prop->AsVector3();
                if (propName == "transform" && (len == 48 || len == 64)) {
                    const float* m = reinterpret_cast<const float*>(blob.data());
                    if (len == 48) info.position = { m[9], m[10], m[11] };
                    else           info.position = { m[12], m[13], m[14] };
                }
                if (propName == "size") info.scale = prop->AsVector3();
            }
            if (!info.uid.empty()) {
                if (info.type.empty()) info.type = "water";
                outInfos.push_back(info);
            }
        }
        RecursiveVloScan(child, outInfos);
    }
}

void Chunk::ScanForModels(std::shared_ptr<BwPackedSection> root) {
    if (!root) return;
    std::vector<std::shared_ptr<BwPackedSection>> stack;
    stack.push_back(root);

    while (!stack.empty()) {
        auto section = stack.back();
        stack.pop_back();
        std::string type = section->GetName();
        // FIX ME ИСПРАВИТЬ ДЛЯ РЕАЛЬНЫХ ШЕЛОВ
        if (type == "model" || type == "shell") {
            std::string resource;
            DirectX::XMFLOAT4X4 localTransform;
            DirectX::XMStoreFloat4x4(&localTransform, DirectX::XMMatrixIdentity());
            bool hasRes = false;

            for (auto child : section->GetChildren()) {
                if (child->GetName() == "resource") {
                    resource = ConvertToNewModelPath(child->GetValueAsString());
                    hasRes = true;
                }
                else if (child->GetName() == "transform") {
                    localTransform = child->AsMatrix();
                }
            }

            if (hasRes) {
                float chunkCornerX = (float)m_gridX * 100.0f;
                float chunkCornerZ = (float)m_gridZ * 100.0f;

                DirectX::XMMATRIX mLocal = DirectX::XMLoadFloat4x4(&localTransform);
                DirectX::XMMATRIX mOffset = DirectX::XMMatrixTranslation(chunkCornerX, 0.0f, chunkCornerZ);
                DirectX::XMMATRIX mWorld = DirectX::XMMatrixMultiply(mLocal, mOffset);

                DirectX::XMVECTOR scaleVec, rotQuat, transVec;
                DirectX::XMMatrixDecompose(&scaleVec, &rotQuat, &transVec, mWorld);

                ParsedStaticEntity entity;
                entity.ModelPath = resource;
                DirectX::XMStoreFloat3(&entity.Position, transVec);
                DirectX::XMStoreFloat3(&entity.Scale, scaleVec);

                DirectX::XMFLOAT4 q;
                DirectX::XMStoreFloat4(&q, rotQuat);
                if (q.w < 0.0f) { q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w; }
                entity.RotXYZ = { q.x, q.y, q.z };

                m_staticEntities.push_back(entity);
            }
        }

        for (auto child : section->GetChildren()) stack.push_back(child);
    }
}