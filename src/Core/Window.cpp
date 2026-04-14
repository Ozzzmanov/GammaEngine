//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
// ================================================================================
// Window.cpp
// ================================================================================
#include "Window.h"
#include "InputSystem.h"

#ifdef GAMMA_EDITOR
// FIXME Просто extern, так как ImGui компилирует этот хендлер как C++ функцию! А правильно ли это работает?
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

Window::Window() {}

Window::~Window() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        UnregisterClass("GammaEngineWindowClass", GetModuleHandle(nullptr));
    }
}

LRESULT CALLBACK Window::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_MOUSEWHEEL) {
        InputSystem::AddScroll((short)HIWORD(wParam) / (float)WHEEL_DELTA);
    }

#ifdef GAMMA_EDITOR
    // Отдаем события в редактор
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
        return true;
#endif

    switch (uMsg) {
    case WM_DESTROY:
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

bool Window::Initialize(int width, int height, const std::string& title) {
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "GammaEngineWindowClass";

    if (!RegisterClassEx(&wc)) {
        Logger::Error(LogCategory::General, "Failed to register window class.");
        return false;
    }

    // Расчет реального размера окна с учетом рамок
    RECT rect = { 0, 0, width, height };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    int posX = (GetSystemMetrics(SM_CXSCREEN) - (rect.right - rect.left)) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - (rect.bottom - rect.top)) / 2;

    m_hwnd = CreateWindowEx(
        0,
        "GammaEngineWindowClass",
        title.c_str(),
        WS_OVERLAPPEDWINDOW,
        posX, posY,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );

    if (!m_hwnd) {
        Logger::Error(LogCategory::General, "Failed to create window handle.");
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    SetFocus(m_hwnd);

    return true;
}

bool Window::ProcessMessages() {
    MSG msg = { 0 };
    // Используем PeekMessage для неблокирующей обработки
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

void Window::SetTitle(const std::string& title) {
    SetWindowTextA(m_hwnd, title.c_str());
}