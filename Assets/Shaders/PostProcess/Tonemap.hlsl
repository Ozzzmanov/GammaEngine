// ================================================================================
// Tonemap.hlsl
// ================================================================================

Texture2D tSceneHDR : register(t0);
Texture2D tBloom    : register(t1);
Texture2D tExposure : register(t2); 

SamplerState sLinear : register(s0);

cbuffer PostProcessCB : register(b0) {
    float4 BloomParams;     // X = Intensity
    float4 VignetteParams;  // X = Intensity, Y = Power
    float4 GrainParams;     // не используется
    float4 TonemapParams;   // X = Exposure Compensation
};

struct VS_Output { 
    float4 Pos : SV_POSITION; 
    float2 UV : TEXCOORD0; 
};

VS_Output VSMain(uint id : SV_VertexID) {
    VS_Output output;
    output.UV = float2((id << 1) & 2, id & 2);
    output.Pos = float4(output.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

// Быстрый ACES Тонемаппинг
float3 ACESFitted(float3 x) {
    return saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
}

float4 PSMain(VS_Output input) : SV_Target {
    float2 uv = input.UV;
    
    // Читаем сырые данные
    float3 sceneColor = tSceneHDR.Sample(sLinear, uv).rgb;
    float exposure = tExposure.Load(int3(0, 0, 0)).r;
    
    // Экспозиция + Компенсация
    exposure *= exp2(TonemapParams.x);
    sceneColor *= exposure;
    
    // Блум
    [branch]
    if (BloomParams.x > 0.001f) {
        sceneColor += tBloom.Sample(sLinear, uv).rgb * BloomParams.x * exposure;
    }
    
    // Виньетка
    [branch]
    if (VignetteParams.x > 0.001f) {
        float2 dist = abs(uv - 0.5f) * 2.0f;
        float v = 1.0f - dot(dist, dist);
        v = (VignetteParams.y > 1.9f && VignetteParams.y < 2.1f) ? v * v : pow(abs(v), VignetteParams.y);
        sceneColor *= lerp(1.0f, v, VignetteParams.x);
    }
    
    // Тонемаппинг
    float3 ldrColor = ACESFitted(sceneColor);
    
    // Гамма коррекция (чистый sRGB)
    float3 srgbColor = pow(abs(ldrColor), 1.0f / 2.2f);
    
    // Выводим идеально гладкий пиксель
    return float4(saturate(srgbColor), 1.0f);
}