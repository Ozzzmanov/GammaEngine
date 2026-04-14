// ================================================================================
// SettingsPanel.h
// Панель глобальных настроек движка и редактора.
// ================================================================================
#pragma once
#include "../EditorPanel.h"
#include "../../Config/EngineConfig.h"

class SettingsPanel : public EditorPanel {
public:
    SettingsPanel(const std::string& name);
    ~SettingsPanel() = default;

    void OnImGuiRender() override;

private:
    void DrawGameSettings();
    void DrawEditorSettings();

    // Категории настроек
    enum class Category {
        System, 
        Graphics,
        Optimization,
        Shadows,
        Terrain,
        Input
    };

    Category m_selectedGameCategory = Category::Input;
    Category m_selectedEditorCategory = Category::Input;

    // Имя экшена, который сейчас ждет нажатия клавиши для бинда
    std::string m_actionWaitingForBind = "";

    //Для создания и удаления кастомных экшенов
    char m_newActionBuffer[64] = "";
    std::string m_actionToDelete = "";

    // Вспомогательная функция для вывода кнопок
    std::string KeyCodeToString(int keyCode);
};