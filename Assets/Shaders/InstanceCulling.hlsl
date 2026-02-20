struct LodInfo {
    uint FirstBatch;
    uint PartCount;
    uint FirstVisibleOffset;
    uint MaxInstances;
};

struct EntityMetaData {
    LodInfo Lod[3];
    float3  LocalCenter;
    float   Radius;
};

struct InstanceData {
    float3 Position;
    float3 Scale;
    float3 RotXYZ;
    uint   EntityID;
    float2 Padding;
};

cbuffer CullingParams : register(b0) {
    float4x4 View;
    float4x4 Projection;
    float4x4 PrevView;
    float4x4 PrevProjection;
    float4   FrustumPlanes[6];
    float3   CameraPos;
    uint     NumInstances;
    float    LODDist1Sq;
    float    LODDist2Sq;
    float    LODDist3Sq;
    uint     EnableLODs;    
    uint     EnableFrustum; 
    uint     EnableOcclusion;
    uint     EnableSizeCulling;
    float    MinPixelSizeSq;
    float    ScreenHeight;
    float2   HZBSize;
    float    PaddingCB;
};

StructuredBuffer<InstanceData>   AllInstances   : register(t0);
StructuredBuffer<EntityMetaData> MetaData       : register(t1);
Texture2D<float>                 HZBMap         : register(t2); 

RWStructuredBuffer<uint> VisibleIndices : register(u0);
RWByteAddressBuffer IndirectArgs        : register(u1);

SamplerState PointClampSampler : register(s0);

float3 RotateVec(float3 v, float4 q) {
    float3 t = 2.0f * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint idx = id.x;
    if (idx >= NumInstances) return;

    InstanceData inst = AllInstances[idx];
    EntityMetaData meta = MetaData[inst.EntityID];

    int lodLevel = 0;
    if (EnableLODs == 1) {
        float2 diffXZ = inst.Position.xz - CameraPos.xz;
        float distSqXZ = dot(diffXZ, diffXZ); 
        if (distSqXZ > LODDist3Sq) return;
        if (distSqXZ > LODDist2Sq) lodLevel = 2;
        else if (distSqXZ > LODDist1Sq) lodLevel = 1;
    }

    LodInfo info = meta.Lod[lodLevel];
    if (info.PartCount == 0) return; 

    float wQuat = sqrt(max(0.0f, 1.0f - dot(inst.RotXYZ, inst.RotXYZ)));
    float4 q = float4(inst.RotXYZ, wQuat);
    
    float3 rotatedCenter = RotateVec(meta.LocalCenter * inst.Scale, q);
    float3 worldCenter = inst.Position + rotatedCenter;
    
    float maxScale = max(abs(inst.Scale.x), max(abs(inst.Scale.y), abs(inst.Scale.z)));
    float worldRadius = meta.Radius * maxScale;

    if (EnableFrustum == 1) {
        bool isVisible = true;
        [unroll]
        for (int i = 0; i < 6; ++i) {
            float d = dot(worldCenter, FrustumPlanes[i].xyz) + FrustumPlanes[i].w;
            if (d > worldRadius) { isVisible = false; break; }
        }
        if (!isVisible) return;
    }

    float4 viewCenter = mul(float4(worldCenter, 1.0f), View);

    if (EnableSizeCulling == 1 && MinPixelSizeSq > 0.0f && viewCenter.z > worldRadius) {
        float projectedRadius = (worldRadius * abs(Projection[1][1])) / viewCenter.z * ScreenHeight * 0.5f;
        if ((projectedRadius * projectedRadius * 4.0f) < MinPixelSizeSq) return; 
    }

    if (EnableOcclusion == 1) {
        float4 prevViewCenter = mul(float4(worldCenter, 1.0f), PrevView);
        
        if (prevViewCenter.z - worldRadius > 0.1f) {
            float4 clipCenter = mul(prevViewCenter, PrevProjection);
            float2 ndcCenter = clipCenter.xy / clipCenter.w;

            float radiusNDC_X = (worldRadius * abs(PrevProjection[0][0])) / prevViewCenter.z;
            float radiusNDC_Y = (worldRadius * abs(PrevProjection[1][1])) / prevViewCenter.z;

            float2 ndcMin = ndcCenter - float2(radiusNDC_X, radiusNDC_Y);
            float2 ndcMax = ndcCenter + float2(radiusNDC_X, radiusNDC_Y);

            bool wasOffscreen = (ndcMin.x > 1.0f || ndcMax.x < -1.0f || ndcMin.y > 1.0f || ndcMax.y < -1.0f);

            if (!wasOffscreen) {
                float2 uvCenter = float2(ndcCenter.x * 0.5f + 0.5f, 0.5f - ndcCenter.y * 0.5f);
                float2 uvRadius = float2(radiusNDC_X * 0.5f, radiusNDC_Y * 0.5f);

                float2 uvMin = saturate(uvCenter - uvRadius);
                float2 uvMax = saturate(uvCenter + uvRadius);

                float2 uvSize = uvMax - uvMin;
                uint w, h, mips;
                HZBMap.GetDimensions(0, w, h, mips);

                float mip = ceil(log2(max(uvSize.x * w, uvSize.y * h)));
                mip = clamp(mip, 0.0f, (float)mips - 1.0f);
                
                float2 mipRes = float2(w, h) / exp2(mip);
                
                uint2 texMin = clamp(uint2(floor(uvMin * mipRes)), 0, uint2(mipRes.x - 1, mipRes.y - 1));
                uint2 texMax = clamp(uint2(floor(uvMax * mipRes)), 0, uint2(mipRes.x - 1, mipRes.y - 1));

                float d1 = HZBMap.Load(int3(texMin.x, texMin.y, mip)).r;
                float d2 = HZBMap.Load(int3(texMax.x, texMin.y, mip)).r;
                float d3 = HZBMap.Load(int3(texMin.x, texMax.y, mip)).r;
                float d4 = HZBMap.Load(int3(texMax.x, texMax.y, mip)).r;

                float maxDepth = max(max(d1, d2), max(d3, d4));

                float3 dirToCamera = normalize(prevViewCenter.xyz);
                float3 viewFront = prevViewCenter.xyz - dirToCamera * worldRadius;
                float4 clipFront = mul(float4(viewFront, 1.0f), PrevProjection);
                float closestZ = clipFront.z / clipFront.w;

                if (closestZ > maxDepth + 0.001f) return; 
            }
        }
    }

    for (uint p = 0; p < info.PartCount; ++p) {
        uint batchIndex = info.FirstBatch + p;
        uint byteOffset = batchIndex * 20 + 4;
        uint localIdx = 0;
        
        IndirectArgs.InterlockedAdd(byteOffset, 1, localIdx);
        
        uint writePos = info.FirstVisibleOffset + (p * info.MaxInstances) + localIdx;
        VisibleIndices[writePos] = idx;
    }
}