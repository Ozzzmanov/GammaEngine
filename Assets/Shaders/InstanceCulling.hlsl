// ================================================================================
// InstanceCulling.hlsl 
// Выполняет Frustum Culling (отсечение по пирамиде видимости), 
// Occlusion Culling (отсечение перекрытых объектов через Hierarchical Z-Buffer),
// LOD Selection (выбор уровня детализации) и упаковку данных для Indirect Draw.
// Оптимизировано с использованием groupshared памяти для снижения нагрузки на VRAM.
// ================================================================================

struct LodInfo {
    uint FirstBatch;
    uint PartCount;
    uint FirstVisibleOffset;
    uint MaxInstances;
};

struct EntityMetaData {
    LodInfo Lod[3];         // Данные для 3 уровней детализации (LOD0, LOD1, LOD2)
    float3  LocalCenter;    // Локальный центр Bounding Sphere
    float   Radius;         // Радиус Bounding Sphere
};

struct InstanceData {
    float3 Position;
    float3 Scale;
    uint   RotPacked;       // Кватернион (10-10-10-2)
    uint   EntityID;        // Ссылка на EntityMetaData
};

// --------------------------------------------------------------------------------
// ФУНКЦИИ-ХЕЛПЕРЫ
// --------------------------------------------------------------------------------
float3 UnpackRotation(uint packed) {
    float x = (float)(packed & 0x3FF) / 1023.0f;
    float y = (float)((packed >> 10) & 0x3FF) / 1023.0f;
    float z = (float)((packed >> 20) & 0x3FF) / 1023.0f;
    return float3(x, y, z) * 2.0f - 1.0f;
}

