// ================================================================================
// AutoExposure.hlsl (Compute Shader Histogram) Если работает - не трогай!
// ================================================================================

cbuffer AECB : register(b0) {
    float MinLogLuminance; 
    float MaxLogLuminance; 
    float MinExposure;
    float MaxExposure;
    
    float TimeDelta;
    float SpeedUp;
    float SpeedDown;
    float PixelCount;
};

Texture2D<float4> HDRTex : register(t0);
RWStructuredBuffer<uint> HistogramBuffer : register(u0);

groupshared uint sharedHist[128];

[numthreads(16, 16, 1)]
void CS_BuildHistogram(uint3 dtid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID) {
    uint localIdx = gtid.y * 16 + gtid.x;
    if (localIdx < 128) sharedHist[localIdx] = 0;
    GroupMemoryBarrierWithGroupSync();

    uint width, height;
    HDRTex.GetDimensions(width, height);
    
    if (dtid.x < width && dtid.y < height) {
        float3 color = HDRTex[dtid.xy].rgb;
        float lum = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        
        // Мы больше не игнорируем темные пиксели!
        float logLum = saturate((log2(max(lum, 0.0001f)) - MinLogLuminance) / (MaxLogLuminance - MinLogLuminance));
        uint binIndex = (uint)(logLum * 127.0f);
        InterlockedAdd(sharedHist[binIndex], 1);
    }
    GroupMemoryBarrierWithGroupSync();

    if (localIdx < 128 && sharedHist[localIdx] > 0) {
        InterlockedAdd(HistogramBuffer[localIdx], sharedHist[localIdx]);
    }
}

// ВЫЧИСЛЕНИЕ ЭКСПОЗИЦИИ

StructuredBuffer<uint> ReadHistogram : register(t0);
Texture2D<float> CurrentExposureTex  : register(t1);
RWTexture2D<float> OutputExposureTex : register(u0);

[numthreads(1, 1, 1)] 
void CS_AdaptExposure() {
    uint totalPixels = (uint)PixelCount;
    uint minPixels = (uint)(totalPixels * 0.15f); 
    uint maxPixels = (uint)(totalPixels * 0.85f); 
    
    uint currentPixelCount = 0;
    float weightedLogAverage = 0.0f;
    float weightSum = 0.0f;

    for (uint i = 0; i < 128; i++) {
        uint binCount = ReadHistogram[i];
        
        uint binStart = currentPixelCount;
        uint binEnd = currentPixelCount + binCount;
        uint overlapStart = max(binStart, minPixels);
        uint overlapEnd = min(binEnd, maxPixels);
        
        uint subPixels = (overlapEnd > overlapStart) ? (overlapEnd - overlapStart) : 0;
        
        if (subPixels > 0) {
            float binValue = ((float)i / 127.0f);
            weightedLogAverage += binValue * subPixels;
            weightSum += subPixels;
        }
        currentPixelCount += binCount;
    }

    float targetExposure = MaxExposure; 
    
    if (weightSum > 0.0f) {
        float avgLogLum = weightedLogAverage / weightSum;
        float avgLuminance = exp2(avgLogLum * (MaxLogLuminance - MinLogLuminance) + MinLogLuminance);
        
        float keyTarget = 1.03f - (2.0f / (2.0f + log10(avgLuminance + 1.0f)));
        keyTarget = clamp(keyTarget, 0.05f, 0.30f);
        
        targetExposure = clamp(keyTarget / max(avgLuminance, 0.0001f), MinExposure, MaxExposure);
    }

    float currentExposure = CurrentExposureTex[uint2(0, 0)];
    float speed = (targetExposure > currentExposure) ? SpeedDown : SpeedUp;
    float newExposure = currentExposure + (targetExposure - currentExposure) * (1.0f - exp(-TimeDelta * speed));
    
    OutputExposureTex[uint2(0, 0)] = newExposure;
}