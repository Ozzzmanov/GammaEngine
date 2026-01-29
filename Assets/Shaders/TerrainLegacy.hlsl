// TerrainLegacy.hlsl (24 Layers Version)

cbuffer TransformBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
};

// Буфер слоев (24 штуки)
cbuffer LayerInfo : register(b1) {
    // Padding: 6 int4 = 96 байт
    int4 P1; int4 P2; int4 P3; int4 P4; int4 P5; int4 P6;
    
    float4 UProj[24]; 
    float4 VProj[24]; 
};

struct VS_INPUT {
    float3 Pos : POSITION;
    float2 UV  : TEXCOORD0; 
    float3 Norm : NORMAL;
};

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float3 WorldPos : TEXCOORD0; 
    float2 ChunkUV : TEXCOORD1;
    float3 Norm : NORMAL;
};

// --- 24 ТЕКСТУРЫ (t0 - t23) ---
Texture2D Tex0 : register(t0);  Texture2D Tex1 : register(t1);
Texture2D Tex2 : register(t2);  Texture2D Tex3 : register(t3);
Texture2D Tex4 : register(t4);  Texture2D Tex5 : register(t5);
Texture2D Tex6 : register(t6);  Texture2D Tex7 : register(t7);
Texture2D Tex8 : register(t8);  Texture2D Tex9 : register(t9);
Texture2D Tex10 : register(t10); Texture2D Tex11 : register(t11);
Texture2D Tex12 : register(t12); Texture2D Tex13 : register(t13);
Texture2D Tex14 : register(t14); Texture2D Tex15 : register(t15);
Texture2D Tex16 : register(t16); Texture2D Tex17 : register(t17);
Texture2D Tex18 : register(t18); Texture2D Tex19 : register(t19);
Texture2D Tex20 : register(t20); Texture2D Tex21 : register(t21);
Texture2D Tex22 : register(t22); Texture2D Tex23 : register(t23);


// --- 6 КАРТ СМЕШИВАНИЯ (t24 - t29) ---
Texture2D BlendMap1 : register(t24); // 0-3
Texture2D BlendMap2 : register(t25); // 4-7
Texture2D BlendMap3 : register(t26); // 8-11
Texture2D BlendMap4 : register(t27); // 12-15
Texture2D BlendMap5 : register(t28); // 16-19
Texture2D BlendMap6 : register(t29); // 20-23

SamplerState Sampler : register(s0);

PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;
    float4 worldPos = mul(float4(input.Pos, 1.0f), World);
    output.WorldPos = worldPos.xyz;
    output.Pos = mul(worldPos, View);
    output.Pos = mul(output.Pos, Projection);
    output.ChunkUV = input.UV;
    output.Norm = mul(input.Norm, (float3x3)World);
    return output;
}

float3 SampleLayerLegacy(Texture2D tex, float4 uProj, float4 vProj, float3 worldPos) {
    float2 uv = float2(dot(float4(worldPos, 1.0), uProj), dot(float4(worldPos, 1.0), vProj));
    return tex.Sample(Sampler, uv).rgb;
}

