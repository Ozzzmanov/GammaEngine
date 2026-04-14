//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// InputSystem.h
// Система ввода. Поддерживает маппинг действий (Action Mapping), блокировку 
// для UI и правильное отслеживание "Tap" (IsKeyPressed).
// ================================================================================
#pragma once
#include <Windows.h>
#include <DirectXMath.h>
#include <unordered_map>
#include <string>     

class InputSystem {
public:
    InputSystem();
    ~InputSystem();

    void Initialize();
    void Update(HWND hWnd);

    // Low Level (Прямой доступ)
    /// @brief Зажата ли кнопка прямо сейчас (Hold)
    bool IsKeyDown(int vKey) const;

    /// @brief Была ли кнопка нажата ИМЕННО в этом кадре (Tap)
    bool IsKeyPressed(int vKey) const;

    DirectX::XMFLOAT2 GetMouseDelta() const { return m_mouseDelta; }
    float GetMouseScrollDelta() const { return m_scrollDelta; }

    // Action Mapping
    // FIXME: Для производительности ключи в m_actionMap лучше сделать uint32_t (хэши), а не std::string.
    void BindAction(const std::string& actionName, int vKey);
    bool IsActionActive(const std::string& actionName) const;
    bool IsActionTriggered(const std::string& actionName) const;

    void SetGameViewportHovered(bool hovered) { m_gameViewportHovered = hovered; }
    void ClearBindings() { m_actionMap.clear(); }

    void SetBlockInput(bool block);
    bool IsInputBlocked() const { return m_blockInput; }

    // FIXME: Костыль для Window.cpp. Лучше переделать на очередь оконных сообщений (Event Queue).
    static void AddScroll(float delta) { s_scrollDelta += delta; }

private:
    POINT m_prevMousePos;
    DirectX::XMFLOAT2 m_mouseDelta;
    bool m_firstMouse;

    // Двойной буфер состояния клавиатуры для вычисления "Tap" (IsKeyPressed)
    bool m_keysCurrent[256];
    bool m_keysPrevious[256];

    std::unordered_map<std::string, int> m_actionMap;

    bool m_gameViewportHovered = false;
    bool m_blockInput = false;

    float m_scrollDelta = 0.0f;
    static float s_scrollDelta;
};