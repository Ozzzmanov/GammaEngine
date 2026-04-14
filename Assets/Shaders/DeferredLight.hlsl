// ================================================================================
// DeferredLight.hlsl
// Главный проход освещения: читает G-Buffer, считает PBR, тени и локальный свет.
// ================================================================================

cbuffer LightParams : register(b0) {
    matrix InvViewProj;
    matrix LightViewProj[4];
    matrix ViewMatrix;   
    float4 CascadeSplits;
    float3 CameraPos;
    float  CascadeCount;
    float4 LightSettings; 
    float4 ExtraSettings; 
};

cbuffer CB_GlobalWeather : register(b1) {
    float4 WindParams1;
    float4 WindParams2;
    float4 SunDirection;
    float4 SunColor;
    float4 SunParams;       
    float4 SunPositionNDC; 
    float4 SkyZenithColor;
    float4 SkyHorizonColor;
};

struct PointLight { float3 Position; float Radius; float3 Color; float Intensity; };
struct SpotLight  { float3 Position; float Radius; float3 Color; float Intensity; float3 Direction; float CosConeAngle; };

Texture2D              AlbedoTex     : register(t0);
Texture2D              NormalTex     : register(t1);
Texture2D              MRAOTex       : register(t2); 
Texture2D<float>       DepthTex      : register(t3);
Texture2DArray<float>  ShadowMap     : register(t4);
Texture2D              VolumetricTex : register(t5);

StructuredBuffer<PointLight> PointLights : register(t6); 
StructuredBuffer<SpotLight>  SpotLights  : register(t7); 

SamplerState           Sampler       : register(s0);
SamplerComparisonState ShadowSampler : register(s1);

static const float PI = 3.14159265359f;

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

// --------------------------------------------------------------------------------
// VERTEX SHADER: Zero-Buffer Fullscreen Triangle
// Генерирует один огромный треугольник, покрывающий весь экран, без использования Vertex Buffer.
// --------------------------------------------------------------------------------
VS_OUTPUT VSMain(uint id : SV_VertexID) {
    VS_OUTPUT output;
    output.UV = float2((id << 1) & 2, id & 2);
    output.Pos = float4(output.UV * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

// --------------------------------------------------------------------------------
// PBR MATH (Cook-Torrance BRDF)
// --------------------------------------------------------------------------------
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float nom   = a2;
    float denom = (NdotH * NdotH * (a2 - 1.0f) + 1.0f);
    return nom / max(PI * denom * denom, 0.0000001f); 
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0f), roughness) * GeometrySchlickGGX(max(dot(N, L), 0.0f), roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0) - F0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

float3 DoLightBRDF(float3 L, float3 V, float3 N, float3 albedo, float metallic, float roughness, float3 F0, float3 radiance) {
    float3 H = normalize(V + L);
    float NDF = DistributionGGX(N, H, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);      
    float3 F  = FresnelSchlick(max(dot(H, V), 0.0f), F0);
    
    float3 specular = (NDF * G * F) / max(4.0f * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f), 0.001f);
    float3 kD = (1.0f - F) * (1.0f - metallic); 
    
    return (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0.0f);
}

// --------------------------------------------------------------------------------
// SHADOW MAPPING (Cascaded + PCF)
// --------------------------------------------------------------------------------
float CalculateShadowGeometry(float3 worldPos, float3 N, float3 L) {
    int maxCascades = (int)CascadeCount;
    float viewZ = mul(float4(worldPos, 1.0f), ViewMatrix).z;
    
    if (viewZ > CascadeSplits[maxCascades - 1]) return 1.0f;

    // Расчет весов для 4-го каскада
    float4 cascadeWeights = float4(
        viewZ <= CascadeSplits[0] ? 1.0f : 0.0f,
        (viewZ > CascadeSplits[0] && viewZ <= CascadeSplits[1]) ? 1.0f : 0.0f,
        (viewZ > CascadeSplits[1] && viewZ <= CascadeSplits[2]) ? 1.0f : 0.0f,
        (viewZ > CascadeSplits[2]) ? 1.0f : 0.0f // Раньше тут был жесткий 0.0f
    );
    int cascadeIndex = (int)dot(cascadeWeights, float4(0, 1, 2, 3));

    // Смещение по нормали (Normal Bias) для борьбы с теневой угревой сыпью (Shadow Acne)
    float texSize = 0.08f * (cascadeIndex + 1);
    float NdotL = max(dot(N, L), 0.0001f);
    float normalOffset = texSize * min(sqrt(1.0f - NdotL * NdotL) / max(NdotL, 0.001f), 2.5f);  
    float3 biasedPos = worldPos + N * normalOffset;

    float4 lightSpacePos = mul(float4(biasedPos, 1.0f), LightViewProj[cascadeIndex]);
    float2 shadowUV = float2(lightSpacePos.x * 0.5f + 0.5f, -lightSpacePos.y * 0.5f + 0.5f);
    float currentDepth = lightSpacePos.z;

    if(shadowUV.x < 0.0f || shadowUV.x > 1.0f || shadowUV.y < 0.0f || shadowUV.y > 1.0f || currentDepth > 1.0f)
        return 1.0f;

    float depthBias = 0.00004f * (cascadeIndex + 1);
    float pcfRadius = (1.2f + cascadeIndex * 0.4f) / 2048.0f; 
    
    // Фиксированный паттерн Poisson Disk (Аппаратный PCF через SampleCmpLevelZero)
    static const float2 PoissonDisk[4] = {
        float2(-0.94201624f, -0.39906216f),
        float2( 0.94558609f, -0.76890725f),
        float2(-0.094184101f, -0.92938870f),
        float2( 0.34495938f,  0.29387760f)
    };

    float shadow = 0.0f;
    [unroll]
    for(int i = 0; i < 4; i++) {
        shadow += ShadowMap.SampleCmpLevelZero(ShadowSampler, float3(shadowUV + PoissonDisk[i] * pcfRadius, cascadeIndex), currentDepth - depthBias);
    }
    return shadow / 4.0f; 
}

