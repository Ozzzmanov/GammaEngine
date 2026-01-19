#pragma once
#include "Core/Prerequisites.h"

// Класс отвечает за создание окна и обработку системных сообщений (клавиатура, мышь, закрытие)
class Window {
public:
    Window();
    ~Window();

    // Создает окно заданного размера
    bool Initialize(int width, int height, const std::string& title);

    // Обрабатывает очередь сообщений Windows. Возвращает false, если нажали "Закрыть".
    bool ProcessMessages();

    // Получить системный дескриптор окна
    HWND GetHWND() const { return m_hwnd; }

private:
    HWND m_hwnd = nullptr; // Дескриптор окна

    // Системная функция обработки сообщений
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};