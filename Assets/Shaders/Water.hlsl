// ================================================================================
// Water.hlsl
// Работает ПЛОХО! В будущих обновлениях ПЕРЕПИСАТЬ!
// Океан с поддержкой волн Герстнера, аппаратной тесселяцией, физическим 
// поглощением света (Depth Absorption) и генерацией процедурной пены.
// Разделен на два пути: POTATO_MODE (Vertex) и ULTRA_MODE (Hull/Domain).
// ================================================================================

cbuffer CB_Transform : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
    float4 TimeAndParams; // x = GameTime
};

cbuffer CB_GlobalWeather : register(b1) {
    float4 WindParams1;
    float4 WindParams2;
    float4 SunDirection;
    float4 SunColor;
};

cbuffer CB_WaterParams : register(b2) {
    float4 DeepColor;
    float4 ShallowColor;
    float4 FoamColor;
    float4 Waves[4];          // Параметры волн: XY = Direction, Z = Steepness, W = Wavelength
    float3 CamPos;
    float  Time;
    float  TessellationFactor;
    float  TessellationMaxDist;
    float  DepthAbsorptionScale;
    float  GlobalWaveScale;
    int    QualityLevel;
    int    EnableRefraction;
    float2 ZBufferParams;     // X = NearZ, Y = FarZ
};

// --------------------------------------------------------------------------------
// РЕСУРСЫ
// --------------------------------------------------------------------------------
Texture2D   NormalMap     : register(t0);
Texture2D   FoamMap       : register(t1);
TextureCube EnvMap        : register(t2); // Skybox для отражений
Texture2D   DepthMap      : register(t3); // Z-Buffer непрозрачной геометрии
Texture2D   RefractionMap : register(t4); // HDR-текстура сцены до рендера воды

SamplerState SamplerWrap  : register(s0);
SamplerState SamplerClamp : register(s1);

struct VS_IN {
    float3 Pos : POSITION;
    float2 UV  : TEXCOORD;
};

struct PS_IN {
    float4 ScreenPos        : SV_Position;
    float4 ClipPos          : TEXCOORD0;
    float3 WorldPos         : TEXCOORD1;
    float3 Normal           : TEXCOORD2;
    float3 Tangent          : TEXCOORD3;
    float3 BiTangent        : TEXCOORD4;
    float2 UV               : TEXCOORD5;
    float  WaveDisplacement : TEXCOORD6; // Высота волны: 0.0 (впадина) .. 1.0 (гребень)
};

// --------------------------------------------------------------------------------
// МАТЕМАТИКА ВОДЫ
// --------------------------------------------------------------------------------

// Линеаризация значения глубины из аппаратного Z-Buffer
float LinearizeDepth(float d) {
    float n = ZBufferParams.x;
    float f = ZBufferParams.y;
    return (n * f) / max((f - d * (f - n)), 0.00001f);
}

// Вычисление смещения вершины и касательного пространства по алгоритму Герстнера
void GerstnerWave(
    float4 wave, float3 pos, float time, float scale,
    inout float3 displacement, inout float3 tangent, inout float3 binormal)
{
    float2 dir        = normalize(wave.xy);
    float  steepness  = wave.z;
    float  wavelength = wave.w / max(scale, 0.01f);
    
    float  k          = 6.28318f / wavelength; // Волновое число
    float  c          = sqrt(2.5f / k);        // Фазовая скорость
    float  f          = k * (dot(dir, pos.xz) - c * time);
    float  a          = steepness / k;         // Амплитуда

    displacement.x += dir.x * a * cos(f);
    displacement.y += a * sin(f);
    displacement.z += dir.y * a * cos(f);

    tangent += float3(
         1.0f - dir.x * dir.x * steepness * sin(f),
         dir.x * steepness * cos(f),
        -dir.x * dir.y * steepness * sin(f)
    );
        
    binormal += float3(
        -dir.x * dir.y * steepness * sin(f),
         dir.y * steepness * cos(f),
         1.0f - dir.y * dir.y * steepness * sin(f)
    );
}

// ================================================================================
// ВЕРШИННЫЙ И ТЕССЕЛЯЦИОННЫЙ ЭТАПЫ
// ================================================================================

#ifdef POTATO_MODE

