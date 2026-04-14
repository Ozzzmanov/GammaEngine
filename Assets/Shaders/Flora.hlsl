// ================================================================================
// Flora.hlsl
// Рендеринга растительности (GPU-Driven).
// Включает: Инстансинг без участия CPU, Процедурный ветер, PBR, Normal Mapping 
// и симуляцию подповерхностного рассеивания (SSS).
// ================================================================================

cbuffer TransformBuffer : register(b0) {
    matrix WorldDummy; 
    matrix View;
    matrix Projection;
    float4 TimeAndParams; // x = GameTime
};

cbuffer WeatherBuffer : register(b2) {
    float4 WindParams1; // x = Speed, y = Trunk Amplitude, z = Root Stiffness
    float4 WindParams2; // x = Leaf Speed, y = Leaf Amplitude, z = Micro Turbulence
};

struct InstanceData {
    float3 Position;
    float3 Scale;
    uint   RotPacked; // Кватернион вращения, упакованный в 10-10-10-2
    uint   EntityID;
};

// --------------------------------------------------------------------------------
// ФУНКЦИИ РАСПАКОВКИ
// --------------------------------------------------------------------------------
float3 UnpackRotation(uint packed) {
    float x = (float)(packed & 0x3FF) / 1023.0f;
    float y = (float)((packed >> 10) & 0x3FF) / 1023.0f;
    float z = (float)((packed >> 20) & 0x3FF) / 1023.0f;
    return float3(x, y, z) * 2.0f - 1.0f; // Перевод из [0, 1] в [-1, 1]
}

float4 UnpackTangent(uint packed) {
    float x = (float)(packed & 0x3FF) / 1023.0f;
    float y = (float)((packed >> 10) & 0x3FF) / 1023.0f;
    float z = (float)((packed >> 20) & 0x3FF) / 1023.0f;
    float w = (float)(packed >> 30);
    return float4(x * 2.0f - 1.0f, y * 2.0f - 1.0f, z * 2.0f - 1.0f, (w > 0.5f) ? 1.0f : -1.0f);
}

// --------------------------------------------------------------------------------
// РЕСУРСЫ
// --------------------------------------------------------------------------------
StructuredBuffer<InstanceData> AllInstances           : register(t1); // Мега-буфер всех деревьев на карте
StructuredBuffer<uint>         VisibleInstanceIndices : register(t2); // Буфер видимых (после Compute Shader Culling)

Texture2DArray DiffuseMap : register(t0);
Texture2DArray MRAOMap    : register(t1); 
Texture2DArray NormalMap  : register(t2); 
SamplerState   Sampler    : register(s0);

struct VS_INPUT {
    float3 Pos            : POSITION;
    float4 Normal         : NORMAL;   
    float2 UV             : TEXCOORD0; 
    float4 WindColor      : COLOR0;   // R = Жесткость ствола, G = Вес листьев
    uint   TangentPacked  : COLOR1; 
    uint   InstanceID     : SV_InstanceID; 
    uint   InstanceOffset : INSTANCE_OFFSET; // Приходит из отдельного Vertex Buffer'а
};

struct PS_INPUT {
    float4 Pos      : SV_POSITION;
    float3 Norm     : NORMAL;
    float4 Tangent  : TANGENT; 
    float2 UV       : TEXCOORD0;
    
    // nointerpolation запрещает видеокарте интерполировать эти значения между вершинами, 
    // экономя такты GPU, так как эти данные одинаковы для всего треугольника.
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

    float xx = q.x * q.x; float yy = q.y * q.y; float zz = q.z * q.z;
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
    // Читаем 32-битный индекс из буфера видимости и расшифровываем его:
    uint packedData  = VisibleInstanceIndices[globalInstanceID];
    uint actualIndex = packedData & 0x1FFFFF;      // Биты [0-20]: Уникальный ID инстанса в массиве AllInstances
    uint sliceIndex  = (packedData >> 21) & 0x3FF; // Биты [21-30]: Индекс слоя в Texture2DArray (до 1024 слоев)
    uint isAlpha     = (packedData >> 31) & 0x1;   // Бит [31]: Флаг прозрачности (1 = листья, 0 = ствол)

    InstanceData inst = AllInstances[actualIndex];
    
    matrix worldMatrix = BuildWorldMatrix(inst.Position, inst.RotPacked, inst.Scale);
    float3 unpackedNormal = normalize(input.Normal.xyz * 2.0f - 1.0f);
    float4 unpackedTangent = UnpackTangent(input.TangentPacked); 
    
    // --- СИМУЛЯЦИЯ ВЕТРА ---
    float windSpeed            = WindParams1.x;
    float trunkBendAmplitude   = WindParams1.y;
    float rootStiffness        = WindParams1.z;
    float leafFlutterSpeed     = WindParams2.x;
    float leafFlutterAmplitude = WindParams2.y;
    float leafMicroTurbulence  = WindParams2.z;

    float time = TimeAndParams.x * windSpeed;
    
    // Индивидуальный сдвиг фазы, чтобы деревья качались вразнобой
    float phaseOffset = inst.Position.x * 0.05f + inst.Position.z * 0.08f + sin(inst.Position.x * 0.1f) * 2.0f;
    
