// ================================================================================
// Terrain.hlsl
// Гибридный рендер ландшафта (RVT Clipmap + Heavy Near Blending).
// На дистанции читает запеченные страницы (Virtual Texturing), 
// вблизи камеры динамически смешивает 24 материала через Splat Map.
// ================================================================================

cbuffer TransformBuffer : register(b0) {
    matrix World; 
    matrix View;
    matrix Projection;
    float4 TimeAndParams; // w = RVT Enable Flag
};

cbuffer ClipmapBuffer : register(b3) {
    float4 CascadeScales[6];    // x = 1.0f / WorldCoverage, w = TexelSize
    float4 CascadeDistances[6]; // x = transitionStart, y = transitionEnd
    int    NumCascades;
    int    Resolution;
    float  NearBlendStart;      // Дистанция начала перехода (RVT -> Heavy)
    float  NearBlendInvFade;    // Множитель для быстрого вычисления перехода
};

struct ChunkGpuData {
    float3 WorldPos;      
    uint   ArraySlice; 
    uint   MaterialIndices[24]; 
};

struct TerrainMaterial {
    float4 UProj;
    float4 VProj;
    uint   DiffuseIndex;
    float3 Padding;
};

// --------------------------------------------------------------------------------
// РЕСУРСЫ
// --------------------------------------------------------------------------------
StructuredBuffer<ChunkGpuData>    Chunks          : register(t0);
StructuredBuffer<uint>            VisibleIndices  : register(t1);
Texture2DArray<float>             HeightArray     : register(t2);
Texture2DArray<float>             HoleArray       : register(t3);

// Splat Map данные (Для "Тяжелого" рендера вблизи)
Texture2DArray<float4>            IndexArray      : register(t4); 
Texture2DArray<float4>            WeightArray     : register(t5); 
Texture2DArray<float4>            NormalArray     : register(t6);

// Кэш Runtime Virtual Texturing (Для дальних дистанций)
Texture2DArray<unorm float4>      RvtAlbedoArray  : register(t7); 

// Глобальные текстуры и настройки материалов
Texture2DArray                    DiffuseTextures : register(t10);
StructuredBuffer<TerrainMaterial> GlobalMaterials : register(t11); 

SamplerState SamplerWrap  : register(s0); 
SamplerState SamplerClamp : register(s1); 

struct VS_INPUT {
    float3 Pos            : POSITION;
    float3 Color          : COLOR;
    float3 Normal         : NORMAL;
    float2 UV             : TEXCOORD0;
    float4 Tangent        : TANGENT; 
    uint   InstanceID     : SV_InstanceID; 
    uint   InstanceOffset : INSTANCE_OFFSET;
};

struct PS_INPUT {
    float4 Pos      : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float2 RawUV    : TEXCOORD1;  
    nointerpolation uint ChunkID : TEXCOORD2; 
    float BakedAO   : TEXCOORD3; 
    float3 Normal   : NORMAL;
};

struct PS_OUTPUT {
    float4 Albedo   : SV_Target0;
    float4 Normal   : SV_Target1;
    float4 MRAO     : SV_Target2;
};

// --------------------------------------------------------------------------------
// ВЕРШИННЫЙ ШЕЙДЕР
// --------------------------------------------------------------------------------
PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;
    uint chunkID = VisibleIndices[input.InstanceOffset];
    ChunkGpuData chunk = Chunks[chunkID];

    // Выборка высоты и запеченных нормалей (Vertex Texture Fetch)
    float3 uvw = float3(input.UV, chunk.ArraySlice);
    float height = HeightArray.SampleLevel(SamplerClamp, uvw, 0).r;
    float4 packedNormalAO = NormalArray.SampleLevel(SamplerClamp, uvw, 0);

    float3 worldPos = input.Pos + chunk.WorldPos;
    worldPos.y += height; 

    output.WorldPos = worldPos;
    output.Pos = mul(float4(worldPos, 1.0f), View);
    output.Pos = mul(output.Pos, Projection);
    
    output.RawUV = input.UV;
    output.ChunkID = chunkID; 
    output.Normal = packedNormalAO.rgb * 2.0f - 1.0f; // Восстанавливаем нормаль
    output.BakedAO = packedNormalAO.a;                // Достаем AO из альфа-канала
    
    return output;
}

