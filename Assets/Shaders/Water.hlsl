cbuffer WaterConstants : register(b0) {
    float4 DeepColor;
    float4 ReflectionTint;
    float4 Params1; 
    float4 Params2;
    float4 Scroll1;
    float4 Scroll2;
    float3 CamPos;
    float Padding;
};

cbuffer TransformBuffer : register(b1) {
    matrix World;
    matrix View;
    matrix Projection;
};

Texture2D WaveMap : register(t0);
TextureCube SkyMap : register(t1);
SamplerState Sampler : register(s0);

struct VS_IN {
    float3 Pos : POSITION;
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
};

struct PS_IN {
    float4 Pos : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float4 Color : COLOR;
    float2 UV : TEXCOORD1;
};

PS_IN VSMain(VS_IN input) {
    PS_IN output;
    float4 worldPos = mul(float4(input.Pos, 1.0), World);
    output.WorldPos = worldPos.xyz;
    output.Pos = mul(mul(worldPos, View), Projection);
    output.UV = input.UV;
    output.Color = input.Color;
    return output;
}

float4 PSMain(PS_IN input) : SV_Target {
    float3 sunDir = normalize(float3(0.0, 0.08, 1.0)); 
    float3 sunColor = float3(1.0, 0.4, 0.1);           

    float3 toCam = normalize(CamPos - input.WorldPos);
    float time = Params1.w;

    float2 scale = Params2.zw; 
    float2 uv1 = input.UV * scale + Scroll1.xy * time * Params2.x; 
    float2 uv2 = input.UV * scale * 0.5 + Scroll2.xy * time * Params2.x;
    
    float3 n1 = WaveMap.Sample(Sampler, uv1).rgb * 2.0 - 1.0;
    float3 n2 = WaveMap.Sample(Sampler, uv2).rgb * 2.0 - 1.0;
    float3 combinedN = n1 + n2;

    float3 normal = normalize(float3(combinedN.x, 0.5, combinedN.y)); 

    float3 H = normalize(sunDir + toCam);
    float NdotH_Glow = saturate(dot(normal, H));
    
    float glowFactor = pow(NdotH_Glow, 4.0); 
    
    float3 glowLight = sunColor * glowFactor * 0.6; 

    float3 waterBody = (DeepColor.rgb * 0.8) + float3(0.01, 0.02, 0.05);
    
    waterBody += glowLight; 

    float NdotV = saturate(dot(toCam, normal)); 
    float fresnel = Params1.x + (1.0 - Params1.x) * pow(1.0 - NdotV, Params1.y);
    fresnel *= 0.5;

    float3 reflectDir = reflect(-toCam, normal);
    float3 reflection = SkyMap.Sample(Sampler, reflectDir).rgb * ReflectionTint.rgb * Params1.z;

    float roadWidth = 0.5;
    
    float roadLengthShortener = 55.0; 

    float3 sunNormal = combinedN;
    sunNormal.x *= roadWidth;           
    sunNormal.y *= roadLengthShortener; 
    sunNormal = normalize(float3(sunNormal.x, 3.0, sunNormal.y));

    float NdotH_Spec = saturate(dot(sunNormal, H));
    float sharpPower = Params2.y;
    float specular = pow(NdotH_Spec, sharpPower);

    float distanceMask = smoothstep(0.45, 0.3, NdotV);
    
    specular *= distanceMask;

    float3 sunSpecular = sunColor * specular * 0.1;

    float3 finalColor = lerp(waterBody, reflection, fresnel);
    finalColor += sunSpecular;

    float minAlpha = 0.2; 
    float mask = input.Color.a;
    float baseAlpha = max(mask, minAlpha); 

    float alpha = saturate((DeepColor.a * baseAlpha) + fresnel + specular + (glowFactor * 0.3));

    return float4(finalColor, alpha);
}