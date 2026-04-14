// ================================================================================
// RvtBaker.hlsl (Runtime Virtual Texturing Baker)
// Фоновый Compute Shader, который запекает сложные многослойные материалы 
// ландшафта (до 24 слоев) в плоскую 2D текстуру (Clipmap) вокруг камеры игрока.
// Использует тороидальное обновление и O(1) поиск чанков.
// ================================================================================

struct TerrainMaterial {
    float4 UProj;           // Проекция текстуры по оси U
    float4 VProj;           // Проекция текстуры по оси V
    uint   DiffuseIndex;    // Индекс Albedo в глобальном Texture2DArray
    float3 Padding;
};

struct ChunkGpuData {
    float3 WorldPos;        // Мировые координаты чанка (угол или центр)
    uint   ArraySlice;      // Индекс слоя в SplatMap / HeightMap массиве
    uint   MaterialIndices[24]; // Палитра из 24 материалов для данного чанка
};

// --------------------------------------------------------------------------------
// КОНСТАНТЫ ОБНОВЛЕНИЯ (Передаются каждый раз при сдвиге камеры)
// --------------------------------------------------------------------------------
cbuffer BakeParams : register(b0) {
    float2 WorldPosMin;     // Стартовая точка (Мировые X, Z) для запекания
    uint2  UpdateSize;      // Размер запекаемого куска (Width, Height)
    float  TexelSize;       // Размер одного пикселя (зависит от каскада, например 0.5м)
    uint   Resolution;      // Разрешение одного каскада RVT (например 2048)
    uint   CascadeIndex;    // Номер каскада [0..5]
    uint   Pad;
};

// --------------------------------------------------------------------------------
// РЕСУРСЫ
// --------------------------------------------------------------------------------
StructuredBuffer<TerrainMaterial> GlobalMaterials : register(t0); // Все материалы игры
Texture2DArray<float4>            IndexArray      : register(t1); // SplatMap индексы
Texture2DArray<float4>            WeightArray     : register(t2); // SplatMap веса
Texture2DArray                    DiffuseArray    : register(t3); // Мега-массив текстур
Texture2D<uint>                   ChunkLookup     : register(t4); // O(1) Карта поиска чанков по координатам
StructuredBuffer<ChunkGpuData>    Chunks          : register(t5); // Данные чанков

SamplerState SamplerWrap  : register(s0); // Для тайлинга текстур земли
SamplerState SamplerClamp : register(s1); // Для SplatMap

// Выходные текстуры каскадов (Clipmaps)
RWTexture2DArray<unorm float4> OutAlbedo : register(u0);
RWTexture2DArray<unorm float4> OutNormal : register(u1);

// --------------------------------------------------------------------------------
// ФУНКЦИЯ ВЫБОРКИ (Splatting 4-х материалов)
// --------------------------------------------------------------------------------
float3 SampleTerrainColor(float4 indexRaw, float4 weightRaw, float3 worldPos, ChunkGpuData chunk, float mipLevel) {
    // Распаковываем индексы (хранятся как [0..1], переводим в байты [0..255])
    uint4 localIdx = uint4(round(indexRaw * 255.0f));
    float3 color = 0.0f;
    float  weightSum = 0.0f;

    [unroll]
    for (int i = 0; i < 4; ++i) {
        float w = weightRaw[i];
        [branch] // не сэмплим материалы с нулевым или микроскопическим весом
        if (w > 0.01f) {
            TerrainMaterial mat = GlobalMaterials[chunk.MaterialIndices[localIdx[i]]];
            
            // Вычисляем UV-координаты для текстуры на основе мировых координат
            float2 texUV = float2(
                dot(float4(worldPos, 1.0f), mat.UProj),
                dot(float4(worldPos, 1.0f), mat.VProj)
            );
            
            // Т.к. мы в Compute Shader, аппаратных MIP-градиентов (ddx/ddy) нет. 
            // Используем ручной расчет MipLevel через SampleLevel.
            color += DiffuseArray.SampleLevel(SamplerWrap, float3(texUV, mat.DiffuseIndex), mipLevel).rgb * w;
            weightSum += w;
        }
    }
    // Нормализация (чтобы избежать артефактов затемнения на швах)
    return weightSum > 0.001f ? (color / weightSum) : color;
}

