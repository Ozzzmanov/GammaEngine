Texture2D tDepth    : register(t0); 
SamplerState sPoint : register(s0);

cbuffer CB_GlobalWeather : register(b1) {
    float4 WindParams1;
    float4 WindParams2;
    float4 SunDirection; 
    float4 SunColor;     
    float4 SunParams;       
    float4 SunPositionNDC; 
};

struct VS_Output {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
    nointerpolation float SunVisibility : TEXCOORD1; // Считаем 1 раз на вершину!
};

VS_Output VSMain(uint id : SV_VertexID) {
    VS_Output output;
    output.UV = float2((id << 1) & 2, id & 2);
    output.Pos = float4(output.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);

    // =========================================================
    // OCCLUSION TEST В ВЕРШИННОМ ШЕЙДЕРЕ 
    // =========================================================
    float2 sunPos = SunPositionNDC.xy;
    bool isValidSun = (sunPos.x > -2.0f && sunPos.x < 3.0f) && (sunPos.y > -2.0f && sunPos.y < 3.0f);
    
    float visibility = 0.0f;
    
    if (SunPositionNDC.w >= 0.5f && SunPositionNDC.z > 0.0f && isValidSun) {
        float2 offsets[13] = {
            float2(0,0), 
            float2(0.04f, 0), float2(-0.04f, 0), float2(0, 0.06f), float2(0, -0.06f),
            float2(0.03f, 0.05f), float2(-0.03f, 0.05f), float2(0.03f, -0.05f), float2(-0.03f, -0.05f),
            float2(0.08f, 0), float2(-0.08f, 0), float2(0, 0.12f), float2(0, -0.12f)
        };
        
        uint width, height;
        tDepth.GetDimensions(width, height);
        
        [unroll]
        for (int i = 0; i < 13; i++) {
            float2 sampleUV = sunPos + offsets[i];
            if (sampleUV.x >= 0.0f && sampleUV.x <= 1.0f && sampleUV.y >= 0.0f && sampleUV.y <= 1.0f) {
                int3 texCoords = int3(sampleUV.x * width, sampleUV.y * height, 0);
                float d = tDepth.Load(texCoords).r; 
                visibility += step(0.999f, d); 
            }
        }
        visibility /= 13.0f;
    }
    
    output.SunVisibility = visibility;
    return output;
}

// Улучшенная форма блика (с хроматической аберрацией)
float3 Ghost(float2 uv, float2 center, float2 sunPos, float offset, float size, float aspect, float3 color) {
    float2 dirToCenter = center - sunPos;
    float2 ghostPos = sunPos + dirToCenter * offset; 
    float dist = length((uv - ghostPos) * float2(aspect, 1.0f));
    float intensity = pow(max(1.0f - dist / size, 0.0f), 5.0f);
    return color * intensity;
}

float4 PSMain(VS_Output input) : SV_Target {
    float sunVisibility = input.SunVisibility;
    if (sunVisibility <= 0.01f) discard;

    float2 sunPos = SunPositionNDC.xy;
    float2 uv = input.UV;
    float aspect = 1.777f; 

    // Затухание за краем экрана
    float edgeDistX = max(0.0f, abs(sunPos.x - 0.5f) - 0.5f);
    float edgeDistY = max(0.0f, abs(sunPos.y - 0.5f) - 0.5f);
    float screenEdgeFade = saturate(1.0f - length(float2(edgeDistX, edgeDistY)) * 2.0f);

    float3 finalFlares = float3(0, 0, 0);
    
    if (screenEdgeFade > 0.0f) {
        float2 screenCenter = float2(0.5f, 0.5f);
        float3 lensFlare = float3(0, 0, 0);
        
        lensFlare += Ghost(uv, screenCenter, sunPos, 1.5f, 0.05f, aspect, float3(0.1f, 0.3f, 0.5f)); 
        lensFlare += Ghost(uv, screenCenter, sunPos, 0.8f, 0.12f, aspect, float3(0.5f, 0.15f, 0.05f)); 
        lensFlare += Ghost(uv, screenCenter, sunPos, 2.0f, 0.20f, aspect, float3(0.2f, 0.05f, 0.4f)); 
        lensFlare += Ghost(uv, screenCenter, sunPos, 0.3f, 0.08f, aspect, float3(0.05f, 0.5f, 0.2f)); 
        
        finalFlares = lensFlare * screenEdgeFade * sunVisibility;
    }

    finalFlares.x = (finalFlares.x >= 0.0f && finalFlares.x < 65000.0f) ? finalFlares.x : 0.0f;
    finalFlares.y = (finalFlares.y >= 0.0f && finalFlares.y < 65000.0f) ? finalFlares.y : 0.0f;
    finalFlares.z = (finalFlares.z >= 0.0f && finalFlares.z < 65000.0f) ? finalFlares.z : 0.0f;

    return float4(finalFlares, 1.0f);
}