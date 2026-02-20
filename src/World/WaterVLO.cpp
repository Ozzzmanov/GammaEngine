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

// FIX ME исправить rigid.
#include "WaterVLO.h"
#include "../Core/ResourceManager.h"
#include "../Core/Logger.h"
#include "../Core/DataSection.h"
#include "../Resources/BwBinSection.h"
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

WaterVLO::WaterVLO(ID3D11Device* device, ID3D11DeviceContext* context) : m_device(device), m_context(context) {
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_device->CreateBlendState(&bd, m_blendState.GetAddressOf());

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_ANISOTROPIC;
    sd.MaxAnisotropy = 8;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    m_device->CreateSamplerState(&sd, m_samplerState.GetAddressOf());

    m_cbWater = std::make_unique<ConstantBuffer<WaterConstants>>(device, context);
    m_cbWater->Initialize(false);
}

void WaterVLO::SetWorldPosition(const Vector3& pos) {
    m_position = pos;
    UpdateBoundingBox();
}

void WaterVLO::OverrideSize(Vector2 newSize) {
    if (newSize.x < 1.0f) return;
    m_size = newSize;
    m_gridSizeX = std::clamp((int)ceilf(m_size.x / m_cfg.tessellation) + 1, 2, 512);
    m_gridSizeZ = std::clamp((int)ceilf(m_size.y / m_cfg.tessellation) + 1, 2, 512);

    CreateRenderData();
    UpdateBoundingBox();
}

// Обновления коробки
void WaterVLO::UpdateBoundingBox() {
    m_boundingBox.Center = m_position;
    // X, Z половина размера. Y небольшой запас (10м) для волн.
    m_boundingBox.Extents = Vector3(m_size.x * 0.5f, 10.0f, m_size.y * 0.5f);
}

bool WaterVLO::Load(const std::string& vloPath) {
    fs::path p(vloPath);
    m_uid = p.stem().string();
    if (!m_uid.empty() && m_uid[0] == '_') m_uid = m_uid.substr(1);

    DataSectionPtr pSection = DataSection::CreateFromXML(vloPath);
    if (!pSection) return false;

    DataSectionPtr w = pSection->openSection("water");
    if (!w) w = pSection;

    m_position = w->readVector3("position", Vector3(0, 0, 0));
    Vector3 sizeV3 = w->readVector3("size", Vector3(100, 0, 100));
    m_size = Vector2(sizeV3.x, sizeV3.z);
    m_orientation = w->readFloat("orientation", 0.f);

    // Сразу обновляем BoundingBox после загрузки
    UpdateBoundingBox();

    m_cfg.fresnelConstant = w->readFloat("fresnelConstant", 0.3f);
    m_cfg.fresnelExponent = w->readFloat("fresnelExponent", 5.0f);
    m_cfg.reflectionTint = w->readVector4("reflectionTint", Vector4(1, 1, 1, 1));
    m_cfg.reflectionScale = w->readFloat("reflectionStrength", 0.04f);
    m_cfg.deepColour = w->readVector4("deepColour", Vector4(0.0f, 0.20f, 0.33f, 1.0f));
    m_cfg.tessellation = w->readFloat("tessellation", 10.f);

    float oldX = w->readFloat("scrollSpeedX", -1.f);
    float oldY = w->readFloat("scrollSpeedY", 1.f);
    m_cfg.scrollSpeed1 = w->readVector2("scrollSpeed1", Vector2(oldX, 0.5f));
    m_cfg.scrollSpeed2 = w->readVector2("scrollSpeed2", Vector2(oldY, 0.0f));

    m_cfg.waveScale = w->readVector2("waveScale", Vector2(1.f, 0.75f));
    m_cfg.windVelocity = w->readFloat("windVelocity", 0.02f);
    m_cfg.sunPower = w->readFloat("sunPower", 94.0f);

    std::string waveTex = w->readString("waveTexture", "system/maps/waves2.dds");
    std::string cubeTex = w->readString("reflectionTexture", "system/maps/cloudyhillscubemap2.dds");

    // Используем LoadTextureAsync
    m_waveMap = ResourceManager::Get().LoadTextureAsync(waveTex);
    m_skyMap = ResourceManager::Get().LoadTextureAsync(cubeTex);

    m_alphaData.clear();
    std::string oPath = p.parent_path().string() + "/" + m_uid + ".odata";
    if (!fs::exists(oPath)) oPath = p.parent_path().string() + "/_" + m_uid + ".odata";

    if (fs::exists(oPath)) {
        std::ifstream f(oPath, std::ios::binary);
        if (f.is_open()) {
            f.seekg(0, std::ios::end); size_t sz = f.tellg(); f.seekg(0);
            std::vector<char> buf(sz); f.read(buf.data(), sz);
            auto bin = BwBinSection::Create(buf.data(), sz);
            std::vector<uint8_t> raw;
            if (bin && bin->GetSectionData("alpha", raw)) {
                DecompressVector<uint32_t>((const char*)raw.data(), (uint32_t)raw.size(), m_alphaData);
            }
        }
    }

    m_gridSizeX = std::clamp((int)ceilf(m_size.x / m_cfg.tessellation) + 1, 2, 512);
    m_gridSizeZ = std::clamp((int)ceilf(m_size.y / m_cfg.tessellation) + 1, 2, 512);

    if (m_alphaData.empty()) BuildTransparencyTable();
    CreateRenderData();

    if (!m_shader) {
        m_shader = std::make_unique<Shader>(m_device, m_context);
        if (m_shader->Load(L"Assets/Shaders/Water.hlsl")) {
            D3D11_INPUT_ELEMENT_DESC layout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            m_device->CreateInputLayout(layout, 3, m_shader->GetVSBlob()->GetBufferPointer(), m_shader->GetVSBlob()->GetBufferSize(), m_inputLayout.GetAddressOf());
        }
    }

    return true;
}

