//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GpuTypes.h
// ================================================================================
#pragma once
#include <DirectXMath.h>
#include <d3d11.h>
#include <algorithm>

// Ультра-компактный инстанс (32 байта)
// Гарантируем выравнивание 16 байт для StructuredBuffer
__declspec(align(16)) struct InstanceData {
    DirectX::XMFLOAT3 Position; // 12 байт
    DirectX::XMFLOAT3 Scale;    // 12 байт
    uint32_t RotPacked;         // 4 байта (Кватернион XYZ упакованный в 10-10-10)
    uint32_t EntityID;          // 4 байта
};

// Хелпер для упаковки
inline uint32_t PackQuaternionXYZ(const DirectX::XMFLOAT3& rot) {
    uint32_t x = static_cast<uint32_t>(std::clamp(rot.x * 0.5f + 0.5f, 0.0f, 1.0f) * 1023.0f);
    uint32_t y = static_cast<uint32_t>(std::clamp(rot.y * 0.5f + 0.5f, 0.0f, 1.0f) * 1023.0f);
    uint32_t z = static_cast<uint32_t>(std::clamp(rot.z * 0.5f + 0.5f, 0.0f, 1.0f) * 1023.0f);
    return x | (y << 10) | (z << 20);
}

// Инструкция для GPU: как рисовать конкретный уровень LOD (16 байт)
struct LodInfo {
    UINT FirstBatch;         // Начиная с какой команды Indirect Draw рисовать
    UINT PartCount;          // Сколько частей (материалов) у этого ЛОДа
    UINT FirstVisibleOffset; // Где в мега-буфере индексов начинается место под эту модель
    UINT MaxInstances;       // Шаг смещения для следующих частей этой же модели
};

__declspec(align(16)) struct EntityMetaData {
    LodInfo Lod[3];                 // 48 байт (3 * 16)
    DirectX::XMFLOAT3 LocalCenter;  // 12 байт (Локальный центр сферы)
    float Radius;                   // 4 байта (Радиус сферы)
};

// Стандартная структура DX11 для Indirect Draw (20 байт)
struct IndirectCommand {
    UINT IndexCountPerInstance;
    UINT InstanceCount;
    UINT StartIndexLocation;
    INT  BaseVertexLocation;
    UINT StartInstanceLocation;
};

// ================================================================================
// ВЕРШИНЫ
// ================================================================================

struct TreeVertex {
    DirectX::XMFLOAT3 Pos;       // 12 байт (0-11)
    uint32_t Normal;             // 4 байта (12-15) - Упаковано в R10G10B10A2
    DirectX::XMFLOAT2 UV;        // 8 байт (16-23) - Честный 32-битный float
    uint32_t WindColor;          // 4 байта (24-27) - Упаковано в R8G8B8A8_UNORM
    uint32_t TangentPacked;      // 4 байта (28-31) - Упаковано в R10G10B10A2_UNORM
}; // Итого: ровно 32 байта

// ================================================================================
// ИСТОЧНИКИ СВЕТА
// ================================================================================

__declspec(align(16)) struct GPUPointLight {
    DirectX::XMFLOAT3 Position; // 12 байт
    float Radius;               // 4 байта 

    DirectX::XMFLOAT3 Color;    // 12 байт
    float Intensity;            // 4 байта
}; // Итого: 32 байта

__declspec(align(16)) struct GPUSpotLight {
    DirectX::XMFLOAT3 Position; // 12 байт
    float Radius;               // 4 байта 

    DirectX::XMFLOAT3 Color;    // 12 байт
    float Intensity;            // 4 байта

    DirectX::XMFLOAT3 Direction;// 12 байт
    float CosConeAngle;         // 4 байта
}; // Итого: 48 байт