    // Чем выше вершина (Pos.y), тем сильнее она раскачивается
    float heightFactor = saturate(input.Pos.y * rootStiffness);
    float trunkWeight = input.WindColor.r * heightFactor; 
    
    // Глобальное раскачивание ствола
    float2 windTime = time * float2(0.8f, 0.6f) + phaseOffset;
    float2 sway = sin(windTime) * trunkBendAmplitude * trunkWeight;
    float3 windOffset = float3(sway.x, 0.0f, sway.y);
    
    // Детальное дрожание листьев (рассчитывается только для листвы)
    if (isAlpha == 1) {
        float leafWeight = input.WindColor.g;
        float spatialPhase = dot(input.Pos, float3(0.5f, 0.3f, 0.7f)) * leafMicroTurbulence * 0.5f;
        float fTime = time * leafFlutterSpeed * 0.5f + phaseOffset * 2.0f + spatialPhase;
        
        float chaoticFlutter = sin(fTime) + sin(fTime * 1.43f) * 0.5f + cos(fTime * 1.94f) * 0.25f;
        float flutter = chaoticFlutter * leafFlutterAmplitude * leafWeight * heightFactor; 
        float flapY = (sin(fTime * 1.1f) + cos(fTime * 1.4f)) * 0.5f * leafFlutterAmplitude * leafWeight * heightFactor;
        
        windOffset += float3(flutter, flapY, flutter * 0.8f);
    }

    // Применяем смещение и переводим в мировые координаты
    float4 localPos = float4(input.Pos + windOffset, 1.0f);
    float4 worldPos = mul(localPos, worldMatrix);
    output.Pos = mul(mul(worldPos, View), Projection);
    
    output.Norm = mul(unpackedNormal, (float3x3)worldMatrix);
    output.Tangent.xyz = mul(unpackedTangent.xyz, (float3x3)worldMatrix);
    output.Tangent.w = unpackedTangent.w;
    
    output.UV = input.UV;
    
    output.SliceIndex = sliceIndex;
    output.IsAlpha = isAlpha;
    
    return output;
}

// --------------------------------------------------------------------------------
// ПИКСЕЛЬНЫЙ ШЕЙДЕР
// --------------------------------------------------------------------------------
struct PS_OUTPUT {
    float4 Albedo   : SV_Target0;
    float4 Normal   : SV_Target1;
    float4 MRAO     : SV_Target2;
};

PS_OUTPUT PSMain(PS_INPUT input) {
    PS_OUTPUT output;
        
    float4 color = DiffuseMap.Sample(Sampler, float3(input.UV, input.SliceIndex));
    
    // --- АЛЬФА-ТЕСТ (Early-Z Penalty) ---
    // Выполняется только для ветвей (IsAlpha == 1). Стволы (IsAlpha == 0) рисуются
    // без этого блока, что позволяет видеокарте включить Early-Z Rejection.
    if (input.IsAlpha == 1) {
        float brightness = color.r + color.g + color.b;
        if (color.a < 0.3f || brightness < 0.1f) discard; 
    }
    
    output.Albedo = color;
    
    // --- NORMAL MAPPING ---
    float3 sampledNormalRaw = NormalMap.Sample(Sampler, float3(input.UV, input.SliceIndex)).xyz;
    float3 sampledNormal;
    
    if (dot(sampledNormalRaw, sampledNormalRaw) < 0.01f) {
        sampledNormal = float3(0.0f, 0.0f, 1.0f); 
    } else {
        sampledNormal = sampledNormalRaw * 2.0f - 1.0f;
        sampledNormal.y = -sampledNormal.y; // Инверсия Y для DirectX (зависит от формата экспорта)
    }
    
    float3 N = normalize(input.Norm);
    float3 T = normalize(input.Tangent.xyz);
    
    // Ортогонализация Грама-Шмидта
    T = T - dot(T, N) * N;
    if (dot(T, T) < 0.001f) {
        float3 up = abs(N.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        T = normalize(cross(up, N));
    } else {
        T = normalize(T);
    }
    
    float3 B = cross(N, T) * input.Tangent.w;
    float3x3 TBN = float3x3(T, B, N);
    
    float3 finalNormal = normalize(mul(sampledNormal, TBN));

    // --- SSS (Subsurface Scattering Mask) ---
    // Упаковываем силу просвечивания в W-канал нормали для Deferred Light.
    // Стволы не просвечивают (1.0 = нет SSS), листья просвечивают (0.5).
    float sssMask = (input.IsAlpha == 1) ? 0.5f : 1.0f; 
    output.Normal = float4(finalNormal * 0.5f + 0.5f, sssMask);
    
    // --- PBR ---
    float3 pbrRaw = MRAOMap.Sample(Sampler, float3(input.UV, input.SliceIndex)).rgb;
    float metallic, roughness, ao;
    
    // Защита от пустых PBR-карт (если не подгрузились)
    if (dot(pbrRaw, pbrRaw) < 0.01f) {
        metallic = 0.0f;
        roughness = 0.8f;
        ao = 1.0f;
    } else {
        metallic = pbrRaw.r;
        roughness = max(pbrRaw.g, 0.05f); // Защита от нулевого roughness (артефакты в BRDF)
        ao = pbrRaw.b == 0.0f ? 1.0f : pbrRaw.b;
    }

    output.MRAO = float4(metallic, roughness, ao, 1.0f);
    
    return output;
}