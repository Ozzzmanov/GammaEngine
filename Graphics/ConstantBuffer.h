#pragma once
#include "Core/Prerequisites.h"

// Структура, которая полетит в шейдер (register b0)
struct CB_VS_Transform {
    XMMATRIX World;
    XMMATRIX View;
    XMMATRIX Projection;
};

// Обертка для удобной работы с Constant Buffer
class ConstantBuffer {
public:
    ConstantBuffer(ID3D11Device* device, ID3D11DeviceContext* context)
        : m_device(device), m_context(context) {
    }

    // Инициализация буфера
    bool Initialize() {
        D3D11_BUFFER_DESC bd = { 0 };
        bd.Usage = D3D11_USAGE_DEFAULT; // CPU пишет, GPU читает (обычно DYNAMIC, но пока DEFAULT ок)
        bd.ByteWidth = sizeof(CB_VS_Transform);
        // Размер буфера должен быть кратен 16 байтам
        if (bd.ByteWidth % 16 != 0) bd.ByteWidth += 16 - (bd.ByteWidth % 16);

        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = 0;

        HRESULT hr = m_device->CreateBuffer(&bd, nullptr, m_buffer.GetAddressOf());
        return SUCCEEDED(hr);
    }

    // Обновление данных в буфере
    void Update(const CB_VS_Transform& data) {
        // Так как Usage Default, используем UpdateSubresource
        m_context->UpdateSubresource(m_buffer.Get(), 0, nullptr, &data, 0, 0);
    }

    // Активация буфера для Vertex Shader (VS)
    void Bind(UINT slot) {
        m_context->VSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
    }

private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Buffer> m_buffer;
};