// --------------------------------------------------------------------------------
// VOLUMETRICS
// --------------------------------------------------------------------------------
float3 SampleVolumetric(float2 uv) {
    uint volW, volH;
    VolumetricTex.GetDimensions(volW, volH);
    float2 texel = 1.0f / float2(volW, volH);
    
    // Простой Tent Blur (Крест) для сглаживания вокселей
    float3 vol = VolumetricTex.SampleLevel(Sampler, uv, 0).rgb * 0.5f;
    vol += VolumetricTex.SampleLevel(Sampler, uv + float2(-texel.x, -texel.y), 0).rgb * 0.125f;
    vol += VolumetricTex.SampleLevel(Sampler, uv + float2( texel.x, -texel.y), 0).rgb * 0.125f;
    vol += VolumetricTex.SampleLevel(Sampler, uv + float2(-texel.x,  texel.y), 0).rgb * 0.125f;
    vol += VolumetricTex.SampleLevel(Sampler, uv + float2( texel.x,  texel.y), 0).rgb * 0.125f;
    return vol;
}

// --------------------------------------------------------------------------------
// PIXEL SHADER
// --------------------------------------------------------------------------------
float4 PSMain(VS_OUTPUT input) : SV_Target {
    float depth = DepthTex.SampleLevel(Sampler, input.UV, 0);
    
    // Восстановление позиции из буфера глубины
    float4 ndcPos = float4(input.UV.x * 2.0f - 1.0f, (1.0f - input.UV.y) * 2.0f - 1.0f, depth, 1.0f);
    float4 worldPos4 = mul(ndcPos, InvViewProj);
    float3 worldPos = worldPos4.xyz / worldPos4.w;

    float3 V = normalize(CameraPos - worldPos);
    float sunAltitude = saturate(SunDirection.y); 

    float3 volumetricLight = SampleVolumetric(input.UV);
    volumetricLight *= 0.6f; 
    volumetricLight = min(volumetricLight, float3(1.5f, 1.5f, 1.5f));

    // ОПТИМИЗАЦИЯ НЕБА (Early Exit)
    // Если глубина равна 1.0, это чистый фон. Рисуем небо и выходим.
    [branch] 
    if (depth >= 1.0f) {
        float3 viewDir = normalize(worldPos - CameraPos); 
        float VoL = max(dot(viewDir, SunDirection.xyz), 0.0f);
        
        float3 skyColor = lerp(SkyHorizonColor.rgb, SkyZenithColor.rgb, saturate(max(viewDir.y, 0.0f) * 2.5f));
        skyColor *= (0.05f + sunAltitude * 3.0f); 
        
        float mie = pow(VoL, 250.0f) * 0.15f; 
        float3 haloColor = (SunColor.rgb * SunColor.w) * mie; 
        float sunDiscMask = smoothstep(0.99985f, 0.99995f, VoL);
        float3 sunDisc = sunDiscMask * (SunColor.rgb * SunColor.w) * 2.0f; 

        return float4(saturate(skyColor + haloColor) + sunDisc + volumetricLight, 1.0f); 
    }

    // ЧТЕНИЕ G-BUFFER
    // Дегамма (SRGB -> Linear) для корректного PBR
    float3 albedo = pow(abs(AlbedoTex.Sample(Sampler, input.UV).rgb), 2.2f); 
    
    float3 N = normalize(NormalTex.Sample(Sampler, input.UV).xyz * 2.0f - 1.0f);
    float sssMask = NormalTex.Sample(Sampler, input.UV).w; // Фейк SSS в W-канале нормалей
    
    float4 mrao = MRAOTex.Sample(Sampler, input.UV);
    float metallic = mrao.r;
    float roughness = max(mrao.g, 0.05f); 
    float ao = pow(abs(mrao.b), 2.5f); 

    // Базовая отражательная способность (F0)
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 Lo = float3(0, 0, 0);

    // ОСВЕЩЕНИЕ ОТ СОЛНЦА (Directional)
    float sunFadeOut = smoothstep(0.0f, 0.15f, sunAltitude);
    float3 sunDir = normalize(SunDirection.xyz);
    float3 sunColor = SunColor.rgb * SunColor.w * 2.5f; 
    
    float rawShadow = CalculateShadowGeometry(worldPos, N, sunDir);
    float shadowFactor = lerp(1.0f, rawShadow, saturate(SunDirection.y * 10.0f));

    // Фейковый Subsurface Scattering (Просвечивание листвы)
    float sssLight = 0.0f;
    [branch]
    if (sssMask < 0.9f) { 
        float backlight = max(dot(V, -sunDir), 0.0f);
        sssLight = pow(abs(backlight), 4.0f) * (1.0f - sssMask) * 0.5f; 
    }

    Lo += DoLightBRDF(sunDir, V, N, albedo, metallic, roughness, F0, sunColor) * shadowFactor;
    // Лист должен слегка светиться даже в тени (lerp 0.2f к 1.0f)
    Lo += albedo * sunColor * sssLight * lerp(0.2f, 1.0f, shadowFactor); 

    // ЛОКАЛЬНЫЕ ИСТОЧНИКИ СВЕТА
    float artificialLightScale = lerp(1.0f, 0.2f, saturate(sunAltitude * 2.0f)); 
    float distToCamSq = dot(worldPos - CameraPos, worldPos - CameraPos);
    
    [branch]
    if (distToCamSq < (LightSettings.z * LightSettings.z)) {
        
        // Point Lights
        uint pointCount = (uint)LightSettings.x;
        [loop]
        for (uint i = 0; i < pointCount; i++) {
            PointLight light = PointLights[i];
            float3 lightVec = light.Position - worldPos;
            float distSq = dot(lightVec, lightVec);
            
            [branch]
            if (distSq < (light.Radius * light.Radius)) {
                // Frostbite Windowing Falloff
                float falloff = saturate(1.0f - pow(distSq / (light.Radius * light.Radius), 2.0f));
                float3 radiance = light.Color * (light.Intensity * artificialLightScale) * ((falloff * falloff) / (distSq + 1.0f));
                Lo += DoLightBRDF(normalize(lightVec), V, N, albedo, metallic, roughness, F0, radiance);
            }
        }

        // Spot Lights
        uint spotCount = (uint)LightSettings.y;
        [loop]
        for (uint j = 0; j < spotCount; j++) {
            SpotLight light = SpotLights[j];
            float3 lightVec = light.Position - worldPos;
            float distSq = dot(lightVec, lightVec);
            
            [branch]
            if (distSq < (light.Radius * light.Radius)) {
                float3 L = lightVec / sqrt(distSq);
                float spotIntensity = saturate((dot(L, normalize(-light.Direction)) - light.CosConeAngle) / 0.05f);
                
                [branch]
                if (spotIntensity > 0.0f) {
                    float falloff = saturate(1.0f - pow(distSq / (light.Radius * light.Radius), 2.0f));
                    float3 radiance = light.Color * (light.Intensity * artificialLightScale) * ((falloff * falloff) / (distSq + 1.0f)) * spotIntensity;
                    Lo += DoLightBRDF(L, V, N, albedo, metallic, roughness, F0, radiance);
                }
            }
        }
    }

    Lo *= (0.5f + 0.5f * ao); 
    
    // AMBIENT / IBL (Global Illumination)
    float shadowOcclusion = lerp(0.02f, 1.0f, shadowFactor); 
    float3 skyDiffuse = lerp(float3(0.002f, 0.002f, 0.002f), float3(0.02f, 0.05f, 0.09f), saturate(N.y * 0.5f + 0.5f));
    skyDiffuse *= (0.05f + sunAltitude * 2.0f); 
    
    float3 F_ambient = FresnelSchlickRoughness(max(dot(N, V), 0.0f), F0, roughness);
    float3 diffuseAmbient = skyDiffuse * albedo * (1.0f - F_ambient) * (1.0f - metallic) * shadowOcclusion;
    
    float3 reflectDir = reflect(-V, N);
    float3 reflectionColor = lerp(float3(0.002f, 0.002f, 0.002f), float3(0.04f, 0.05f, 0.08f), saturate(reflectDir.y * 0.5f + 0.5f)); 
    reflectionColor *= (0.05f + sunAltitude * 2.0f);
    
    float3 specularAmbient = reflectionColor * F_ambient * shadowOcclusion; 
    float3 ambient = (diffuseAmbient + specularAmbient) * ao;

    // Итоговый цвет
    return float4(ambient + Lo + volumetricLight, 1.0f);
}