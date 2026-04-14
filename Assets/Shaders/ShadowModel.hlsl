// ================================================================================
// ShadowModel.hlsl
// Облегченный GPU-Driven шейдер для рендера статики в карту теней (Shadow Map).
// Отключены расчеты нормалей, касательных (TBN) и PBR.
// Вычисляет только позиции вершин и выполняет Alpha-Test для решеток/заборов.
// ================================================================================

cbuffer TransformBuffer : register(b0) {
    matrix WorldDummy; 
    matrix View;
    matrix Projection; // В Shadow Pass сюда передается матрица LightViewProj каскада!
    float4 TimeAndParams;
};

struct InstanceData {
    float3 Position;
    float3 Scale;
    uint   RotPacked;  // Кватернион вращения (10-10-10-2)
    uint   EntityID;
};

// --------------------------------------------------------------------------------
// ХЕЛПЕРЫ РАСПАКОВКИ
// --------------------------------------------------------------------------------
float3 UnpackRotation(uint packed) {
    float x = (float)(packed & 0x3FF) / 1023.0f;
    float y = (float)((packed >> 10) & 0x3FF) / 1023.0f;
    float z = (float)((packed >> 20) & 0x3FF) / 1023.0f;
    return float3(x, y, z) * 2.0f - 1.0f; // Перевод из [0, 1] обратно в [-1, 1]
}

// --------------------------------------------------------------------------------
// РЕСУРСЫ (Zero CPU-Overhead Binding)
// --------------------------------------------------------------------------------
StructuredBuffer<InstanceData> AllInstances           : register(t1); // Мега-буфер
StructuredBuffer<uint>         VisibleInstanceIndices : register(t2); // Буфер видимых после куллинга

Texture2DArray DiffuseMap : register(t0); // Нужна только для чтения Альфа-маски
SamplerState   Sampler    : register(s0);

struct VS_INPUT {
    float3 Pos            : POSITION; 
    float3 Col            : COLOR;    // Не используется в тенях
    float3 Norm           : NORMAL;   // Не используется в тенях
    float2 UV             : TEXCOORD0; 
    float4 Tangent        : TANGENT;  // Не используется в тенях
    uint   InstanceID     : SV_InstanceID; 
    uint   InstanceOffset : INSTANCE_OFFSET;
};

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
    
    // nointerpolation запрещает GPU сглаживать эти значения по треугольнику
    nointerpolation uint SliceIndex : TEXCOORD1;
    nointerpolation uint IsAlpha    : COLOR1;
};

// --------------------------------------------------------------------------------
// МАТЕМАТИКА ТРАНСФОРМАЦИЙ
// --------------------------------------------------------------------------------
matrix BuildWorldMatrix(float3 pos, uint rotPacked, float3 scale) {
    float3 rotXYZ = UnpackRotation(rotPacked);
    float w = sqrt(max(0.0f, 1.0f - dot(rotXYZ, rotXYZ)));
    float4 q = float4(rotXYZ, w);
    
    float xx = q.x*q.x; float yy = q.y*q.y; float zz = q.z*q.z;
    float xy = q.x*q.y; float xz = q.x*q.z; float yz = q.y*q.z;
    float wx = q.w*q.x; float wy = q.w*q.y; float wz = q.w*q.z;

    matrix m;
    m[0][0] = (1.0f - 2.0f * (yy + zz)) * scale.x; m[0][1] = (2.0f * (xy + wz)) * scale.x; m[0][2] = (2.0f * (xz - wy)) * scale.x; m[0][3] = 0.0f;
    m[1][0] = (2.0f * (xy - wz)) * scale.y; m[1][1] = (1.0f - 2.0f * (xx + zz)) * scale.y; m[1][2] = (2.0f * (yz + wx)) * scale.y; m[1][3] = 0.0f;
    m[2][0] = (2.0f * (xz + wy)) * scale.z; m[2][1] = (2.0f * (yz - wx)) * scale.z; m[2][2] = (1.0f - 2.0f * (xx + yy)) * scale.z; m[2][3] = 0.0f;
    m[3][0] = pos.x; m[3][1] = pos.y; m[3][2] = pos.z; m[3][3] = 1.0f;
    return m;
}

// --------------------------------------------------------------------------------
// ВЕРШИННЫЙ ШЕЙДЕР
// --------------------------------------------------------------------------------
PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;
    uint globalInstanceID = input.InstanceOffset;
    
    // --- GPU-DRIVEN DECODING ---
    uint packedData  = VisibleInstanceIndices[globalInstanceID];
    uint actualIndex = packedData & 0x1FFFFF;      // 21 бит под ID инстанса
    uint sliceIndex  = (packedData >> 21) & 0x3FF; // 10 бит под индекс текстуры
    uint isAlpha     = (packedData >> 31) & 0x1;   // Флаг прозрачности

    InstanceData inst = AllInstances[actualIndex];
    
    // Перевод локальных координат вершины в мировые
    matrix worldMatrix = BuildWorldMatrix(inst.Position, inst.RotPacked, inst.Scale);
    float4 worldPos = mul(float4(input.Pos, 1.0f), worldMatrix);
    
    // В Shadow Pass переменная Projection содержит LightViewProj матрицу!
    // Она переводит мировые координаты сразу в пространство источника света (каскада теней).
    output.Pos = mul(worldPos, Projection);
    output.UV = input.UV;
    
    output.SliceIndex = sliceIndex;
    output.IsAlpha = isAlpha;
    
    return output;
}

// --------------------------------------------------------------------------------
// ПИКСЕЛЬНЫЙ ШЕЙДЕР
// Вызывается ТОЛЬКО для геометрии с IsAlpha == 1 (например, листва, заборы-сетки).
// Глухая геометрия (камни) рендерится вообще без Pixel Shader'а (состояние nullptr), 
// что позволяет видеокарте писать напрямую в Z-Buffer с максимальной скоростью (Depth-Only).
// --------------------------------------------------------------------------------
void PSMain(PS_INPUT input) {
    if (input.IsAlpha == 1) {
        float a = DiffuseMap.Sample(Sampler, float3(input.UV, input.SliceIndex)).a;
        clip(a - 0.5f); // Пробиваем "дырки" в тени
    }
}