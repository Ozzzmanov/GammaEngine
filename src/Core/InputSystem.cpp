//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// InputSystem.cpp
// ================================================================================
#include "InputSystem.h"

InputSystem::InputSystem()
    : m_prevMousePos({ 0, 0 }), m_mouseDelta({ 0, 0 }), m_firstMouse(true) {
    ZeroMemory(m_keysPrevious, sizeof(m_keysPrevious));
}

InputSystem::~InputSystem() {}

void InputSystem::Initialize() {
    m_firstMouse = true;
    m_mouseDelta = { 0, 0 };
    m_actionMap.clear(); // Очищаем бинды при рестарте
}

void InputSystem::Update(HWND hWnd) {
    // Мышь
    POINT curMouse;
    GetCursorPos(&curMouse);
    ScreenToClient(hWnd, &curMouse);

    if (m_firstMouse) {
        m_prevMousePos = curMouse;
        m_firstMouse = false;
    }

    m_mouseDelta.x = (float)(curMouse.x - m_prevMousePos.x) * 0.003f;
    m_mouseDelta.y = (float)(curMouse.y - m_prevMousePos.y) * 0.003f;
    m_prevMousePos = curMouse;

    // Клавиатура (запоминаем состояние для IsKeyPressed)
    for (int i = 0; i < 256; ++i) {
        m_keysPrevious[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    }
}

// --- Low Level ---
bool InputSystem::IsKeyDown(int vKey) const {
    return (GetAsyncKeyState(vKey) & 0x8000) != 0;
}

bool InputSystem::IsKeyPressed(int vKey) {
    bool isDown = (GetAsyncKeyState(vKey) & 0x8000) != 0;
    // FIX ME Возвращаем true если нажата СЕЙЧАС, но НЕ была нажата в прошлом кадре
    // (Примечание: m_keysPrevious здесь обновляется в Update, поэтому используем простую логику)
    // Для более точного Tap нужно сравнивать с состоянием ДО апдейта, 
    // но для простоты используем такой подход:
    static bool wasDown[256] = { false };

    if (isDown && !wasDown[vKey]) {
        wasDown[vKey] = true;
        return true;
    }
    if (!isDown) {
        wasDown[vKey] = false;
    }
    return false;
}

// --- Action Mapping ---
void InputSystem::BindAction(const std::string& actionName, int vKey) {
    m_actionMap[actionName] = vKey;
}

bool InputSystem::IsActionActive(const std::string& actionName) const {
    auto it = m_actionMap.find(actionName);
    if (it != m_actionMap.end()) {
        return IsKeyDown(it->second);
    }
    return false;
}

bool InputSystem::IsActionTriggered(const std::string& actionName) {
    auto it = m_actionMap.find(actionName);
    if (it != m_actionMap.end()) {
        return IsKeyPressed(it->second);
    }
    return false;
}