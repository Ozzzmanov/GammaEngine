//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// ComputeShader.h
// ================================================================================
#pragma once
#include "../Core/Prerequisites.h"
#include <string>

class ComputeShader {
public:
    ComputeShader(ID3D11Device* device, ID3D11DeviceContext* context);
    ~ComputeShader() = default;

    // Загрузка и компиляция шейдера
    bool Load(const std::wstring& filename, const std::string& entryPoint = "CSMain");

    // Установка шейдера в пайплайн
    void Bind();

    // Запуск вычислений (Dispatch)
    void Dispatch(UINT x, UINT y, UINT z);

    // Очистка (снятие шейдера)
    void Unbind();

private:
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    ComPtr<ID3D11ComputeShader> m_shader;
};