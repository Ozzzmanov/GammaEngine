// ================================================================================
// EditorPanel.h
// ================================================================================
#pragma once
#include <string>

class GammaEditor;

class EditorPanel {
public:
    EditorPanel(const std::string& name) : m_name(name) {}
    virtual ~EditorPanel() = default;

    // Вызывается каждый кадр для отрисовки ImGui окна
    virtual void OnImGuiRender() = 0;

    const std::string& GetName() const { return m_name; }
    bool IsOpen() const { return m_isOpen; }
    void SetOpen(bool isOpen) { m_isOpen = isOpen; }

protected:
    std::string m_name;
    bool m_isOpen = true;
};