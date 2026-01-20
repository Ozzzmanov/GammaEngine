// Water.hlsl

cbuffer TransformBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
};

// Индивидуальные параметры куска воды
cbuffer WaterParams : register(b1) {
    float4 DeepColor;
    float4 ReflectionTint;
    float4 PositionSize; // xyz = pos, w = unused
    float4 Config1;      // x=scaleX, y=scaleY, z=scroll1, w=scroll2
    float4 Config2;      // x=windX, y=windY, z=fresnelC, w=fresnelExp
    float2 Size;         // w, h
    float2 Padding;
};

struct VS_INPUT {
    float3 Pos : POSITION;
    float2 UV  : TEXCOORD0; 
    float3 Norm : NORMAL;
};

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float3 WorldPos : TEXCOORD0; 
    float2 UV : TEXCOORD1;
    float3 Norm : NORMAL;
};

Texture2D WaveTexture : register(t0);
Texture2D FoamTexture : register(t1);
Texture2D ReflectionMap : register(t2); // CubeMap или Planar reflection
SamplerState Sampler : register(s0);

PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;
    
    // Трансформация: Масштабируем единичный квадрат на размер воды
    float3 localPos = input.Pos;
    localPos.x *= Size.x;
    localPos.z *= Size.y;
    
    // Перемещаем в позицию воды
    float3 worldPos = localPos + PositionSize.xyz;
    
    output.WorldPos = worldPos;
    output.Pos = mul(float4(worldPos, 1.0f), View);
    output.Pos = mul(output.Pos, Projection);
    
    // UV с учетом тайлинга
    output.UV = input.UV * Config1.xy; 
    output.Norm = float3(0, 1, 0); // Вода смотрит вверх
    
    return output;
}

float4 PSMain(PS_INPUT input) : SV_Target {
    // 1. Анимация волн (Два слоя нормалей, движущихся навстречу/под углом)
    float2 uv1 = input.UV + float2(Config1.z, Config1.z * 0.5);
    float2 uv2 = input.UV * 0.7 - float2(Config1.w, Config1.w * 0.3); // Второй слой медленнее и крупнее
    
    float3 n1 = WaveTexture.Sample(Sampler, uv1).rgb * 2.0 - 1.0;
    float3 n2 = WaveTexture.Sample(Sampler, uv2).rgb * 2.0 - 1.0;
    
    float3 normal = normalize(n1 + n2); // Смешиваем нормали
    
    // 2. Освещение (Specular / Sun Reflection)
    float3 viewDir = normalize(float3(0, 500, 0) - input.WorldPos); // Примерная камера
    float3 lightDir = normalize(float3(0.5, 1.0, -0.5));
    float3 halfVec = normalize(lightDir + viewDir);
    
    float spec = pow(max(dot(normal, halfVec), 0.0), 128.0) * 0.8;
    
    // 3. Френель (Прозрачность/Отражение)
    float fresnel = Config2.z + (1.0 - Config2.z) * pow(1.0 - dot(viewDir, float3(0,1,0)), Config2.w);
    
    // 4. Итоговый цвет
    float3 waterColor = DeepColor.rgb;
    float3 reflectionColor = float3(0.6, 0.7, 0.8); // Пока просто цвет неба
    
    // Смешиваем глубину и отражение
    float3 finalColor = lerp(waterColor, reflectionColor, fresnel) + spec;
    
    return float4(finalColor, 0.9f); // Небольшая прозрачность
}