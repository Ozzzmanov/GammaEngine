// ================================================================================
// SettingsPanel.cpp
// ================================================================================
#include "SettingsPanel.h"
#include "../../Vendor/ImGui/imgui.h"
#include <Windows.h>

SettingsPanel::SettingsPanel(const std::string& name) : EditorPanel(name) {
    m_isOpen = false;
}

std::string SettingsPanel::KeyCodeToString(int keyCode) {
    if (keyCode == 0) return "None";
    if (keyCode >= 'A' && keyCode <= 'Z') return std::string(1, (char)keyCode);
    if (keyCode >= '0' && keyCode <= '9') return std::string(1, (char)keyCode);

    switch (keyCode) {
    case VK_SPACE: return "Space";
    case VK_SHIFT: return "Shift";
    case VK_CONTROL: return "Ctrl";
    case VK_MENU: return "Alt";
    case VK_ESCAPE: return "Esc";
    case VK_TAB: return "Tab";
    case VK_RETURN: return "Enter";
    case VK_F1: return "F1";
    case VK_F2: return "F2";
    case VK_F3: return "F3";
    case VK_F4: return "F4";
    case VK_LBUTTON: return "LMB";
    case VK_RBUTTON: return "RMB";
    case VK_MBUTTON: return "MMB";
    case VK_UP: return "Up Arrow";
    case VK_DOWN: return "Down Arrow";
    case VK_LEFT: return "Left Arrow";
    case VK_RIGHT: return "Right Arrow";
    default: return "Key " + std::to_string(keyCode);
    }
}