void WaterVLO::LoadDefaults(const std::string& uid) {
    m_uid = uid;
    m_gridSizeX = 11; m_gridSizeZ = 11;
    m_cfg.deepColour = Vector4(0.0f, 0.2f, 0.33f, 1.0f);
    m_cfg.reflectionTint = Vector4(1, 1, 1, 1);
    m_cfg.fresnelConstant = 0.3f;
    m_cfg.fresnelExponent = 5.0f;
    m_cfg.reflectionScale = 0.04f;
    m_cfg.scrollSpeed1 = Vector2(-1.f, 0.5f);
    m_cfg.scrollSpeed2 = Vector2(1.f, 0.0f);
    m_cfg.waveScale = Vector2(1.f, 0.75f);
    m_cfg.windVelocity = 0.02f;
    m_cfg.sunPower = 32.f;

    // Используем LoadTextureAsync
    m_waveMap = ResourceManager::Get().LoadTextureAsync("system/maps/waves2.dds");
    m_skyMap = ResourceManager::Get().LoadTextureAsync("system/maps/cloudyhillscubemap2.dds");

    BuildTransparencyTable();
    CreateRenderData();
    UpdateBoundingBox();
}

void WaterVLO::BuildTransparencyTable() {
    m_alphaData.assign(m_gridSizeX * m_gridSizeZ, 0xFFFFFFFF);
}

