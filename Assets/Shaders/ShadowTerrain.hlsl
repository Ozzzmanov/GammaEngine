// ================================================================================
// ShadowTerrain.hlsl
// Облегченный шейдер террейна для записи в каскадные карты теней (CSM).
// Вычисляет только смещение вершин (Displacement/Height) и вырезает дыры (Holes).
// ================================================================================

cbuffer TransformBuffer : register(b0) {
    matrix WorldDummy; 
    matrix View;
    matrix Projection; // Для теней здесь лежит LightViewProj матрица каскада
    float4 TimeAndParams;
};

// Данные текущего чанка
struct ChunkGpuData {
    float3 WorldPos;  
    uint   ArraySlice;          // Слой в мега-текстуре (Texture2DArray)
    uint   MaterialIndices[24]; // Индексы материалов (не используются в тени)
};

// --------------------------------------------------------------------------------
// РЕСУРСЫ
// --------------------------------------------------------------------------------
StructuredBuffer<ChunkGpuData> Chunks         : register(t0);
StructuredBuffer<uint>         VisibleIndices : register(t1); // После Frustum Culling
Texture2DArray<float>          HeightArray    : register(t2); // Карта высот
Texture2DArray<float>          HoleArray      : register(t3); // Маска дыр (пещеры)

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
    float4 Pos : SV_POSITION; 
    float2 UV  : TEXCOORD0;      
    nointerpolation uint SliceIndex : TEXCOORD1; 
};

// --------------------------------------------------------------------------------
// ВЕРШИННЫЙ ШЕЙДЕР (Displacement Mapping)
// --------------------------------------------------------------------------------
PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;
    
    // Получаем данные текущего чанка
    uint chunkID = VisibleIndices[input.InstanceOffset];
    ChunkGpuData chunk = Chunks[chunkID];

    // Выборка высоты (Vertex Texture Fetch)
    float height = HeightArray.SampleLevel(SamplerClamp, float3(input.UV, chunk.ArraySlice), 0).r;
    
    // Смещение вершины по оси Y
    float3 worldPos = input.Pos + chunk.WorldPos;
    worldPos.y = height; 

    // Перевод в clip-space источника света
    output.Pos = mul(float4(worldPos, 1.0f), Projection);
    output.UV = input.UV;
    output.SliceIndex = chunk.ArraySlice;
    
    return output;
}

// --------------------------------------------------------------------------------
// ПИКСЕЛЬНЫЙ ШЕЙДЕР (Alpha Test Only)
// --------------------------------------------------------------------------------
void PSMain(PS_INPUT input) {
    // Читаем маску дыр и отменяем запись в тень, если это пещера.
    // Если дыр нет, этот шейдер отрабатывает мгновенно, так как тут только 1 выборка.
    float holeValue = HoleArray.SampleLevel(SamplerClamp, float3(input.UV, input.SliceIndex), 0).r;
    clip(holeValue - 0.5f); 
}