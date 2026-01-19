#include "Core/Window.h"

Window::Window() {}

Window::~Window() {
    if (m_hwnd) DestroyWindow(m_hwnd);
}

// Главная функция обработки событий Windows
LRESULT CALLBACK Window::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY: 
        PostQuitMessage(0); 
        return 0;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

bool Window::Initialize(int width, int height, const std::string& title) {
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "GammaEngineWindowClass";

    if (!RegisterClassEx(&wc)) {
        MessageBox(nullptr, "Не удалось зарегистрировать класс окна", "Ошибка", MB_OK);
        return false;
    }

    RECT rect = { 0, 0, width, height };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    // Центрирование окна
    int posX = (GetSystemMetrics(SM_CXSCREEN) - (rect.right - rect.left)) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - (rect.bottom - rect.top)) / 2;

    m_hwnd = CreateWindowEx(
        0, "GammaEngineWindowClass", title.c_str(),
        WS_OVERLAPPEDWINDOW,
        posX, posY, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );

    if (!m_hwnd) return false;

    ShowWindow(m_hwnd, SW_SHOW);
    return true;
}

bool Window::ProcessMessages() {
    MSG msg = { 0 };
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}