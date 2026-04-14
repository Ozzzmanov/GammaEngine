// ================================================================================
// VolumetricLight.hlsl
// Screen-Space God Rays (Сумеречные лучи) и Lens Flares (Блики линзы).
// Использует радиальный блюр маски неба с добавлением IGN (Interleaved Gradient Noise) 
// для устранения бандинга (полос) при малом количестве сэмплов.
// ================================================================================

Texture2D    tDepth : register(t0); 
SamplerState sPoint : register(s0);

cbuffer CB_GlobalWeather : register(b1) {
    float4 WindParams1;
    float4 WindParams2;
    float4 SunDirection; 
    float4 SunColor;     
    float4 SunParams;       // X=AngularSize, Y=Density, Z=NumSamples, W=Intensity
    float4 SunPositionNDC;  // XY=Screen Pos, Z=View Depth, W=EnableFlares
};

struct VS_Output {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

// --------------------------------------------------------------------------------
// VERTEX SHADER: Zero-Buffer Fullscreen Triangle
// --------------------------------------------------------------------------------
VS_Output VSMain(uint id : SV_VertexID) {
    VS_Output output;
    output.UV = float2((id << 1) & 2, id & 2);
    output.Pos = float4(output.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

// --------------------------------------------------------------------------------
// ФУНКЦИИ ХЕЛПЕРЫ
// --------------------------------------------------------------------------------

// Генерирует "призрак" (блик) на линии между солнцем и центром экрана
float Ghost(float2 uv, float2 center, float2 sunPos, float offset, float size, float aspect) {
    float2 dirToCenter = center - sunPos;
    float2 ghostPos = sunPos + dirToCenter * offset; 
    // Учитываем соотношение сторон (Aspect Ratio), чтобы блики были круглыми, а не овальными
    float dist = length((uv - ghostPos) * float2(aspect, 1.0f));
    return pow(max(1.0f - dist / size, 0.0f), 4.0f); // Falloff от центра блика к краям
}

// Interleaved Gradient Noise (IGN) от Jorge Jimenez (Activision)
// Генерирует стабильный высокочастотный пространственный шум без использования текстур.
float IGN(float2 pixelPos) {
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(pixelPos, magic.xy)));
}

// --------------------------------------------------------------------------------
// PIXEL SHADER
// --------------------------------------------------------------------------------
float4 PSMain(VS_Output input) : SV_Target {
    int numSamples = (int)SunParams.z;
    bool enableRays = numSamples > 0;
    bool enableFlares = SunPositionNDC.w > 0.5f;

    // --- РАННИЙ ВЫХОД (EARLY EXIT) ---
    // Если эффекты выключены или солнце находится физически за спиной камеры (Z < 0)
    if ((!enableRays && !enableFlares) || SunPositionNDC.z <= 0.0f) discard;

    float2 uv = input.UV;
    float2 sunPos = SunPositionNDC.xy;
    float aspect = 1.777f; // 16:9 (в идеале нужно передавать реальный Aspect Ratio из C++)

    // --- 1. ЗАТУХАНИЕ ЗА КРАЕМ ЭКРАНА (Off-screen Fade) ---
    // Плавно гасим эффекты, когда солнце покидает видимую зону монитора.
    // Это скрывает артефакты Screen-Space алгоритмов на границах кадра.
    float edgeDistX = max(0.0f, abs(sunPos.x - 0.5f) - 0.5f);
    float edgeDistY = max(0.0f, abs(sunPos.y - 0.5f) - 0.5f);
    float offscreenDist = length(float2(edgeDistX, edgeDistY));
    
    // Как только солнце удалится на 0.25 от края экрана - лучи исчезнут в 0
    float screenEdgeFade = saturate(1.0f - offscreenDist * 4.0f); 

    // --- 2. БЛИКИ КАМЕРЫ (LENS FLARES) ---
    float3 finalFlares = float3(0, 0, 0);
    if (enableFlares && screenEdgeFade > 0.0f) {
        
        // Проверяем, не перекрыто ли солнце геометрией (горой, зданием)
        float sunCenterDepth = 0.0f;
        if (sunPos.x >= 0.0f && sunPos.x <= 1.0f && sunPos.y >= 0.0f && sunPos.y <= 1.0f) {
            sunCenterDepth = tDepth.SampleLevel(sPoint, sunPos, 0).r;
        }
        float isSunVisible = (sunCenterDepth >= 0.999f) ? 1.0f : 0.0f; // 1.0 = Небо

        // Если солнце видно - рисуем блики на линзе
        if (isSunVisible > 0.0f) {
            float2 screenCenter = float2(0.5f, 0.5f);
            float3 lensFlare = float3(0, 0, 0);
            
            // Три цветных "призрака" разного размера и на разном удалении от центра
            lensFlare += float3(1.0f, 0.4f, 0.2f) * Ghost(uv, screenCenter, sunPos, 1.2f, 0.08f, aspect); 
            lensFlare += float3(0.2f, 1.0f, 0.4f) * Ghost(uv, screenCenter, sunPos, 1.5f, 0.05f, aspect); 
            lensFlare += float3(0.1f, 0.3f, 1.0f) * Ghost(uv, screenCenter, sunPos, 0.8f, 0.12f, aspect); 
            
            // Основной засвет (Halo) вокруг самого солнца
            lensFlare += SunColor.rgb * Ghost(uv, sunPos, sunPos, 0.0f, 0.6f, aspect) * 0.5f;
            
            finalFlares = lensFlare * 0.8f * screenEdgeFade;
        }
    }

    // --- 3. СУМЕРЕЧНЫЕ ЛУЧИ (GOD RAYS) ---
    // Raymarching маски неба к позиции солнца в экранном пространстве.
    float3 finalGodRays = float3(0, 0, 0);
    if (enableRays && screenEdgeFade > 0.0f) {
        float2 deltaUV = (uv - sunPos); // Вектор от солнца к текущему пикселю
        
        // Радиальная маска: лучи рисуются только в круге вокруг солнца.
        float distToSun = length(deltaUV * float2(aspect, 1.0f));
        float radialMask = saturate(1.0f - distToSun * 1.5f); 
        
        if (radialMask > 0.0f) {
            const float density = SunParams.y;  // Расстояние между сэмплами
            const float intensity = SunParams.w; // Яркость
            const float decay = 0.95f;           // Коэффициент затухания света в пыли (экспоненциальный спад)
            const float weight = 0.5f;           // Базовый вес луча

            // Защита от переполнения шага (предотвращает появление явных полос по краям лучей)
            if (length(deltaUV) > 0.5f) deltaUV = (deltaUV / length(deltaUV)) * 0.5f;

            deltaUV *= density / float(numSamples);
            
            // Сдвигаем стартовую позицию луча на основе шума (Dithering)
            float jitter = IGN(input.Pos.xy);
            float2 currentUV = uv - deltaUV * jitter;

            float illuminationDecay = 1.0f;
            float godRaysMask = 0.0f;

            // Реймарчинг к солнцу
            [loop]
            for(int i = 0; i < numSamples; i++) {
                currentUV -= deltaUV; // Шагаем
                
                // Ранний выход, если луч покинул границы экрана
                if (currentUV.x < 0.0f || currentUV.x > 1.0f || currentUV.y < 0.0f || currentUV.y > 1.0f) break;
                
                // Проверяем, небо ли здесь
                float depthSample = tDepth.SampleLevel(sPoint, currentUV, 0).r;
                float isSky = step(0.999f, depthSample);
                
                // Накапливаем маску
                godRaysMask += isSky * illuminationDecay;
                illuminationDecay *= decay;
            }
            
            godRaysMask *= (weight / float(numSamples));
            godRaysMask = saturate(godRaysMask); 

            // Применяем Радиальную маску и Маску краев экрана
            finalGodRays = SunColor.rgb * godRaysMask * intensity * radialMask * screenEdgeFade;
        }
    }

    return float4(finalGodRays + finalFlares, 1.0f);
}