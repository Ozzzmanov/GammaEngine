struct ShaderChunkInfo {
    float3 Extents; float Pad1;
    float3 Center;  float Pad2;
};

cbuffer CullingParams : register(b0) {
    float4x4 ViewProj;
    float4x4 PrevViewProj;
    float4   FrustumPlanes[6];
    float3   CameraPos;
    uint     NumChunks;
    float2   HZBSize;
    float    MaxDistanceSq;
    uint     EnableFrustum;
    uint     EnableOcclusion;
    float3   PaddingCB;
};

StructuredBuffer<ShaderChunkInfo> ChunkInfo   : register(t0);
Texture2D<float>                  HZBMap      : register(t1);

RWStructuredBuffer<uint> VisibleIndices       : register(u0);
RWByteAddressBuffer      IndirectArgs         : register(u1);

SamplerState PointClampSampler : register(s0);

groupshared uint sg_VisibleCount;
groupshared uint sg_BaseOffset;

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
    
    if (groupIndex == 0) {
        sg_VisibleCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint idx = id.x;
    bool isVisible = false;

    if (idx < NumChunks) {
        isVisible = true;
        ShaderChunkInfo chunk = ChunkInfo[idx];

        float3 toCamera = chunk.Center - CameraPos;
        float distSq = dot(toCamera, toCamera) - (70.0f * 70.0f);
        if (distSq > MaxDistanceSq) {
            isVisible = false;
        }

        if (isVisible && EnableFrustum == 1) {
            [unroll]
            for (int i = 0; i < 6; ++i) {
                float3 absNormal = abs(FrustumPlanes[i].xyz);
                float radius = dot(chunk.Extents, absNormal);
                float dist = dot(chunk.Center, FrustumPlanes[i].xyz) + FrustumPlanes[i].w;
                
                if (dist > radius) {
                    isVisible = false;
                    break;
                }
            }
        }

        if (isVisible && EnableOcclusion == 1) {
            float3 minExt = chunk.Center - chunk.Extents;
            float3 maxExt = chunk.Center + chunk.Extents;

            float3 corners[8] = {
                float3(minExt.x, minExt.y, minExt.z), float3(minExt.x, minExt.y, maxExt.z),
                float3(minExt.x, maxExt.y, minExt.z), float3(minExt.x, maxExt.y, maxExt.z),
                float3(maxExt.x, minExt.y, minExt.z), float3(maxExt.x, minExt.y, maxExt.z),
                float3(maxExt.x, maxExt.y, minExt.z), float3(maxExt.x, maxExt.y, maxExt.z)
            };

            float minX = 10000.0f, minY = 10000.0f, maxX = -10000.0f, maxY = -10000.0f;
            float closestZ = 1.0f;
            bool behindCamera = false;

            [unroll]
            for(int i = 0; i < 8; ++i) {
                float4 clipPos = mul(float4(corners[i], 1.0f), PrevViewProj);
                if (clipPos.w <= 0.1f) {
                    behindCamera = true;
                    break; 
                }
                clipPos.xyz /= clipPos.w; 
                minX = min(minX, clipPos.x); minY = min(minY, clipPos.y);
                maxX = max(maxX, clipPos.x); maxY = max(maxY, clipPos.y);
                closestZ = min(closestZ, clipPos.z);
            }

            if (!behindCamera) {
                bool wasOffscreen = (maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f);
                if (!wasOffscreen) {
                    float2 uvMin = saturate(float2(minX * 0.5f + 0.5f, 0.5f - maxY * 0.5f));
                    float2 uvMax = saturate(float2(maxX * 0.5f + 0.5f, 0.5f - minY * 0.5f));

                    float2 sizePixels = (uvMax - uvMin) * HZBSize;
                    float mip = max(0.0f, floor(log2(max(sizePixels.x, sizePixels.y))));
                    float2 mipResolution = max(float2(1.0f, 1.0f), floor(HZBSize / exp2(mip)));

                    uint2 texMin = uint2(floor(uvMin * mipResolution));
                    uint2 texMax = uint2(floor(uvMax * mipResolution));

                    if (texMax.x - texMin.x > 1 || texMax.y - texMin.y > 1) {
                        mip += 1.0f;
                        mipResolution = max(float2(1.0f, 1.0f), floor(HZBSize / exp2(mip)));
                        texMin = uint2(floor(uvMin * mipResolution));
                        texMax = uint2(floor(uvMax * mipResolution));
                    }

                    texMin = clamp(texMin, uint2(0, 0), uint2(mipResolution.x - 1, mipResolution.y - 1));
                    texMax = clamp(texMax, uint2(0, 0), uint2(mipResolution.x - 1, mipResolution.y - 1));

                    float d1 = HZBMap.Load(int3(texMin.x, texMin.y, mip)).r;
                    float d2 = HZBMap.Load(int3(texMax.x, texMin.y, mip)).r;
                    float d3 = HZBMap.Load(int3(texMin.x, texMax.y, mip)).r;
                    float d4 = HZBMap.Load(int3(texMax.x, texMax.y, mip)).r;

                    float maxDepth = max(max(d1, d2), max(d3, d4));
                    if (closestZ > maxDepth + 0.00005f) isVisible = false;
                }
            }
        }
    }

    uint localOffset = 0;
    
    if (isVisible) {
        InterlockedAdd(sg_VisibleCount, 1, localOffset);
    }
    
    GroupMemoryBarrierWithGroupSync();

    if (groupIndex == 0 && sg_VisibleCount > 0) {
        IndirectArgs.InterlockedAdd(4, sg_VisibleCount, sg_BaseOffset);
    }

    GroupMemoryBarrierWithGroupSync();

    if (isVisible) {
        VisibleIndices[sg_BaseOffset + localOffset] = idx;
    }
}