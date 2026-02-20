cbuffer SceneBuffer : register(b0) {
    matrix WorldDummy; 
    matrix View;
    matrix Projection;
};

cbuffer BatchBuffer : register(b1) {
    uint BatchOffset;
    float3 BatchPadding;
};

struct InstanceData {
    float3 Position;
    float3 Scale;
    float3 RotXYZ;
    uint   EntityID;
    float2 Padding;
};

StructuredBuffer<InstanceData> AllInstances : register(t1);
StructuredBuffer<uint> VisibleInstanceIndices : register(t2);

Texture2D DiffuseMap : register(t0);
SamplerState Sampler : register(s0);

struct VS_INPUT {
    float3 Pos : POSITION;
    float3 Col : COLOR;     
    float3 Norm : NORMAL;
    float2 UV : TEXCOORD0;
    uint InstanceID : SV_InstanceID; 
};

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float4 WorldPos : TEXCOORD1;
    float3 Norm : NORMAL;
    float2 UV : TEXCOORD0;
};

struct PS_OUTPUT {
    float4 Albedo   : SV_Target0;
    float4 Normal   : SV_Target1;
};

matrix BuildWorldMatrix(float3 pos, float3 rotXYZ, float3 scale) {
    float w = sqrt(max(0.0f, 1.0f - dot(rotXYZ, rotXYZ)));
    float4 q = float4(rotXYZ, w);

    float xx = q.x * q.x; float yy = q.y * q.y; float zz = q.z * q.z;
    float xy = q.x * q.y; float xz = q.x * q.z; float yz = q.y * q.z;
    float wx = q.w * q.x; float wy = q.w * q.y; float wz = q.w * q.z;

    matrix m;
    m[0][0] = (1.0f - 2.0f * (yy + zz)) * scale.x;
    m[0][1] = (2.0f * (xy + wz)) * scale.x;
    m[0][2] = (2.0f * (xz - wy)) * scale.x;
    m[0][3] = 0.0f;

    m[1][0] = (2.0f * (xy - wz)) * scale.y;
    m[1][1] = (1.0f - 2.0f * (xx + zz)) * scale.y;
    m[1][2] = (2.0f * (yz + wx)) * scale.y;
    m[1][3] = 0.0f;

    m[2][0] = (2.0f * (xz + wy)) * scale.z;
    m[2][1] = (2.0f * (yz - wx)) * scale.z;
    m[2][2] = (1.0f - 2.0f * (xx + yy)) * scale.z;
    m[2][3] = 0.0f;

    m[3][0] = pos.x; m[3][1] = pos.y; m[3][2] = pos.z; m[3][3] = 1.0f;
    return m;
}

PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;

    uint globalInstanceID = input.InstanceID + BatchOffset;
    uint actualIndex = VisibleInstanceIndices[globalInstanceID];
    InstanceData inst = AllInstances[actualIndex];
    
    matrix worldMatrix = BuildWorldMatrix(inst.Position, inst.RotXYZ, inst.Scale);

    float4 worldPos = mul(float4(input.Pos, 1.0f), worldMatrix);
    output.WorldPos = worldPos;
    
    output.Pos = mul(worldPos, View);
    output.Pos = mul(output.Pos, Projection);

    output.Norm = mul(input.Norm, (float3x3)worldMatrix);
    output.UV = input.UV;

    return output;
}

PS_OUTPUT PSMain(PS_INPUT input) {
    PS_OUTPUT output;
    
    float4 color = DiffuseMap.Sample(Sampler, input.UV);
    clip(color.a - 0.5f);

    output.Albedo = color;
    output.Normal = float4(normalize(input.Norm), 1.0f);
    
    return output;
}