// --------------------------------------------------------------------------------
// ГЛАВНЫЙ COMPUTE SHADER (Запускается блоками 8x8 пикселей)
// --------------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    // Отсекаем потоки, вышедшие за границы зоны обновления (L-Shape Update)
    if (DTid.x >= UpdateSize.x || DTid.y >= UpdateSize.y) return;

    // Вычисляем мировые координаты пикселя, который мы сейчас "печем"
    float2 worldXZ = WorldPosMin + (float2(DTid.x, DTid.y) + 0.5f) * TexelSize;
    float3 worldPos = float3(worldXZ.x, 0.0f, worldXZ.y);

    // --- 1. O(1) ПОИСК ЧАНКА (Spatial Hashing) ---
    // Превращаем мировые X,Z в пиксельные координаты Lookup-текстуры (1 пиксель = 100м чанк)
    int lx = (int)floor(worldXZ.x / 100.0f) + 256;
    int lz = (int)floor(worldXZ.y / 100.0f) + 256;
    
    // Защита от выхода за карту (например, карта 51.2 х 51.2 км)
    if (lx < 0 || lx >= 512 || lz < 0 || lz >= 512) return;

    uint chunkIndex = ChunkLookup.Load(int3(lx, lz, 0));
    if (chunkIndex == 0xFFFFFFFF) return; // Пустой чанк (дырка в мире)

    ChunkGpuData chunk = Chunks[chunkIndex];
    uint arraySlice = chunk.ArraySlice;

    // --- 2. РАСЧЕТ КООРДИНАТ ДЛЯ SPLAT MAP ---
    float chunkMinX = floor(worldXZ.x / 100.0f) * 100.0f;
    float chunkMinZ = floor(worldXZ.y / 100.0f) * 100.0f;
    float2 pct      = (worldXZ - float2(chunkMinX, chunkMinZ)) / 100.0f; // [0.0 .. 1.0]
    float2 pixelUV  = pct * 128.0f - 0.5f; // Разрешение SplatMap = 128x128
    int2   p00      = (int2)floor(pixelUV);
    float2 f        = frac(pixelUV);       // Доли для билинейной интерполяции

    // 4 соседних текселя SplatMap (Clamp защищает от швов на краях чанка)
    int3 c00 = int3(clamp(p00,               0, 127), arraySlice);
    int3 c10 = int3(clamp(p00 + int2(1,0),   0, 127), arraySlice);
    int3 c01 = int3(clamp(p00 + int2(0,1),   0, 127), arraySlice);
    int3 c11 = int3(clamp(p00 + int2(1,1),   0, 127), arraySlice);

    float4 i00 = IndexArray.Load(int4(c00, 0)); float4 w00 = WeightArray.Load(int4(c00, 0));
    float4 i10 = IndexArray.Load(int4(c10, 0)); float4 w10 = WeightArray.Load(int4(c10, 0));
    float4 i01 = IndexArray.Load(int4(c01, 0)); float4 w01 = WeightArray.Load(int4(c01, 0));
    float4 i11 = IndexArray.Load(int4(c11, 0)); float4 w11 = WeightArray.Load(int4(c11, 0));

    // Вычисляем MipLevel на основе размера текселя каскада RVT
    float mipLevel = clamp(log2(TexelSize * 12.0f), 0.0f, 6.0f);

    // --- 3. (Uniform Branching) ---
    // Если 4 пикселя SplatMap имеют одинаковые индексы и веса (плоский цвет),
    // мы пропускаем 12 тяжелых чтений из памяти и делаем всего 4!
    bool isUniform = all(abs(i00 - i10) < 0.01f) &&
                     all(abs(i00 - i01) < 0.01f) &&
                     all(abs(i00 - i11) < 0.01f) &&
                     all(abs(w00 - w10) < 0.01f) &&
                     all(abs(w00 - w01) < 0.01f) &&
                     all(abs(w00 - w11) < 0.01f);

    float3 finalColor;
    
    [branch] 
    if (isUniform) {
        // Ускоренный путь
        finalColor = SampleTerrainColor(i00, w00, worldPos, chunk, mipLevel);
    } else {
        // Тяжелый путь: Билинейная интерполяция 4-х смесей (до 16 чтений из текстур!)
        float3 c00c = SampleTerrainColor(i00, w00, worldPos, chunk, mipLevel);
        float3 c10c = SampleTerrainColor(i10, w10, worldPos, chunk, mipLevel);
        float3 c01c = SampleTerrainColor(i01, w01, worldPos, chunk, mipLevel);
        float3 c11c = SampleTerrainColor(i11, w11, worldPos, chunk, mipLevel);
        finalColor  = lerp(lerp(c00c, c10c, f.x), lerp(c01c, c11c, f.x), f.y);
    }

    // --- 4. ТОРОИДАЛЬНАЯ ЗАПИСЬ ---
    // Вычисляем зацикленный пиксель на текстуре RVT
    int    res      = (int)Resolution;
    int    px       = (int)floor(worldXZ.x / TexelSize);
    int    pz       = (int)floor(worldXZ.y / TexelSize);
    
    // (A % B + B) % B — классическая C++ формула безопасного взятия остатка для отрицательных чисел
    uint2  writePos = uint2((px % res + res) % res, (pz % res + res) % res);

    OutAlbedo[uint3(writePos, CascadeIndex)] = float4(finalColor, 1.0f);
    
    // В будущем здесь можно допекать полноценные нормали вместо дефолтных
    OutNormal[uint3(writePos, CascadeIndex)] = float4(0.5f, 0.5f, 1.0f, 1.0f);
}