// Упрощенный путь для низких настроек графики (расчет волн в Vertex Shader)
PS_IN VSMain(VS_IN input) {
    float3 pos = input.Pos;
    float3 displacement = float3(0, 0, 0);
    float3 tangent      = float3(1, 0, 0);
    float3 binormal     = float3(0, 0, 1);

    float t = Time * 0.35f;
    float3 pos0 = pos;
    float3 pos1 = pos + float3( 7.3f, 0.0f, 13.7f);
    float3 pos2 = pos + float3(-11.1f, 0.0f,  5.9f);
    float3 pos3 = pos + float3( 3.7f, 0.0f, -17.3f);

    if (QualityLevel >= 1) GerstnerWave(Waves[0], pos0, t * 1.00f, GlobalWaveScale, displacement, tangent, binormal);
    if (QualityLevel >= 2) GerstnerWave(Waves[1], pos1, t * 0.73f, GlobalWaveScale, displacement, tangent, binormal);
    if (QualityLevel >= 3) GerstnerWave(Waves[2], pos2, t * 1.31f, GlobalWaveScale, displacement, tangent, binormal);
    if (QualityLevel >= 4) GerstnerWave(Waves[3], pos3, t * 0.57f, GlobalWaveScale, displacement, tangent, binormal);

    float waveDisplacement = saturate((displacement.y + 2.0f) / 4.0f);
    pos += displacement;

    float3 N = normalize(cross(tangent, binormal));
    float3 T = normalize(tangent - dot(tangent, N) * N);
    float3 B = cross(N, T);

    PS_IN output;
    output.WorldPos         = pos;
    output.Normal           = N;
    output.Tangent          = T;
    output.BiTangent        = B;
    output.UV               = input.UV;
    output.WaveDisplacement = waveDisplacement;

    float4 clipPos   = mul(mul(float4(pos, 1.0f), View), Projection);
    output.ScreenPos = clipPos;
    output.ClipPos   = clipPos;
    return output;
}

#else 

// Путь с аппаратной тесселяцией (Hull & Domain Shaders)
struct HS_OUT {
    float3 Pos : POSITION;
    float2 UV  : TEXCOORD;
};

struct HS_CONST_OUT {
    float EdgeTess[4]   : SV_TessFactor;
    float InsideTess[2] : SV_InsideTessFactor;
};

HS_OUT VSMain(VS_IN input) {
    HS_OUT output;
    output.Pos = input.Pos;
    output.UV  = input.UV;
    return output;
}

HS_CONST_OUT HSConst(InputPatch<HS_OUT, 4> patch, uint patchID : SV_PrimitiveID) {
    HS_CONST_OUT output;
    float3 center = (patch[0].Pos + patch[1].Pos + patch[2].Pos + patch[3].Pos) * 0.25f;
    
    // Дистанционный LOD тесселяции
    float  dist   = distance(CamPos, center);
    float  t      = 1.0f - saturate(dist / max(TessellationMaxDist, 1.0f));
    float  tess   = max(1.0f, TessellationFactor * t);

    output.EdgeTess[0]   = tess;
    output.EdgeTess[1]   = tess;
    output.EdgeTess[2]   = tess;
    output.EdgeTess[3]   = tess;
    output.InsideTess[0] = tess;
    output.InsideTess[1] = tess;
    return output;
}

[domain("quad")]
[partitioning("fractional_even")]
[outputtopology("triangle_ccw")]
[outputcontrolpoints(4)]
[patchconstantfunc("HSConst")]
HS_OUT HSMain(InputPatch<HS_OUT, 4> patch, uint i : SV_OutputControlPointID) {
    return patch[i];
}

