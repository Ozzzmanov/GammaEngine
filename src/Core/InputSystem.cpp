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

float InputSystem::s_scrollDelta = 0.0f;

InputSystem::InputSystem()
    : m_prevMousePos({ 0, 0 }), m_mouseDelta({ 0, 0 }), m_firstMouse(true)
{
    ZeroMemory(m_keysCurrent, sizeof(m_keysCurrent));
    ZeroMemory(m_keysPrevious, sizeof(m_keysPrevious));
}

InputSystem::~InputSystem() {}

void InputSystem::Initialize() {
    m_firstMouse = true;
    m_mouseDelta = { 0, 0 };
    m_actionMap.clear();
    ZeroMemory(m_keysCurrent, sizeof(m_keysCurrent));
    ZeroMemory(m_keysPrevious, sizeof(m_keysPrevious));
}

void InputSystem::Update(HWND hWnd) {
    // Забираем скролл (даже если инпут заблокирован, мы сохраняем дельту, но обнуляем глобальную)
    m_scrollDelta = s_scrollDelta;
    s_scrollDelta = 0.0f;

    // Обновление состояний клавиатуры (Двойной буфер)
    // Делаем это ДО проверки m_blockInput, чтобы состояния всегда были консистентны.
    for (int i = 0; i < 256; ++i) {
        m_keysPrevious[i] = m_keysCurrent[i];
        m_keysCurrent[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    }

    if (m_blockInput) return; // Глушим обновление мыши, если мы в UI

    // Обновление мыши (FIXME: В будущем лучше перейти на Raw Input, а не GetCursorPos)
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
}

void InputSystem::SetBlockInput(bool block) {
    if (block != m_blockInput) {
        m_firstMouse = true;
        m_mouseDelta = { 0.0f, 0.0f }; // Гасим инерцию камеры при переходе в UI!
    }
    m_blockInput = block;
}

// Low Level
bool InputSystem::IsKeyDown(int vKey) const {
    if (m_blockInput) return false;
    return m_keysCurrent[vKey];
}

bool InputSystem::IsKeyPressed(int vKey) const {
    if (m_blockInput) return false;
    // Возвращаем true, только если кнопка зажата СЕЙЧАС, но НЕ была зажата в ПРОШЛОМ кадре
    return m_keysCurrent[vKey] && !m_keysPrevious[vKey];
}

// Action Mapping ---
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

bool InputSystem::IsActionTriggered(const std::string& actionName) const {
    auto it = m_actionMap.find(actionName);
    if (it != m_actionMap.end()) {
        return IsKeyPressed(it->second);
    }
    return false;
}