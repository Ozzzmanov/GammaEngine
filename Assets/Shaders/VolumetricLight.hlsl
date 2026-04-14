// ================================================================================
// VolumetricLight.hlsl
// Screen-Space Raymarching для объемного освещения (God Rays).
// Выполняется в половинном разрешении (Half-Res). Использует фазовую функцию 
// Хени-Гринштейна (Henyey-Greenstein) для физически корректного рассеивания света
// и Interleaved Gradient Noise (IGN) для маскировки полос (Banding).
// ================================================================================

cbuffer LightParams : register(b0) {
    matrix InvViewProj;
    matrix LightViewProj[4];
    matrix ViewMatrix;   
    float4 CascadeSplits;
    float3 CameraPos;
    float  CascadeCount;
};

cbuffer CB_GlobalWeather : register(b1) {
    float4 WindParams1;
    float4 WindParams2;
    float4 SunDirection;
    float4 SunColor;
    float4 SunParams;       // X=Density, Y=GAnisotropy, Z=Steps, W=RayLength
    float4 SunPositionNDC; 
};

// --------------------------------------------------------------------------------
// РЕСУРСЫ
// --------------------------------------------------------------------------------
Texture2D<float>        DepthTex  : register(t0); // Depth Buffer
Texture2DArray<float>   ShadowMap : register(t1); // Каскадные тени (CSM)

SamplerState            SamplerPoint  : register(s0);
SamplerComparisonState  ShadowSampler : register(s1); // Аппаратный PCF сэмплер

static const float PI = 3.14159265359f;

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

// --------------------------------------------------------------------------------
// ВЕРШИННЫЙ ШЕЙДЕР (Zero-Buffer Fullscreen)
// --------------------------------------------------------------------------------
VS_OUTPUT VSMain(uint id : SV_VertexID) {
    VS_OUTPUT output;
    output.UV = float2((id << 1) & 2, id & 2);
    output.Pos = float4(output.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

// --------------------------------------------------------------------------------
// ФУНКЦИИ ХЕЛПЕРЫ
// --------------------------------------------------------------------------------

// Interleaved Gradient Noise (IGN). 
// Дает идеальный пространственный шум для сдвига лучей (Dithering).
float GetDither(float2 pos) {
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(pos, magic.xy)));
}

// Фазовая функция Хени-Гринштейна (Henyey-Greenstein).
// Описывает, как свет рассеивается в частицах тумана (анизотропия).
// g > 0: Свет рассеивается вперед (Mie scattering - характерно для пыли/влаги).
// g = 0: Свет рассеивается равномерно (Rayleigh).
float ComputePhaseHG(float dotVL, float g) {
    g = clamp(g, -0.999f, 0.999f);
    float a = 1.0f - g * g;
    float b = 1.0f + g * g - 2.0f * g * dotVL;
    return a / max(4.0f * PI * pow(abs(b), 1.5f), 0.00001f); 
}

// Выборка из конкретного каскада карты теней
float SampleCascadeVisibility(float3 worldPos, int cascade) {
    float4 lightSpacePos = mul(float4(worldPos, 1.0f), LightViewProj[cascade]);
    
    // Перевод в UV координаты текстуры тени
    float2 shadowUV;
    shadowUV.x =  lightSpacePos.x * 0.5f + 0.5f;
    shadowUV.y = -lightSpacePos.y * 0.5f + 0.5f;
    float currentDepth = lightSpacePos.z;

    // Отсечение за пределами текущего каскада
    if(shadowUV.x < 0.0f || shadowUV.x > 1.0f || shadowUV.y < 0.0f || shadowUV.y > 1.0f || currentDepth > 1.0f)
        return 0.0f; // Вне тени (света нет) — для объемных лучей за картой лучше возвращать 0 или 1 в зависимости от логики сцены. 
                     // Вернем 1.0f (освещено), чтобы не было черных квадратов на границах каскада.
                     // ИСПРАВЛЕНО: Возвращаем 1.0f для плавного перехода.
        
    // Микро-смещение для борьбы с теневой "сыпью" (Acne)
    float depthBias = 0.00005f * (cascade + 1);
    
    // Используем SampleCmpLevelZero для аппаратного PCF-фильтрования (Smooth Shadows)
    return ShadowMap.SampleCmpLevelZero(ShadowSampler, float3(shadowUV, cascade), currentDepth - depthBias);
}

