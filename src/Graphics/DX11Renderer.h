//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Класс DX11Renderer.
// Отвечает за инициализацию устройства DirectX 11, создание цепочки обмена (SwapChain),
// управление буфером глубины (Z-Buffer) и состояниями растеризатора.
// ================================================================================

#pragma once
#include "../Core/Prerequisites.h"
#include "../Core/Logger.h"

class DX11Renderer {
public:
    DX11Renderer();
    ~DX11Renderer() = default;

    // Инициализация Direct3D. Создает Device, Context и SwapChain.
    bool Initialize(HWND hwnd, int width, int height);

    // Очищает задний буфер (цвет) и буфер глубины перед отрисовкой нового кадра.
    void Clear(float r, float g, float b, float a);

    // Отображает кадр на экране (смена буферов).
    void Present(bool vsync);

    // Устанавливает состояние отображения (Сплошное заполнение или Каркас/Wireframe).
    void SetWireframe(bool enable);

    // Сбрасывает состояние глубины к стандартному (запись включена, проверка включена).
    void BindDefaultDepthState();

    // Геттеры для доступа к низкоуровневым интерфейсам
    ID3D11Device* GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }

private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;

    ComPtr<ID3D11RenderTargetView> m_renderTargetView; // Ссылка на задний буфер

    // --- Ресурсы буфера глубины ---
    ComPtr<ID3D11Texture2D> m_depthStencilBuffer;
    ComPtr<ID3D11DepthStencilView> m_depthStencilView;
    ComPtr<ID3D11DepthStencilState> m_depthStencilState;

    // --- Состояния растеризатора ---
    ComPtr<ID3D11RasterizerState> m_rasterStateSolid;
    ComPtr<ID3D11RasterizerState> m_rasterStateWireframe;
};