// --------------------------------------------------------------------------------
// ФУНКЦИИ БЛЕНДИНГА МАТЕРИАЛОВ (HEAVY TERRAIN)
// --------------------------------------------------------------------------------
// Вычисляет цвет одной конкретной точки на основе ее индексов и весов.
// ВАЖНО: Использует SampleGrad для корректной фильтрации текстур внутри динамического [branch].
float3 SampleTerrainColor(float4 indexRaw, float4 weightRaw, float3 worldPos, float3 ddxWP, float3 ddyWP, ChunkGpuData chunk) {
    uint4 localIndices = uint4(round(indexRaw * 255.0f));
    float3 color = 0.0f;
    float weightSum = 0.0f;
    
    [unroll]
    for (int i = 0; i < 4; ++i) {
        float w = weightRaw[i];
        [branch] 
        if (w > 0.01f) { 
            uint matID = chunk.MaterialIndices[localIndices[i]];
            TerrainMaterial mat = GlobalMaterials[matID];
            
            // Трипланарная (или планарная) проекция текстур
            float2 texUV = float2(dot(float4(worldPos, 1.0f), mat.UProj), dot(float4(worldPos, 1.0f), mat.VProj));
            float2 dx = float2(dot(ddxWP, mat.UProj.xyz), dot(ddxWP, mat.VProj.xyz));
            float2 dy = float2(dot(ddyWP, mat.UProj.xyz), dot(ddyWP, mat.VProj.xyz));
            
            // Выборка с явно указанными градиентами (SampleGrad)
            color += DiffuseTextures.SampleGrad(SamplerWrap, float3(texUV, mat.DiffuseIndex), dx, dy).rgb * w;
            weightSum += w;
        }
    }
    return weightSum > 0.001f ? (color / weightSum) : color;
}

// FIXME Дорогая функция рендера. Делает 4 выборки Splat Map и билинейно смешивает их.
float3 ComputeHeavyTerrain(PS_INPUT input, ChunkGpuData chunk, float3 ddxWP, float3 ddyWP) {
    float2 texSize = 128.0f; 
    float2 pixelUV = input.RawUV * texSize - 0.5f; 
    int2 p00 = floor(pixelUV); 
    float2 f = frac(pixelUV);  

    int3 c00 = int3(clamp(p00,             0, 127), chunk.ArraySlice);
    int3 c10 = int3(clamp(p00 + int2(1, 0), 0, 127), chunk.ArraySlice);
    int3 c01 = int3(clamp(p00 + int2(0, 1), 0, 127), chunk.ArraySlice);
    int3 c11 = int3(clamp(p00 + int2(1, 1), 0, 127), chunk.ArraySlice);

    float4 i00 = IndexArray.Load(int4(c00, 0)); float4 w00 = WeightArray.Load(int4(c00, 0));
    float4 i10 = IndexArray.Load(int4(c10, 0)); float4 w10 = WeightArray.Load(int4(c10, 0));
    float4 i01 = IndexArray.Load(int4(c01, 0)); float4 w01 = WeightArray.Load(int4(c01, 0));
    float4 i11 = IndexArray.Load(int4(c11, 0)); float4 w11 = WeightArray.Load(int4(c11, 0));

    // Если соседние пиксели SplatMap'ы идентичны (плоский цвет),
    // мы пропускаем билинейную интерполяцию и делаем одну выборку (экономим до 75% тактов).
    bool isUniform = all(abs(i00 - i10) < 0.01f) &&
                     all(abs(i00 - i01) < 0.01f) &&
                     all(abs(i00 - i11) < 0.01f) &&
                     all(abs(w00 - w10) < 0.01f) &&
                     all(abs(w00 - w01) < 0.01f) &&
                     all(abs(w00 - w11) < 0.01f);

    if (isUniform) {
        return SampleTerrainColor(i00, w00, input.WorldPos, ddxWP, ddyWP, chunk);
    } else {
        float3 color00 = SampleTerrainColor(i00, w00, input.WorldPos, ddxWP, ddyWP, chunk);
        float3 color10 = SampleTerrainColor(i10, w10, input.WorldPos, ddxWP, ddyWP, chunk);
        float3 color01 = SampleTerrainColor(i01, w01, input.WorldPos, ddxWP, ddyWP, chunk);
        float3 color11 = SampleTerrainColor(i11, w11, input.WorldPos, ddxWP, ddyWP, chunk);
        return lerp(lerp(color00, color10, f.x), lerp(color01, color11, f.x), f.y);
    }
}

