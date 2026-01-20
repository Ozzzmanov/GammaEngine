Texture2D txDiffuse : register(t0);
SamplerState samLinear : register(s0);

cbuffer TransformBuffer : register(b0) {
    matrix World;
    matrix View;
    matrix Projection;
};

struct VS_INPUT {
    float3 position : POSITION;
    float3 color    : COLOR;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD; // <--- Новое: Текстурные координаты
};

struct PS_INPUT {
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

// --- ВЕРШИННЫЙ ШЕЙДЕР ---
PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;
    
    float4 pos = float4(input.position, 1.0f);
    pos = mul(pos, World);
    pos = mul(pos, View);
    pos = mul(pos, Projection);
    
    output.position = pos;
    output.normal = normalize(mul(input.normal, (float3x3)World));
    output.color = input.color;
    output.uv = input.uv; // Прокидываем UV дальше
    
    return output;
}

// --- ПИКСЕЛЬНЫЙ ШЕЙДЕР ---
float4 PSMain(PS_INPUT input) : SV_TARGET {
    // Освещение
    float3 lightDir = normalize(float3(0.5f, 1.0f, -0.5f));
    float diff = max(dot(input.normal, lightDir), 0.0f);
    float ambient = 0.3f;
    
    // Читаем цвет из текстуры (или сетки)
    float4 texColor = txDiffuse.Sample(samLinear, input.uv);
    
    // Смешиваем цвет текстуры с цветом вершин (для отладки)
    float3 finalColor = texColor.rgb * (diff + ambient);

    return float4(finalColor, 1.0f);
}