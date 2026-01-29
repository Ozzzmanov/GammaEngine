//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Shader.h
// Управление HLSL шейдерами (VS + PS).
// ================================================================================

#pragma once
#include "../Core/Prerequisites.h"

class Shader {
public:
    Shader(ID3D11Device* device, ID3D11DeviceContext* context);
    ~Shader() = default;

    bool Load(const std::wstring& filename);
    void Bind();

    ID3DBlob* GetVSBlob() const { return m_vsBlob.Get(); }

private:
    bool CompileShader(const std::wstring& filename, const std::string& entryPoint,
        const std::string& profile, ID3DBlob** shaderBlob);

    ComPtr<ID3D11Device>        m_device;
    ComPtr<ID3D11DeviceContext> m_context;

    ComPtr<ID3DBlob>            m_vsBlob;
    ComPtr<ID3D11VertexShader>  m_vertexShader;
    ComPtr<ID3D11PixelShader>   m_pixelShader;
    ComPtr<ID3D11InputLayout>   m_inputLayout;
    ComPtr<ID3D11SamplerState>  m_samplerState;
};