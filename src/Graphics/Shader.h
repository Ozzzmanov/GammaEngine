//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Shader.h
// Управление HLSL шейдерами (VS + HS + DS + PS).
// FIXME: Дублирует логику ShaderManager. Игнорирует кэш.
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <d3dcompiler.h>

class Shader {
public:
    Shader(ID3D11Device* device, ID3D11DeviceContext* context);
    ~Shader() = default;

    bool Load(const std::wstring& filename, const D3D_SHADER_MACRO* defines = nullptr, bool isTreeShader = false, bool isWaterShader = false, bool useTessellation = false);
    void Bind();

    ID3DBlob* GetVSBlob() const { return m_vsBlob.Get(); }

private:
    bool CompileShader(const std::wstring& filename, const std::string& entryPoint,
        const std::string& profile, const D3D_SHADER_MACRO* defines, ID3DBlob** shaderBlob);

    ComPtr<ID3D11Device>        m_device;
    ComPtr<ID3D11DeviceContext> m_context;

    ComPtr<ID3DBlob>            m_vsBlob;
    ComPtr<ID3D11VertexShader>  m_vertexShader;

    ComPtr<ID3D11HullShader>    m_hullShader;
    ComPtr<ID3D11DomainShader>  m_domainShader;
    ComPtr<ID3D11PixelShader>   m_pixelShader;

    ComPtr<ID3D11InputLayout>   m_inputLayout;
    ComPtr<ID3D11SamplerState>  m_samplerState;
};