float3 RotateVec(float3 v, float4 q) {
    float3 t = 2.0f * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// --------------------------------------------------------------------------------
// КОНСТАНТЫ И РЕСУРСЫ
// --------------------------------------------------------------------------------
cbuffer CullingParams : register(b0) {
    float4x4 View;
    float4x4 Projection;
    float4x4 PrevView;              // Для HZB Occlusion (используем матрицу прошлого кадра)
    float4x4 PrevProjection;
    float4   FrustumPlanes[6];
    float3   CameraPos;
    uint     NumInstances;
    float    LODDist1Sq;            // Квадрат дистанции переключения на LOD1
    float    LODDist2Sq;            // Квадрат дистанции переключения на LOD2
    float    LODDist3Sq;            // Максимальная дистанция отрисовки
    uint     EnableLODs;    
    uint     EnableFrustum; 
    uint     EnableOcclusion;
    uint     EnableSizeCulling;
    float    MinPixelSizeSq;        // Минимальный размер объекта в пикселях (отсекаем пылинки)
    float    ScreenHeight;
    float2   HZBSize;
    float    MinDistanceSq;
    float    MaxDistanceSq;
    uint     IsShadowPass;          // 1 = Рендерим в карту теней, 0 = Главный экран
    uint     EnableShadowSizeCulling;
    float    ShadowSizeCullingDistSq;
    float    MinShadowSize;
    uint     EnableShadowFrustumCulling;
    float2   PaddingCB;         
    float4   ShadowPlanes[18];      // Плоскости фрустума для 3 каскадов теней (3 * 6 = 18)
};

StructuredBuffer<InstanceData>   AllInstances   : register(t0);
StructuredBuffer<EntityMetaData> MetaData       : register(t1);
Texture2D<float>                 HZBMap         : register(t2); // Пирамида глубины прошлого кадра

// Инфа о батчах: содержит SliceIndex (нижние 10 бит) и IsAlpha (11-й бит)
struct BatchGpuInfo {
    uint PackedData; 
};
StructuredBuffer<BatchGpuInfo> Batches : register(t3); 

RWStructuredBuffer<uint> VisibleIndices0 : register(u0); // Главный буфер (или Тень Каскад 0)
RWStructuredBuffer<uint> VisibleIndices1 : register(u1); // Тень Каскад 1
RWStructuredBuffer<uint> VisibleIndices2 : register(u2); // Тень Каскад 2

RWByteAddressBuffer IndirectArgs0 : register(u3);
RWByteAddressBuffer IndirectArgs1 : register(u4);
RWByteAddressBuffer IndirectArgs2 : register(u5);

SamplerState PointClampSampler : register(s0); // HZB требует POINT сэмплинга (без интерполяции)

// --------------------------------------------------------------------------------
// GROUPSHARED ПАМЯТЬ (Для агрегации атомарных операций)
// --------------------------------------------------------------------------------
groupshared uint sg_UniqueBatches[128];
groupshared uint sg_BatchCounts[128];
groupshared uint sg_GlobalOffsets[128];

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
    
    // Инициализация локальной памяти группы
    if (IsShadowPass == 0) {
        if (groupIndex == 0) {
            for (int i = 0; i < 128; ++i) {
                sg_BatchCounts[i] = 0;
                sg_UniqueBatches[i] = 0xFFFFFFFF; 
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    uint idx = id.x;
    bool isVisibleMain = false;
    bool inCascade[3] = { false, false, false };
    
    uint myPartCount = 0;
    uint myFirstBatch = 0;
    uint myFirstVisibleOffset = 0;
    uint myMaxInstances = 0;

    if (idx < NumInstances) {
        InstanceData inst = AllInstances[idx];
        EntityMetaData meta = MetaData[inst.EntityID];

        int lodLevel = 0;
        float3 diff3D = inst.Position - CameraPos;
        float distSq3D = dot(diff3D, diff3D);

        // --- ДИСТАНЦИОННЫЙ КУЛЛИНГ ---
        if (distSq3D <= MaxDistanceSq) {
            bool validLOD = true;

            // --- ВЫБОР LOD ---
            if (EnableLODs == 1) {
                float2 diffXZ = inst.Position.xz - CameraPos.xz;
                float distSqXZ = dot(diffXZ, diffXZ); 
                
                if (distSqXZ > LODDist3Sq) validLOD = false; 
                else if (distSqXZ > LODDist2Sq) lodLevel = 2;
                else if (distSqXZ > LODDist1Sq) lodLevel = 1;
            }

            if (validLOD) {
                LodInfo info = meta.Lod[lodLevel];
                if (info.PartCount > 0) {
                    
                    // Вычисление мировых координат Bounding Sphere
                    float3 rotXYZ = UnpackRotation(inst.RotPacked);
                    float wQuat = sqrt(max(0.0f, 1.0f - dot(rotXYZ, rotXYZ)));
                    float4 q = float4(rotXYZ, wQuat);
                    
                    float3 rotatedCenter = RotateVec(meta.LocalCenter * inst.Scale, q);
                    float3 worldCenter = inst.Position + rotatedCenter;
                    float maxScale = max(abs(inst.Scale.x), max(abs(inst.Scale.y), abs(inst.Scale.z)));
                    float worldRadius = meta.Radius * maxScale;

                    myPartCount = info.PartCount;
                    myFirstBatch = info.FirstBatch;
                    myFirstVisibleOffset = info.FirstVisibleOffset;
                    myMaxInstances = info.MaxInstances;

                    // =================================================================
                    // ПРОХОД ТЕНЕЙ (Shadow Pass)
                    // =================================================================
                    if (IsShadowPass == 1) {
                        bool skipSmall = false;
                        // Мелкие объекты (листья, камни) не отбрасывают тени вдали
                        if (EnableShadowSizeCulling == 1 && distSq3D > ShadowSizeCullingDistSq && worldRadius < MinShadowSize) {
                            skipSmall = true;
                        }

                        if (!skipSmall) {
                            for (int c = 0; c < 3; ++c) {
                                bool visibleInThisCascade = true;
                                if (EnableShadowFrustumCulling == 1) {
                                    for (int p = 0; p < 4; ++p) { 
                                        float4 plane = ShadowPlanes[c * 6 + p];
                                        float d = dot(worldCenter, plane.xyz) + plane.w;
                                        if (d > worldRadius) { 
                                            visibleInThisCascade = false; 
                                            break; 
                                        }
                                    }
                                }
                                inCascade[c] = visibleInThisCascade;
                            }
                        }
                    } 
                    // =================================================================
                    // ГЛАВНЫЙ ПРОХОД (Main Pass)
                    // =================================================================
                    else {
                        isVisibleMain = true;

                        // 1. Frustum Culling
                        if (EnableFrustum == 1) {
                            [unroll]
                            for (int i = 0; i < 6; ++i) {
                                float d = dot(worldCenter, FrustumPlanes[i].xyz) + FrustumPlanes[i].w;
                                if (d > worldRadius) { isVisibleMain = false; break; }
                            }
                        }

                        // 2. Pixel Size Culling (Отсекаем объекты меньше N пикселей на экране)
                        if (isVisibleMain && EnableSizeCulling == 1) {
                            float4 viewCenter = mul(float4(worldCenter, 1.0f), View);
                            if (MinPixelSizeSq > 0.0f && viewCenter.z > worldRadius) {
                                float projectedRadius = (worldRadius * abs(Projection[1][1])) / viewCenter.z * ScreenHeight * 0.5f;
                                if ((projectedRadius * projectedRadius * 4.0f) < MinPixelSizeSq) isVisibleMain = false; 
                            }
                        }

                        // 3. Occlusion Culling (HZB - Hierarchical Z-Buffer)
                        if (isVisibleMain && EnableOcclusion == 1) {
                            float4 prevViewCenter = mul(float4(worldCenter, 1.0f), PrevView);
                            
                            // Проверяем только объекты перед камерой
                            if (prevViewCenter.z - worldRadius > 0.1f) {
                                // Проецируем сферу на экран
                                float4 clipCenter = mul(prevViewCenter, PrevProjection);
                                float2 ndcCenter = clipCenter.xy / clipCenter.w;
                                float radiusNDC_X = (worldRadius * abs(PrevProjection[0][0])) / prevViewCenter.z;
                                float radiusNDC_Y = (worldRadius * abs(PrevProjection[1][1])) / prevViewCenter.z;

                                float2 ndcMin = ndcCenter - float2(radiusNDC_X, radiusNDC_Y);
                                float2 ndcMax = ndcCenter + float2(radiusNDC_X, radiusNDC_Y);
                                bool wasOffscreen = (ndcMin.x > 1.0f || ndcMax.x < -1.0f || ndcMin.y > 1.0f || ndcMax.y < -1.0f);

                                if (!wasOffscreen) {
                                    // Перевод в UV пространство HZB (0..1)
                                    float2 uvCenter = float2(ndcCenter.x * 0.5f + 0.5f, 0.5f - ndcCenter.y * 0.5f);
                                    float2 uvRadius = float2(radiusNDC_X * 0.5f, radiusNDC_Y * 0.5f);
                                    float2 uvMin = saturate(uvCenter - uvRadius);
                                    float2 uvMax = saturate(uvCenter + uvRadius);
                                    float2 uvSize = uvMax - uvMin;
                                    
                                    // Вычисляем нужный Mip-уровень HZB (Тексель должен быть чуть больше bounding box'а)
                                    uint w, h, mips;
                                    HZBMap.GetDimensions(0, w, h, mips);
                                    float mip = ceil(log2(max(uvSize.x * w, uvSize.y * h)));
                                    mip = clamp(mip, 0.0f, (float)mips - 1.0f);
                                    float2 mipRes = float2(w, h) / exp2(mip);
                                    
                                    uint2 texMin = clamp(uint2(floor(uvMin * mipRes)), 0, uint2(mipRes.x - 1, mipRes.y - 1));
                                    uint2 texMax = clamp(uint2(floor(uvMax * mipRes)), 0, uint2(mipRes.x - 1, mipRes.y - 1));

                                    // Читаем 4 пикселя из HZB, покрывающих объект
                                    float d1 = HZBMap.Load(int3(texMin.x, texMin.y, mip)).r;
                                    float d2 = HZBMap.Load(int3(texMax.x, texMin.y, mip)).r;
                                    float d3 = HZBMap.Load(int3(texMin.x, texMax.y, mip)).r;
                                    float d4 = HZBMap.Load(int3(texMax.x, texMax.y, mip)).r;

                                    float maxDepth = max(max(d1, d2), max(d3, d4));
                                    
                                    // Вычисляем Z-координату передней точки сферы объекта
                                    float3 dirToCamera = normalize(prevViewCenter.xyz);
                                    float3 viewFront = prevViewCenter.xyz - dirToCamera * worldRadius;
                                    float4 clipFront = mul(float4(viewFront, 1.0f), PrevProjection);
                                    float closestZ = clipFront.z / clipFront.w;

                                    // Если объект дальше, чем глубина в HZB -> он перекрыт горой/зданием!
                                    if (closestZ > maxDepth + 0.001f) isVisibleMain = false; 
                                }
                            }
                        }
                    }
                }
            }
        }
    } 

    // =========================================================================
    // ЗАПИСЬ РЕЗУЛЬТАТОВ (Вывод в Indirect Arguments и Visible Indices)
    // =========================================================================

    // Для теней используем прямой InterlockedAdd, так как каскадов 3 и они 
    // рендерятся реже, оверхед приемлем.
    if (IsShadowPass == 1) {
        for (uint c = 0; c < 3; ++c) {
            if (inCascade[c]) {
                for (uint p = 0; p < myPartCount; ++p) {
                    uint batchIndex = myFirstBatch + p;
                    uint globalOffset;
                    uint writePos;

                    // УПАКОВКА: Индекс Инстанса (21 бит) + Slice/Alpha (11 бит)
                    uint batchPacked = Batches[batchIndex].PackedData;
                    uint finalPacked = (idx & 0x1FFFFF) | (batchPacked << 21);

                    if (c == 0) {
                        IndirectArgs0.InterlockedAdd(batchIndex * 20 + 4, 1, globalOffset);
                        writePos = myFirstVisibleOffset + (p * myMaxInstances) + globalOffset;
                        VisibleIndices0[writePos] = finalPacked; 
                    } else if (c == 1) {
                        IndirectArgs1.InterlockedAdd(batchIndex * 20 + 4, 1, globalOffset);
                        writePos = myFirstVisibleOffset + (p * myMaxInstances) + globalOffset;
                        VisibleIndices1[writePos] = finalPacked; 
                    } else if (c == 2) {
                        IndirectArgs2.InterlockedAdd(batchIndex * 20 + 4, 1, globalOffset);
                        writePos = myFirstVisibleOffset + (p * myMaxInstances) + globalOffset;
                        VisibleIndices2[writePos] = finalPacked; 
                    }
                }
            }
        }
    } 
    // Для Main Pass используем продвинутую Wave-Level агрегацию!
    else {
        uint myBins[32];
        uint myLocalOffsets[32];

        // ШАГ 1: Поиск локальных корзин (Bins) внутри группы из 64 потоков
        if (isVisibleMain) {
            for (uint p = 0; p < myPartCount && p < 32; ++p) {
                uint batchIndex = myFirstBatch + p;
                uint bin = 255;
                
                for (uint i = 0; i < 128; ++i) {
                    if (sg_UniqueBatches[i] == batchIndex) {
                        bin = i;
                        break;
                    } else if (sg_UniqueBatches[i] == 0xFFFFFFFF) {
                        uint prev;
                        InterlockedCompareExchange(sg_UniqueBatches[i], 0xFFFFFFFF, batchIndex, prev);
                        if (prev == 0xFFFFFFFF || prev == batchIndex) {
                            bin = i;
                            break;
                        }
                    }
                }
                
                myBins[p] = bin;
                // Атомарно плюсуем в ЛОКАЛЬНУЮ память
                if (bin < 128) {
                    InterlockedAdd(sg_BatchCounts[bin], 1, myLocalOffsets[p]);
                } else {
                    // Fallback, если корзин не хватило (редкий случай)
                    uint globalOffset;
                    IndirectArgs0.InterlockedAdd(batchIndex * 20 + 4, 1, globalOffset);
                    myLocalOffsets[p] = globalOffset;
                }
            }
        }

        GroupMemoryBarrierWithGroupSync();

        // ШАГ 2: ТОЛЬКО ОДИН вызов глобального InterlockedAdd на всю группу потоков
        if (groupIndex < 128) {
            uint batchIndex = sg_UniqueBatches[groupIndex];
            if (batchIndex != 0xFFFFFFFF) {
                uint count = sg_BatchCounts[groupIndex];
                if (count > 0) {
                    uint byteOffset = batchIndex * 20 + 4; // Смещение до поля InstanceCount
                    uint globalOffset;
                    IndirectArgs0.InterlockedAdd(byteOffset, count, globalOffset);
                    sg_GlobalOffsets[groupIndex] = globalOffset;
                }
            }
        }

        GroupMemoryBarrierWithGroupSync();

        // ШАГ 3: Распределение глобальных индексов обратно потокам и запись в буфер
        if (isVisibleMain) {
            for (uint p = 0; p < myPartCount && p < 32; ++p) {
                uint bin = myBins[p];
                uint writePos;
                
                if (bin < 128) {
                    uint localIdx = myLocalOffsets[p];
                    uint globalStart = sg_GlobalOffsets[bin];
                    writePos = myFirstVisibleOffset + (p * myMaxInstances) + globalStart + localIdx;
                } else {
                    writePos = myFirstVisibleOffset + (p * myMaxInstances) + myLocalOffsets[p];
                }
                
                uint batchIndex = myFirstBatch + p;
                
                // УПАКОВКА: Индекс Инстанса (21 бит) + Slice/Alpha (11 бит)
                uint batchPacked = Batches[batchIndex].PackedData;
                uint finalPacked = (idx & 0x1FFFFF) | (batchPacked << 21);

                VisibleIndices0[writePos] = finalPacked; 
            }
        }
    }
}