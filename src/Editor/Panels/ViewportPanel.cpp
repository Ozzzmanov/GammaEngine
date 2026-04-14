#include "ViewportPanel.h"
#include "../../Vendor/ImGui/imgui.h"

ViewportPanel::ViewportPanel(const std::string& name) : EditorPanel(name) {}

void ViewportPanel::OnImGuiRender() {
    if (!m_isOpen) return;

    // Убираем отступы, чтобы игра была прямо до краев окна
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    // Флаг NoScrollbar обязателен для вьюпорта
    ImGui::Begin(m_name.c_str(), &m_isOpen, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Читаем состояния ДО отрисовки картинки
    m_isHovered = ImGui::IsWindowHovered();
    m_isFocused = ImGui::IsWindowFocused();

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    m_viewportSize = { viewportSize.x, viewportSize.y };

    if (m_textureSRV && viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
        ImGui::Image((void*)m_textureSRV, viewportSize);
    }

    ImGui::End();
    ImGui::PopStyleVar();
}