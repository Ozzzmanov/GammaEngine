Texture2D<float> InputDepth : register(t0);
RWTexture2D<float> OutputDepth : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint2 outCoord = id.xy;
    uint dstW, dstH;
    OutputDepth.GetDimensions(dstW, dstH);
    
    if (outCoord.x >= dstW || outCoord.y >= dstH) return;

    uint srcW, srcH;
    InputDepth.GetDimensions(srcW, srcH);
    
    uint2 base = outCoord * 2;
    uint2 p0 = min(base + uint2(0, 0), uint2(srcW - 1, srcH - 1));
    uint2 p1 = min(base + uint2(1, 0), uint2(srcW - 1, srcH - 1));
    uint2 p2 = min(base + uint2(0, 1), uint2(srcW - 1, srcH - 1));
    uint2 p3 = min(base + uint2(1, 1), uint2(srcW - 1, srcH - 1));

    float d1 = InputDepth.Load(int3(p0.x, p0.y, 0));
    float d2 = InputDepth.Load(int3(p1.x, p1.y, 0));
    float d3 = InputDepth.Load(int3(p2.x, p2.y, 0));
    float d4 = InputDepth.Load(int3(p3.x, p3.y, 0));

    float maxD = max(max(d1, d2), max(d3, d4));

    OutputDepth[outCoord] = maxD;
}