void WaterVLO::CreateRenderData() {
    std::vector<WaterVertex> vertices;
    std::vector<uint32_t> indices;

    float halfW = m_size.x * 0.5f;
    float halfH = m_size.y * 0.5f;
    float uvScale = 0.1f;

    for (int z = 0; z < m_gridSizeZ; ++z) {
        for (int x = 0; x < m_gridSizeX; ++x) {
            WaterVertex v;
            float px = -halfW + (x * (m_size.x / (m_gridSizeX - 1)));
            float pz = -halfH + (z * (m_size.y / (m_gridSizeZ - 1)));
            v.Pos = Vector3(px, 0, pz);

            int idx = x + (z * m_gridSizeX);
            uint8_t alpha = (idx < (int)m_alphaData.size()) ? (uint8_t)(m_alphaData[idx] & 0xFF) : 255;

            v.Color = (alpha << 24) | 0x00FFFFFF;
            v.UV = Vector2(px * uvScale, pz * uvScale);
            vertices.push_back(v);
        }
    }

    for (int z = 0; z < m_gridSizeZ - 1; ++z) {
        for (int x = 0; x < m_gridSizeX - 1; ++x) {
            uint32_t v0 = x + z * m_gridSizeX;
            uint32_t v1 = v0 + 1;
            uint32_t v2 = v0 + m_gridSizeX;
            uint32_t v3 = v2 + 1;
            indices.push_back(v0); indices.push_back(v2); indices.push_back(v1);
            indices.push_back(v1); indices.push_back(v2); indices.push_back(v3);
        }
    }

    m_indexCount = (UINT)indices.size();
    D3D11_BUFFER_DESC vbd = { (UINT)(sizeof(WaterVertex) * vertices.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA vi = { vertices.data(), 0, 0 };
    m_device->CreateBuffer(&vbd, &vi, m_vertexBuffer.ReleaseAndGetAddressOf());
    D3D11_BUFFER_DESC ibd = { (UINT)(sizeof(uint32_t) * indices.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA ii = { indices.data(), 0, 0 };
    m_device->CreateBuffer(&ibd, &ii, m_indexBuffer.ReleaseAndGetAddressOf());
}

// РЕНДЕР
void WaterVLO::Render(const Matrix& view, const Matrix& proj,
    const Vector3& camPos, float time,
    ConstantBuffer<CB_VS_Transform>* tb,
    const DirectX::BoundingFrustum& frustum,
    float renderDistSq,
    bool checkVisibility)
{
    if (m_indexCount == 0 || !m_shader) return;

    // ПРОВЕРКА ВИДИМОСТИ
    if (checkVisibility) {
        if (!frustum.Intersects(m_boundingBox)) {
            return;
        }

        XMVECTOR vCam = XMLoadFloat3(&camPos);
        float distSq = 0.0f;

        Vector3 closestPoint;
        closestPoint.x = std::clamp(camPos.x, m_boundingBox.Center.x - m_boundingBox.Extents.x, m_boundingBox.Center.x + m_boundingBox.Extents.x);
        closestPoint.y = std::clamp(camPos.y, m_boundingBox.Center.y - m_boundingBox.Extents.y, m_boundingBox.Center.y + m_boundingBox.Extents.y);
        closestPoint.z = std::clamp(camPos.z, m_boundingBox.Center.z - m_boundingBox.Extents.z, m_boundingBox.Center.z + m_boundingBox.Extents.z);

        distSq = Vector3::DistanceSquared(camPos, closestPoint);

        if (distSq > renderDistSq) {
            return;
        }
    }

    // ОТРИСОВКА
    CB_VS_Transform tf;
    tf.World = (Matrix::CreateRotationY(m_orientation) * Matrix::CreateTranslation(m_position)).Transpose();
    tf.View = view.Transpose();
    tf.Projection = proj.Transpose();

    tb->UpdateDynamic(tf);
    tb->BindVS(1);

    WaterConstants wc = {};
    wc.DeepColor = m_cfg.deepColour;
    wc.ReflectionTint = m_cfg.reflectionTint;
    wc.Params1 = Vector4(m_cfg.fresnelConstant, m_cfg.fresnelExponent, m_cfg.reflectionScale, time);
    wc.Params2 = Vector4(m_cfg.windVelocity, m_cfg.sunPower, m_cfg.waveScale.x, m_cfg.waveScale.y);
    wc.Scroll1 = Vector4(m_cfg.scrollSpeed1.x, m_cfg.scrollSpeed1.y, 0, 0);
    wc.Scroll2 = Vector4(m_cfg.scrollSpeed2.x, m_cfg.scrollSpeed2.y, 0, 0);
    wc.CamPos = camPos;

    m_cbWater->Update(wc);
    m_cbWater->BindPS(0);

    // Берем SRV из объектов Texture через ->Get()
    ID3D11ShaderResourceView* srvs[] = {
        (m_waveMap ? m_waveMap->Get() : nullptr),
        (m_skyMap ? m_skyMap->Get() : nullptr)
    };
    m_context->PSSetShaderResources(0, 2, srvs);
    m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());

    m_context->OMSetBlendState(m_blendState.Get(), nullptr, 0xFFFFFFFF);
    m_shader->Bind();
    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT s = sizeof(WaterVertex), o = 0;
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &s, &o);
    m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_context->DrawIndexed(m_indexCount, 0, 0);
    m_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
}