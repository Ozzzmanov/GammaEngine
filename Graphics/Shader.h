#pragma once
#include "Core/Prerequisites.h"

struct SimpleVertex {
    float x, y, z;    // Позиция
    float r, g, b;    // Цвет
    float nx, ny, nz; // Нормаль
    float u, v;       // UV координаты
};

class Shader {
public:
    Shader(ID3D11Device* device, ID3D11DeviceContext* context);
    ~Shader() = default;

    bool Load(const std::wstring& filename);
    void Bind();

private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;

    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11InputLayout> m_inputLayout;

    // Сэмплер для текстур (как фильтровать пиксели)
    ComPtr<ID3D11SamplerState> m_samplerState;

    bool CompileShader(const std::wstring& filename, const std::string& entryPoint,
        const std::string& profile, ID3DBlob** shaderBlob);
};