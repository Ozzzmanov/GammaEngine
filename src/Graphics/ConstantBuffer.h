//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// ConstantBuffer.h
// Шаблонный класс для управления константными буферами DirectX 11.
// Поддерживает как статическое (UpdateSubresource), так и динамическое (Map) обновление.
// ================================================================================

#pragma once
#include "../Core/Prerequisites.h"
#include "../Core/Logger.h"

template <typename T>
class ConstantBuffer {
public:
    ConstantBuffer(ID3D11Device* device, ID3D11DeviceContext* context)
        : m_device(device), m_context(context) {
    }

    ~ConstantBuffer() = default;

    // Инициализация. Если isDynamic = true, буфер создается с D3D11_USAGE_DYNAMIC.
    bool Initialize(bool isDynamic = false) {
        D3D11_BUFFER_DESC bd = {};

        // Размер должен быть кратен 16 байтам
        bd.ByteWidth = sizeof(T);
        if (bd.ByteWidth % 16 != 0) bd.ByteWidth += 16 - (bd.ByteWidth % 16);

        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        if (isDynamic) {
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        }
        else {
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.CPUAccessFlags = 0;
        }

        HRESULT hr = m_device->CreateBuffer(&bd, nullptr, m_buffer.GetAddressOf());
        if (FAILED(hr)) {
            Logger::Error(LogCategory::Render, "Failed to create ConstantBuffer.");
            return false;
        }
        return true;
    }

    // Обновление (для USAGE_DEFAULT) - через драйвер
    void Update(const T& data) {
        m_context->UpdateSubresource(m_buffer.Get(), 0, nullptr, &data, 0, 0);
    }

    // Быстрое обновление (для USAGE_DYNAMIC) - прямой доступ к памяти
    // Используйте это для буферов, которые меняются каждый кадр (Transform, Light).
    void UpdateDynamic(const T& data) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, &data, sizeof(T));
            m_context->Unmap(m_buffer.Get(), 0);
        }
    }

    void BindVS(UINT slot) {
        m_context->VSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
    }

    void BindPS(UINT slot) {
        m_context->PSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
    }

    void BindCS(UINT slot) {
        m_context->CSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
    }

    ID3D11Buffer* Get() const { return m_buffer.Get(); }

private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Buffer> m_buffer;
};