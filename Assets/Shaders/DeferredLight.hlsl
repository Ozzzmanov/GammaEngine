cbuffer LightParams : register(b0) {
    matrix InvViewProj;
    float3 CameraPos;
    float  Pad;
};

Texture2D        AlbedoTex : register(t0);
Texture2D        NormalTex : register(t1);
Texture2D<float> DepthTex  : register(t2);
SamplerState     Sampler   : register(s0);

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

VS_OUTPUT VSMain(uint id : SV_VertexID) {
    VS_OUTPUT output;
    output.UV = float2((id << 1) & 2, id & 2);
    output.Pos = float4(output.UV * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

float4 PSMain(VS_OUTPUT input) : SV_Target {
    float depth = DepthTex.SampleLevel(Sampler, input.UV, 0);
    
    if (depth >= 1.0f) {
        return float4(0.1f, 0.15f, 0.25f, 1.0f);
    }

    float4 albedo = AlbedoTex.Sample(Sampler, input.UV);
    float3 normal = NormalTex.Sample(Sampler, input.UV).xyz;

    float x = input.UV.x * 2.0f - 1.0f;
    float y = (1.0f - input.UV.y) * 2.0f - 1.0f;
    
    float4 ndcPos = float4(x, y, depth, 1.0f);
    
    float4 worldPos4 = mul(ndcPos, InvViewProj);
    
    float3 worldPos = worldPos4.xyz / worldPos4.w;

    float3 lightDir = normalize(float3(0.5, 1.0, -0.5));
    float NdotL = max(dot(normal, lightDir), 0.2f);
    
    float3 ambient = float3(0.4, 0.4, 0.4);
    float3 diffuse = float3(0.6, 0.6, 0.6) * NdotL;

    float3 finalColor = albedo.rgb * (ambient + diffuse);

    return float4(finalColor, 1.0f);
}