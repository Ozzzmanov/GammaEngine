// ================================================================================
// TerrainCulling.hlsl
// GPU-Driven Culling для ландшафта (Clipmap RVT + Heavy Rendering).
// Выполняет Frustum Culling (AABB), Occlusion Culling (HZB) и выбор LOD'а.
// Формирует косвенные аргументы отрисовки (Indirect Draw) через быструю 
// агрегацию в groupshared памяти.
// ================================================================================

struct ShaderChunkInfo {
    float3 Extents; float Pad1; // Половина размера AABB чанка
    float3 Center;  float Pad2; // Центр AABB чанка
};

cbuffer CullingParams : register(b0) {
    float4x4 ViewProj;
    float4x4 PrevViewProj;          // Для репроецирования в прошлый кадр (HZB)
    float4   FrustumPlanes[6];
    float3   CameraPos;
    uint     NumChunks;             // Общее количество чанков на карте
    float2   HZBSize;
    float    MaxDistanceSq;         // Дистанция полного отсечения
    uint     EnableFrustum;
    uint     EnableOcclusion;
    float    LOD1_DistSq;           // Квадрат дистанции для перехода на LOD1
    float    LOD2_DistSq;           // Квадрат дистанции для перехода на LOD2
    uint     Pad;
};

// --------------------------------------------------------------------------------
// РЕСУРСЫ
// --------------------------------------------------------------------------------
StructuredBuffer<ShaderChunkInfo> ChunkInfo    : register(t0); // Статические данные чанков
Texture2D<float>                  HZBMap       : register(t1); // Иерархический Z-буфер

// Для каждого LODа (0, 1, 2) выделен свой диапазон в буфере видимости.
RWStructuredBuffer<uint> VisibleIndices : register(u0); 
RWByteAddressBuffer      IndirectArgs   : register(u1); // Команды DrawIndexedInstancedIndirect

SamplerState PointClampSampler : register(s0);

