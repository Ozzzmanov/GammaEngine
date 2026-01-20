//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Класс Shader.
// Компилирует HLSL код, создает Vertex/Pixel шейдеры, Input Layout 
// и настраивает Sampler State (фильтрацию текстур).
// ================================================================================

#pragma once
#include "../Core/Prerequisites.h"

class Shader {
public:
    Shader(ID3D11Device* device, ID3D11DeviceContext* context);
    ~Shader() = default;

    // Загрузка и компиляция .hlsl файла
    bool Load(const std::wstring& filename);

    // Активация шейдера в конвейере
    void Bind();

private:
    // Вспомогательная функция компиляции через D3DCompileFromFile
    bool CompileShader(const std::wstring& filename, const std::string& entryPoint,
        const std::string& profile, ID3DBlob** shaderBlob);

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;

    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11InputLayout> m_inputLayout;

    // Сэмплер для текстур (линейная фильтрация + тайлинг)
    ComPtr<ID3D11SamplerState> m_samplerState;
};