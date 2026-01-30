//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// InputSystem.h
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

    // --- Low Level (Прямой доступ) ---
    bool IsKeyDown(int vKey) const;
    bool IsKeyPressed(int vKey);
    DirectX::XMFLOAT2 GetMouseDelta() const { return m_mouseDelta; }

    // --- Action Mapping ---
    void BindAction(const std::string& actionName, int vKey); // Назначить кнопку
    bool IsActionActive(const std::string& actionName) const; // Зажата ли кнопка действия?
    bool IsActionTriggered(const std::string& actionName);    // Была ли нажата (один раз)?

private:
    POINT m_prevMousePos;
    DirectX::XMFLOAT2 m_mouseDelta;
    bool m_firstMouse;
    bool m_keysPrevious[256];

    std::unordered_map<std::string, int> m_actionMap; // Хранилище привязок
};