// --------------------------------------------------------------------------------
// GROUPSHARED ПАМЯТЬ 
// --------------------------------------------------------------------------------
// У нас 3 LOD'а, поэтому нам нужно всего по 3 ячейки памяти на всю группу из 64 потоков.
groupshared uint sg_VisibleCount[3];
groupshared uint sg_BaseOffset[3];

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
    // 1. Инициализация счетчиков первыми тремя потоками группы
    if (groupIndex < 3) sg_VisibleCount[groupIndex] = 0;
    GroupMemoryBarrierWithGroupSync();

    uint idx = id.x;
    bool isVisible = false;
    uint lodLevel = 0;

    if (idx < NumChunks) {
        isVisible = true;
        ShaderChunkInfo chunk = ChunkInfo[idx];

        // --- ДИСТАНЦИЯ И LOD ---
        float3 toCamera = chunk.Center - CameraPos;
        // Смещаем проверку чуть ближе к центру чанка (компенсация размера чанка 100х100)
        float distSq = dot(toCamera, toCamera) - (70.0f * 70.0f); 

        if (distSq > MaxDistanceSq) {
            isVisible = false;
        } else {
            lodLevel = (distSq > LOD2_DistSq) ? 2 : (distSq > LOD1_DistSq) ? 1 : 0;
        }

        // --- FRUSTUM CULLING (AABB vs 6 Planes) ---
        if (isVisible && EnableFrustum == 1) {
            [unroll] 
            for (int i = 0; i < 6; ++i) {
                // Вычисляем эффективный радиус AABB вдоль нормали плоскости
                float3 absNormal = abs(FrustumPlanes[i].xyz);
                float radius = dot(chunk.Extents, absNormal);
                // Расстояние от центра до плоскости
                float dist   = dot(chunk.Center, FrustumPlanes[i].xyz) + FrustumPlanes[i].w;
                // Если центр дальше радиуса в сторону внешней нормали -> отсекаем
                if (dist > radius) { isVisible = false; break; }
            }
        }

        // --- OCCLUSION CULLING (HZB) ---
        if (isVisible && EnableOcclusion == 1) {
            float3 minExt = chunk.Center - chunk.Extents;
            float3 maxExt = chunk.Center + chunk.Extents;
            
            // 8 вершин AABB чанка
            float3 corners[8] = {
                float3(minExt.x, minExt.y, minExt.z), float3(minExt.x, minExt.y, maxExt.z),
                float3(minExt.x, maxExt.y, minExt.z), float3(minExt.x, maxExt.y, maxExt.z),
                float3(maxExt.x, minExt.y, minExt.z), float3(maxExt.x, minExt.y, maxExt.z),
                float3(maxExt.x, maxExt.y, minExt.z), float3(maxExt.x, maxExt.y, maxExt.z)
            };

            float minX = 1e5f, minY = 1e5f, maxX = -1e5f, maxY = -1e5f;
            float closestZ = 1.0f;
            bool behindCamera = false;

            // Проецируем 8 вершин AABB в NDC пространство прошлого кадра
            [unroll] 
            for (int i = 0; i < 8; ++i) {
                float4 clipPos = mul(float4(corners[i], 1.0f), PrevViewProj);
                if (clipPos.w <= 0.1f) { behindCamera = true; break; } // Чанк пересекает плоскость ближнего отсечения
                
                clipPos.xyz /= clipPos.w;
                minX = min(minX, clipPos.x); minY = min(minY, clipPos.y);
                maxX = max(maxX, clipPos.x); maxY = max(maxY, clipPos.y);
                closestZ = min(closestZ, clipPos.z); // Ищем самую ближнюю к камере точку чанка
            }

            // Если чанк целиком перед камерой - делаем HZB тест
            if (!behindCamera) {
                bool offscreen = (maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f);
                
                if (!offscreen) {
                    // Перевод из NDC [-1..1] в UV [0..1]
                    float2 uvMin = saturate(float2(minX * 0.5f + 0.5f, 0.5f - maxY * 0.5f));
                    float2 uvMax = saturate(float2(maxX * 0.5f + 0.5f, 0.5f - minY * 0.5f));
                    float2 sizePixels = (uvMax - uvMin) * HZBSize;
                    
                    // ИСПРАВЛЕНО: Заменили floor на ceil для 100% гарантии полного покрытия (защита от Leaking'а)
                    float mip = max(0.0f, ceil(log2(max(sizePixels.x, sizePixels.y))));
                    float2 mipRes = max(1.0f, floor(HZBSize / exp2(mip)));

                    uint2 texMin = clamp(uint2(floor(uvMin * mipRes)), 0, uint2(mipRes) - 1);
                    uint2 texMax = clamp(uint2(floor(uvMax * mipRes)), 0, uint2(mipRes) - 1);

                    // Сэмпл 4-х пикселей (2х2 квадрат)
                    float d = max(max(HZBMap.Load(int3(texMin.x, texMin.y, mip)),
                                      HZBMap.Load(int3(texMax.x, texMin.y, mip))),
                               max(HZBMap.Load(int3(texMin.x, texMax.y, mip)),
                                   HZBMap.Load(int3(texMax.x, texMax.y, mip))));

                    // Если ближайшая точка чанка находится дальше, чем глубина в Z-буфере -> чанк перекрыт
                    if (closestZ > d + 0.00005f) isVisible = false;
                }
            }
        }
    }

    // =========================================================================
    // АТОМАРНАЯ ЗАПИСЬ (Wave/Group Level Aggregation)
    // =========================================================================
    
    // 2. Локальная атомарная операция в быструю память L1 (без нагрузки на VRAM)
    uint localOffset = 0;
    if (isVisible) InterlockedAdd(sg_VisibleCount[lodLevel], 1, localOffset);
    
    GroupMemoryBarrierWithGroupSync();

    // 3. ТОЛЬКО 3 ПОТОКА ИЗ 64 делают запись в глобальную память Indirect Arguments!
    // 20 = sizeof(D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS)
    // 4  = Смещение до поля InstanceCount
    if (groupIndex < 3 && sg_VisibleCount[groupIndex] > 0) {
        IndirectArgs.InterlockedAdd(groupIndex * 20 + 4, sg_VisibleCount[groupIndex], sg_BaseOffset[groupIndex]);
    }
        
    GroupMemoryBarrierWithGroupSync();

    // 4. Распределяем глобальные смещения обратно потокам и пишем индексы видимых чанков
    if (isVisible) {
        VisibleIndices[lodLevel * NumChunks + sg_BaseOffset[lodLevel] + localOffset] = idx;
    }
}