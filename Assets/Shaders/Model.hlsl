// ================================================================================
// Model.hlsl
// GPU-Driven шейдер для статической геометрии (камни, дома и т.д.).
// Поддерживает PBR (Albedo, Normal, Metallic, Roughness, AO) и альфа-тест.
// ================================================================================

cbuffer SceneBuffer : register(b0) {
    matrix WorldDummy; 
    matrix View;
    matrix Projection;
    float4 TimeAndParams;
};

struct InstanceData {
    float3 Position;
    float3 Scale;
    uint   RotPacked; // Кватернион вращения (10-10-10-2)
    uint   EntityID;
};

// --------------------------------------------------------------------------------
// ХЕЛПЕРЫ РАСПАКОВКИ
// --------------------------------------------------------------------------------
float3 UnpackRotation(uint packed) {
    float x = (float)(packed & 0x3FF) / 1023.0f;
    float y = (float)((packed >> 10) & 0x3FF) / 1023.0f;
    float z = (float)((packed >> 20) & 0x3FF) / 1023.0f;
    return float3(x, y, z) * 2.0f - 1.0f; // Восстанавливаем диапазон [-1, 1]
}

// --------------------------------------------------------------------------------
// РЕСУРСЫ (Zero CPU-Overhead Binding)
// --------------------------------------------------------------------------------
StructuredBuffer<InstanceData> AllInstances           : register(t1); // Мега-буфер
StructuredBuffer<uint>         VisibleInstanceIndices : register(t2); // Буфер видимых после куллинга

Texture2DArray DiffuseMap : register(t0);
Texture2DArray MRAOMap    : register(t1); 
Texture2DArray NormalMap  : register(t2); 
SamplerState   Sampler    : register(s0);

struct VS_INPUT {
    float3 Pos            : POSITION;
    float3 Col            : COLOR;      // Vertex Color (например, для запеченного AO)
    float3 Norm           : NORMAL;
    float2 UV             : TEXCOORD0;
    float4 Tangent        : TANGENT;
    uint   InstanceID     : SV_InstanceID; 
    uint   InstanceOffset : INSTANCE_OFFSET; // Смещение из Vertex Buffer
};

struct PS_INPUT {
    float4 Pos      : SV_POSITION;
    float4 WorldPos : TEXCOORD1;
    float3 Col      : COLOR;
    float3 Norm     : NORMAL;
    float4 Tangent  : TANGENT;
    float2 UV       : TEXCOORD0;
    
    // nointerpolation экономит циклы растеризатора
    nointerpolation uint SliceIndex : TEXCOORD2;
    nointerpolation uint IsAlpha    : COLOR1;    
};

struct PS_OUTPUT {
    float4 Albedo   : SV_Target0;
    float4 Normal   : SV_Target1;
    float4 MRAO     : SV_Target2;
};

// --------------------------------------------------------------------------------
// МАТЕМАТИКА ТРАНСФОРМАЦИЙ
// --------------------------------------------------------------------------------
matrix BuildWorldMatrix(float3 pos, uint rotPacked, float3 scale) {
    float3 rotXYZ = UnpackRotation(rotPacked);
    float w = sqrt(max(0.0f, 1.0f - dot(rotXYZ, rotXYZ)));
    float4 q = float4(rotXYZ, w);

    float xx = q.x*q.x; float yy = q.y*q.y; float zz = q.z*q.z;
    float xy = q.x * q.y; float xz = q.x * q.z; float yz = q.y * q.z;
    float wx = q.w * q.x; float wy = q.w * q.y; float wz = q.w * q.z;

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
    uint sliceIndex  = (packedData >> 21) & 0x3FF; // 10 бит под индекс слоя (до 1024 слоев)
    uint isAlpha     = (packedData >> 31) & 0x1;   // Бит прозрачности

    InstanceData inst = AllInstances[actualIndex];
    
    // Применяем мировые трансформации
    matrix worldMatrix = BuildWorldMatrix(inst.Position, inst.RotPacked, inst.Scale);
    float4 worldPos = mul(float4(input.Pos, 1.0f), worldMatrix);
    output.WorldPos = worldPos;
    
    output.Pos = mul(worldPos, View);
    output.Pos = mul(output.Pos, Projection);

    output.Col = input.Col; 
    output.Norm = mul(input.Norm, (float3x3)worldMatrix);
    output.Tangent.xyz = mul(input.Tangent.xyz, (float3x3)worldMatrix);
    output.Tangent.w = input.Tangent.w; // Знак битангенса
    output.UV = input.UV;

    output.SliceIndex = sliceIndex;
    output.IsAlpha = isAlpha;

    return output;
}

// --------------------------------------------------------------------------------
// ПИКСЕЛЬНЫЙ ШЕЙДЕР
// --------------------------------------------------------------------------------
PS_OUTPUT PSMain(PS_INPUT input) {
    PS_OUTPUT output;
    
    // Множим текстуру на Vertex Color (удобно для Ambient Occlusion из 3D-редактора)
    float4 color = DiffuseMap.Sample(Sampler, float3(input.UV, input.SliceIndex)) * float4(input.Col, 1.0f);
    
    // --- АЛЬФА-ТЕСТ (Early-Z Penalty) ---
    if (input.IsAlpha == 1) {
        clip(color.a - 0.5f);
    }
    
    output.Albedo = color;
    
    // --- NORMAL MAPPING ---
    float3 sampledNormalRaw = NormalMap.Sample(Sampler, float3(input.UV, input.SliceIndex)).xyz;
    float3 sampledNormal;
    
    if (dot(sampledNormalRaw, sampledNormalRaw) < 0.01f) {
        sampledNormal = float3(0.0f, 0.0f, 1.0f); // Заглушка, если текстуры нет
    } else {
        sampledNormal = sampledNormalRaw * 2.0f - 1.0f;
        sampledNormal.y = -sampledNormal.y; // Инверсия оси Y (DirectX формат)
    }
    
    float3 N = normalize(input.Norm);
    float3 T = normalize(input.Tangent.xyz);
    
    // Ортогонализация Грама-Шмидта (фикс артефактов растяжения)
    T = T - dot(T, N) * N;
    if (dot(T, T) < 0.001f) {
        float3 up = abs(N.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        T = normalize(cross(up, N));
    } else {
        T = normalize(T);
    }
    
    float3 B = cross(N, T) * input.Tangent.w;
    float3x3 TBN = float3x3(T, B, N);
    
    // ИСПРАВЛЕНО ВОТ ЗДЕСЬ: Возвращен правильный порядок вектора и матрицы!
    // Это делает X*T + Y*B + Z*N, переводя нормаль из текстуры в мировой вектор.
    float3 finalNormal = normalize(mul(sampledNormal, TBN)); 
    
    output.Normal = float4(finalNormal * 0.5f + 0.5f, 1.0f);
    
    // --- PBR ---
    float3 pbrRaw = MRAOMap.Sample(Sampler, float3(input.UV, input.SliceIndex)).rgb;
    float metallic, roughness, ao;
    
    if (dot(pbrRaw, pbrRaw) < 0.01f) {
        metallic = 0.0f;
        roughness = 0.8f;
        ao = 1.0f;
    } else {
        metallic = pbrRaw.r;
        roughness = max(pbrRaw.g, 0.05f); // Защита от деления на ноль в BRDF
        ao = pbrRaw.b == 0.0f ? 1.0f : pbrRaw.b;
    }

    output.MRAO = float4(metallic, roughness, ao, 1.0f);
    return output;
}