[domain("quad")]
PS_IN DSMain(
    HS_CONST_OUT hsConst,
    float2 uv : SV_DomainLocation,
    const OutputPatch<HS_OUT, 4> patch)
{
    float3 pos   = lerp(lerp(patch[0].Pos, patch[1].Pos, uv.x),
                        lerp(patch[2].Pos, patch[3].Pos, uv.x), uv.y);
    float2 texUV = lerp(lerp(patch[0].UV,  patch[1].UV,  uv.x),
                        lerp(patch[2].UV,  patch[3].UV,  uv.x), uv.y);

    float3 displacement = float3(0, 0, 0);
    float3 tangent      = float3(1, 0, 0);
    float3 binormal     = float3(0, 0, 1);

    float t = Time * 0.35f;
    float3 pos0 = pos;
    float3 pos1 = pos + float3( 7.3f, 0.0f, 13.7f);
    float3 pos2 = pos + float3(-11.1f, 0.0f,  5.9f);
    float3 pos3 = pos + float3( 3.7f, 0.0f, -17.3f);

    // Применение волн Герстнера к сгенерированной геометрии
    if (QualityLevel >= 1) GerstnerWave(Waves[0], pos0, t * 1.00f, GlobalWaveScale, displacement, tangent, binormal);
    if (QualityLevel >= 2) GerstnerWave(Waves[1], pos1, t * 0.73f, GlobalWaveScale, displacement, tangent, binormal);
    if (QualityLevel >= 3) GerstnerWave(Waves[2], pos2, t * 1.31f, GlobalWaveScale, displacement, tangent, binormal);
    if (QualityLevel >= 4) GerstnerWave(Waves[3], pos3, t * 0.57f, GlobalWaveScale, displacement, tangent, binormal);

    float waveDisplacement = saturate((displacement.y + 2.0f) / 4.0f);
    pos += displacement;

    // Восстановление ортогонального базиса
    float3 N = normalize(cross(tangent, binormal));
    float3 T = normalize(tangent - dot(tangent, N) * N);
    float3 B = cross(N, T);

    PS_IN output;
    output.WorldPos         = pos;
    output.Normal           = N;
    output.Tangent          = T;
    output.BiTangent        = B;
    output.UV               = texUV;
    output.WaveDisplacement = waveDisplacement;

    float4 clipPos   = mul(mul(float4(pos, 1.0f), View), Projection);
    output.ScreenPos = clipPos;
    output.ClipPos   = clipPos;
    return output;
}
#endif // POTATO_MODE

// ================================================================================
// ПИКСЕЛЬНЫЙ ЭТАП (Shading & Optical Effects)
// ================================================================================

