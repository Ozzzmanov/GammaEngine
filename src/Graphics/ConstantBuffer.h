//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Шаблонный класс ConstantBuffer<T>.
// Обертка над ID3D11Buffer для удобной передачи структур данных в шейдеры.
// Автоматически выравнивает данные по 16 байт.
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

    // Инициализация буфера
    bool Initialize() {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT; // Используем Default, обновление через UpdateSubresource
        bd.ByteWidth = sizeof(T);

        // Размер буфера констант ОБЯЗАН быть кратен 16 байтам
        if (bd.ByteWidth % 16 != 0)
            bd.ByteWidth += 16 - (bd.ByteWidth % 16);

        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = 0; // Для USAGE_DEFAULT доступ CPU не нужен напрямую

        HRESULT hr = m_device->CreateBuffer(&bd, nullptr, m_buffer.GetAddressOf());
        if (FAILED(hr)) {
            Logger::Error(LogCategory::General, "Failed to create ConstantBuffer.");
            return false;
        }
        return true;
    }

    // Обновление данных в видеопамяти
    void Update(const T& data) {
        m_context->UpdateSubresource(m_buffer.Get(), 0, nullptr, &data, 0, 0);
    }

    // Привязка к Вершинному Шейдеру (VS)
    void BindVS(UINT slot) {
        m_context->VSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
    }

    // Привязка к Пиксельному Шейдеру (PS)
    void BindPS(UINT slot) {
        m_context->PSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
    }

    ID3D11Buffer* Get() const { return m_buffer.Get(); }

private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Buffer> m_buffer;
};