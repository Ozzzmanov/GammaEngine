//  ██████╗  █████╗ ███╗   ███╗███╗   ███╗ █████╗  
//  ██╔════╝ ██╔══██╗████╗ ████║████╗ ████║██╔══██╗
//  ██║  ███╗███████║██╔████╔██║██╔████╔██║███████║
//  ██║   ██║██╔══██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║
//  ╚██████╔╝██║  ██║██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║
//   ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝
//
// ================================================================================
// Файл предварительных требований.
// Содержит общие подключения, макросы и определения типов для всего движка.
// ================================================================================

#pragma once


//Отключаем макросы min/max из Windows.h
#define NOMINMAX

// --- Системные библиотеки ---
#include <Windows.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <mutex>
#include <map>
#include <filesystem>

// --- DirectX 11 ---
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

// --- DirectXTK (Убедитесь, что пути настроены) ---
#include <CommonStates.h>
#include <SpriteBatch.h>
#include <SpriteFont.h>
#include <SimpleMath.h>

// --- Пространства имен ---
namespace fs = std::filesystem;
using namespace Microsoft::WRL;
using namespace DirectX;
using namespace DirectX::SimpleMath;

// --- Макросы безопасности ---

// Макрос для проверки HRESULT и логирования ошибки
#define HR_CHECK(hr, msg) \
    if (FAILED(hr)) { \
        /* Теперь явно указываем категорию Render */ \
        Logger::Error(LogCategory::Render, std::string(msg) + " [Line: " + std::to_string(__LINE__) + "]"); \
        return false; \
    }

// Макрос для проверки HRESULT без возврата (если есть)
#define HR_CHECK_VOID(hr, msg) \
    if (FAILED(hr)) { \
        Logger::Error(LogCategory::Render, std::string(msg)); \
        return; \
    }

// --- Базовые структуры ---

// Вершина для ландшафта и простых моделей
struct SimpleVertex {
    Vector3 Pos;    // Позиция (x, y, z)
    Vector3 Color;  // Цвет (r, g, b) - используется для отладки
    Vector3 Normal; // Нормаль (nx, ny, nz) - для освещения
    Vector2 Tex;    // Текстурные координаты (u, v)
};

// Структура для Constant Buffer (register b0)
// Должна быть выровнена по 16 байт
struct CB_VS_Transform {
    Matrix World;
    Matrix View;
    Matrix Projection;
};