float4 PSMain(PS_INPUT input) : SV_Target {
    // 1. Читаем 6 карт смешивания
    float4 b1 = BlendMap1.Sample(Sampler, input.ChunkUV);
    float4 b2 = BlendMap2.Sample(Sampler, input.ChunkUV);
    float4 b3 = BlendMap3.Sample(Sampler, input.ChunkUV);
    float4 b4 = BlendMap4.Sample(Sampler, input.ChunkUV);
    float4 b5 = BlendMap5.Sample(Sampler, input.ChunkUV);
    float4 b6 = BlendMap6.Sample(Sampler, input.ChunkUV);

    // 2. Распаковка весов (BGRA)
    float w0=b1.b; float w1=b1.g; float w2=b1.r; float w3=b1.a;
    float w4=b2.b; float w5=b2.g; float w6=b2.r; float w7=b2.a;
    float w8=b3.b; float w9=b3.g; float w10=b3.r; float w11=b3.a;
    float w12=b4.b; float w13=b4.g; float w14=b4.r; float w15=b4.a;
    float w16=b5.b; float w17=b5.g; float w18=b5.r; float w19=b5.a;
    float w20=b6.b; float w21=b6.g; float w22=b6.r; float w23=b6.a;

    // 3. Нормализация
    float sum = w0+w1+w2+w3+w4+w5+w6+w7+w8+w9+w10+w11+w12+w13+w14+w15+w16+w17+w18+w19+w20+w21+w22+w23;
    if (sum < 0.001) w0 = 1.0f;
    else if (sum > 1.0) {
        float inv = 1.0 / sum;
        w0*=inv; w1*=inv; w2*=inv; w3*=inv; w4*=inv; w5*=inv; w6*=inv; w7*=inv;
        w8*=inv; w9*=inv; w10*=inv; w11*=inv; w12*=inv; w13*=inv; w14*=inv; w15*=inv;
        w16*=inv; w17*=inv; w18*=inv; w19*=inv; w20*=inv; w21*=inv; w22*=inv; w23*=inv;
    }

    // 4. Смешивание
    float3 color = float3(0,0,0);
    if (w0 > 0.001) color += SampleLayerLegacy(Tex0, UProj[0], VProj[0], input.WorldPos) * w0;
    if (w1 > 0.001) color += SampleLayerLegacy(Tex1, UProj[1], VProj[1], input.WorldPos) * w1;
    if (w2 > 0.001) color += SampleLayerLegacy(Tex2, UProj[2], VProj[2], input.WorldPos) * w2;
    if (w3 > 0.001) color += SampleLayerLegacy(Tex3, UProj[3], VProj[3], input.WorldPos) * w3;
    if (w4 > 0.001) color += SampleLayerLegacy(Tex4, UProj[4], VProj[4], input.WorldPos) * w4;
    if (w5 > 0.001) color += SampleLayerLegacy(Tex5, UProj[5], VProj[5], input.WorldPos) * w5;
    if (w6 > 0.001) color += SampleLayerLegacy(Tex6, UProj[6], VProj[6], input.WorldPos) * w6;
    if (w7 > 0.001) color += SampleLayerLegacy(Tex7, UProj[7], VProj[7], input.WorldPos) * w7;
    
    if (w8 > 0.001) color += SampleLayerLegacy(Tex8, UProj[8], VProj[8], input.WorldPos) * w8;
    if (w9 > 0.001) color += SampleLayerLegacy(Tex9, UProj[9], VProj[9], input.WorldPos) * w9;
    if (w10 > 0.001) color += SampleLayerLegacy(Tex10, UProj[10], VProj[10], input.WorldPos) * w10;
    if (w11 > 0.001) color += SampleLayerLegacy(Tex11, UProj[11], VProj[11], input.WorldPos) * w11;
    if (w12 > 0.001) color += SampleLayerLegacy(Tex12, UProj[12], VProj[12], input.WorldPos) * w12;
    if (w13 > 0.001) color += SampleLayerLegacy(Tex13, UProj[13], VProj[13], input.WorldPos) * w13;
    if (w14 > 0.001) color += SampleLayerLegacy(Tex14, UProj[14], VProj[14], input.WorldPos) * w14;
    if (w15 > 0.001) color += SampleLayerLegacy(Tex15, UProj[15], VProj[15], input.WorldPos) * w15;

    if (w16 > 0.001) color += SampleLayerLegacy(Tex16, UProj[16], VProj[16], input.WorldPos) * w16;
    if (w17 > 0.001) color += SampleLayerLegacy(Tex17, UProj[17], VProj[17], input.WorldPos) * w17;
    if (w18 > 0.001) color += SampleLayerLegacy(Tex18, UProj[18], VProj[18], input.WorldPos) * w18;
    if (w19 > 0.001) color += SampleLayerLegacy(Tex19, UProj[19], VProj[19], input.WorldPos) * w19;
    if (w20 > 0.001) color += SampleLayerLegacy(Tex20, UProj[20], VProj[20], input.WorldPos) * w20;
    if (w21 > 0.001) color += SampleLayerLegacy(Tex21, UProj[21], VProj[21], input.WorldPos) * w21;
    if (w22 > 0.001) color += SampleLayerLegacy(Tex22, UProj[22], VProj[22], input.WorldPos) * w22;
    if (w23 > 0.001) color += SampleLayerLegacy(Tex23, UProj[23], VProj[23], input.WorldPos) * w23;

    float3 norm = normalize(input.Norm);
    float3 lightDir = normalize(float3(0.5, 1.0, -0.5));
    float NdotL = max(dot(norm, lightDir), 0.0);
    float3 lighting = float3(0.4, 0.4, 0.4) + float3(0.6, 0.6, 0.6) * NdotL;

    return float4(color * lighting, 1.0);
}