// ================================================================================
// ShadowFlora.hlsl
// Облегченный шейдер для рендера растительности в карту теней (Cascaded Shadow Maps).
// Исключены PBR и нормали, оставлена только симуляция ветра и альфа-тест.
// ================================================================================

cbuffer TransformBuffer : register(b0) {
    matrix WorldDummy; 
    matrix View;
    matrix Projection; // Содержит LightViewProj для текущего каскада тени
    float4 TimeAndParams; 
};

cbuffer WeatherBuffer : register(b2) {
    float4 WindParams1;
    float4 WindParams2;
};

struct InstanceData {
    float3 Position; 
    float3 Scale; 
    uint   RotPacked; 
    uint   EntityID;
};

float3 UnpackRotation(uint packed) {
    float x = (float)(packed & 0x3FF) / 1023.0f;
    float y = (float)((packed >> 10) & 0x3FF) / 1023.0f;
    float z = (float)((packed >> 20) & 0x3FF) / 1023.0f;
    return float3(x, y, z) * 2.0f - 1.0f;
}

StructuredBuffer<InstanceData> AllInstances           : register(t1);
StructuredBuffer<uint>         VisibleInstanceIndices : register(t2);

Texture2DArray DiffuseMap : register(t0); // Нужно только для чтения Альфа-канала
SamplerState   Sampler    : register(s0);

struct VS_INPUT {
    float3 Pos            : POSITION;
    float4 Normal         : NORMAL;   
    float2 UV             : TEXCOORD0; 
    float4 WindColor      : COLOR0;   
    uint   TangentPacked  : COLOR1; 
    uint   InstanceID     : SV_InstanceID; 
    uint   InstanceOffset : INSTANCE_OFFSET;
};

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
    nointerpolation uint SliceIndex : TEXCOORD1; 
    nointerpolation uint IsAlpha    : COLOR1;    
};

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
    uint actualIndex = packedData & 0x1FFFFF;      
    uint sliceIndex  = (packedData >> 21) & 0x3FF; 
    uint isAlpha     = (packedData >> 31) & 0x1;   

    InstanceData inst = AllInstances[actualIndex];
    
    matrix worldMatrix = BuildWorldMatrix(inst.Position, inst.RotPacked, inst.Scale);
    
    // --- СИМУЛЯЦИЯ ВЕТРА (Должна 1:1 совпадать с Flora.hlsl для корректных теней) ---
    float time = TimeAndParams.x * WindParams1.x;
    float phaseOffset = inst.Position.x * 0.05f + inst.Position.z * 0.08f + sin(inst.Position.x * 0.1f) * 2.0f;
    float heightFactor = saturate(input.Pos.y * WindParams1.z);
    float trunkWeight = input.WindColor.r * heightFactor; 
    
    float2 windTime = time * float2(0.8f, 0.6f) + phaseOffset;
    float2 sway = sin(windTime) * WindParams1.y * trunkWeight;
    float3 windOffset = float3(sway.x, 0.0f, sway.y);
    
    if (isAlpha == 1) {
        float leafWeight = input.WindColor.g;
        float spatialPhase = dot(input.Pos, float3(0.5f, 0.3f, 0.7f)) * WindParams2.z * 0.5f;
        float fTime = time * WindParams2.x * 0.5f + phaseOffset * 2.0f + spatialPhase;
        
        float chaoticFlutter = sin(fTime) + sin(fTime * 1.43f) * 0.5f + cos(fTime * 1.94f) * 0.25f;
        float flutter = chaoticFlutter * WindParams2.y * leafWeight * heightFactor; 
        float flapY = (sin(fTime * 1.1f) + cos(fTime * 1.4f)) * 0.5f * WindParams2.y * leafWeight * heightFactor;
        
        windOffset += float3(flutter, flapY, flutter * 0.8f);
    }

    float4 localPos = float4(input.Pos + windOffset, 1.0f);
    float4 worldPos = mul(localPos, worldMatrix);
    
    // Projection содержит LightViewProj. Записываем Z-буфер тени.
    output.Pos = mul(worldPos, Projection);
    output.UV = input.UV;
    
    output.SliceIndex = sliceIndex;
    output.IsAlpha = isAlpha;
    
    return output;
}

// --------------------------------------------------------------------------------
// ПИКСЕЛЬНЫЙ ШЕЙДЕР (Вызывается только для прохода Альфа-канала)
// Стволы (IsOpaque) рендерятся вообще без этого метода (Pixel Shader = nullptr),
// что ускоряет запекание теней в 2-3 раза.
// --------------------------------------------------------------------------------
void PSMain(PS_INPUT input) {
    if (input.IsAlpha == 1) {
        float a = DiffuseMap.Sample(Sampler, float3(input.UV, input.SliceIndex)).a;
        clip(a - 0.5f); // Отбрасываем "пустые" пиксели листьев, чтобы тень была фигурной
    }
}