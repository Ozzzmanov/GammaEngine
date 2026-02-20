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

// Ультра-компактный инстанс (32 байта)
// 48 байт ккратно 16 для StructuredBuffer
struct InstanceData {
    DirectX::XMFLOAT3 Position; // 12 байт
    DirectX::XMFLOAT3 Scale;    // 12 байт ПО ВСЕМ ОСЯМ
    DirectX::XMFLOAT3 RotXYZ;   // 12 байт (Кватернион без W)
    UINT EntityID;              // 4 байта
    float Padding[2];           // 8 байт выравнивание до 48 байт
};

// Инструкция для GPU: как рисовать конкретный уровень LOD (16 байт)
struct LodInfo {
    UINT FirstBatch;         // Начиная с какой команды Indirect Draw рисовать
    UINT PartCount;          // Сколько частей (материалов) у этого ЛОДа
    UINT FirstVisibleOffset; // Где в мега-буфере индексов начинается место под эту модель
    UINT MaxInstances;       // Шаг смещения для следующих частей этой же модели
};

struct EntityMetaData {
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