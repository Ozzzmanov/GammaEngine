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
// Гарантирует правильное выравнивание памяти (кратное 16 байтам).
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

    /// @brief Создает аппаратный буфер на GPU.
    /// @param isDynamic Если true, буфер можно быстро обновлять каждый кадр через Map/Unmap.
    bool Initialize(bool isDynamic = false) {
        // Защита от утечек при повторном вызове (например, при смене настроек)
        m_buffer.Reset();

        D3D11_BUFFER_DESC bd = {};

        // ВАЖНО: Размер CB в DX11 ДОЛЖЕН быть кратен 16 байтам.
        bd.ByteWidth = sizeof(T);
        if (bd.ByteWidth % 16 != 0) {
            bd.ByteWidth += 16 - (bd.ByteWidth % 16);
        }

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
            GAMMA_LOG_ERROR(LogCategory::Render, "Failed to create ConstantBuffer of size " + std::to_string(bd.ByteWidth));
            return false;
        }
        return true;
    }

    /// @brief Обновляет статический буфер (USAGE_DEFAULT) через очередь команд.
    /// Используется для данных, которые меняются редко (например, настройки графики).
    void Update(const T& data) {
        if (!m_buffer) return;
        m_context->UpdateSubresource(m_buffer.Get(), 0, nullptr, &data, 0, 0);
    }

    /// @brief Быстрое обновление (USAGE_DYNAMIC) через прямой доступ к памяти (Zero-copy).
    /// Использовать для данных, меняющихся каждый кадр (Матрицы View/Proj, Позиция камеры).
    void UpdateDynamic(const T& data) {
        if (!m_buffer) return;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            // Типобезопасное копирование вместо memcpy. Позволяет компилятору оптимизировать пересылку матриц.
            *static_cast<T*>(mapped.pData) = data;
            m_context->Unmap(m_buffer.Get(), 0);
        }
    }

    // Binders (Привязка к стадиям пайплайна)
    void BindVS(UINT slot) {
        m_context->VSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
    }

    void BindPS(UINT slot) {
        m_context->PSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
    }

    void BindCS(UINT slot) {
        m_context->CSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
    }

    void BindHS(UINT slot) {
        m_context->HSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
    }

    void BindDS(UINT slot) {
        m_context->DSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
    }

    ID3D11Buffer* Get() const { return m_buffer.Get(); }

private:
    ComPtr<ID3D11Device>        m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Buffer>        m_buffer;
};