void SettingsPanel::OnImGuiRender() {
    if (!m_isOpen) return;

    auto& config = EngineConfig::Get();

    ImGui::SetNextWindowSize(ImVec2(850, 650), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(m_name.c_str(), &m_isOpen)) {

        // ВЕРХНЯЯ ПАНЕЛЬ УПРАВЛЕНИЯ (Профили и Сброс)
        ImGui::BeginGroup();

        // Кнопка переключения профилей (С цветовой индикацией)
        if (config.UseEditorProfile) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Current Viewport: EDITOR PERFORMANCE");
            ImGui::SameLine(300);
            if (ImGui::Button("Switch to GAME PREVIEW", ImVec2(200, 25))) {
                config.UseEditorProfile = false; // Включаем игровые настройки во вьюпорте
            }
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Current Viewport: GAME PREVIEW (ULTRA)");
            ImGui::SameLine(300);
            if (ImGui::Button("Switch to EDITOR MODE", ImVec2(200, 25))) {
                config.UseEditorProfile = true; // Возвращаем быстрые настройки редактора
            }
        }

        // Кнопка сброса
        ImGui::SameLine(ImGui::GetWindowWidth() - 160);
        if (ImGui::Button("Reset to Defaults", ImVec2(140, 25))) {
            config.ResetToDefaults();
        }
        ImGui::EndGroup();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ВКЛАДКИ НАСТРОЕК
        if (ImGui::BeginTabBar("SettingsTabs")) {
            if (ImGui::BeginTabItem("Game (Release)")) {
                DrawGameSettings();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Editor")) {
                DrawEditorSettings();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void SettingsPanel::DrawGameSettings() {
    ImGui::BeginChild("GameCategories", ImVec2(180, 0), true);

    // Расширенное левое меню
    if (ImGui::Selectable("System", m_selectedGameCategory == Category::System)) m_selectedGameCategory = Category::System;
    if (ImGui::Selectable("Graphics", m_selectedGameCategory == Category::Graphics)) m_selectedGameCategory = Category::Graphics;
    if (ImGui::Selectable("Optimization", m_selectedGameCategory == Category::Optimization)) m_selectedGameCategory = Category::Optimization;
    if (ImGui::Selectable("Shadows", m_selectedGameCategory == Category::Shadows)) m_selectedGameCategory = Category::Shadows;
    if (ImGui::Selectable("Terrain", m_selectedGameCategory == Category::Terrain)) m_selectedGameCategory = Category::Terrain;
    if (ImGui::Selectable("Input bindings", m_selectedGameCategory == Category::Input)) m_selectedGameCategory = Category::Input;

    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("GameContent", ImVec2(0, 0), true);

    auto& config = EngineConfig::Get();
    bool needsSave = false;

    // 1. SYSTEM SETTINGS
    if (m_selectedGameCategory == Category::System) {
        ImGui::Text("System Settings");
        ImGui::Separator();
        ImGui::Spacing();

        // Буфер для редактирования названия окна
        char titleBuffer[128];
        strcpy_s(titleBuffer, config.System.WindowTitle.c_str());
        if (ImGui::InputText("Window Title", titleBuffer, IM_ARRAYSIZE(titleBuffer))) {
            config.System.WindowTitle = titleBuffer;
        }

        ImGui::InputInt("Resolution Width", &config.System.WindowWidth, 100);
        ImGui::InputInt("Resolution Height", &config.System.WindowHeight, 100);

        ImGui::Checkbox("Windowed Mode", &config.System.IsWindowed);
        ImGui::Checkbox("Enable VSync", &config.System.VSync);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "* Resolution and Windowed mode require engine restart.");
    }
    // 2. GRAPHICS SETTINGS
    else if (m_selectedGameCategory == Category::Graphics) {
        ImGui::Text("Graphics Settings");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::SliderFloat("Field of View (FOV)", &config.GameProfile.Graphics.FOV, 30.0f, 120.0f, "%.1f deg");
        ImGui::SliderFloat("Render Distance", &config.GameProfile.Graphics.RenderDistance, 500.0f, 30000.0f, "%.0f m");

        ImGui::Spacing();
        ImGui::Text("Camera Clipping Planes");
        ImGui::InputFloat("Near Z", &config.GameProfile.Graphics.NearZ, 0.1f, 1.0f, "%.2f");
        ImGui::InputFloat("Far Z", &config.GameProfile.Graphics.FarZ, 100.0f, 1000.0f, "%.0f");
    }
    // 3. OPTIMIZATION SETTINGS
    else if (m_selectedGameCategory == Category::Optimization) {
        ImGui::Text("Optimization & Culling");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Checkbox("Frustum Culling", &config.GameProfile.Optimization.EnableFrustumCulling);
        ImGui::Checkbox("Occlusion Culling (HZB)", &config.GameProfile.Optimization.EnableOcclusionCulling);
        ImGui::Checkbox("Z-Prepass", &config.GameProfile.Optimization.EnableZPrepass);

        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Checkbox("Enable Size Culling", &config.GameProfile.Optimization.EnableSizeCulling);
        if (!config.GameProfile.Optimization.EnableSizeCulling) ImGui::BeginDisabled();
        ImGui::SliderFloat("Min Pixel Size", &config.GameProfile.Optimization.MinPixelSize, 1.0f, 50.0f, "%.1f px");
        if (!config.GameProfile.Optimization.EnableSizeCulling) ImGui::EndDisabled();
    }
    // 4. SHADOWS SETTINGS
    else if (m_selectedGameCategory == Category::Shadows) {
        ImGui::Text("Shadow Settings");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("General Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Shadows", &config.GameProfile.Shadows.Enabled);
            ImGui::Checkbox("Soft Shadows (PCF)", &config.GameProfile.Shadows.SoftShadows);
            ImGui::Checkbox("Update Every Frame", &config.GameProfile.Shadows.UpdateEveryFrame);
        }

        if (ImGui::CollapsingHeader("Cascades & Quality", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "* Resolution & Cascade Count require engine restart");

            const int resOptions[] = { 512, 1024, 2048, 4096, 8192 };
            std::string currentRes = std::to_string(config.GameProfile.Shadows.Resolution);
            if (ImGui::BeginCombo("Resolution", currentRes.c_str())) {
                for (int res : resOptions) {
                    bool isSelected = (config.GameProfile.Shadows.Resolution == res);
                    if (ImGui::Selectable(std::to_string(res).c_str(), isSelected)) {
                        config.GameProfile.Shadows.Resolution = res;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::SliderInt("Cascade Count", &config.GameProfile.Shadows.CascadeCount, 1, 3);

            ImGui::Text("Cascade Splits (Distances)");

            ImGui::DragFloat3("Splits", config.GameProfile.Shadows.Splits, 1.0f, 10.0f, 2000.0f, "%.0f m");
        }

        if (ImGui::CollapsingHeader("Shadow Optimization & Culling")) {
            ImGui::SliderFloat("Max Shadow Distance", &config.GameProfile.Shadows.MaxShadowDistance, 100.0f, 5000.0f, "%.0f m");
            ImGui::Checkbox("Enable Frustum Culling (Shadows)", &config.GameProfile.Shadows.EnableShadowFrustumCulling);

            ImGui::Separator();
            ImGui::Checkbox("Size Culling", &config.GameProfile.Shadows.EnableShadowSizeCulling);
            if (!config.GameProfile.Shadows.EnableShadowSizeCulling) ImGui::BeginDisabled();
            ImGui::SliderFloat("Min Shadow Size", &config.GameProfile.Shadows.MinShadowSize, 0.1f, 10.0f, "%.1f m");
            ImGui::SliderFloat("Size Culling Distance", &config.GameProfile.Shadows.ShadowSizeCullingDistance, 10.0f, 500.0f, "%.0f m");
            if (!config.GameProfile.Shadows.EnableShadowSizeCulling) ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::Checkbox("Alpha Culling (Leaves/Grass)", &config.GameProfile.Shadows.EnableShadowAlphaCulling);
            if (!config.GameProfile.Shadows.EnableShadowAlphaCulling) ImGui::BeginDisabled();
            ImGui::SliderFloat("Alpha Cull Distance", &config.GameProfile.Shadows.ShadowAlphaCullingDistance, 10.0f, 500.0f, "%.0f m");
            if (!config.GameProfile.Shadows.EnableShadowAlphaCulling) ImGui::EndDisabled();
        }
    }
    // 5. TERRAIN & RVT SETTINGS
    else if (m_selectedGameCategory == Category::Terrain) {
        ImGui::Text("Terrain & Virtual Texturing");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Terrain LOD", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Terrain LOD", &config.GameProfile.TerrainLod.Enabled);
            ImGui::SliderFloat("LOD 1 Distance", &config.GameProfile.TerrainLod.Dist1, 100.0f, 2000.0f, "%.0f m");
            ImGui::SliderFloat("LOD 2 Distance", &config.GameProfile.TerrainLod.Dist2, 200.0f, 5000.0f, "%.0f m");

            ImGui::Text("Grid Steps (Requires Restart)");
            ImGui::InputInt("Step 0 (High)", &config.GameProfile.TerrainLod.Step0);
            ImGui::InputInt("Step 1 (Mid)", &config.GameProfile.TerrainLod.Step1);
            ImGui::InputInt("Step 2 (Low)", &config.GameProfile.TerrainLod.Step2);
        }

        if (ImGui::CollapsingHeader("Runtime Virtual Texturing (RVT)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "* Core RVT params require engine restart");
            ImGui::Checkbox("Enable RVT", &config.GameProfile.TerrainLod.EnableRVT);

            if (!config.GameProfile.TerrainLod.EnableRVT) ImGui::BeginDisabled();

            ImGui::SliderFloat("Near Blend Distance", &config.GameProfile.TerrainLod.RVTNearBlendDistance, 10.0f, 200.0f, "%.0f m");

            const int rvtResOptions[] = { 1024, 2048, 4096 };
            std::string currentRvtRes = std::to_string(config.GameProfile.TerrainLod.RVTResolution);
            if (ImGui::BeginCombo("RVT Resolution", currentRvtRes.c_str())) {
                for (int res : rvtResOptions) {
                    bool isSelected = (config.GameProfile.TerrainLod.RVTResolution == res);
                    if (ImGui::Selectable(std::to_string(res).c_str(), isSelected)) {
                        config.GameProfile.TerrainLod.RVTResolution = res;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::SliderInt("RVT Cascades", &config.GameProfile.TerrainLod.RVTCascadeCount, 1, 4);
            ImGui::Text("Cascade Coverages");
            ImGui::DragFloat("Cascade 0", &config.GameProfile.TerrainLod.RVTCascade0Coverage, 10.0f, 50.0f, 2000.0f, "%.0f m");
            ImGui::DragFloat("Cascade 1", &config.GameProfile.TerrainLod.RVTCascade1Coverage, 10.0f, 200.0f, 5000.0f, "%.0f m");
            ImGui::DragFloat("Cascade 2", &config.GameProfile.TerrainLod.RVTCascade2Coverage, 50.0f, 1000.0f, 15000.0f, "%.0f m");
            ImGui::DragFloat("Cascade 3", &config.GameProfile.TerrainLod.RVTCascade3Coverage, 100.0f, 5000.0f, 30000.0f, "%.0f m");

            if (!config.GameProfile.TerrainLod.EnableRVT) ImGui::EndDisabled();
        }
    }
    // 6. INPUT BINDINGS
    else if (m_selectedGameCategory == Category::Input) {
        ImGui::Text("Game Input Bindings");
        ImGui::Separator();
        ImGui::Spacing();

        // СОЗДАНИЕ НОВОГО ЭКШЕНА
        ImGui::InputText("##NewActionName", m_newActionBuffer, IM_ARRAYSIZE(m_newActionBuffer));
        ImGui::SameLine();
        if (ImGui::Button("Add Custom Action")) {
            std::string newAction(m_newActionBuffer);
            if (!newAction.empty()) {
                bool exists = false;
                for (const auto& bind : config.KeyBindings) {
                    if (bind.actionName == newAction) { exists = true; break; }
                }
                if (!exists) {
                    config.KeyBindings.push_back({ newAction, 0, GammaHash(newAction.c_str()) });
                    m_newActionBuffer[0] = '\0';
                    config.bInputBindingsChanged = true;
                }
            }
        }
        ImGui::Spacing();

        // ПЕРЕХВАТ НАЖАТИЯ
        if (!m_actionWaitingForBind.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Press any key for [%s]... (ESC to cancel)", m_actionWaitingForBind.c_str());

            for (int vKey = 8; vKey <= 255; ++vKey) {
                if (vKey == VK_LBUTTON || vKey == VK_RBUTTON || vKey == VK_MBUTTON) continue;
                if (GetAsyncKeyState(vKey) & 0x8000) {
                    if (vKey == VK_ESCAPE) {
                        m_actionWaitingForBind = "";
                    }
                    else {
                        for (auto& bind : config.KeyBindings) {
                            if (bind.actionName == m_actionWaitingForBind) {
                                bind.keyCode = vKey;
                                break;
                            }
                        }
                        m_actionWaitingForBind = "";
                    }
                    break;
                }
            }
        }
        ImGui::Spacing();

        // ТАБЛИЦА БИНДОВ
        if (ImGui::BeginTable("InputTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 300))) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Bind", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableHeadersRow();

            for (auto& bind : config.KeyBindings) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", bind.actionName.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", config.KeyCodeToString(bind.keyCode).c_str());

                ImGui::TableSetColumnIndex(2);
                if (!m_actionWaitingForBind.empty() && m_actionWaitingForBind != bind.actionName) ImGui::BeginDisabled();
                std::string btnLabel = (m_actionWaitingForBind == bind.actionName) ? "Waiting..." : "Rebind##" + bind.actionName;
                if (ImGui::Button(btnLabel.c_str(), ImVec2(-FLT_MIN, 0))) m_actionWaitingForBind = bind.actionName;
                if (!m_actionWaitingForBind.empty() && m_actionWaitingForBind != bind.actionName) ImGui::EndDisabled();

                ImGui::TableSetColumnIndex(3);
                ImGui::PushID(bind.actionName.c_str());
                if (ImGui::Button("X")) m_actionToDelete = bind.actionName;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (!m_actionToDelete.empty()) {
            config.KeyBindings.erase(std::remove_if(config.KeyBindings.begin(), config.KeyBindings.end(),
                [&](const KeyBinding& b) { return b.actionName == m_actionToDelete; }), config.KeyBindings.end());
            m_actionToDelete = "";
            config.bInputBindingsChanged = true;
        }
    }
    // Заглушки для будущих теней и террейна
    else {
        ImGui::Text("Work in Progress...");
    }

    // КНОПКА СОХРАНЕНИЯ
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Apply & Save All Settings", ImVec2(200, 30))) {
        if (m_selectedGameCategory == Category::Input) {
            config.bInputBindingsChanged = true;
        }
        config.Save("engine.json");
    }

    ImGui::EndChild();
}

void SettingsPanel::DrawEditorSettings() {
    // Левая колонка Навигация
    ImGui::BeginChild("EditorCategories", ImVec2(180, 0), true);
    if (ImGui::Selectable("Viewport Input", m_selectedEditorCategory == Category::Input)) m_selectedEditorCategory = Category::Input;
    if (ImGui::Selectable("Graphics", m_selectedEditorCategory == Category::Graphics)) m_selectedEditorCategory = Category::Graphics;
    if (ImGui::Selectable("Optimization", m_selectedEditorCategory == Category::Optimization)) m_selectedEditorCategory = Category::Optimization;
    if (ImGui::Selectable("Shadows", m_selectedEditorCategory == Category::Shadows)) m_selectedEditorCategory = Category::Shadows;
    if (ImGui::Selectable("Terrain", m_selectedEditorCategory == Category::Terrain)) m_selectedEditorCategory = Category::Terrain;
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("EditorContent", ImVec2(0, 0), true);

    auto& config = EngineConfig::Get();

    // 1. EDITOR INPUTS
    if (m_selectedEditorCategory == Category::Input) {
        ImGui::Text("Editor Viewport Bindings");
        ImGui::Separator();
        ImGui::Spacing();

        if (!m_actionWaitingForBind.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Press any key for [%s]... (ESC to cancel)", m_actionWaitingForBind.c_str());
            for (int vKey = 8; vKey <= 255; ++vKey) {
                if (vKey == VK_LBUTTON || vKey == VK_RBUTTON || vKey == VK_MBUTTON) continue;
                if (GetAsyncKeyState(vKey) & 0x8000) {
                    if (vKey == VK_ESCAPE) m_actionWaitingForBind = "";
                    else {
                        for (auto& bind : config.EditorKeyBindings) {
                            if (bind.actionName == m_actionWaitingForBind) {
                                bind.keyCode = vKey; break;
                            }
                        }
                        m_actionWaitingForBind = "";
                    }
                    break;
                }
            }
        }
        ImGui::Spacing();

        if (ImGui::BeginTable("EditorInputTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Bind", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (auto& bind : config.EditorKeyBindings) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", bind.actionName.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", config.KeyCodeToString(bind.keyCode).c_str());
                ImGui::TableSetColumnIndex(2);

                if (!m_actionWaitingForBind.empty() && m_actionWaitingForBind != bind.actionName) ImGui::BeginDisabled();
                std::string btnLabel = (m_actionWaitingForBind == bind.actionName) ? "Waiting..." : "Rebind##" + bind.actionName;
                if (ImGui::Button(btnLabel.c_str(), ImVec2(-FLT_MIN, 0))) m_actionWaitingForBind = bind.actionName;
                if (!m_actionWaitingForBind.empty() && m_actionWaitingForBind != bind.actionName) ImGui::EndDisabled();
            }
            ImGui::EndTable();
        }
    }
    // 2. EDITOR GRAPHICS
    else if (m_selectedEditorCategory == Category::Graphics) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Editor Graphics");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::SliderFloat("Field of View (FOV)", &config.EditorProfile.Graphics.FOV, 30.0f, 120.0f, "%.1f deg");
        ImGui::SliderFloat("Render Distance", &config.EditorProfile.Graphics.RenderDistance, 500.0f, 30000.0f, "%.0f m");
        ImGui::Spacing();
        ImGui::Text("Camera Clipping Planes");
        ImGui::InputFloat("Near Z", &config.EditorProfile.Graphics.NearZ, 0.1f, 1.0f, "%.2f");
        ImGui::InputFloat("Far Z", &config.EditorProfile.Graphics.FarZ, 100.0f, 1000.0f, "%.0f");
    }
    // 3. EDITOR OPTIMIZATION
    else if (m_selectedEditorCategory == Category::Optimization) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Editor Optimization");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Checkbox("Frustum Culling", &config.EditorProfile.Optimization.EnableFrustumCulling);
        ImGui::Checkbox("Occlusion Culling (HZB)", &config.EditorProfile.Optimization.EnableOcclusionCulling);
        ImGui::Checkbox("Z-Prepass", &config.EditorProfile.Optimization.EnableZPrepass);
        ImGui::Separator();
        ImGui::Checkbox("Enable Size Culling", &config.EditorProfile.Optimization.EnableSizeCulling);
        if (!config.EditorProfile.Optimization.EnableSizeCulling) ImGui::BeginDisabled();
        ImGui::SliderFloat("Min Pixel Size", &config.EditorProfile.Optimization.MinPixelSize, 1.0f, 50.0f, "%.1f px");
        if (!config.EditorProfile.Optimization.EnableSizeCulling) ImGui::EndDisabled();
    }
    // 4. EDITOR SHADOWS
    else if (m_selectedEditorCategory == Category::Shadows) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Editor Shadows");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("General Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Shadows", &config.EditorProfile.Shadows.Enabled);
            ImGui::Checkbox("Soft Shadows (PCF)", &config.EditorProfile.Shadows.SoftShadows);
            ImGui::Checkbox("Update Every Frame", &config.EditorProfile.Shadows.UpdateEveryFrame);
        }
        if (ImGui::CollapsingHeader("Cascades & Quality", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "* Resolution & Cascade Count require engine restart");
            const int resOptions[] = { 512, 1024, 2048, 4096, 8192 };
            std::string currentRes = std::to_string(config.EditorProfile.Shadows.Resolution);
            if (ImGui::BeginCombo("Resolution", currentRes.c_str())) {
                for (int res : resOptions) {
                    bool isSelected = (config.EditorProfile.Shadows.Resolution == res);
                    if (ImGui::Selectable(std::to_string(res).c_str(), isSelected)) config.EditorProfile.Shadows.Resolution = res;
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SliderInt("Cascade Count", &config.EditorProfile.Shadows.CascadeCount, 1, 3);
            ImGui::DragFloat3("Splits", config.EditorProfile.Shadows.Splits, 1.0f, 10.0f, 2000.0f, "%.0f m");
        }
        if (ImGui::CollapsingHeader("Shadow Optimization & Culling")) {
            ImGui::SliderFloat("Max Shadow Distance", &config.EditorProfile.Shadows.MaxShadowDistance, 100.0f, 5000.0f, "%.0f m");
            ImGui::Checkbox("Enable Frustum Culling (Shadows)", &config.EditorProfile.Shadows.EnableShadowFrustumCulling);
            ImGui::Separator();
            ImGui::Checkbox("Size Culling", &config.EditorProfile.Shadows.EnableShadowSizeCulling);
            if (!config.EditorProfile.Shadows.EnableShadowSizeCulling) ImGui::BeginDisabled();
            ImGui::SliderFloat("Min Shadow Size", &config.EditorProfile.Shadows.MinShadowSize, 0.1f, 10.0f, "%.1f m");
            ImGui::SliderFloat("Size Culling Distance", &config.EditorProfile.Shadows.ShadowSizeCullingDistance, 10.0f, 500.0f, "%.0f m");
            if (!config.EditorProfile.Shadows.EnableShadowSizeCulling) ImGui::EndDisabled();
            ImGui::Separator();
            ImGui::Checkbox("Alpha Culling (Leaves/Grass)", &config.EditorProfile.Shadows.EnableShadowAlphaCulling);
            if (!config.EditorProfile.Shadows.EnableShadowAlphaCulling) ImGui::BeginDisabled();
            ImGui::SliderFloat("Alpha Cull Distance", &config.EditorProfile.Shadows.ShadowAlphaCullingDistance, 10.0f, 500.0f, "%.0f m");
            if (!config.EditorProfile.Shadows.EnableShadowAlphaCulling) ImGui::EndDisabled();
        }
    }
    // 5. EDITOR TERRAIN
    else if (m_selectedEditorCategory == Category::Terrain) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Editor Terrain & Virtual Texturing");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Terrain LOD", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Terrain LOD", &config.EditorProfile.TerrainLod.Enabled);
            ImGui::SliderFloat("LOD 1 Distance", &config.EditorProfile.TerrainLod.Dist1, 100.0f, 2000.0f, "%.0f m");
            ImGui::SliderFloat("LOD 2 Distance", &config.EditorProfile.TerrainLod.Dist2, 200.0f, 5000.0f, "%.0f m");
            ImGui::Text("Grid Steps (Requires Restart)");
            ImGui::InputInt("Step 0 (High)", &config.EditorProfile.TerrainLod.Step0);
            ImGui::InputInt("Step 1 (Mid)", &config.EditorProfile.TerrainLod.Step1);
            ImGui::InputInt("Step 2 (Low)", &config.EditorProfile.TerrainLod.Step2);
        }
        if (ImGui::CollapsingHeader("Runtime Virtual Texturing (RVT)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "* Core RVT params require engine restart");
            ImGui::Checkbox("Enable RVT", &config.EditorProfile.TerrainLod.EnableRVT);
            if (!config.EditorProfile.TerrainLod.EnableRVT) ImGui::BeginDisabled();
            ImGui::SliderFloat("Near Blend Distance", &config.EditorProfile.TerrainLod.RVTNearBlendDistance, 10.0f, 200.0f, "%.0f m");
            const int rvtResOptions[] = { 1024, 2048, 4096 };
            std::string currentRvtRes = std::to_string(config.EditorProfile.TerrainLod.RVTResolution);
            if (ImGui::BeginCombo("RVT Resolution", currentRvtRes.c_str())) {
                for (int res : rvtResOptions) {
                    bool isSelected = (config.EditorProfile.TerrainLod.RVTResolution == res);
                    if (ImGui::Selectable(std::to_string(res).c_str(), isSelected)) config.EditorProfile.TerrainLod.RVTResolution = res;
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SliderInt("RVT Cascades", &config.EditorProfile.TerrainLod.RVTCascadeCount, 1, 4);
            ImGui::DragFloat("Cascade 0", &config.EditorProfile.TerrainLod.RVTCascade0Coverage, 10.0f, 50.0f, 2000.0f, "%.0f m");
            ImGui::DragFloat("Cascade 1", &config.EditorProfile.TerrainLod.RVTCascade1Coverage, 10.0f, 200.0f, 5000.0f, "%.0f m");
            ImGui::DragFloat("Cascade 2", &config.EditorProfile.TerrainLod.RVTCascade2Coverage, 50.0f, 1000.0f, 15000.0f, "%.0f m");
            ImGui::DragFloat("Cascade 3", &config.EditorProfile.TerrainLod.RVTCascade3Coverage, 100.0f, 5000.0f, 30000.0f, "%.0f m");
            if (!config.EditorProfile.TerrainLod.EnableRVT) ImGui::EndDisabled();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Apply & Save Editor Settings", ImVec2(220, 30))) {
        if (m_selectedEditorCategory == Category::Input) config.bInputBindingsChanged = true;
        config.Save("engine.json");
    }

    ImGui::EndChild();
}