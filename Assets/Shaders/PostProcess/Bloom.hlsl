// ================================================================================
// Bloom.hlsl
// AAA-стандарт Screen-Space Bloom. 
// Реализует 3 стадии:
// 1. Extraction: Извлечение ярких пикселей с использованием Soft Knee кривой и 
//    Karis Average (для подавления мерцающих пикселей - Fireflies).
// 2. Downsample: Понижение разрешения с использованием 4-Tap Bilinear фильтра.
// 3. Upsample: Повышение разрешения с использованием 5-Tap Tent фильтра.
// ================================================================================

Texture2D    tSource : register(t0);
SamplerState sLinear : register(s0);

cbuffer BloomCB : register(b0) {
    float4 BloomParams; // X = Threshold, Y = Mode, Z = TexelW, W = TexelH
};

struct VS_Output {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

// --------------------------------------------------------------------------------
// ВЕРШИННЫЙ ШЕЙДЕР (Zero-Buffer Fullscreen)
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

// Фильтрация битых пикселей (NaN, Inf, или значений больше предела Float16)
float3 SafeHDR(float3 c) {
    bool valid = all(c >= 0.0f) && all(c < 65000.0f);
    return valid ? c : float3(0.0f, 0.0f, 0.0f);
}

// Karis Average: Локальный Tonemapping перед фильтрацией для защиты от "Fireflies".
// Усреднение субпикселей сверхвысокой яркости в линейном пространстве дает жесткие артефакты.
// Мы сжимаем их яркость, усредняем, а затем разжимаем обратно.
float3 KarisAverage(float3 col) {
    float luma = dot(col, float3(0.299f, 0.587f, 0.114f));
    return col / (1.0f + luma); 
}

// --------------------------------------------------------------------------------
// ПИКСЕЛЬНЫЙ ШЕЙДЕР (Мульти-режимный)
// --------------------------------------------------------------------------------
float4 PSMain(VS_Output input) : SV_Target {
    float2 uv = input.UV;
    float mode = BloomParams.y;
    float2 texel = BloomParams.zw; // Размер текселя текущего Target-буфера
    
    // ====================================================================
    // EXTRACTION (Извлечение ярких участков из HDR буфера)
    // ====================================================================
    if (mode < 0.5f) {
        // Читаем 4 пикселя с применением Anti-Flicker фильтра
        float3 A = KarisAverage(SafeHDR(tSource.Sample(sLinear, uv + float2(-texel.x, -texel.y)).rgb));
        float3 B = KarisAverage(SafeHDR(tSource.Sample(sLinear, uv + float2( texel.x, -texel.y)).rgb));
        float3 C = KarisAverage(SafeHDR(tSource.Sample(sLinear, uv + float2(-texel.x,  texel.y)).rgb));
        float3 D = KarisAverage(SafeHDR(tSource.Sample(sLinear, uv + float2( texel.x,  texel.y)).rgb));
        
        float3 color = (A + B + C + D) * 0.25f;
        
        // Обратное преобразование (Inverse Karis Average)
        color = color / max(1.0f - color, 0.0001f);
        
        // --- Soft Knee Curve (Кривая мягкого порога) ---
        // Делает так, чтобы блум начинался плавно, а не резко обрубался.
        float brightness = max(color.r, max(color.g, color.b));
        float knee = BloomParams.x * 0.5f; 
        
        float soft = clamp(brightness - BloomParams.x + knee, 0.0f, 2.0f * knee);
        soft = (soft * soft) / (4.0f * knee + 0.00001f);
        float contribution = max(soft, brightness - BloomParams.x) / max(brightness, 0.00001f);
        
        return float4(color * contribution, 1.0f);
    }
    // ====================================================================
    // DOWNSAMPLE (Понижение разрешения для пирамиды Блума)
    // ====================================================================
    else if (mode < 1.5f) {
        // 4-Tap Hardware Bilinear Filter
        // Читает 4 точки. Благодаря sLinear (Bilinear Filtering) каждая точка 
        // захватывает квадрат 2x2 текселя. Итого: выборка 16 текселей за 4 инструкции.
        float3 s0 = tSource.Sample(sLinear, uv + float2(-texel.x, -texel.y)).rgb;
        float3 s1 = tSource.Sample(sLinear, uv + float2( texel.x, -texel.y)).rgb;
        float3 s2 = tSource.Sample(sLinear, uv + float2(-texel.x,  texel.y)).rgb;
        float3 s3 = tSource.Sample(sLinear, uv + float2( texel.x,  texel.y)).rgb;
        
        return float4((s0 + s1 + s2 + s3) * 0.25f, 1.0f);
    }
    // ====================================================================
    // UPSAMPLE (Повышение разрешения и слияние слоев)
    // ====================================================================
    else {
        // 5-Tap Tent Filter (Крест)
        // Вес центра = 0.5, вес 4 углов = по 0.125. Идеально сглаживает квадратные артефакты.
        float3 color = tSource.Sample(sLinear, uv).rgb * 0.5f;
        color += tSource.Sample(sLinear, uv + float2(-texel.x, -texel.y)).rgb * 0.125f;
        color += tSource.Sample(sLinear, uv + float2( texel.x, -texel.y)).rgb * 0.125f;
        color += tSource.Sample(sLinear, uv + float2(-texel.x,  texel.y)).rgb * 0.125f;
        color += tSource.Sample(sLinear, uv + float2( texel.x,  texel.y)).rgb * 0.125f;
        
        return float4(color, 1.0f);
    }
}