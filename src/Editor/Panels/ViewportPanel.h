#pragma once
#include "../EditorPanel.h"
#include <d3d11.h>
#include <DirectXMath.h>

class ViewportPanel : public EditorPanel {
public:
    ViewportPanel(const std::string& name);
    ~ViewportPanel() = default;

    void OnImGuiRender() override;

    // Передаем текстуру, которую нужно отобразить
    void SetTexture(ID3D11ShaderResourceView* srv) { m_textureSRV = srv; }

    bool IsHovered() const { return m_isHovered; }
    bool IsFocused() const { return m_isFocused; }

    // Размеры панели для динамического ресайза Render Target в будущем
    DirectX::XMFLOAT2 GetViewportSize() const { return m_viewportSize; }

private:
    ID3D11ShaderResourceView* m_textureSRV = nullptr;
    DirectX::XMFLOAT2 m_viewportSize = { 0.0f, 0.0f };

    bool m_isHovered = false;
    bool m_isFocused = false;
};