// --------------------------------------------------------------------------------
// ПИКСЕЛЬНЫЙ ШЕЙДЕР
// --------------------------------------------------------------------------------
PS_OUTPUT PSMain(PS_INPUT input) {
    PS_OUTPUT output;
    ChunkGpuData chunk = Chunks[input.ChunkID];

    // Вырезаем пещеры
    float holeValue = HoleArray.Sample(SamplerClamp, float3(input.RawUV, chunk.ArraySlice)).r;
    clip(holeValue - 0.5f);

    float distToCam = length(mul(float4(input.WorldPos, 1.0f), View).xyz);
    float3 finalColor = 0.0f;

    // ===================================================================
    // Вычисляем экранные градиенты (ddx/ddy) 
    // СТРОГО за пределами динамических ветвлений [branch].
    // Это гарантирует корректную фильтрацию текстур.
    // ===================================================================
    float3 ddxWP = ddx(input.WorldPos);
    float3 ddyWP = ddy(input.WorldPos);

    // ===================================================================
    // 2. ГИБРИДНЫЙ РЕНДЕР (RVT + Heavy Rendering)
    // ===================================================================
    [branch]
    if (TimeAndParams.w > 0.5f) { // Включено ли RVT в настройках?
        
        // --- 2.1. ПОИСК КАСКАДОВ RVT ---
        int cascadeA = NumCascades - 1;
        int cascadeB = NumCascades - 1;

        for (int i = 0; i < NumCascades; ++i) {
            if (distToCam < CascadeDistances[i].x) {
                cascadeA = i;
                cascadeB = (i + 1 < NumCascades) ? (i + 1) : i;
                break;
            }
        }

        // Выборка из текущего каскада (RVT)
        float2 uvA = input.WorldPos.xz * CascadeScales[cascadeA].x;
        float3 rvtColor = RvtAlbedoArray.Sample(SamplerWrap, float3(uvA, cascadeA)).rgb;

        // Блендинг швов между соседними каскадами RVT
        float transitionStart = CascadeDistances[cascadeA].x;
        float transitionEnd   = CascadeDistances[cascadeA].y;
        float cascadeBlend    = saturate((distToCam - transitionStart) / (transitionEnd - transitionStart));

        [branch]
        if (cascadeBlend > 0.001f && cascadeA != cascadeB) {
            float2 uvB = input.WorldPos.xz * CascadeScales[cascadeB].x;
            float3 colorB = RvtAlbedoArray.Sample(SamplerWrap, float3(uvB, cascadeB)).rgb;
            rvtColor = lerp(rvtColor, colorB, cascadeBlend);
        }

        // --- 2.2. БЛИЖНИЙ ПЕРЕХОД (RVT -> Heavy Terrain) ---
        float nearBlend = 1.0f - saturate((distToCam - NearBlendStart) * NearBlendInvFade);

        [branch]
        if (nearBlend > 0.001f) {
            // Рисуем дорогую, кристально четкую поверхность
            float3 nearColor = ComputeHeavyTerrain(input, chunk, ddxWP, ddyWP);
            finalColor = lerp(rvtColor, nearColor, nearBlend);
        } else {
            // Для 90% пикселей на экране рендер ландшафта 
            // завершается прямо здесь (1 выборка текстуры вместо 16+).
            finalColor = rvtColor; 
        }
    }
    // Фолбэк, если RVT отключено в настройках графики
    else {
        finalColor = ComputeHeavyTerrain(input, chunk, ddxWP, ddyWP);
    }

    // Вывод в G-Buffer
    output.Albedo = float4(finalColor, 1.0f);
    output.Normal = float4(normalize(input.Normal) * 0.5f + 0.5f, 1.0f);
    output.MRAO   = float4(0.0f, 0.9f, input.BakedAO, 1.0f); // Террейн не металлический (0), шероховатость (0.9)
    
    return output;
}