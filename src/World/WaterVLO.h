//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// WaterVLO.h
// ================================================================================

#pragma once
#include "../Core/Prerequisites.h"
#include "../Graphics/ConstantBuffer.h"
#include "../Graphics/Shader.h"
#include <vector>
#include <string>
#include <memory>
#include <DirectXCollision.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct WaterVertex {
    Vector3 Pos;
    uint32_t Color;
    Vector2 UV;
};

class WaterVLO {
public:
    struct WaterConstants {
        Vector4 DeepColor;
        Vector4 ReflectionTint;
        Vector4 Params1;
        Vector4 Params2;
        Vector4 Scroll1;
        Vector4 Scroll2;
        Vector3 CamPos;
        float   Padding;
    };

    WaterVLO(ID3D11Device* device, ID3D11DeviceContext* context);

    bool Load(const std::string& vloPath);
    void LoadDefaults(const std::string& uid);

    // Оптимизированный рендер с проверкой видимости
    void Render(const Matrix& view, const Matrix& proj,
        const Vector3& camPos, float time,
        ConstantBuffer<CB_VS_Transform>* transformBuffer,
        const DirectX::BoundingFrustum& frustum, // Пирамида
        float renderDistSq,                      // Дистанция (квадрат)
        bool checkVisibility);                   // Флаг оптимизации

    void SetWorldPosition(const Vector3& pos);
    void OverrideSize(Vector2 newSize);

private:
    void BuildTransparencyTable();
    void CreateRenderData();
    void UpdateBoundingBox();

    template <typename T>
    void DecompressVector(const char* data, uint32_t length, std::vector<T>& out) {
        uint32_t i = 0;
        while (i < length) {
            if (data[i] == (char)53) {
                i++; T val; if (i + sizeof(T) > length) break;
                memcpy(&val, &data[i], sizeof(T)); i += sizeof(T);
                if (i >= length) break;
                unsigned char count = (unsigned char)data[i]; i++;
                for (int j = 0; j < count; j++) out.push_back(val);
            }
            else {
                if (i + sizeof(T) > length) break;
                T val; memcpy(&val, &data[i], sizeof(T));
                out.push_back(val); i += sizeof(T);
            }
        }
    }

    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    std::string m_uid;
    Vector3 m_position = { 0,0,0 };
    Vector2 m_size = { 100,100 };
    float m_orientation = 0;

    // Границы для отсечения
    DirectX::BoundingBox m_boundingBox;

    struct Config {
        float tessellation = 10.f;
        Vector4 deepColour;
        Vector4 reflectionTint;
        float fresnelConstant;
        float fresnelExponent;
        float reflectionScale;
        Vector2 scrollSpeed1;
        Vector2 scrollSpeed2;
        Vector2 waveScale;
        float windVelocity;
        float sunPower;
    } m_cfg;

    std::vector<uint32_t> m_alphaData;
    int m_gridSizeX = 0, m_gridSizeZ = 0;

    ComPtr<ID3D11Buffer> m_vertexBuffer, m_indexBuffer;
    UINT m_indexCount = 0;
    std::unique_ptr<Shader> m_shader;
    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3D11ShaderResourceView> m_waveMap, m_skyMap;
    std::unique_ptr<ConstantBuffer<WaterConstants>> m_cbWater;
    ComPtr<ID3D11BlendState> m_blendState;
    ComPtr<ID3D11SamplerState> m_samplerState;
};