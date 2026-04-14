//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// GammaEditor.cpp
// ================================================================================

#include "GammaEditor.h"

#ifdef GAMMA_EDITOR

#include "Vendor/ImGui/imgui.h"
#include "Vendor/ImGui/imgui_impl_win32.h"
#include "Vendor/ImGui/imgui_impl_dx11.h"
#include "../Core/Window.h"
#include "../Graphics/DX11Renderer.h"
#include "../API/GammaAPI.h"
#include <Vendor/ImGui/imgui_internal.h>
#include "Panels/SettingsPanel.h"

bool GammaEditor::Initialize(Window* window, DX11Renderer* renderer) {
    m_window = window;
    m_renderer = renderer;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // Включаем систему докинга
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Принудительно отключаем отрывные окна вне главного экрана
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(m_window->GetHWND());
    ImGui_ImplDX11_Init(m_renderer->GetDevice(), m_renderer->GetContext());

    // ИНИЦИАЛИЗИРУЕМ ПРОФАЙЛЕР
    m_profiler = std::make_unique<EditorProfiler>();
    m_profiler->Initialize(m_renderer->GetDevice());

    // РЕГИСТРАЦИЯ ПАНЕЛЕЙ (Модульная архитектура)
    m_mainViewport = std::make_shared<ViewportPanel>("Viewport");
    m_panels.push_back(m_mainViewport);

    m_panels.push_back(std::make_shared<SettingsPanel>("Project Settings"));

    // В будущем здесь будут добавляться другие панели:
    // m_panels.push_back(std::make_shared<InspectorPanel>("Inspector"));
    // m_panels.push_back(std::make_shared<ContentBrowserPanel>("Content Browser"));

    GAMMA_LOG_INFO(LogCategory::System, "ImGui Editor Initialized (Modular Mode).");
    return true;
}

void GammaEditor::Render(GammaState* gameState, ID3D11ShaderResourceView* gameSRV, const FrameProfilerStats& stats) {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Создаем главный невидимый контейнер (DockSpace)
    SetupDockSpace();

    // Отрисовываем верхнее меню
    DrawMenuBar();

    // Обновляем данные для главного вьюпорта
    if (m_mainViewport) {
        m_mainViewport->SetTexture(gameSRV);
    }

    // Отрисовываем все зарегистрированные модульные панели
    for (auto& panel : m_panels) {
        panel->OnImGuiRender();
    }

    // Временное хардкод окно Инспектора (Пока не создал InspectorPanel)
    ImGui::Begin("GameState Inspector");
    if (gameState) {
        ImGui::Text("Environment");
        ImGui::Separator();
        ImGui::SliderFloat("Time of Day", &gameState->env.timeOfDay, 0.0f, 24.0f);
        ImGui::DragFloat("Time Speed", &gameState->env.timeSpeed, 0.01f, 0.0f, 10.0f);
        ImGui::SliderFloat("Humidity", &gameState->env.humidity, 0.0f, 1.0f);
    }
    ImGui::End();

    // Профайлер
    if (m_profiler) {
        m_profiler->UpdateAndRender(stats);
    }

    // Закрываем главное окно DockSpace
    ImGui::End();

    // Финализация и отправка команд отрисовки
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Очистка слотов видеокарты
    if (m_renderer && m_renderer->GetContext()) {
        auto* ctx = m_renderer->GetContext();
        ID3D11ShaderResourceView* nullSRVs[8] = {};
        ctx->PSSetShaderResources(0, 8, nullSRVs);
        ctx->VSSetShaderResources(0, 8, nullSRVs);
    }
}

void GammaEditor::SetupDockSpace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    // Флаг MenuBar нужен, чтобы внутри этого окна можно было вызвать BeginMenuBar
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("GammaEditorDockSpace", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    // Создаем сам узел докинга
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
}

void GammaEditor::DrawMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene")) {}
            if (ImGui::MenuItem("Open Scene")) {}
            ImGui::Separator();

            if (ImGui::MenuItem("Settings...")) {
                for (auto& p : m_panels) {
                    if (p->GetName() == "Project Settings") p->SetOpen(true);
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) PostQuitMessage(0);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
            // Динамическое управление видимостью панелей
            for (auto& panel : m_panels) {
                bool isOpen = panel->IsOpen();
                if (ImGui::MenuItem(panel->GetName().c_str(), nullptr, &isOpen)) {
                    panel->SetOpen(isOpen);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void GammaEditor::Shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

bool GammaEditor::IsMainViewportHovered() const {
    return m_mainViewport ? m_mainViewport->IsHovered() : false;
}

#endif // GAMMA_EDITOR