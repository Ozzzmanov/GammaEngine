//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// WaterVLO.h
// Very Large Object: Water. 
// Реализует рендер поверхности воды с поддержкой аппаратной тесселяции (DX11),
// волнами Герстнера и эффектами поглощения глубины/рефракции.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include "../Graphics/ConstantBuffer.h"
#include <vector>
#include <string>
#include <memory>
#include <DirectXCollision.h>
#include "../Graphics/Texture.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// Очень легкая вершина — это просто опорная точка для тесселяции (Patch Control Point)
struct WaterControlPoint {
    Vector3 Pos;
    Vector2 UV;
};

// Константный буфер для PBR, Волн Герстнера и Поглощения (выровнен по 16 байт)
__declspec(align(16)) struct CB_WaterParams {
    Vector4 DeepColor;
    Vector4 ShallowColor;
    Vector4 FoamColor;
    Vector4 Waves[4];
    Vector3 CamPos;
    float Time;
    float TessellationFactor;
    float TessellationMaxDist;
    float DepthAbsorptionScale;
    float GlobalWaveScale;
    int QualityLevel;
    int EnableRefraction;
    Vector2 ZBufferParams; // X = NearZ, Y = FarZ (Для линеаризации глубины)
};

/**
 * @class WaterVLO
 * @brief Класс, описывающий один физический водоем в мире.
 */
class WaterVLO {
public:
    WaterVLO(ID3D11Device* device, ID3D11DeviceContext* context);
    ~WaterVLO() = default;

    /// @brief Загружает параметры водоема из файла .vlo
    bool Load(const std::string& vloPath);

    /// @brief Создает водоем с параметрами по умолчанию, если файл не найден.
    void LoadDefaults(const std::string& uid);

    void Render(const Matrix& view, const Matrix& proj,
        const Vector3& camPos, float time,
        const DirectX::BoundingFrustum& frustum,
        float renderDistSq,
        bool checkVisibility);

    void SetWorldPosition(const Vector3& pos);
    void OverrideSize(Vector2 newSize);

private:
    void GeneratePatches();
    void UpdateBoundingBox();

private:
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    std::string m_uid;
    Vector3 m_position = { 0.0f, 0.0f, 0.0f };
    Vector2 m_size = { 100.0f, 100.0f };
    float m_orientation = 0.0f;

    DirectX::BoundingBox m_boundingBox;
    D3D11_PRIMITIVE_TOPOLOGY m_topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // Индивидуальные настройки конкретного водоема (Океан/Озеро)
    struct WaterConfig {
        Vector4 DeepColor = Vector4(0.01f, 0.1f, 0.2f, 0.9f);
        Vector4 ShallowColor = Vector4(0.1f, 0.3f, 0.35f, 0.2f);
        float WaveAmplitudeMult = 1.0f;
    } m_cfg;

    // Сетка контрольных точек (Patch Control Points)
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_indexCount = 0;

    std::shared_ptr<Texture> m_normalMap; // Для микро-ряби
    std::shared_ptr<Texture> m_foamMap;   // Текстура пены
    std::shared_ptr<Texture> m_envMap;    // Кубмапа (Skybox)

    std::unique_ptr<ConstantBuffer<CB_WaterParams>> m_cbWater;
};