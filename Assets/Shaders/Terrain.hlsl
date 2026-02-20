cbuffer TransformBuffer : register(b0) {
    matrix World; 
    matrix View;
    matrix Projection;
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

StructuredBuffer<ChunkGpuData> Chunks             : register(t0);
StructuredBuffer<uint>         VisibleIndices   : register(t1);
StructuredBuffer<TerrainMaterial> GlobalMaterials : register(t11); 

Texture2DArray<float>  HeightArray  : register(t2);
Texture2DArray<float>  HoleArray    : register(t3);

Texture2DArray<float4> IndexArray   : register(t4); 
Texture2DArray<float4> WeightArray  : register(t5); 

Texture2DArray DiffuseTextures      : register(t10);

SamplerState SamplerWrap  : register(s0); 
SamplerState SamplerClamp : register(s1); 

struct VS_INPUT {
    float3 Pos      : POSITION;
    float3 Color    : COLOR;
    float3 Normal   : NORMAL;
    float2 UV       : TEXCOORD0;
    uint InstanceID : SV_InstanceID; 
};

struct PS_INPUT {
    float4 Pos      : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float2 RawUV    : TEXCOORD1;  
    float3 Normal   : NORMAL;
    nointerpolation uint ChunkID : TEXCOORD2; 
};

struct PS_OUTPUT {
    float4 Albedo   : SV_Target0;
    float4 Normal   : SV_Target1;
};

PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;
    uint chunkID = VisibleIndices[input.InstanceID];
    ChunkGpuData chunk = Chunks[chunkID];

    float3 uvw = float3(input.UV, chunk.ArraySlice);
    float height = HeightArray.SampleLevel(SamplerClamp, uvw, 0).r;

    float3 worldPos = input.Pos + chunk.WorldPos;
    worldPos.y = height; 

    output.WorldPos = worldPos;
    output.Pos = mul(float4(worldPos, 1.0f), View);
    output.Pos = mul(output.Pos, Projection);
    output.RawUV = input.UV;
    output.ChunkID = chunkID; 

    float pixelStep = 1.0f / 36.0f; 
    float hL = HeightArray.SampleLevel(SamplerClamp, float3(saturate(input.UV.x - pixelStep), input.UV.y, chunk.ArraySlice), 0).r;
    float hR = HeightArray.SampleLevel(SamplerClamp, float3(saturate(input.UV.x + pixelStep), input.UV.y, chunk.ArraySlice), 0).r;
    float hD = HeightArray.SampleLevel(SamplerClamp, float3(input.UV.x, saturate(input.UV.y - pixelStep), chunk.ArraySlice), 0).r;
    float hU = HeightArray.SampleLevel(SamplerClamp, float3(input.UV.x, saturate(input.UV.y + pixelStep), chunk.ArraySlice), 0).r;
    
    output.Normal = normalize(float3(hL - hR, 2.0f * (100.0f / 36.0f), hD - hU));
    return output;
}

float3 SampleTerrainColor(float4 indexRaw, float4 weightRaw, float3 worldPos, float3 ddxWP, float3 ddyWP, ChunkGpuData chunk) {
    uint4 localIndices = uint4(round(indexRaw * 255.0f));
    float3 color = 0.0f;
    
    [unroll]
    for (int i = 0; i < 4; ++i) {
        float w = weightRaw[i];
        
        [branch] 
        if (w > 0.001f) {
            uint matID = chunk.MaterialIndices[localIndices[i]];
            TerrainMaterial mat = GlobalMaterials[matID];
            
            float2 texUV = float2(dot(float4(worldPos, 1.0f), mat.UProj), dot(float4(worldPos, 1.0f), mat.VProj));
            float2 dx = float2(dot(ddxWP, mat.UProj.xyz), dot(ddxWP, mat.VProj.xyz));
            float2 dy = float2(dot(ddyWP, mat.UProj.xyz), dot(ddyWP, mat.VProj.xyz));
            
            color += DiffuseTextures.SampleGrad(SamplerWrap, float3(texUV, mat.DiffuseIndex), dx, dy).rgb * w;
        }
    }
    return color;
}

PS_OUTPUT PSMain(PS_INPUT input) {
    PS_OUTPUT output;

    ChunkGpuData chunk = Chunks[input.ChunkID];

    float holeValue = HoleArray.SampleLevel(SamplerClamp, float3(input.RawUV, chunk.ArraySlice), 0).r;
    clip(holeValue - 0.5f); 

    float3 ddxWP = ddx(input.WorldPos);
    float3 ddyWP = ddy(input.WorldPos);

    float2 texSize = 128.0f; 
    float2 pixelUV = input.RawUV * texSize - 0.5f; 
    
    int2 p00 = floor(pixelUV); 
    float2 f = frac(pixelUV);  

    int3 c00 = int3(clamp(p00 + int2(0, 0), 0, 127), chunk.ArraySlice);
    int3 c10 = int3(clamp(p00 + int2(1, 0), 0, 127), chunk.ArraySlice);
    int3 c01 = int3(clamp(p00 + int2(0, 1), 0, 127), chunk.ArraySlice);
    int3 c11 = int3(clamp(p00 + int2(1, 1), 0, 127), chunk.ArraySlice);

    float4 i00 = IndexArray.Load(int4(c00, 0));
    float4 i10 = IndexArray.Load(int4(c10, 0));
    float4 i01 = IndexArray.Load(int4(c01, 0));
    float4 i11 = IndexArray.Load(int4(c11, 0));

    float4 w00 = WeightArray.Load(int4(c00, 0));
    float4 w10 = WeightArray.Load(int4(c10, 0));
    float4 w01 = WeightArray.Load(int4(c01, 0));
    float4 w11 = WeightArray.Load(int4(c11, 0));

    float3 color00 = SampleTerrainColor(i00, w00, input.WorldPos, ddxWP, ddyWP, chunk);
    float3 color10 = SampleTerrainColor(i10, w10, input.WorldPos, ddxWP, ddyWP, chunk);
    float3 color01 = SampleTerrainColor(i01, w01, input.WorldPos, ddxWP, ddyWP, chunk);
    float3 color11 = SampleTerrainColor(i11, w11, input.WorldPos, ddxWP, ddyWP, chunk);

    float3 finalColor = lerp(lerp(color00, color10, f.x), lerp(color01, color11, f.x), f.y);

    output.Albedo = float4(finalColor, 1.0f);
    output.Normal = float4(normalize(input.Normal), 1.0f);

    return output;
}