// Выбор нужного каскада на основе глубины от камеры (ViewZ)
float SampleCascadedShadowRay(float3 worldPos, float viewZ) {
    int maxCascades = (int)CascadeCount;
    
    // Если вышли за пределы всех теней — считаем, что там всегда свет
    if (viewZ > CascadeSplits[maxCascades - 1]) return 1.0f;

    [branch]
    if (maxCascades > 1 && viewZ > CascadeSplits[0]) {
        if (maxCascades > 2 && viewZ > CascadeSplits[1]) return SampleCascadeVisibility(worldPos, 2);
        return SampleCascadeVisibility(worldPos, 1);
    }
    return SampleCascadeVisibility(worldPos, 0);
}

// --------------------------------------------------------------------------------
// ПИКСЕЛЬНЫЙ ШЕЙДЕР (RAYMARCHING LOOP)
// --------------------------------------------------------------------------------
float4 PSMain(VS_OUTPUT input) : SV_Target {
    // 1. Восстанавливаем мировую позицию пикселя из Depth Buffer
    float depth = DepthTex.SampleLevel(SamplerPoint, input.UV, 0);
    
    float x_ndc = input.UV.x * 2.0f - 1.0f;
    float y_ndc = (1.0f - input.UV.y) * 2.0f - 1.0f;
    float4 ndcPos = float4(x_ndc, y_ndc, depth, 1.0f);
    float4 worldPos4 = mul(ndcPos, InvViewProj);
    float3 worldPos = worldPos4.xyz / worldPos4.w;

    float3 volumetricLight = float3(0, 0, 0);
    int numSteps = (int)SunParams.z;
    
    if (numSteps > 0) {
        float density = SunParams.x;
        float g = SunParams.y;
        float maxRayLen = SunParams.w;
        float3 lightColor = SunColor.rgb * SunColor.w;
        float3 lightDir = normalize(SunDirection.xyz); 

        // Вектор от камеры до пикселя
        float3 rayDir = worldPos - CameraPos;
        float rayLen = length(rayDir);
        rayDir = normalize(rayDir); 
        
        // Ограничиваем длину луча (чтобы не маршить бесконечность в небо)
        float finalRayLen = min(rayLen, maxRayLen);
        float3 finalWorldPos = CameraPos + rayDir * finalRayLen;

        float3 currentRayVec = finalWorldPos - CameraPos;
        float3 stepSize = currentRayVec / float(numSteps);

        // Сдвиг стартовой позиции луча (Jittering) через IGN
        float jitter = GetDither(input.Pos.xy);
        float3 startPos = CameraPos + stepSize * jitter;

        // --- СУПЕР-ОПТИМИЗАЦИЯ: Линейный ViewZ ---
        // Избегаем тяжелого умножения матриц mul(World, View) внутри цикла!
        float startViewZ = mul(float4(startPos, 1.0f), ViewMatrix).z;
        float endViewZ = mul(float4(startPos + stepSize * float(numSteps), 1.0f), ViewMatrix).z;
        float stepViewZ = (endViewZ - startViewZ) / float(numSteps);
        float currentViewZ = startViewZ;

        float scatteringAccum = 0.0f;
        float transmittance = 1.0f;

        // Вычисляем фазовую функцию один раз на весь луч (т.к. солнце - Directional Light)
        float HG_phase = ComputePhaseHG(dot(rayDir, lightDir), g);
        
        float stepLen = length(stepSize);
        float stepTransmittance = exp(-density * stepLen); 
        float scatteringStep = density * stepLen;

        // --- ГЛАВНЫЙ ЦИКЛ (RAYMARCHING) ---
        [loop]
        for(int i = 0; i < numSteps; i++) {
            // РАННИЙ ВЫХОД: Если свет уже поглощен туманом, прерываем цикл (спасает FPS в густом тумане)
            if (transmittance < 0.01f) break; 

            float3 currentSamplePos = startPos + stepSize * float(i);
            
            // Проверяем, находится ли текущая точка луча в тени
            float shadow = SampleCascadedShadowRay(currentSamplePos, currentViewZ);
            
            currentViewZ += stepViewZ; // Дешево шагаем вглубь View пространства
            
            [branch]
            if (shadow > 0.05f) {
                // Если точка освещена, она рассеивает свет в камеру с учетом пропускания (transmittance)
                scatteringAccum += transmittance * shadow;
            }
            
            // Свет поглощается с каждым шагом
            transmittance *= stepTransmittance; 
        }
        
        float rayIntensity = 0.4f; 
        volumetricLight = lightColor * scatteringAccum * scatteringStep * HG_phase * rayIntensity; 
    }

    return float4(volumetricLight, 1.0f);
}