float4 PSMain(PS_IN input) : SV_Target
{
    // --- 1. ЭКРАННЫЕ КООРДИНАТЫ И ГЛУБИНА ---
    float2 ndcXY    = input.ClipPos.xy / input.ClipPos.w;
    float2 screenUV = ndcXY * float2(0.5f, -0.5f) + 0.5f;

    float rawDepth    = DepthMap.SampleLevel(SamplerClamp, screenUV, 0).r;
    float sceneDepthM = LinearizeDepth(rawDepth);
    
    float waterNDZ    = input.ClipPos.z / input.ClipPos.w;
    float waterDepthM = LinearizeDepth(waterNDZ);
    
    float thickness   = max(sceneDepthM - waterDepthM, 0.0f);
    float depthBlend  = smoothstep(0.0f, 15.0f, thickness);

    // --- 2. НОРМАЛИ И КАСАТЕЛЬНОЕ ПРОСТРАНСТВО ---
    // Смешивание двух слоев карт нормалей для эффекта мелкой ряби
    float2 uv1 = input.UV * 18.0f - float2( Time * 0.005f,  Time * 0.005f);
    float2 uv2 = input.UV * 32.0f - float2( Time * 0.006f, -Time * 0.011f);

    float3 n1 = NormalMap.Sample(SamplerWrap, uv1).rgb * 2.0f - 1.0f;
    float3 n2 = NormalMap.Sample(SamplerWrap, uv2).rgb * 2.0f - 1.0f;
    n1.xy *= 1.5f; 
    n2.xy *= 1.5f;
    float3 normalTS = normalize(n1 + n2 * 0.5f);

    // Ортогонализация Грама-Шмидта (восстановление базиса после интерполяции)
    float3 N_geom = normalize(input.Normal);
    float3 T_geom = normalize(input.Tangent);
    T_geom = normalize(T_geom - dot(T_geom, N_geom) * N_geom);
    float3 B_geom = cross(N_geom, T_geom);
    
    float3x3 TBN = float3x3(T_geom, B_geom, N_geom);
    float3 N = normalize(mul(TBN, normalTS));

    // --- 3. БАЗОВЫЕ ВЕКТОРЫ ---
    float3 V      = normalize(CamPos - input.WorldPos);
    float  NdotV  = saturate(dot(N, V));
    float3 sunDir = normalize(SunDirection.xyz);
    float  NdotL  = saturate(dot(N, sunDir));
    float  crest  = input.WaveDisplacement; 
    float3 sColor = SunColor.rgb * SunColor.w;

    float shoreFade = saturate(thickness * 0.3f);
    float fresnel   = lerp(0.02f, 1.0f, pow(1.0f - NdotV, 5.0f)) * shoreFade;

    float3 finalColor = float3(0, 0, 0);
    float  finalAlpha = 1.0f;

    // --- 4. ФИЗИЧЕСКОЕ ПОГЛОЩЕНИЕ (Depth Absorption & Refraction) ---
    if (EnableRefraction == 1) {
        float2 distOffset = normalTS.xy * 0.03f * saturate(thickness * 0.2f);
        float2 refUV = saturate(screenUV + distOffset);

        // Предотвращение сэмплирования объектов перед поверхностью воды
        float refRawDepth = DepthMap.SampleLevel(SamplerClamp, refUV, 0).r;
        float refDepthM   = LinearizeDepth(refRawDepth);
        if (refDepthM < waterDepthM) { 
            refUV = screenUV; 
        }

        float3 bottomColor = RefractionMap.SampleLevel(SamplerClamp, refUV, 0).rgb;

        // Закон Бера-Ламберта: экспоненциальное поглощение света
        float3 extinction = float3(1.0f, 0.9f, 0.3f);
        float3 absorption = exp(-extinction * thickness * max(DepthAbsorptionScale, 0.5f));
        float3 underwaterColor = bottomColor * absorption;
        
        float visibility = exp(-thickness * DepthAbsorptionScale * 1.0f);
        finalColor = lerp(DeepColor.rgb, underwaterColor, visibility);
        finalColor += ShallowColor.rgb * crest * 0.03f; 
        
        finalAlpha = 1.0f; // Поверхность непрозрачна, так как дно уже отрендерено внутри finalColor
    } 
    else {
        // Fallback: Альфа-блендинг без преломлений
        float3 waterVolume = lerp(ShallowColor.rgb, DeepColor.rgb, saturate(thickness * 0.2f));
        finalColor = waterVolume + (ShallowColor.rgb * crest * 0.03f);
        finalAlpha = saturate(thickness * 0.15f + fresnel);
    }

    // --- 5. ПРОЦЕДУРНАЯ ПЕНА (Foam Generation) ---
    float whitecap = pow(saturate(crest * 1.5f - 0.75f), 8.0f); 
    float shoreFoam = smoothstep(3.0f, 0.1f, thickness);
    
    float waveTide = smoothstep(0.2f, 0.8f, crest);
    shoreFoam *= lerp(0.15f, 1.0f, waveTide); 
    
    float totalFoamMask = saturate(whitecap + shoreFoam);

    // Смещение текстурных координат пены вдоль направления волны
    float2 foamUV = input.UV * 3.0f + (N_geom.xz * 0.5f) + (normalTS.xy * 0.2f);
    foamUV += float2(Time * 0.015f, Time * 0.01f); 
    
    float foamTex = FoamMap.Sample(SamplerWrap, foamUV).r;
    foamTex = smoothstep(0.4f, 0.8f, foamTex);
    
    float3 brightAmbient = float3(0.5f, 0.5f, 0.6f); 
    float3 directSunLine = sColor * 1.5f; 
    float3 litFoamColor = FoamColor.rgb * (brightAmbient + directSunLine * NdotL);
    litFoamColor *= 3.0f; // Переход в HDR-диапазон
    
    finalColor = lerp(finalColor, litFoamColor, totalFoamMask * foamTex);
    
    if (EnableRefraction == 0) {
        finalAlpha = saturate(finalAlpha + (totalFoamMask * foamTex * 2.0f));
    }

    // --- 6. БЛИКИ И ПОДПОВЕРХНОСТНОЕ РАССЕИВАНИЕ (Specular & SSS) ---
    float3 H      = normalize(V + sunDir);
    float  NdotH  = max(dot(N, H), 0.0f);

    float specSoft = pow(NdotH, 128.0f);
    finalColor += sColor * specSoft * 0.1f * shoreFade;

    float specHard = pow(NdotH, 2048.0f);
    finalColor += sColor * specHard * 1.0f * saturate(crest * 1.5f) * shoreFade;

    // SSS (Имитация просвечивания света сквозь толщу волны)
    float3 H_sss = normalize(V + sunDir);
    float sssIntensity = max(0.0f, dot(V, -sunDir)) * max(0.0f, dot(N, -H_sss));
    finalColor += ShallowColor.rgb * sColor * pow(sssIntensity, 4.0f) * crest * 0.05f;

    // --- 7. ОТРАЖЕНИЯ (Environment Mapping) ---
    float3 R = reflect(-V, N);
    float3 envColor = EnvMap.SampleLevel(SamplerWrap, R, 2.0f).rgb;
    finalColor = lerp(finalColor, envColor, fresnel * 0.85f);

    return float4(finalColor, finalAlpha);
}