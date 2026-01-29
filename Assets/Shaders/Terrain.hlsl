// ===================================================================================
// Terrain.hlsl (ARRAY MODE - 24 LAYERS)
// Исправлено: Жесткое выравнивание памяти (packoffset) и порядок компонентов.
// ===================================================================================

cbuffer TransformBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
};

// C++ Структура:
// TextureIndices[6] -> 6 векторов по 16 байт = 96 байт. (Регистры c0 - c5)
// UProj[24]         -> Начинается с 96-го байта. (Регистр c6)
// VProj[24]         -> Начинается с 96 + 384 = 480-го байта. (Регистр c30)

cbuffer LayerInfo : register(b1) {
    // Жестко указываем начало данных (c0)
    int4 TextureIndices[6] : packoffset(c0);
    
    // Жестко указываем начало UProj (c6) - это ГАРАНТИРУЕТ, что шейдер будет читать там где надо
    float4 UProj[24]       : packoffset(c6);
    
    // Жестко указываем начало VProj (c30) (6 + 24 = 30)
    float4 VProj[24]       : packoffset(c30);
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

// Текстурный массив
Texture2DArray TextureArray : register(t0);

// Карты смешивания (6 штук для 24 слоев)
Texture2D BlendMap1 : register(t1); 
Texture2D BlendMap2 : register(t2); 
Texture2D BlendMap3 : register(t3); 
Texture2D BlendMap4 : register(t4);
Texture2D BlendMap5 : register(t5);
Texture2D BlendMap6 : register(t6);

SamplerState Sampler : register(s0);

// --- Vertex Shader ---
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

// --- Pixel Shader ---
float4 PSMain(PS_INPUT input) : SV_Target {
    // 1. Читаем веса из 6 карт смешивания
    float4 b1 = BlendMap1.Sample(Sampler, input.ChunkUV);
    float4 b2 = BlendMap2.Sample(Sampler, input.ChunkUV);
    float4 b3 = BlendMap3.Sample(Sampler, input.ChunkUV);
    float4 b4 = BlendMap4.Sample(Sampler, input.ChunkUV);
    float4 b5 = BlendMap5.Sample(Sampler, input.ChunkUV);
    float4 b6 = BlendMap6.Sample(Sampler, input.ChunkUV);

    // Собираем веса в массив (BGRA порядок, как в C++)
    float weights[24];
    weights[0] = b1.b; weights[1] = b1.g; weights[2] = b1.r; weights[3] = b1.a;
    weights[4] = b2.b; weights[5] = b2.g; weights[6] = b2.r; weights[7] = b2.a;
    weights[8] = b3.b; weights[9] = b3.g; weights[10] = b3.r; weights[11] = b3.a;
    weights[12] = b4.b; weights[13] = b4.g; weights[14] = b4.r; weights[15] = b4.a;
    weights[16] = b5.b; weights[17] = b5.g; weights[18] = b5.r; weights[19] = b5.a;
    weights[20] = b6.b; weights[21] = b6.g; weights[22] = b6.r; weights[23] = b6.a;

    // 2. Нормализация
    float sum = 0;
    for(int j=0; j<24; ++j) sum += weights[j];

    if (sum < 0.001) {
        weights[0] = 1.0f; 
    } else if (sum > 1.0) {
        float invSum = 1.0 / sum;
        for(int j=0; j<24; ++j) weights[j] *= invSum;
    }

    // 3. Смешивание
    float3 color = float3(0, 0, 0);

    [unroll]
    for (int i = 0; i < 24; ++i) {
        if (weights[i] > 0.001) {
            int vecIdx = i / 4;
            int compIdx = i % 4;
            
            // --- ИСПРАВЛЕНИЕ: Выбор компонента ---
            // В C++ мы писали индексы в порядке: [0]=Z(B), [1]=Y(G), [2]=X(R), [3]=W(A)
            // Шейдер читает int4 как xyzw. Нам нужно "перевернуть" логику чтения, 
            // чтобы она совпала с записью в C++.
            
            int texIndex = 0;
            int4 indicesVec = TextureIndices[vecIdx];

            if (compIdx == 0) texIndex = indicesVec.z;      // Layer 0 (Blue channel) -> stored in .z
            else if (compIdx == 1) texIndex = indicesVec.y; // Layer 1 (Green channel) -> stored in .y
            else if (compIdx == 2) texIndex = indicesVec.x; // Layer 2 (Red channel) -> stored in .x
            else texIndex = indicesVec.w;                   // Layer 3 (Alpha channel) -> stored in .w

            // Расчет UV с использованием правильного UProj
            float2 uv = float2(
                dot(float4(input.WorldPos, 1.0), UProj[i]),
                dot(float4(input.WorldPos, 1.0), VProj[i])
            );

            color += TextureArray.Sample(Sampler, float3(uv, texIndex)).rgb * weights[i];
        }
    }

    float3 norm = normalize(input.Norm);
    float3 lightDir = normalize(float3(0.5, 1.0, -0.5));
    float NdotL = max(dot(norm, lightDir), 0.0);
    float3 lighting = float3(0.4, 0.4, 0.4) + float3(0.6, 0.6, 0.6) * NdotL;

    return float4(color * lighting, 1.0);
}