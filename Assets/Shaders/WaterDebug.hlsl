// ===================================================================================
// Assets/Shaders/Water.hlsl
// ===================================================================================

struct VS_INPUT {
    float3 Pos : POSITION;
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
};

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float4 Color : COLOR;
    float2 UV : TEXCOORD1;
};

cbuffer WaterConstants : register(b0) {
    float4 DeepColor;
    float4 ReflectionTint;
    float4 Params1; 
    float4 Params2; 
    float4 Scroll1;
    float4 Scroll2;
    float4 FoamParams;
    float3 CamPos;
    float Padding;
};

cbuffer TransformBuffer : register(b1) {
    matrix World;
    matrix View;
    matrix Projection;
};

Texture2D WaveMap : register(t0);
TextureCube SkyMap : register(t1);
Texture2D FoamMap : register(t2);
SamplerState Sampler : register(s0);

// --- Vertex Shader ---
PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;
    
    // Позиция вершин идет из WaterVLO (локальная плоскость), умножаем на World Matrix
    float4 worldPos = mul(float4(input.Pos, 1.0f), World);
    output.WorldPos = worldPos.xyz;
    
    output.Pos = mul(worldPos, View);
    output.Pos = mul(output.Pos, Projection);
    
    output.UV = input.UV;
    
    // input.Color.a - это данные из .odata (прозрачность у берега)
    output.Color = input.Color; 
    
    return output;
}

// --- Pixel Shader ---
// Assets/Shaders/Water.hlsl - Pixel Shader Diagnostic
float4 PSMain(PS_INPUT input) : SV_Target {
    // ОТЛАДКА ШАГ 1: Игнорируем всё, просто красим в ярко-розовый
    // Если после этого ты увидишь розовые пятна - значит геометрия на месте!
    return float4(1.0f, 0.0f, 1.0f, 1.0f); 

    /* Старый код пока закомментируем
    float maskAlpha = input.Color.a;
    if (maskAlpha < 0.01) discard;
    return float4(DeepColor.rgb, maskAlpha);
    */
}