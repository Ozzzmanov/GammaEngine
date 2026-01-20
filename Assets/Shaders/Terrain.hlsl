cbuffer TransformBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
};

cbuffer LayerInfo : register(b1) {
    // [0] = Слои 0-3 (x=L2, y=L1, z=L0, w=L3)
    // [1] = Слои 4-7 (x=L4, y=L5, z=L6, w=L7)
    int4 TextureIndices[2]; 
    
    float4 UProj[8];
    float4 VProj[8];
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

Texture2DArray DiffuseArray : register(t0);
Texture2D BlendMap1 : register(t1); // Слои 0-3
Texture2D BlendMap2 : register(t2); // Слои 4-7
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

float4 PSMain(PS_INPUT input) : SV_Target {
    // 1. Читаем ОБЕ карты
    float4 b1 = BlendMap1.Sample(Sampler, input.ChunkUV);
    float4 b2 = BlendMap2.Sample(Sampler, input.ChunkUV);

    // 2. Извлекаем веса. Порядок должен совпадать с C++!
    // Map1: B=L0, G=L1, R=L2, A=L3
    float w0_base = b1.b; // Это не "остаток", а данные из файла. Но часто там 0.
    float w1 = b1.g;
    float w2 = b1.r;
    float w3 = b1.a;
    
    // Map2: R=L4, G=L5, B=L6, A=L7
    float w4 = b2.r;
    float w5 = b2.g;
    float w6 = b2.b;
    float w7 = b2.a;

    // 3. Вычисляем РЕАЛЬНЫЙ вес базового слоя (Layer 0).
    // Он заполняет всё пространство, не занятое другими слоями.
    // Если в карте w0_base было что-то записано, мы это игнорируем (или добавляем),
    // так как в BigWorld слой 0 всегда считается "подложкой".
    float wTotalOverlay = w1 + w2 + w3 + w4 + w5 + w6 + w7;
    float w0 = saturate(1.0 - wTotalOverlay);

    float3 finalColor = float3(0,0,0);
    float4 projPos = float4(input.WorldPos, 1.0f);
    float2 uv;

    // --- СМЕШИВАНИЕ ---
    
    // Слой 0 (Base) -> TextureIndices[0].z
    if (w0 > 0.001) {
        uv = float2(dot(projPos, UProj[0]), dot(projPos, VProj[0]));
        finalColor += DiffuseArray.Sample(Sampler, float3(uv, TextureIndices[0].z)).rgb * w0;
    }
    // Слой 1 -> TextureIndices[0].y
    if (w1 > 0.001) {
        uv = float2(dot(projPos, UProj[1]), dot(projPos, VProj[1]));
        finalColor += DiffuseArray.Sample(Sampler, float3(uv, TextureIndices[0].y)).rgb * w1;
    }
    // Слой 2 -> TextureIndices[0].x
    if (w2 > 0.001) {
        uv = float2(dot(projPos, UProj[2]), dot(projPos, VProj[2]));
        finalColor += DiffuseArray.Sample(Sampler, float3(uv, TextureIndices[0].x)).rgb * w2;
    }
    // Слой 3 -> TextureIndices[0].w
    if (w3 > 0.001) {
        uv = float2(dot(projPos, UProj[3]), dot(projPos, VProj[3]));
        finalColor += DiffuseArray.Sample(Sampler, float3(uv, TextureIndices[0].w)).rgb * w3;
    }
    // Слой 4 -> TextureIndices[1].x
    if (w4 > 0.001) {
        uv = float2(dot(projPos, UProj[4]), dot(projPos, VProj[4]));
        finalColor += DiffuseArray.Sample(Sampler, float3(uv, TextureIndices[1].x)).rgb * w4;
    }
    // Слой 5 -> TextureIndices[1].y
    if (w5 > 0.001) {
        uv = float2(dot(projPos, UProj[5]), dot(projPos, VProj[5]));
        finalColor += DiffuseArray.Sample(Sampler, float3(uv, TextureIndices[1].y)).rgb * w5;
    }
    // Слой 6 -> TextureIndices[1].z
    if (w6 > 0.001) {
        uv = float2(dot(projPos, UProj[6]), dot(projPos, VProj[6]));
        finalColor += DiffuseArray.Sample(Sampler, float3(uv, TextureIndices[1].z)).rgb * w6;
    }
    // Слой 7 -> TextureIndices[1].w
    if (w7 > 0.001) {
        uv = float2(dot(projPos, UProj[7]), dot(projPos, VProj[7]));
        finalColor += DiffuseArray.Sample(Sampler, float3(uv, TextureIndices[1].w)).rgb * w7;
    }

    // --- Освещение ---
    float3 norm = normalize(input.Norm);
    float3 lightDir = normalize(float3(0.5, 1.0, -0.5));
    float diff = max(dot(norm, lightDir), 0.0);
    float3 lighting = float3(0.3, 0.3, 0.35) + (diff * float3(1.0, 0.95, 0.8));

    return float4(finalColor * lighting, 1.0f);
}